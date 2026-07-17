/**********************************************************
* 文件名: sampling_capture.cpp
* 日期: 2026-07-16
* 版本: v1.3
* 更新记录: 打开 FT601 时不再中止或清空数据管道，避免破坏后续同步传输。
* 描述: 实现线协议校验、采集组装、D3XX 动态枚举和标量 VCD/sidecar 输出。
**********************************************************/

#include "sampling_capture.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QSaveFile>
#include <QSharedPointer>
#include <QtEndian>

namespace lockstep::acquisition {
namespace {

constexpr quint32 kMaxPayloadBytes = 4U * 1024U * 1024U;
constexpr quint32 kCrcDisabledFlag = 1U << 1U;
constexpr int kD3xxReadChunkBytes = 4096;
constexpr int kStatusResponseBytes = 64;

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

class D3xxRuntime::Private final {
public:
    QLibrary library;
    using CreateDeviceInfoList = unsigned long (*)(unsigned long*);
    using GetDeviceInfoDetail = unsigned long (*)(unsigned long, unsigned long*, unsigned long*,
        unsigned long*, unsigned long*, void*, void*, void**);
    using Create = unsigned long (*)(void*, unsigned long, void**);
    using Close = unsigned long (*)(void*);
    using PipeIo = unsigned long (*)(void*, unsigned char, unsigned char*, unsigned long,
                                     unsigned long*, void*);
    using SetPipeTimeout = unsigned long (*)(void*, unsigned char, unsigned long);
    CreateDeviceInfoList createDeviceInfoList = nullptr;
    GetDeviceInfoDetail getDeviceInfoDetail = nullptr;
    Create create = nullptr;
    Close close = nullptr;
    PipeIo readPipe = nullptr;
    PipeIo writePipe = nullptr;
    SetPipeTimeout setPipeTimeout = nullptr;
    void* handle = nullptr;
};

bool D3xxRuntime::load(QString* error)
{
    d_.reset(new Private);
    QString libraryPath = qEnvironmentVariable("LOCKSTEP_FTD3XX_LIBRARY");
    if (libraryPath.isEmpty()) {
        const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("FTD3XX.dll"));
        libraryPath = QFileInfo::exists(bundled) ? bundled : QStringLiteral("FTD3XX");
    }
    d_->library.setFileName(libraryPath);
    if (!d_->library.load()) {
        if (error != nullptr) *error = QStringLiteral("无法加载 FTD3XX 动态库: %1").arg(d_->library.errorString());
        return false;
    }
    d_->createDeviceInfoList = reinterpret_cast<Private::CreateDeviceInfoList>(d_->library.resolve("FT_CreateDeviceInfoList"));
    d_->getDeviceInfoDetail = reinterpret_cast<Private::GetDeviceInfoDetail>(d_->library.resolve("FT_GetDeviceInfoDetail"));
    d_->create = reinterpret_cast<Private::Create>(d_->library.resolve("FT_Create"));
    d_->close = reinterpret_cast<Private::Close>(d_->library.resolve("FT_Close"));
    d_->readPipe = reinterpret_cast<Private::PipeIo>(d_->library.resolve("FT_ReadPipe"));
    d_->writePipe = reinterpret_cast<Private::PipeIo>(d_->library.resolve("FT_WritePipe"));
    d_->setPipeTimeout = reinterpret_cast<Private::SetPipeTimeout>(d_->library.resolve("FT_SetPipeTimeout"));
    if (d_->createDeviceInfoList == nullptr || d_->getDeviceInfoDetail == nullptr ||
        d_->create == nullptr || d_->close == nullptr || d_->readPipe == nullptr || d_->writePipe == nullptr) {
        if (error != nullptr) *error = QStringLiteral("FTD3XX 缺少设备枚举接口");
        d_.reset();
        return false;
    }
    return true;
}

QList<D3xxDeviceInfo> D3xxRuntime::enumerate(QString* error) const
{
    QList<D3xxDeviceInfo> devices;
    if (!isLoaded()) {
        if (error != nullptr) *error = QStringLiteral("FTD3XX 尚未加载");
        return devices;
    }
    unsigned long count = 0;
    if (d_->createDeviceInfoList(&count) != 0U) {
        if (error != nullptr) *error = QStringLiteral("FT601 枚举失败");
        return devices;
    }
    for (unsigned long index = 0; index < count; ++index) {
        char serial[64] = {};
        char description[128] = {};
        unsigned long flags = 0, type = 0, id = 0, location = 0;
        void* handle = nullptr;
        if (d_->getDeviceInfoDetail(index, &flags, &type, &id, &location, serial, description, &handle) == 0U) {
            D3xxDeviceInfo info;
            info.index = static_cast<quint32>(index);
            info.serialNumber = QString::fromLocal8Bit(serial);
            info.description = QString::fromLocal8Bit(description);
            devices.append(info);
        }
    }
    return devices;
}

bool D3xxRuntime::isLoaded() const { return !d_.isNull() && d_->library.isLoaded(); }

bool D3xxRuntime::open(const quint32 index, QString* error)
{
    if (!isLoaded() || d_->create(reinterpret_cast<void*>(static_cast<quintptr>(index)), 0x10U,
                                  &d_->handle) != 0U) {
        if (error != nullptr) *error = QStringLiteral("无法按索引打开 FT601");
        return false;
    }
    if (d_->setPipeTimeout != nullptr) {
        d_->setPipeTimeout(d_->handle, 0x02U, 2000U);
        d_->setPipeTimeout(d_->handle, 0x82U, 2000U);
    }
    return true;
}

bool D3xxRuntime::writePipe(const quint8 pipeId, const QByteArray& bytes, int* transferred, QString* error)
{
    unsigned long count = 0;
    const unsigned long status = !isOpen() ? 1U : d_->writePipe(
        d_->handle, pipeId, reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.constData())),
        static_cast<unsigned long>(bytes.size()), &count, nullptr);
    if (transferred != nullptr) *transferred = static_cast<int>(count);
    if (status != 0U || count != static_cast<unsigned long>(bytes.size())) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "FT601 pipe 写入失败或不完整: status=%1, pipe=0x%2, requested=%3, transferred=%4")
                         .arg(status)
                         .arg(pipeId, 2, 16, QLatin1Char('0'))
                         .arg(bytes.size())
                         .arg(count);
        }
        return false;
    }
    return true;
}

