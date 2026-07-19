/**********************************************************
* 文件名: sampling_capture.cpp
* 日期: 2026-07-16
* 版本: v2.1
* 更新记录: 将 FT601 枚举、bulk 传输和重连迁移为 libusb。
* 描述: 实现线协议校验、采集组装、libusb 传输和标量 VCD/sidecar 输出。
**********************************************************/

#include "sampling_capture.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSharedPointer>
#include <QtEndian>

#include <libusb.h>

namespace lockstep::acquisition {
namespace {

constexpr quint32 kMaxPayloadBytes = 4U * 1024U * 1024U;
constexpr quint32 kCrcDisabledFlag = 1U << 1U;
constexpr int kUsbReadChunkBytes = 4096;
constexpr int kStatusResponseBytes = 64;
constexpr quint16 kFt601VendorId = 0x0403U;
constexpr quint16 kFt601ProductId = 0x601fU;
constexpr quint8 kFt601OutEndpoint = 0x02U;
constexpr quint8 kFt601InEndpoint = 0x82U;
constexpr unsigned int kUsbTransferTimeoutMs = 2000U;

struct CaptureUsbInterface final {
    int interfaceNumber = -1;
    int alternateSetting = 0;
    quint8 outEndpoint = 0;
    quint8 inEndpoint = 0;
};

QString usbSpeedText(libusb_device* device)
{
    switch (libusb_get_device_speed(device)) {
    case LIBUSB_SPEED_LOW: return QStringLiteral("low-speed");
    case LIBUSB_SPEED_FULL: return QStringLiteral("full-speed");
    case LIBUSB_SPEED_HIGH: return QStringLiteral("high-speed");
    case LIBUSB_SPEED_SUPER: return QStringLiteral("super-speed");
#if defined(LIBUSB_SPEED_SUPER_PLUS)
    case LIBUSB_SPEED_SUPER_PLUS: return QStringLiteral("super-speed-plus");
#endif
    default: return QStringLiteral("unknown");
    }
}

bool findCaptureUsbInterface(libusb_device* device, CaptureUsbInterface* selection, QString* error)
{
    libusb_config_descriptor* config = nullptr;
    const int status = libusb_get_active_config_descriptor(device, &config);
    if (status != LIBUSB_SUCCESS || config == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("无法读取 FT601 active USB configuration: %1 (%2)")
                         .arg(QString::fromLatin1(libusb_error_name(status))).arg(status);
        }
        return false;
    }

    CaptureUsbInterface found;
    for (quint8 interfaceIndex = 0; interfaceIndex < config->bNumInterfaces &&
         found.interfaceNumber < 0; ++interfaceIndex) {
        const libusb_interface& usbInterface = config->interface[interfaceIndex];
        for (int alternateIndex = 0; alternateIndex < usbInterface.num_altsetting; ++alternateIndex) {
            const libusb_interface_descriptor& alternate = usbInterface.altsetting[alternateIndex];
            bool hasOut = false;
            bool hasIn = false;
            for (quint8 endpointIndex = 0; endpointIndex < alternate.bNumEndpoints; ++endpointIndex) {
                const libusb_endpoint_descriptor& endpoint = alternate.endpoint[endpointIndex];
                if ((endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }
                hasOut = hasOut || endpoint.bEndpointAddress == kFt601OutEndpoint;
                hasIn = hasIn || endpoint.bEndpointAddress == kFt601InEndpoint;
            }
            if (!hasOut || !hasIn) continue;
            found.interfaceNumber = alternate.bInterfaceNumber;
            found.alternateSetting = alternate.bAlternateSetting;
            found.outEndpoint = kFt601OutEndpoint;
            found.inEndpoint = kFt601InEndpoint;
            break;
        }
    }
    libusb_free_config_descriptor(config);
    if (found.interfaceNumber < 0) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "FT601 active USB configuration 中未找到同时包含 bulk 端点 0x02/0x82 的接口");
        }
        return false;
    }
    if (selection != nullptr) *selection = found;
    return true;
}

quint32 crc32(const QByteArray& bytes)
{
    quint32 crc = 0xffffffffU;
    for (const char byte : bytes) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}

void append16(QByteArray* bytes, const quint16 value)
{
    const quint16 little = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char*>(&little), sizeof(little));
}

void append32(QByteArray* bytes, const quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char*>(&little), sizeof(little));
}

quint16 read16(const char* data)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data));
}

quint32 read32(const char* data)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data));
}

bool parseStatusV2(const CaptureFrame& frame, CaptureStatusV2* status, QString* error)
{
    if (frame.header.type != CaptureFrameType::StatusResponse || frame.payload.size() < 64) {
        if (error != nullptr) *error = QStringLiteral("STATUS_RSP v2 长度或类型错误");
        return false;
    }
    const char* data = frame.payload.constData();
    status->state = read32(data + 0);
    status->requestSequence = read32(data + 4);
    status->captureId = read32(data + 8);
    status->samplesSeen = read32(data + 12);
    status->samplesUploaded = read32(data + 16);
    status->deviceStatusFlags = read32(data + 20);
    status->lastErrorCode = read32(data + 24);
    status->commandState = read32(data + 28);
    status->captureState = read32(data + 32);
    status->captureFlags = read32(data + 36);
    status->pretriggerSamples = read32(data + 40);
    status->posttriggerSamples = read32(data + 44);
    status->frameSourceState = read32(data + 48);
    status->txGeneratorState = read32(data + 52);
    status->ft601State = read32(data + 56);
    status->txBytes = read32(data + 60);
    return true;
}

QString identifierForChannel(int channel)
{
    QString id;
    int value = channel;
    do {
        id.append(QChar(33 + (value % 94)));
        value /= 94;
    } while (value > 0);
    return id;
}

bool saveFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法写入采集产物: %1").arg(path);
        }
        return false;
    }
    return true;
}

QJsonObject statusToJson(const CaptureStatusV2& status)
{
    QJsonObject object;
    object.insert(QStringLiteral("state"), static_cast<double>(status.state));
    object.insert(QStringLiteral("request_sequence"), static_cast<double>(status.requestSequence));
    object.insert(QStringLiteral("capture_id"), static_cast<double>(status.captureId));
    object.insert(QStringLiteral("samples_seen"), static_cast<double>(status.samplesSeen));
    object.insert(QStringLiteral("samples_uploaded"), static_cast<double>(status.samplesUploaded));
    object.insert(QStringLiteral("device_status_flags"), static_cast<double>(status.deviceStatusFlags));
    object.insert(QStringLiteral("last_error_code"), static_cast<double>(status.lastErrorCode));
    object.insert(QStringLiteral("command_state"), static_cast<double>(status.commandState));
    object.insert(QStringLiteral("capture_state"), static_cast<double>(status.captureState));
    object.insert(QStringLiteral("capture_flags"), static_cast<double>(status.captureFlags));
    object.insert(QStringLiteral("pretrigger_samples"), static_cast<double>(status.pretriggerSamples));
    object.insert(QStringLiteral("posttrigger_samples"), static_cast<double>(status.posttriggerSamples));
    object.insert(QStringLiteral("frame_source_state"), static_cast<double>(status.frameSourceState));
    object.insert(QStringLiteral("tx_generator_state"), static_cast<double>(status.txGeneratorState));
    object.insert(QStringLiteral("ft601_state"), static_cast<double>(status.ft601State));
    object.insert(QStringLiteral("tx_bytes"), static_cast<double>(status.txBytes));
    return object;
}

void saveSessionEvidence(const QString& taskRootPath, const CaptureSessionResult& result)
{
    if (taskRootPath.isEmpty()) return;
    const QString evidenceDir = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidenceDir)) return;
    QJsonObject object;
    object.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-status-v2"));
    object.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("success"), result.success);
    object.insert(QStringLiteral("phase"), result.phase);
    object.insert(QStringLiteral("message"), result.message);
    object.insert(QStringLiteral("recovery_attempted"), result.recoveryAttempted);
    object.insert(QStringLiteral("recovery_succeeded"), result.recoverySucceeded);
    object.insert(QStringLiteral("status"), statusToJson(result.status));
    saveFile(QDir(evidenceDir).filePath(QStringLiteral("capture_status.json")),
             QJsonDocument(object).toJson(QJsonDocument::Indented), nullptr);
}

}  // namespace

QByteArray CaptureFrameCodec::encode(const CaptureFrameType type, const QByteArray& payload,
                                 const quint32 sequence, const quint32 captureId,
                                 const quint32 flags) const
{
    QByteArray padded = payload;
    padded.append(QByteArray((4 - (payload.size() % 4)) % 4, '\0'));
    QByteArray header;
    append32(&header, kCaptureFrameMagic);
    append16(&header, kCaptureProtocolVersion);
    append16(&header, static_cast<quint16>(type));
    append32(&header, kCaptureFrameHeaderBytes);
    append32(&header, static_cast<quint32>(payload.size()));
    append32(&header, sequence);
    append32(&header, captureId);
    append32(&header, flags);
    append32(&header, 0U);
    const quint32 checksum = (flags & kCrcDisabledFlag) != 0U ? 0U : crc32(header + padded);
    const quint32 little = qToLittleEndian(checksum);
    memcpy(header.data() + 28, &little, sizeof(little));
    return header + padded;
}

CaptureDecodeResult CaptureFrameCodec::feed(const QByteArray& bytes)
{
    buffer_.append(bytes);
    CaptureDecodeResult result;
    const QByteArray magicBytes = QByteArray::fromHex("4c534332");
    while (buffer_.size() >= static_cast<int>(kCaptureFrameHeaderBytes)) {
        if (!buffer_.startsWith(magicBytes)) {
            const int nextFrame = buffer_.indexOf(magicBytes, 1);
            if (nextFrame >= 0) {
                buffer_.remove(0, nextFrame);
                continue;
            }
            // 保留可能跨越下一次 USB 读取边界的魔数字节前缀。
            buffer_.remove(0, buffer_.size() - (magicBytes.size() - 1));
            break;
        }
        const char* raw = buffer_.constData();
        CaptureFrame frame;
        frame.header.magic = read32(raw);
        frame.header.version = read16(raw + 4);
        frame.header.type = static_cast<CaptureFrameType>(read16(raw + 6));
        frame.header.headerLength = read32(raw + 8);
        frame.header.payloadLength = read32(raw + 12);
        frame.header.sequence = read32(raw + 16);
        frame.header.captureId = read32(raw + 20);
        frame.header.flags = read32(raw + 24);
        frame.header.crc32 = read32(raw + 28);
        if (frame.header.version != kCaptureProtocolVersion ||
            frame.header.headerLength != kCaptureFrameHeaderBytes || frame.header.payloadLength > kMaxPayloadBytes) {
            result.success = false;
            result.error = QStringLiteral("采集帧头合同错误");
            return result;
        }
        const quint32 paddedLength = (frame.header.payloadLength + 3U) & ~3U;
        const quint32 frameLength = kCaptureFrameHeaderBytes + paddedLength;
        if (buffer_.size() < static_cast<int>(frameLength)) {
            break;
        }
        QByteArray checksumBytes = buffer_.left(static_cast<int>(frameLength));
        memset(checksumBytes.data() + 28, 0, 4);
        const bool crcEnabled = (frame.header.flags & kCrcDisabledFlag) == 0U;
        if (crcEnabled && crc32(checksumBytes) != frame.header.crc32) {
            result.success = false;
            result.error = QStringLiteral("采集帧 CRC 校验失败");
            return result;
        }
        frame.payload = buffer_.mid(static_cast<int>(kCaptureFrameHeaderBytes),
                                    static_cast<int>(frame.header.payloadLength));
        result.frames.append(frame);
        buffer_.remove(0, static_cast<int>(frameLength));
    }
    return result;
}

void CaptureFrameCodec::reset()
{
    buffer_.clear();
}

bool SamplingCaptureConfig::validate(QString* error) const
{
    // 协议字段 posttrigger 包含触发样本；硬件内部另有 post_after_trigger=2048。
    const bool countsValid = sampleCount > 0U && pretriggerCount + posttriggerCount == sampleCount;
    const bool rateValid = sampleRateHz == 1'000'000U || sampleRateHz == 10'000'000U ||
                           sampleRateHz == 50'000'000U || sampleRateHz == 120'000'000U;
    if (!countsValid || !rateValid || (protocolGroupMask & ~0x1ffU) != 0U || mode != 0U ||
        triggerTimeoutSamples == 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("采样配置不符合 1024 路有限采集合同");
        }
        return false;
    }
    return true;
}

QByteArray SamplingCaptureConfig::toPayload() const
{
    QByteArray payload;
    append32(&payload, sampleRateHz);
    append32(&payload, sampleCount);
    append32(&payload, pretriggerCount);
    append32(&payload, posttriggerCount);
    append32(&payload, protocolGroupMask);
    append32(&payload, inputInvertMask);
    append16(&payload, static_cast<quint16>(kCapturePhysicalChannels));
    append16(&payload, static_cast<quint16>(kCaptureSampleWordBits));
    append32(&payload, triggerMask);
    append32(&payload, triggerValue);
    append32(&payload, triggerEdgeRise);
    append32(&payload, triggerEdgeFall);
    append32(&payload, mode);
    append32(&payload, triggerTimeoutSamples);
    return payload;
}