QByteArray D3xxRuntime::readPipe(const quint8 pipeId, const int maximumBytes, QString* error)
{
    if (!isOpen() || maximumBytes <= 0) {
        if (error != nullptr) *error = QStringLiteral("FT601 未打开或读取长度无效");
        return QByteArray();
    }
    QByteArray bytes(maximumBytes, '\0');
    unsigned long count = 0;
    const unsigned long status = d_->readPipe(
        d_->handle,
        pipeId,
        reinterpret_cast<unsigned char*>(bytes.data()),
        static_cast<unsigned long>(bytes.size()),
        &count,
        nullptr);
    if (status != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "FT601 pipe 读取失败: status=%1, pipe=0x%2, requested=%3, transferred=%4")
                         .arg(status)
                         .arg(pipeId, 2, 16, QLatin1Char('0'))
                         .arg(maximumBytes)
                         .arg(count);
        }
        return QByteArray();
    }
    bytes.resize(static_cast<int>(count));
    return bytes;
}

CapturePipeReadResult D3xxRuntime::readPipeDetailed(const quint8 pipeId, const int maximumBytes)
{
    CapturePipeReadResult result;
    result.pipeId = pipeId;
    result.requestedBytes = maximumBytes;
    if (!isOpen() || maximumBytes <= 0) {
        result.fatalError = true;
        result.error = QStringLiteral("FT601 transport is not open or read length is invalid");
        return result;
    }
    QByteArray bytes(maximumBytes, '\0');
    unsigned long count = 0;
    const unsigned long status = d_->readPipe(
        d_->handle, pipeId, reinterpret_cast<unsigned char*>(bytes.data()),
        static_cast<unsigned long>(bytes.size()), &count, nullptr);
    result.status = status;
    result.transferredBytes = static_cast<int>(count);
    if (status != 0U) {
        result.pendingTimeout = status == 19U;
        result.fatalError = !result.pendingTimeout;
        result.error = QStringLiteral(
            "FT601 pipe read failed: status=%1 pipe=0x%2 requested=%3 transferred=%4")
            .arg(status).arg(pipeId, 2, 16, QLatin1Char('0'))
            .arg(maximumBytes).arg(count);
        return result;
    }
    bytes.resize(static_cast<int>(count));
    result.data = bytes;
    return result;
}