bool SamplingCaptureAssembler::append(const CaptureFrame& frame, QString* error)
{
    if (!hasMeta_) expectedSequence_ = frame.header.sequence;
    if (frame.header.sequence != expectedSequence_++) {
        if (error != nullptr) *error = QStringLiteral("采集帧序号不连续");
        return false;
    }
    if (hasMeta_ && frame.header.captureId != record_.captureId) {
        if (error != nullptr) *error = QStringLiteral("采集帧 capture_id 不一致");
        return false;
    }
    if (frame.header.type == CaptureFrameType::CaptureMeta) {
        if (hasMeta_ || frame.payload.size() != 40) {
            if (error != nullptr) *error = QStringLiteral("CAPTURE_META 重复或长度错误");
            return false;
        }
        record_.captureId = frame.header.captureId;
        record_.sampleRateHz = read32(frame.payload.constData());
        record_.requestedSampleCount = read32(frame.payload.constData() + 4);
        const quint32 physicalChannels = read32(frame.payload.constData() + 24);
        const quint32 sampleWordBits = read32(frame.payload.constData() + 28);
        record_.windowStartIndex = read32(frame.payload.constData() + 32);
        record_.triggerIndex = read32(frame.payload.constData() + 36);
        if (physicalChannels != kCapturePhysicalChannels || sampleWordBits != kCaptureSampleWordBits) {
            if (error != nullptr) *error = QStringLiteral("硬件上报的通道数或样本位宽不是 1024");
            return false;
        }
        hasMeta_ = true;
        return true;
    }
    if (frame.header.type == CaptureFrameType::CaptureEnd && !hasMeta_) {
        if (frame.payload.size() != 32) {
            if (error != nullptr) *error = QStringLiteral("无 META 的 CAPTURE_END 长度错误");
            return false;
        }
        record_.actualSampleCount = read32(frame.payload.constData());
        record_.windowStartIndex = read32(frame.payload.constData() + 4);
        record_.triggerIndex = read32(frame.payload.constData() + 8);
        record_.stopReason = read32(frame.payload.constData() + 12);
        record_.deviceStatusFlags = read32(frame.payload.constData() + 16);
        const quint32 sampleBytes = read32(frame.payload.constData() + 20);
        const quint32 payloadBytes = read32(frame.payload.constData() + 24);
        if (record_.actualSampleCount != 0U || sampleBytes != 0U || payloadBytes != 0U) {
            if (error != nullptr) *error = QStringLiteral("无 META 的 CAPTURE_END 携带非空样本");
            return false;
        }
        hasEnd_ = true;
        return true;
    }
    if (!hasMeta_ || hasEnd_) {
        if (error != nullptr) *error = QStringLiteral("采集帧状态顺序错误");
        return false;
    }
    if (frame.header.type == CaptureFrameType::SampleData) {
        sampleBytes_.append(frame.payload);
        payloadBytes_ += static_cast<quint32>(frame.payload.size());
        return true;
    }
    if (frame.header.type != CaptureFrameType::CaptureEnd || frame.payload.size() != 32) {
        if (error != nullptr) *error = QStringLiteral("采集流包含非预期帧");
        return false;
    }
    record_.actualSampleCount = read32(frame.payload.constData());
    const quint32 windowStart = read32(frame.payload.constData() + 4);
    const quint32 trigger = read32(frame.payload.constData() + 8);
    record_.stopReason = read32(frame.payload.constData() + 12);
    record_.deviceStatusFlags = read32(frame.payload.constData() + 16);
    const quint32 sampleBytes = read32(frame.payload.constData() + 20);
    const quint32 payloadBytes = read32(frame.payload.constData() + 24);
    if (windowStart != record_.windowStartIndex || trigger != record_.triggerIndex ||
        sampleBytes != record_.actualSampleCount * kCaptureSampleBytes || payloadBytes != payloadBytes_ ||
        sampleBytes_.size() != static_cast<int>(sampleBytes)) {
        if (error != nullptr) *error = QStringLiteral("CAPTURE_END 与实际样本流不一致");
        return false;
    }
    for (quint32 index = 0; index < record_.actualSampleCount; ++index) {
        record_.samples.append(sampleBytes_.mid(static_cast<int>(index * kCaptureSampleBytes),
                                                static_cast<int>(kCaptureSampleBytes)));
    }
    hasEnd_ = true;
    return true;
}

bool SamplingCaptureAssembler::complete() const { return hasEnd_; }
SamplingCaptureRecord SamplingCaptureAssembler::record() const { return record_; }

class LibusbRuntime::Private final {
public:
    ~Private()
    {
        closeHandle();
        if (context != nullptr) libusb_exit(context);
    }

    static bool isFt601(libusb_device* device, libusb_device_descriptor* descriptor = nullptr)
    {
        libusb_device_descriptor current = {};
        if (libusb_get_device_descriptor(device, &current) != LIBUSB_SUCCESS) return false;
        if (descriptor != nullptr) *descriptor = current;
        return current.idVendor == kFt601VendorId && current.idProduct == kFt601ProductId;
    }

    static QByteArray portPath(libusb_device* device)
    {
        quint8 ports[8] = {};
        const int count = libusb_get_port_numbers(device, ports, static_cast<int>(sizeof(ports)));
        return count > 0 ? QByteArray(reinterpret_cast<const char*>(ports), count) : QByteArray();
    }

    static QByteArray serialNumber(libusb_device_handle* deviceHandle,
                                   const libusb_device_descriptor& descriptor)
    {
        if (deviceHandle == nullptr || descriptor.iSerialNumber == 0) return QByteArray();
        unsigned char text[256] = {};
        const int length = libusb_get_string_descriptor_ascii(
            deviceHandle, descriptor.iSerialNumber, text, sizeof(text));
        return length > 0 ? QByteArray(reinterpret_cast<const char*>(text), length) : QByteArray();
    }

    void rememberIdentity(libusb_device* device, const libusb_device_descriptor& descriptor)
    {
        identitySerial = serialNumber(handle, descriptor);
        identityBus = libusb_get_bus_number(device);
        identityPortPath = portPath(device);
        hasIdentity = !identitySerial.isEmpty() || !identityPortPath.isEmpty();
    }

    bool matchesIdentity(libusb_device* device) const
    {
        libusb_device_descriptor descriptor = {};
        if (!isFt601(device, &descriptor)) return false;
        if (!identitySerial.isEmpty()) {
            libusb_device_handle* temporaryHandle = nullptr;
            if (libusb_open(device, &temporaryHandle) != LIBUSB_SUCCESS) return false;
            const QByteArray candidateSerial = serialNumber(temporaryHandle, descriptor);
            libusb_close(temporaryHandle);
            return candidateSerial == identitySerial;
        }
        return libusb_get_bus_number(device) == identityBus && portPath(device) == identityPortPath;
    }

    bool selectCaptureInterface(libusb_device* device, QString* error)
    {
        CaptureUsbInterface selection;
        if (!findCaptureUsbInterface(device, &selection, error)) return false;
        interfaceNumber = selection.interfaceNumber;
        alternateSetting = selection.alternateSetting;
        outEndpoint = selection.outEndpoint;
        inEndpoint = selection.inEndpoint;
        return true;
    }

    void closeHandle()
    {
        if (handle == nullptr) return;
        if (interfaceClaimed) libusb_release_interface(handle, interfaceNumber);
        libusb_close(handle);
        handle = nullptr;
        interfaceClaimed = false;
    }

    libusb_context* context = nullptr;
    libusb_device_handle* handle = nullptr;
    quint32 openIndex = 0U;
    int interfaceNumber = -1;
    int alternateSetting = 0;
    quint8 outEndpoint = 0;
    quint8 inEndpoint = 0;
    QByteArray identitySerial;
    QByteArray identityPortPath;
    quint8 identityBus = 0;
    bool hasOpenIndex = false;
    bool hasIdentity = false;
    bool interfaceClaimed = false;
};

QString usbErrorText(const int status)
{
    return QString::fromLatin1(libusb_error_name(status));
}

bool LibusbRuntime::initialize(QString* error)
{
    d_.reset(new Private);
    const int status = libusb_init(&d_->context);
    if (status != LIBUSB_SUCCESS) {
        if (error != nullptr) {
            *error = QStringLiteral("libusb 初始化失败: %1 (%2)").arg(usbErrorText(status)).arg(status);
        }
        d_.reset();
        return false;
    }
    return true;
}

QList<LibusbDeviceInfo> LibusbRuntime::enumerate(QString* error) const
{
    QList<LibusbDeviceInfo> devices;
    if (!isInitialized()) {
        if (error != nullptr) *error = QStringLiteral("libusb 尚未初始化");
        return devices;
    }

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(d_->context, &list);
    if (count < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("FT601 libusb 枚举失败: %1 (%2)")
                         .arg(usbErrorText(static_cast<int>(count))).arg(count);
        }
        return devices;
    }

    for (ssize_t listIndex = 0; listIndex < count; ++listIndex) {
        libusb_device_descriptor descriptor = {};
        if (!Private::isFt601(list[listIndex], &descriptor)) continue;

        LibusbDeviceInfo info;
        info.index = static_cast<quint32>(devices.size());
        info.vendorId = descriptor.idVendor;
        info.productId = descriptor.idProduct;
        info.busNumber = libusb_get_bus_number(list[listIndex]);
        info.deviceAddress = libusb_get_device_address(list[listIndex]);
        info.usbSpeed = usbSpeedText(list[listIndex]);
        CaptureUsbInterface captureInterface;
        info.captureInterfaceAvailable = findCaptureUsbInterface(
            list[listIndex], &captureInterface, &info.captureInterfaceError);
        if (info.captureInterfaceAvailable) {
            info.interfaceNumber = captureInterface.interfaceNumber;
            info.alternateSetting = captureInterface.alternateSetting;
            info.outEndpoint = captureInterface.outEndpoint;
            info.inEndpoint = captureInterface.inEndpoint;
        }
        libusb_device_handle* temporaryHandle = nullptr;
        if (libusb_open(list[listIndex], &temporaryHandle) == LIBUSB_SUCCESS) {
            unsigned char text[256] = {};
            if (descriptor.iSerialNumber != 0 &&
                libusb_get_string_descriptor_ascii(temporaryHandle, descriptor.iSerialNumber,
                                                    text, sizeof(text)) > 0) {
                info.serialNumber = QString::fromLatin1(reinterpret_cast<const char*>(text));
            }
            if (descriptor.iProduct != 0 &&
                libusb_get_string_descriptor_ascii(temporaryHandle, descriptor.iProduct,
                                                    text, sizeof(text)) > 0) {
                info.description = QString::fromLatin1(reinterpret_cast<const char*>(text));
            }
            libusb_close(temporaryHandle);
        }
        devices.append(info);
    }
    libusb_free_device_list(list, 1);
    if (devices.isEmpty() && error != nullptr) {
        *error = QStringLiteral(
            "未枚举到 FT601 libusb 设备 (VID=0x0403 PID=0x601f)；Windows 请确认设备已绑定 WinUSB/libusbK 驱动");
    }
    return devices;
}

bool LibusbRuntime::isInitialized() const { return !d_.isNull() && d_->context != nullptr; }

bool LibusbRuntime::open(const quint32 index, QString* error)
{
    if (!isInitialized()) {
        if (error != nullptr) *error = QStringLiteral("libusb 尚未初始化，无法打开 FT601");
        return false;
    }
    close();

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(d_->context, &list);
    if (count < 0) {
        if (error != nullptr) *error = QStringLiteral("FT601 libusb 枚举失败: %1").arg(count);
        return false;
    }

    quint32 matchedIndex = 0U;
    int status = LIBUSB_ERROR_NOT_FOUND;
    libusb_device* selectedDevice = nullptr;
    libusb_device_descriptor selectedDescriptor = {};
    for (ssize_t listIndex = 0; listIndex < count; ++listIndex) {
        libusb_device_descriptor descriptor = {};
        if (!Private::isFt601(list[listIndex], &descriptor)) continue;
        if (matchedIndex++ != index) continue;
        status = libusb_open(list[listIndex], &d_->handle);
        selectedDevice = list[listIndex];
        selectedDescriptor = descriptor;
        if (status == LIBUSB_SUCCESS) {
            QString interfaceError;
            if (d_->selectCaptureInterface(selectedDevice, &interfaceError)) {
                d_->rememberIdentity(selectedDevice, selectedDescriptor);
            } else {
                if (error != nullptr) *error = interfaceError;
                d_->closeHandle();
                status = LIBUSB_ERROR_NOT_FOUND;
            }
        }
        break;
    }
    libusb_free_device_list(list, 1);
    if (status != LIBUSB_SUCCESS || d_->handle == nullptr) {
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("无法按索引打开 FT601: index=%1, libusb=%2 (%3)")
                         .arg(index).arg(usbErrorText(status)).arg(status);
        }
        d_->handle = nullptr;
        return false;
    }

    libusb_set_auto_detach_kernel_driver(d_->handle, 1);
    status = libusb_claim_interface(d_->handle, d_->interfaceNumber);
    if (status != LIBUSB_SUCCESS) {
        if (error != nullptr) {
            *error = QStringLiteral("无法 claim FT601 USB 接口 %1: %2 (%3)；Windows 请使用 WinUSB/libusbK 驱动")
                         .arg(d_->interfaceNumber).arg(usbErrorText(status)).arg(status);
        }
        d_->closeHandle();
        return false;
    }
    d_->interfaceClaimed = true;
    if (d_->alternateSetting != 0) {
        status = libusb_set_interface_alt_setting(
            d_->handle, d_->interfaceNumber, d_->alternateSetting);
        if (status != LIBUSB_SUCCESS) {
            if (error != nullptr) {
                *error = QStringLiteral("无法选择 FT601 USB 接口 %1 alternate setting %2: %3 (%4)")
                             .arg(d_->interfaceNumber).arg(d_->alternateSetting)
                             .arg(usbErrorText(status)).arg(status);
            }
            d_->closeHandle();
            return false;
        }
    }
    d_->openIndex = index;
    d_->hasOpenIndex = true;
    return true;
}