void D3xxRuntime::close()
{
    if (isOpen()) d_->close(d_->handle);
    if (!d_.isNull()) d_->handle = nullptr;
}

bool D3xxRuntime::isOpen() const { return isLoaded() && d_->handle != nullptr; }

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
            const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kD3xxReadChunkBytes);
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
    if (queryStatus(transport, &current, &statusError)) {
        if (current.state == 1U || current.state == 2U || current.state == 7U || current.state == 8U) {
            if (status != nullptr) *status = current;
            return true;
        }
    }

    CaptureFrameCodec codec;
    int transferred = 0;
    if (!transport->writePipe(0x02U, codec.encode(CaptureFrameType::StopCapture, QByteArray(), 101U),
                              &transferred, error)) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    bool endSeen = false;
    while (timer.elapsed() < 2'000) {
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kD3xxReadChunkBytes);
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
            if (frame.header.type == CaptureFrameType::CaptureEnd) endSeen = true;
            if (frame.header.type == CaptureFrameType::ErrorResponse) {
                if (error != nullptr) *error = QStringLiteral("STOP_CAPTURE 返回 ERROR_RSP");
                return false;
            }
        }
        if (endSeen) break;
    }
    if (!endSeen) {
        if (error != nullptr) *error = QStringLiteral("STOP_CAPTURE 未收到 CAPTURE_END");
        return false;
    }
    CaptureStatusV2 recovered;
    if (!queryStatus(transport, &recovered, error)) return false;
    if (recovered.state != 1U && recovered.state != 2U) {
        if (error != nullptr) *error = QStringLiteral("采集恢复后状态仍为 %1").arg(recovered.state);
        return false;
    }
    if (status != nullptr) *status = recovered;
    return true;
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
        return result;
    }
    if (!configure(transport, config, &error)) {
        result.phase = QStringLiteral("configure");
        result.message = error;
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
        return result;
    }
    result.message = error;
    result.recoveryAttempted = true;
    QString recoveryError;
    result.recoverySucceeded = stopAndRecover(transport, &result.status, &recoveryError);
    if (!result.recoverySucceeded && !recoveryError.isEmpty()) {
        result.message += QStringLiteral("; CAPTURE_RECOVERY_FAILED: ") + recoveryError;
    }
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
    SamplingCaptureAssembler assembler;
    bool captureStarted = false;
    QElapsedTimer timer;
    timer.start();
    while (!assembler.complete() && timer.elapsed() < timeoutMs) {
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kD3xxReadChunkBytes);
        if (read.fatalError) {
            if (error != nullptr) *error = read.error;
            return false;
        }
        if (read.pendingTimeout || read.data.isEmpty()) continue;
        const QByteArray& chunk = read.data;
        rawCapture.append(chunk);
        const CaptureDecodeResult decoded = codec.feed(chunk);
        if (!decoded.success) {
            if (error != nullptr) *error = decoded.error;
            return false;
        }
        for (const CaptureFrame& frame : decoded.frames) {
            if (frame.header.type == CaptureFrameType::ErrorResponse) {
                if (error != nullptr) *error = QStringLiteral("采集流返回 ERROR_RSP");
                return false;
            }
            if (frame.header.type == CaptureFrameType::CaptureMeta) captureStarted = true;
            if (captureStarted && !assembler.append(frame, error)) return false;
        }
    }
    if (!assembler.complete()) {
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