bool LibusbRuntime::writePipe(const quint8 pipeId, const QByteArray& bytes, int* transferred,
                              QString* error)
{
    int count = 0;
    const int status = !isOpen() ? LIBUSB_ERROR_NO_DEVICE : libusb_bulk_transfer(
        d_->handle, pipeId,
        reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.constData())),
        bytes.size(), &count, kUsbTransferTimeoutMs);
    if (transferred != nullptr) *transferred = count;
    if (status != LIBUSB_SUCCESS || count != bytes.size()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "FT601 bulk 写入失败或不完整: libusb=%1 (%2), endpoint=0x%3, requested=%4, transferred=%5")
                         .arg(usbErrorText(status)).arg(status)
                         .arg(pipeId, 2, 16, QLatin1Char('0')).arg(bytes.size()).arg(count);
        }
        return false;
    }
    return true;
}

QByteArray LibusbRuntime::readPipe(const quint8 pipeId, const int maximumBytes, QString* error)
{
    const CapturePipeReadResult result = readPipeDetailed(pipeId, maximumBytes);
    if ((result.fatalError || result.pendingTimeout) && error != nullptr) *error = result.error;
    return result.data;
}

CapturePipeReadResult LibusbRuntime::readPipeDetailed(const quint8 pipeId, const int maximumBytes)
{
    CapturePipeReadResult result;
    result.pipeId = pipeId;
    result.requestedBytes = maximumBytes;
    if (!isOpen() || maximumBytes <= 0) {
        result.status = !isOpen() ? LIBUSB_ERROR_NO_DEVICE : LIBUSB_ERROR_INVALID_PARAM;
        result.fatalError = true;
        result.error = QStringLiteral("FT601 libusb 传输未打开或读取长度无效");
        return result;
    }

    QByteArray bytes(maximumBytes, '\0');
    int count = 0;
    const int status = libusb_bulk_transfer(
        d_->handle, pipeId, reinterpret_cast<unsigned char*>(bytes.data()), bytes.size(),
        &count, kUsbTransferTimeoutMs);
    result.status = status;
    result.transferredBytes = count;
    if (count > 0) {
        bytes.resize(count);
        result.data = bytes;
    }
    if (status != LIBUSB_SUCCESS && !(status == LIBUSB_ERROR_TIMEOUT && count > 0)) {
        result.pendingTimeout = status == LIBUSB_ERROR_TIMEOUT;
        result.fatalError = !result.pendingTimeout;
        result.error = QStringLiteral(
            "FT601 bulk 读取失败: libusb=%1 (%2), endpoint=0x%3, requested=%4, transferred=%5")
                           .arg(usbErrorText(status)).arg(status)
                           .arg(pipeId, 2, 16, QLatin1Char('0')).arg(maximumBytes).arg(count);
    }
    return result;
}

void LibusbRuntime::close()
{
    if (!d_.isNull()) d_->closeHandle();
}

bool LibusbRuntime::reopen(QString* error)
{
    if (!isInitialized() || !d_->hasOpenIndex || !d_->hasIdentity) {
        if (error != nullptr) *error = QStringLiteral("FT601 缺少可重连的稳定 USB 身份");
        return false;
    }
    close();

    libusb_device** list = nullptr;
    const ssize_t count = libusb_get_device_list(d_->context, &list);
    int status = count < 0 ? static_cast<int>(count) : LIBUSB_ERROR_NOT_FOUND;
    for (ssize_t listIndex = 0; listIndex < count; ++listIndex) {
        if (!d_->matchesIdentity(list[listIndex])) continue;
        status = libusb_open(list[listIndex], &d_->handle);
        if (status == LIBUSB_SUCCESS && !d_->selectCaptureInterface(list[listIndex], error)) {
            d_->closeHandle();
            status = LIBUSB_ERROR_NOT_FOUND;
        }
        break;
    }
    if (list != nullptr) libusb_free_device_list(list, 1);
    if (status != LIBUSB_SUCCESS || d_->handle == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("无法按稳定 USB 身份重连 FT601: %1 (%2)")
                         .arg(usbErrorText(status)).arg(status);
        }
        d_->handle = nullptr;
        return false;
    }
    libusb_set_auto_detach_kernel_driver(d_->handle, 1);
    status = libusb_claim_interface(d_->handle, d_->interfaceNumber);
    if (status != LIBUSB_SUCCESS) {
        if (error != nullptr) {
            *error = QStringLiteral("FT601 重连后无法 claim USB 接口: %1 (%2)")
                         .arg(usbErrorText(status)).arg(status);
        }
        d_->closeHandle();
        return false;
    }
    d_->interfaceClaimed = true;
    if (d_->alternateSetting != 0) {
        status = libusb_set_interface_alt_setting(
            d_->handle, d_->interfaceNumber, d_->alternateSetting);
        if (status != LIBUSB_SUCCESS) {
            if (error != nullptr) {
                *error = QStringLiteral("FT601 重连后无法选择 alternate setting: %1 (%2)")
                             .arg(usbErrorText(status)).arg(status);
            }
            d_->closeHandle();
            return false;
        }
    }
    return true;
}

bool LibusbRuntime::isOpen() const
{
    return isInitialized() && d_->handle != nullptr && d_->interfaceClaimed;
}

bool exportScalarVcd(const SamplingCaptureRecord& capture, const QString& taskRootPath,
                     QString* vcdPath, QString* sidecarPath, QString* error)
{
    if (!capture.samples.isEmpty() && capture.samples.size() != static_cast<int>(capture.actualSampleCount)) {
        if (error != nullptr) *error = QStringLiteral("VCD 导出样本数量不一致");
        return false;
    }
    const QString waveformDir = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    const QString evidenceDir = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(waveformDir) || !QDir().mkpath(evidenceDir)) {
        if (error != nullptr) *error = QStringLiteral("无法创建采集产物目录");
        return false;
    }
    const QString outputVcd = QDir(waveformDir).filePath(QStringLiteral("capture.vcd"));
    QByteArray vcd;
    vcd += "$date\n  " + QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8() + "\n$end\n";
    if (capture.sampleRateHz == 0U) {
        if (error != nullptr) *error = QStringLiteral("VCD 导出缺少有效采样率");
        return false;
    }
    vcd += "$version lockstep_ui_preview native FT601 capture $end\n$timescale 1 ps $end\n";
    vcd += "$scope module noelv_lockstep_1024 $end\n";
    for (int channel = 0; channel < static_cast<int>(kCapturePhysicalChannels); ++channel) {
        vcd += "$var wire 1 " + identifierForChannel(channel).toUtf8() + " CH" + QByteArray::number(channel) + " $end\n";
    }
    vcd += "$upscope $end\n$enddefinitions $end\n";
    QByteArray previous(kCaptureSampleBytes, static_cast<char>(0xff));
    for (int sampleIndex = 0; sampleIndex < capture.samples.size(); ++sampleIndex) {
        const QByteArray& sample = capture.samples.at(sampleIndex);
        const quint64 timestampPs =
            (static_cast<quint64>(sampleIndex) * 1'000'000'000'000ULL + capture.sampleRateHz / 2U) /
            capture.sampleRateHz;
        vcd += "#" + QByteArray::number(timestampPs) + "\n";
        for (int channel = 0; channel < static_cast<int>(kCapturePhysicalChannels); ++channel) {
            const int byteIndex = channel / 8;
            const int bitIndex = channel % 8;
            const bool value = (static_cast<quint8>(sample.at(byteIndex)) & (1U << bitIndex)) != 0U;
            const bool oldValue = (static_cast<quint8>(previous.at(byteIndex)) & (1U << bitIndex)) != 0U;
            if (sampleIndex == 0 || value != oldValue) {
                vcd += value ? '1' : '0';
                vcd += identifierForChannel(channel).toUtf8() + "\n";
            }
        }
        previous = sample;
    }
    if (!saveFile(outputVcd, vcd, error)) return false;

    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v2"));
    sidecar.insert(QStringLiteral("trace_profile_id"), QStringLiteral("noelv_lockstep_1024"));
    sidecar.insert(QStringLiteral("capture_id"), static_cast<qint64>(capture.captureId));
    sidecar.insert(QStringLiteral("sample_rate_hz"), static_cast<qint64>(capture.sampleRateHz));
    sidecar.insert(QStringLiteral("sample_word_bits"), static_cast<qint64>(kCaptureSampleWordBits));
    sidecar.insert(QStringLiteral("physical_channels"), static_cast<qint64>(kCapturePhysicalChannels));
    sidecar.insert(QStringLiteral("actual_sample_count"), static_cast<qint64>(capture.actualSampleCount));
    sidecar.insert(QStringLiteral("window_start_index"), static_cast<qint64>(capture.windowStartIndex));
    sidecar.insert(QStringLiteral("trigger_index"), static_cast<qint64>(capture.triggerIndex));
    sidecar.insert(QStringLiteral("stop_reason"), static_cast<qint64>(capture.stopReason));
    sidecar.insert(QStringLiteral("vcd"), QStringLiteral("waveform/capture.vcd"));
    const QString outputSidecar = QDir(evidenceDir).filePath(QStringLiteral("capture_sidecar.json"));
    if (!saveFile(outputSidecar, QJsonDocument(sidecar).toJson(QJsonDocument::Indented), error)) return false;
    if (vcdPath != nullptr) *vcdPath = outputVcd;
    if (sidecarPath != nullptr) *sidecarPath = outputSidecar;
    return true;
}

bool SamplingCaptureSession::configure(CaptureTransport* const transport, const SamplingCaptureConfig& config,
                                  QString* const error) const
{
    if (transport == nullptr || !transport->isOpen() || !config.validate(error)) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("采集配置会话参数无效");
        return false;
    }
    CaptureFrameCodec codec;
    int transferred = 0;
    const auto sendCommand = [&](const CaptureFrameType type, const QByteArray& payload,
                                 const quint32 sequence) {
        return transport->writePipe(0x02U, codec.encode(type, payload, sequence),
                                    &transferred, error);
    };
    quint32 nextResponseSequence = 0U;
    bool hasResponseSequence = false;
    const auto readResponse = [&](const CaptureFrameType expected, const QString& phase,
                                  CaptureFrame* const matched) {
        QElapsedTimer responseTimer;
        responseTimer.start();
        while (responseTimer.elapsed() < 2'000) {
            const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kUsbReadChunkBytes);
            if (read.fatalError) {
                if (error != nullptr) *error = read.error;
                return false;
            }
            if (read.pendingTimeout || read.data.isEmpty()) continue;
            const CaptureDecodeResult decoded = codec.feed(read.data);
            if (!decoded.success) {
                if (error != nullptr) {
                    *error = QStringLiteral("采集帧解析失败: %1; bytes=%2; head=%3")
                                 .arg(decoded.error)
                                 .arg(read.data.size())
                                 .arg(QString::fromLatin1(read.data.left(64).toHex()));
                }
                return false;
            }
            for (const CaptureFrame& frame : decoded.frames) {
                if (frame.header.type == CaptureFrameType::ErrorResponse) {
                    if (error != nullptr) *error = QStringLiteral("设备拒绝采集配置命令");
                    return false;
                }
                if (frame.header.type == expected) {
                    if (hasResponseSequence && frame.header.sequence != nextResponseSequence) {
                        if (error != nullptr) {
                            *error = QStringLiteral("采集响应序号不一致: expected=%1 actual=%2 type=0x%3")
                                         .arg(nextResponseSequence)
                                         .arg(frame.header.sequence)
                                         .arg(static_cast<quint32>(frame.header.type), 4, 16, QLatin1Char('0'));
                        }
                        return false;
                    }
                    nextResponseSequence = frame.header.sequence + 1U;
                    hasResponseSequence = true;
                    if (matched != nullptr) *matched = frame;
                    return true;
                }
            }
        }
        if (error != nullptr) {
            *error = QStringLiteral("采集设备响应超时: phase=%1, expected=0x%2")
                         .arg(phase)
                         .arg(static_cast<quint32>(expected), 4, 16, QLatin1Char('0'));
        }
        return false;
    };

    CaptureFrame response;
    if (!sendCommand(CaptureFrameType::HelloRequest, QByteArray(), 0U) ||
        !readResponse(CaptureFrameType::HelloResponse, QStringLiteral("hello"), &response) ||
        response.payload.size() < 12 ||
        read32(response.payload.constData()) != kCaptureProtocolVersion ||
        read32(response.payload.constData() + 4) != kCapturePhysicalChannels ||
        read32(response.payload.constData() + 8) != kCaptureSampleWordBits) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("采集设备 HELLO 能力不匹配");
        return false;
    }
    if (!sendCommand(CaptureFrameType::ConfigCapture, config.toPayload(), 1U) ||
        !readResponse(CaptureFrameType::StatusResponse, QStringLiteral("config"), &response) ||
        response.payload.size() < 64 ||
        read32(response.payload.constData()) != 2U ||
        read32(response.payload.constData() + 4) != 1U ||
        read32(response.payload.constData() + 24) != 0U ||
        !sendCommand(CaptureFrameType::GetStatus, QByteArray(), 2U) ||
        !readResponse(CaptureFrameType::StatusResponse, QStringLiteral("get_status"), &response) ||
        response.payload.size() < 64 ||
        read32(response.payload.constData()) != 2U ||
        read32(response.payload.constData() + 4) != 2U ||
        read32(response.payload.constData() + 24) != 0U) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("CONFIG_CAPTURE 未进入 CONFIGURED 状态");
        return false;
    }
    return true;
}

bool SamplingCaptureSession::queryStatus(CaptureTransport* const transport, CaptureStatusV2* const status,
                                         QString* const error) const
{
    if (transport == nullptr || !transport->isOpen() || status == nullptr) {
        if (error != nullptr) *error = QStringLiteral("STATUS 查询参数无效");
        return false;
    }
    CaptureFrameCodec codec;
    int transferred = 0;
    if (!transport->writePipe(0x02U, codec.encode(CaptureFrameType::GetStatus, QByteArray(), 100U),
                              &transferred, error)) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2'000) {
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kStatusResponseBytes);
        if (read.fatalError) {
            if (error != nullptr) *error = read.error;
            return false;
        }
        if (read.pendingTimeout || read.data.isEmpty()) continue;
        const CaptureDecodeResult decoded = codec.feed(read.data);
        if (!decoded.success) {
            if (error != nullptr) *error = decoded.error;
            return false;
        }
        for (const CaptureFrame& frame : decoded.frames) {
            if (frame.header.type == CaptureFrameType::ErrorResponse) {
                if (error != nullptr) *error = QStringLiteral("设备拒绝 GET_STATUS");
                return false;
            }
            if (frame.header.type == CaptureFrameType::StatusResponse) {
                return parseStatusV2(frame, status, error);
            }
        }
    }
    if (error != nullptr) *error = QStringLiteral("GET_STATUS 响应超时");
    return false;
}

bool SamplingCaptureSession::stopAndRecover(CaptureTransport* const transport, CaptureStatusV2* const status,
                                            QString* const error) const
{
    if (transport == nullptr || !transport->isOpen()) {
        if (error != nullptr) *error = QStringLiteral("采集恢复时传输未打开");
        return false;
    }
    CaptureStatusV2 current;
    QString statusError;
    const bool initialStatusRead = queryStatus(transport, &current, &statusError);
    const bool captureActive = !initialStatusRead ||
        (current.state == 3U || current.state == 4U || current.state == 5U || current.state == 6U);

    CaptureFrameCodec codec;
    int transferred = 0;
    bool stopSent = !captureActive;
    bool endSeen = !captureActive;
    QString recoveryDetail;
    if (captureActive) {
        stopSent = transport->writePipe(
            0x02U, codec.encode(CaptureFrameType::StopCapture, QByteArray(), 101U), &transferred,
            &recoveryDetail);
        if (stopSent) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 2'000 && !endSeen) {
                const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kUsbReadChunkBytes);
                if (read.fatalError) {
                    recoveryDetail = read.error;
                    break;
                }
                if (read.pendingTimeout || read.data.isEmpty()) continue;
                const CaptureDecodeResult decoded = codec.feed(read.data);
                if (!decoded.success) {
                    recoveryDetail = decoded.error;
                    break;
                }
                for (const CaptureFrame& frame : decoded.frames) {
                    if (frame.header.type == CaptureFrameType::CaptureEnd) endSeen = true;
                    if (frame.header.type == CaptureFrameType::ErrorResponse) {
                        recoveryDetail = QStringLiteral("STOP_CAPTURE 返回 ERROR_RSP");
                    }
                }
            }
        }
    }

    CaptureStatusV2 beforeReopen;
    bool configuredBeforeReopen = false;
    QElapsedTimer statusTimer;
    statusTimer.start();
    while (statusTimer.elapsed() < 2'000) {
        QString queryError;
        if (queryStatus(transport, &beforeReopen, &queryError) &&
            (beforeReopen.state == 1U || beforeReopen.state == 2U)) {
            configuredBeforeReopen = true;
            break;
        }
        if (!queryError.isEmpty()) recoveryDetail = queryError;
    }

    QString reopenError;
    const bool reopened = transport->reopen(&reopenError);
    CaptureStatusV2 recovered;
    QString finalQueryError;
    const bool finalStatusRead = reopened && queryStatus(transport, &recovered, &finalQueryError);
    const bool finalConfigured = finalStatusRead && (recovered.state == 1U || recovered.state == 2U);
    if (status != nullptr) *status = finalStatusRead ? recovered : beforeReopen;
    if (stopSent && endSeen && configuredBeforeReopen && finalConfigured) return true;

    QStringList details;
    if (!initialStatusRead && !statusError.isEmpty()) details.append(statusError);
    if (!stopSent) details.append(recoveryDetail.isEmpty() ? QStringLiteral("STOP_CAPTURE 写入失败") : recoveryDetail);
    if (!endSeen) details.append(QStringLiteral("STOP_CAPTURE 未收到 CAPTURE_END"));
    if (!configuredBeforeReopen) details.append(QStringLiteral("STOP 后 2 秒内未回到 CONFIGURED"));
    if (!reopened) details.append(reopenError.isEmpty() ? QStringLiteral("libusb 重连失败") : reopenError);
    if (reopened && !finalStatusRead) details.append(finalQueryError.isEmpty()
        ? QStringLiteral("libusb 重连后状态查询失败") : finalQueryError);
    if (finalStatusRead && !finalConfigured) {
        details.append(QStringLiteral("libusb 重连后状态仍为 %1").arg(recovered.state));
    }
    if (error != nullptr) *error = details.join(QStringLiteral("; "));
    return false;
}

CaptureSessionResult SamplingCaptureSession::runDetailed(
    CaptureTransport* const transport, const SamplingCaptureConfig& config,
    const QString& taskRootPath, const int timeoutMs, SamplingCaptureRecord* const record) const
{
    CaptureSessionResult result;
    QString error;
    if (transport == nullptr || !transport->isOpen() || record == nullptr || timeoutMs <= 0) {
        result.phase = QStringLiteral("validation");
        result.message = QStringLiteral("采集会话参数无效");
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    if (!configure(transport, config, &error)) {
        result.phase = QStringLiteral("configure");
        result.message = error;
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    quint32 captureId = 0U;
    const bool armOk = armAndWaitAccepted(transport, 3U, &captureId, &error);
    if (!armOk) {
        result.phase = QStringLiteral("arm");
    } else if (!collect(transport, taskRootPath, timeoutMs, record, &error)) {
        result.phase = QStringLiteral("collect");
    } else {
        result.success = true;
        result.phase = QStringLiteral("complete");
        result.message.clear();
        queryStatus(transport, &result.status, nullptr);
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    result.message = error;
    result.recoveryAttempted = true;
    QString recoveryError;
    result.recoverySucceeded = stopAndRecover(transport, &result.status, &recoveryError);
    if (!result.recoverySucceeded && !recoveryError.isEmpty()) {
        result.message += QStringLiteral("; CAPTURE_RECOVERY_FAILED: ") + recoveryError;
    }
    saveSessionEvidence(taskRootPath, result);
    return result;
}

bool SamplingCaptureSession::run(CaptureTransport* transport, const SamplingCaptureConfig& config,
                            const QString& taskRootPath, const int timeoutMs,
                            SamplingCaptureRecord* record, QString* error) const
{
    const CaptureSessionResult result = runDetailed(transport, config, taskRootPath, timeoutMs, record);
    if (error != nullptr) *error = result.message;
    return result.success;
}

bool SamplingCaptureSession::armAndWaitAccepted(CaptureTransport* transport,
                                                const quint32 commandSequence,
                                                quint32* captureId,
                                                QString* error) const
{
    if (transport == nullptr || !transport->isOpen()) {
        if (error != nullptr) *error = QStringLiteral("capture transport is not open");
        return false;
    }
    CaptureFrameCodec codec;
    int transferred = 0;
    if (!transport->writePipe(0x02U, codec.encode(CaptureFrameType::ArmCapture, QByteArray(), commandSequence),
                              &transferred, error)) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000) {
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kStatusResponseBytes);
        if (read.fatalError) {
            if (error != nullptr) *error = read.error;
            return false;
        }
        if (read.pendingTimeout || read.data.isEmpty()) continue;
        const CaptureDecodeResult decoded = codec.feed(read.data);
        if (!decoded.success) {
            if (error != nullptr) *error = decoded.error;
            return false;
        }
        for (const CaptureFrame& frame : decoded.frames) {
            if (frame.header.type == CaptureFrameType::ErrorResponse) {
                if (error != nullptr) *error = QStringLiteral("device rejected ARM_CAPTURE");
                return false;
            }
            if (frame.header.type != CaptureFrameType::StatusResponse || frame.payload.size() < 64) continue;
            const quint32 state = read32(frame.payload.constData());
            if (state != 3U && state != 4U) {
                if (error != nullptr) *error = QStringLiteral("ARM_CAPTURE was not accepted, state=%1").arg(state);
                return false;
            }
            if (read32(frame.payload.constData() + 4) != commandSequence) {
                if (error != nullptr) *error = QStringLiteral("ARM 状态响应请求序号不一致");
                return false;
            }
            if (captureId != nullptr) *captureId = read32(frame.payload.constData() + 8);
            return true;
        }
    }
    if (error != nullptr) *error = QStringLiteral("timed out waiting for ARM_CAPTURE acceptance");
    return false;
}

bool SamplingCaptureSession::collect(CaptureTransport* transport, const QString& taskRootPath,
                                     const int timeoutMs, SamplingCaptureRecord* record,
                                     QString* error) const
{
    if (transport == nullptr || !transport->isOpen() || record == nullptr || timeoutMs <= 0) {
        if (error != nullptr) *error = QStringLiteral("capture collection arguments are invalid");
        return false;
    }
    CaptureFrameCodec codec;

    QByteArray rawCapture;
    const auto persistRawCapture = [&]() {
        const QString evidenceDir = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
        if (QDir().mkpath(evidenceDir)) {
            saveFile(QDir(evidenceDir).filePath(QStringLiteral("raw_capture.dat")), rawCapture, nullptr);
        }
    };
    SamplingCaptureAssembler assembler;
    bool captureStarted = false;
    QElapsedTimer timer;
    timer.start();
    while (!assembler.complete() && timer.elapsed() < timeoutMs) {
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kUsbReadChunkBytes);
        if (read.fatalError) {
            persistRawCapture();
            if (error != nullptr) *error = read.error;
            return false;
        }
        if (read.pendingTimeout || read.data.isEmpty()) continue;
        const QByteArray& chunk = read.data;
        rawCapture.append(chunk);
        const CaptureDecodeResult decoded = codec.feed(chunk);
        if (!decoded.success) {
            persistRawCapture();
            if (error != nullptr) *error = decoded.error;
            return false;
        }
        for (const CaptureFrame& frame : decoded.frames) {
            if (frame.header.type == CaptureFrameType::ErrorResponse) {
                persistRawCapture();
                if (error != nullptr) *error = QStringLiteral("采集流返回 ERROR_RSP");
                return false;
            }
            if (frame.header.type == CaptureFrameType::CaptureMeta) captureStarted = true;
            if ((captureStarted || frame.header.type == CaptureFrameType::CaptureEnd) &&
                !assembler.append(frame, error)) {
                persistRawCapture();
                return false;
            }
        }
    }
    if (!assembler.complete()) {
        persistRawCapture();
        if (error != nullptr) *error = QStringLiteral("等待 CAPTURE_END 超时");
        return false;
    }
    const QString evidenceDir = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidenceDir) ||
        !saveFile(QDir(evidenceDir).filePath(QStringLiteral("raw_capture.dat")), rawCapture, error)) {
        return false;
    }
    *record = assembler.record();
    if (record->stopReason != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("采集未完成: stop_reason=%1 flags=0x%2")
                         .arg(record->stopReason)
                         .arg(record->deviceStatusFlags, 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    return exportScalarVcd(*record, taskRootPath, nullptr, nullptr, error);
}

}  // namespace lockstep::acquisition
