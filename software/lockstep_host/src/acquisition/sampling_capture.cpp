/**********************************************************
* 文件名: sampling_capture.cpp
* 日期: 2026-07-22
* 版本: v4.0
* 更新记录: 增加 v4 统一时间窗口、512-bit 样本与 16-bit 稀疏状态链校验。
* 描述: 实现 v2/v3/v4 采集帧组装、D3XX 传输及采集产物输出。
**********************************************************/

#include "sampling_capture.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QSaveFile>
#include <QSharedPointer>
#include <QStringList>
#include <QtEndian>

namespace lockstep::acquisition {
namespace {

constexpr quint32 kMaxPayloadBytes = 4U * 1024U * 1024U;
constexpr quint32 kCrcDisabledFlag = 1U << 1U;
constexpr int kCaptureReadChunkBytes = 32 * 1024;
constexpr int kStatusResponseBytes = kCaptureReadChunkBytes;

bool writeComplete(CaptureTransport* const transport, const QByteArray& command,
                   int* const transferred, QString* const error)
{
    if (!transport->writePipe(0x02U, command, transferred, error)) return false;
    if (*transferred == command.size()) return true;
    if (error != nullptr) {
        *error = QStringLiteral("capture command short write: expected=%1 actual=%2")
                     .arg(command.size()).arg(*transferred);
    }
    return false;
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

quint64 read64(const char* data)
{
    return qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data));
}

quint8 v4SourceMaskForChange(const quint32 changeMask)
{
    quint8 sourceMask = 0U;
    if ((changeMask & 0x000fU) != 0U) sourceMask |= quint8(1U << 1U);
    if ((changeMask & 0x00f0U) != 0U) sourceMask |= quint8(1U << 2U);
    if ((changeMask & 0x0300U) != 0U) sourceMask |= quint8(1U << 3U);
    if ((changeMask & 0x0c00U) != 0U) sourceMask |= quint8(1U << 4U);
    if ((changeMask & 0xf000U) != 0U) sourceMask |= quint8(1U << 7U);
    return sourceMask;
}

bool parseStatusV2(const CaptureFrame& frame, CaptureStatusV2* status, QString* error)
{
    if (frame.header.type != CaptureFrameType::StatusResponse ||
        frame.header.version != kCaptureProtocolVersion || frame.payload.size() != 64) {
        if (error != nullptr) *error = QStringLiteral("STATUS_RSP v2 版本、长度或类型错误");
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

QString toString(const EventEndReason reason)
{
    switch (reason) {
    case EventEndReason::ProgramDone: return QStringLiteral("program_done");
    case EventEndReason::HostTerminate: return QStringLiteral("host_terminate");
    case EventEndReason::Watchdog: return QStringLiteral("watchdog");
    case EventEndReason::Overflow: return QStringLiteral("overflow");
    case EventEndReason::HardTimeout: return QStringLiteral("hard_timeout");
    case EventEndReason::FatalError: return QStringLiteral("fatal_error");
    }
    return QStringLiteral("unknown");
}

bool validateCaptureCompletion(const SamplingCaptureRecord& capture, QString* const error)
{
    if (capture.stopReason != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("capture incomplete: stop_reason=%1 flags=0x%2")
                         .arg(capture.stopReason)
                         .arg(capture.deviceStatusFlags, 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    if (!capture.hasEventStream) {
        if (error != nullptr) {
            *error = QStringLiteral("capture incomplete: event stream closure is missing");
        }
        return false;
    }
    if (capture.eventEndReason == static_cast<quint32>(EventEndReason::ProgramDone)) return true;

    const EventEndReason reason = static_cast<EventEndReason>(capture.eventEndReason);
    if (error != nullptr) {
        *error = QStringLiteral("CAPTURE_END_%1: event_end_reason=%2 (%3)")
                     .arg(toString(reason).toUpper())
                     .arg(capture.eventEndReason)
                     .arg(toString(reason));
    }
    return false;
}

QByteArray CaptureFrameCodec::encode(const CaptureFrameType type, const QByteArray& payload,
                                 const quint32 sequence, const quint32 captureId,
                                 const quint32 flags, const quint16 version) const
{
    QByteArray padded = payload;
    padded.append(QByteArray((4 - (payload.size() % 4)) % 4, '\0'));
    QByteArray header;
    append32(&header, kCaptureFrameMagic);
    append16(&header, version);
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
        if ((frame.header.version != kCaptureProtocolVersion &&
             frame.header.version != kCaptureProtocolVersionV3 &&
             frame.header.version != kCaptureProtocolVersionV4) ||
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

bool SamplingCaptureConfig::validate(QString* error) const
{
    // 协议字段 posttrigger 包含触发样本；硬件内部另有 post_after_trigger=2048。
    const bool countsValid = sampleCount == 4096U && pretriggerCount == 2047U &&
                             posttriggerCount == 2049U;
    const bool rateValid = sampleRateHz == 120'000'000U;
    if (!countsValid || !rateValid || (protocolGroupMask & ~0x1ffU) != 0U || mode != 0U ||
        inputInvertMask != 0U || (triggerEdgeRise & ~1U) != 0U || triggerEdgeFall != 0U ||
        triggerTimeoutSamples == 0U || (eventEnableMask & ~0x19fU) != 0U || eventLimit != 0U ||
        eventWatchdogTicks == 0U || eventHardTimeoutTicks < eventWatchdogTicks) {
        if (error != nullptr) {
            *error = QStringLiteral("采样配置不符合 1024 路有限采集合同");
        }
        return false;
    }
    return true;
}

QJsonObject SamplingCaptureConfig::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("sample_rate_hz"), static_cast<double>(sampleRateHz));
    object.insert(QStringLiteral("sample_count"), static_cast<double>(sampleCount));
    object.insert(QStringLiteral("pretrigger"), static_cast<double>(pretriggerCount));
    object.insert(QStringLiteral("posttrigger"), static_cast<double>(posttriggerCount));
    object.insert(QStringLiteral("protocol_group_mask"), static_cast<double>(protocolGroupMask));
    object.insert(QStringLiteral("input_invert_mask"), static_cast<double>(inputInvertMask));
    object.insert(QStringLiteral("physical_channels"), static_cast<double>(kCapturePhysicalChannels));
    object.insert(QStringLiteral("sample_word_bits"), static_cast<double>(kCaptureSampleWordBits));
    object.insert(QStringLiteral("trigger_mask"), static_cast<double>(triggerMask));
    object.insert(QStringLiteral("trigger_value"), static_cast<double>(triggerValue));
    object.insert(QStringLiteral("trigger_edge_rise"), static_cast<double>(triggerEdgeRise));
    object.insert(QStringLiteral("trigger_edge_fall"), static_cast<double>(triggerEdgeFall));
    object.insert(QStringLiteral("mode"), static_cast<double>(mode));
    object.insert(QStringLiteral("trigger_timeout_samples"), static_cast<double>(triggerTimeoutSamples));
    object.insert(QStringLiteral("event_enable_mask"), static_cast<double>(eventEnableMask));
    object.insert(QStringLiteral("event_limit"), static_cast<double>(eventLimit));
    object.insert(QStringLiteral("event_watchdog_ticks"), static_cast<double>(eventWatchdogTicks));
    object.insert(QStringLiteral("event_hard_timeout_ticks"), static_cast<double>(eventHardTimeoutTicks));
    return object;
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

QByteArray SamplingCaptureConfig::toEventPayload() const
{
    QByteArray payload;
    append32(&payload, eventEnableMask);
    append32(&payload, eventLimit);
    append32(&payload, eventWatchdogTicks);
    append32(&payload, eventHardTimeoutTicks);
    return payload;
}

SamplingCaptureAssembler::SamplingCaptureAssembler(const quint32 expectedEnabledSourceMask)
    : expectedEnabledSourceMask_(expectedEnabledSourceMask)
{
}

bool SamplingCaptureAssembler::append(const CaptureFrame& frame, QString* error)
{
    const bool eventFrame = frame.header.type == CaptureFrameType::EventMeta ||
                            frame.header.type == CaptureFrameType::EventData ||
                            frame.header.type == CaptureFrameType::EventEnd;
    const bool continuousFrame = frame.header.type == CaptureFrameType::CaptureMeta ||
                                 frame.header.type == CaptureFrameType::SampleData ||
                                 frame.header.type == CaptureFrameType::CaptureEnd;
    const bool v4Frame = frame.header.version == kCaptureProtocolVersionV4;
    const bool versionValid = v4Frame ||
        (eventFrame && frame.header.version == kCaptureProtocolVersionV3) ||
        (continuousFrame && frame.header.version == kCaptureProtocolVersion);
    if (!versionValid) {
        if (error != nullptr) *error = QStringLiteral("采集帧版本与 v2/v3/v4 合同不一致");
        return false;
    }
    const int frameFamily = v4Frame ? 4 : 3;
    if (contractFamily_ == 0) {
        contractFamily_ = frameFamily;
        record_.contractVersion = v4Frame ? kCaptureProtocolVersionV4 : kCaptureProtocolVersion;
    } else if (contractFamily_ != frameFamily) {
        if (error != nullptr) *error = QStringLiteral("采集流混用 legacy 与 v4 帧");
        return false;
    }
    if (!hasAnyFrame_) {
        expectedSequence_ = frame.header.sequence;
        hasAnyFrame_ = true;
    }
    if (frame.header.sequence != expectedSequence_++) {
        if (error != nullptr) *error = QStringLiteral("采集帧序号不连续");
        return false;
    }
    if ((hasMeta_ || hasEventMeta_) && frame.header.captureId != record_.captureId) {
        if (error != nullptr) *error = QStringLiteral("采集帧 capture_id 不一致");
        return false;
    }
    if (frame.header.type == CaptureFrameType::CaptureMeta) {
        const int expectedMetaBytes = v4Frame ? 64 : 40;
        if (hasMeta_ || frame.payload.size() != expectedMetaBytes) {
            if (error != nullptr) *error = QStringLiteral("CAPTURE_META 重复或长度错误");
            return false;
        }
        const char* data = frame.payload.constData();
        const quint32 sampleRateHz = read32(data);
        const quint32 requestedSampleCount = read32(data + 4);
        const quint32 pretriggerCount = read32(data + 8);
        const quint32 posttriggerCount = read32(data + 12);
        const quint32 physicalChannels = read32(data + 24);
        const quint32 sampleWordBits = read32(data + 28);
        const quint32 windowStartIndex = read32(data + 32);
        const quint32 triggerIndex = read32(data + 36);
        const quint64 windowOriginTicks = v4Frame ? read64(data + 40) : windowStartIndex;
        const quint64 triggerTicks = v4Frame ? read64(data + 48) : triggerIndex;
        const quint64 windowEndExclusiveTicks = v4Frame
            ? read64(data + 56) : quint64(windowStartIndex) + requestedSampleCount;
        const bool v4ContractInvalid = v4Frame &&
            (requestedSampleCount != kCaptureWindowSamplesV4 || pretriggerCount != 2047U ||
             posttriggerCount != 2049U || physicalChannels != kCapturePhysicalChannelsV4 ||
             sampleWordBits != kCaptureSampleWordBitsV4 ||
             static_cast<quint32>(windowOriginTicks) != windowStartIndex ||
             static_cast<quint32>(triggerTicks) != triggerIndex ||
             triggerTicks < windowOriginTicks || triggerTicks - windowOriginTicks != 2047U ||
             windowEndExclusiveTicks < windowOriginTicks ||
             windowEndExclusiveTicks - windowOriginTicks != kCaptureWindowSamplesV4);
        const bool legacyContractInvalid = !v4Frame &&
            (physicalChannels != kCapturePhysicalChannels || sampleWordBits != kCaptureSampleWordBits);
        const bool eventMetaMismatch = hasEventMeta_ &&
            (record_.eventTimebaseHz != sampleRateHz ||
             (v4Frame && (record_.windowOriginTicks != windowOriginTicks ||
                          record_.triggerTicks != triggerTicks ||
                          record_.windowEndExclusiveTicks != windowEndExclusiveTicks)));
        if (v4ContractInvalid || legacyContractInvalid || eventMetaMismatch) {
            if (error != nullptr) *error = QStringLiteral("CAPTURE_META 能力或时间窗口合同错误");
            return false;
        }
        record_.captureId = frame.header.captureId;
        record_.sampleRateHz = sampleRateHz;
        record_.requestedSampleCount = requestedSampleCount;
        record_.physicalChannels = physicalChannels;
        record_.sampleWordBits = sampleWordBits;
        record_.sampleBytes = sampleWordBits / 8U;
        record_.windowStartIndex = windowStartIndex;
        record_.triggerIndex = triggerIndex;
        record_.windowOriginTicks = windowOriginTicks;
        record_.triggerTicks = triggerTicks;
        record_.windowEndExclusiveTicks = windowEndExclusiveTicks;
        hasMeta_ = true;
        return true;
    }
    if (frame.header.type == CaptureFrameType::EventMeta) {
        if (hasEventMeta_ || hasEnd_ || frame.payload.size() != 64) {
            if (error != nullptr) *error = QStringLiteral("EVENT_META 重复、版本或长度错误");
            return false;
        }
        const char* data = frame.payload.constData();
        const quint32 recordBytes = read32(data + 16);
        record_.captureId = frame.header.captureId;
        record_.eventTimebaseHz = read32(data + 0);
        record_.hasEventStream = true;
        record_.implementedSourceMask = read32(data + 4);
        record_.enabledSourceMask = read32(data + 8);
        record_.designGapMask = read32(data + 12);
        bool contractInvalid = false;
        if (v4Frame) {
            constexpr quint32 kSparseSourceMaskV4 = 0x09eU;
            const quint64 captureWindowOrigin = record_.windowOriginTicks;
            const quint64 captureTrigger = record_.triggerTicks;
            const quint64 captureWindowEnd = record_.windowEndExclusiveTicks;
            const quint32 layoutWord = read32(data + 20);
            record_.eventLayoutId = layoutWord & 0xffU;
            record_.eventSpiMode = (layoutWord >> 8U) & 0x3U;
            record_.eventSpiModeValid = (layoutWord & (1U << 10U)) != 0U;
            record_.eventWatchdogTicks = read32(data + 24);
            record_.eventHardTimeoutTicks = read32(data + 28);
            const quint32 initialStateWord = read32(data + 32);
            record_.eventInitialState = initialStateWord & 0xffffU;
            const quint64 eventWindowOrigin = read64(data + 36);
            const quint64 eventTrigger = read64(data + 44);
            const quint64 eventWindowEnd = read64(data + 52);
            record_.eventRetainedCount = read32(data + 60);
            v4CurrentState_ = record_.eventInitialState;
            contractInvalid = recordBytes != kCaptureEventRecordBytesV4 ||
                record_.eventLayoutId != kCaptureEventLayoutV4 ||
                (layoutWord & 0xfffff800U) != 0U ||
                (initialStateWord & 0xffff0000U) != 0U ||
                record_.implementedSourceMask != kSparseSourceMaskV4 ||
                record_.enabledSourceMask != (expectedEnabledSourceMask_ & kSparseSourceMaskV4) ||
                record_.designGapMask != 0x060U ||
                record_.eventRetainedCount > kCaptureWindowSamplesV4 ||
                eventTrigger < eventWindowOrigin || eventTrigger - eventWindowOrigin != 2047U ||
                eventWindowEnd < eventWindowOrigin ||
                eventWindowEnd - eventWindowOrigin != kCaptureWindowSamplesV4;
            if (hasMeta_) {
                contractInvalid = contractInvalid ||
                    record_.eventTimebaseHz != record_.sampleRateHz ||
                    captureWindowOrigin != eventWindowOrigin || captureTrigger != eventTrigger ||
                    captureWindowEnd != eventWindowEnd;
            }
            record_.windowOriginTicks = eventWindowOrigin;
            record_.triggerTicks = eventTrigger;
            record_.windowEndExclusiveTicks = eventWindowEnd;
            record_.windowStartIndex = static_cast<quint32>(eventWindowOrigin);
            record_.triggerIndex = static_cast<quint32>(eventTrigger);
        } else {
            contractInvalid = recordBytes != 64U ||
                record_.implementedSourceMask != 0x19fU ||
                record_.enabledSourceMask != expectedEnabledSourceMask_ ||
                record_.designGapMask != 0x060U;
        }
        if (record_.eventTimebaseHz == 0U || contractInvalid ||
            (record_.enabledSourceMask & ~record_.implementedSourceMask) != 0U ||
            (hasMeta_ && record_.eventTimebaseHz != record_.sampleRateHz)) {
            if (error != nullptr) *error = QStringLiteral("EVENT_META 能力或记录宽度合同错误");
            return false;
        }
        hasEventMeta_ = true;
        return true;
    }
    if (frame.header.type == CaptureFrameType::EventData) {
        if (v4Frame) {
            if (!hasEventMeta_ || hasEventEnd_ || hasEnd_ ||
                frame.payload.size() != static_cast<int>(kCaptureEventRecordBytesV4)) {
                if (error != nullptr) *error = QStringLiteral("EVENT_DATA v4 state or length is invalid");
                return false;
            }
            const char* data = frame.payload.constData();
            const quint32 absoluteIndexLow = read32(data);
            const quint32 globalSequence = read32(data + 4);
            const quint32 stateWord = read32(data + 8);
            const quint32 changeWord = read32(data + 12);
            const quint32 stateAfter = stateWord & 0xffffU;
            const quint8 sourceMask = static_cast<quint8>((stateWord >> 20U) & 0xffU);
            const quint32 changeMask = changeWord & 0xffffU;
            const quint8 expectedSourceMask = v4SourceMaskForChange(changeMask);
            const quint32 relativeIndex = absoluteIndexLow -
                static_cast<quint32>(record_.windowOriginTicks);
            const quint64 absoluteIndex = record_.windowOriginTicks + relativeIndex;
            const quint32 stateBefore = stateAfter ^ changeMask;
            const bool sequenceInvalid = hasV4Sequence_ && globalSequence != v4NextSequence_;
            const bool indexInvalid = relativeIndex >= kCaptureWindowSamplesV4 ||
                absoluteIndex >= record_.windowEndExclusiveTicks ||
                (hasV4AbsoluteIndex_ && absoluteIndex <= v4LastAbsoluteIndex_);
            const bool recordInvalid = (stateWord & 0xf00f0000U) != 0U ||
                (changeWord & 0xffff0000U) != 0U || changeMask == 0U ||
                sourceMask != expectedSourceMask ||
                (quint32(sourceMask) & ~record_.enabledSourceMask) != 0U ||
                stateBefore != v4CurrentState_ || sequenceInvalid || indexInvalid;
            if (recordInvalid) {
                if (error != nullptr) {
                    *error = QStringLiteral(
                        "EVENT_DATA v4 contract error: index=%1 seq=%2 state_before=0x%3 "
                        "expected_state=0x%4 change=0x%5 source=0x%6 expected_source=0x%7")
                        .arg(absoluteIndexLow).arg(globalSequence)
                        .arg(stateBefore, 4, 16, QLatin1Char('0'))
                        .arg(v4CurrentState_, 4, 16, QLatin1Char('0'))
                        .arg(changeMask, 4, 16, QLatin1Char('0'))
                        .arg(sourceMask, 2, 16, QLatin1Char('0'))
                        .arg(expectedSourceMask, 2, 16, QLatin1Char('0'));
                }
                return false;
            }
            SamplingCaptureRecord::SparseChangeRecord change;
            change.absoluteIndexLow = absoluteIndexLow;
            change.absoluteIndex = absoluteIndex;
            change.globalSequence = globalSequence;
            change.stateAfter = stateAfter;
            change.changeMask = changeMask;
            change.sourceMask = sourceMask;
            record_.sparseChanges.append(change);
            v4CurrentState_ = stateAfter;
            v4NextSequence_ = globalSequence + 1U;
            v4LastAbsoluteIndex_ = absoluteIndex;
            hasV4Sequence_ = true;
            hasV4AbsoluteIndex_ = true;
            return true;
        }
        if (!hasEventMeta_ || hasEventEnd_ || hasEnd_ || frame.payload.isEmpty() ||
            (frame.payload.size() % 64) != 0) {
            if (error != nullptr) *error = QStringLiteral("EVENT_DATA 状态或长度错误");
            return false;
        }
        for (int offset = 0; offset < frame.payload.size(); offset += 64) {
            const char* data = frame.payload.constData() + offset;
            SamplingCaptureRecord::ProtocolEvent event;
            event.timestampTicks = read64(data + 0);
            event.captureId = read32(data + 8);
            event.localSequence = read32(data + 12);
            event.protocolId = static_cast<quint8>(data[16]);
            event.eventType = static_cast<quint8>(data[17]);
            event.sourceKind = static_cast<quint8>(data[18]);
            event.flags = static_cast<quint8>(data[19]);
            event.eventReasonMask = read32(data + 20);
            const quint32 payloadLength = read32(data + 24);
            const quint32 reserved = read32(data + 28);
            bool unusedPayloadIsZero = true;
            if (payloadLength <= 32U) {
                for (quint32 index = payloadLength; index < 32U; ++index) {
                    if (data[32 + index] != '\0') {
                        unusedPayloadIsZero = false;
                        break;
                    }
                }
            }
            if (event.captureId != record_.captureId || event.protocolId >= 9U ||
                event.sourceKind > 3U ||
                (event.eventReasonMask & ~0x1ffU) != 0U ||
                (event.eventReasonMask & (1U << event.protocolId)) == 0U ||
                payloadLength > 32U || reserved != 0U || !unusedPayloadIsZero) {
                if (error != nullptr) *error = QStringLiteral("事件记录字段合同错误");
                return false;
            }
            if (hasEventSequence_[event.protocolId] &&
                event.localSequence != nextEventSequence_[event.protocolId]) {
                eventSequenceGap_ = true;
            }
            nextEventSequence_[event.protocolId] = event.localSequence + 1U;
            hasEventSequence_[event.protocolId] = true;
            event.payload = QByteArray(data + 32, static_cast<int>(payloadLength));
            record_.protocolEvents.append(event);
        }
        return true;
    }
    if (frame.header.type == CaptureFrameType::EventEnd) {
        if (!hasEventMeta_ || hasEventEnd_ || hasEnd_ || frame.payload.size() != 64) {
            if (error != nullptr) *error = QStringLiteral("EVENT_END 状态或长度错误");
            return false;
        }
        const char* data = frame.payload.constData();
        if (v4Frame) {
            record_.eventEndReason = read32(data);
            record_.eventOverflowMask = read32(data + 4);
            record_.eventObservedTotal = read32(data + 8);
            record_.eventRetainedTotal = read32(data + 12);
            record_.eventUploadedTotal = read32(data + 16);
            record_.eventHardwareDroppedTotal = read32(data + 20);
            record_.eventAcceptedTotal = record_.eventObservedTotal;
            record_.eventEmittedTotal = record_.eventUploadedTotal;
            record_.eventDroppedTotal = record_.eventHardwareDroppedTotal;
            record_.eventDroppedBySource.clear();
            quint64 droppedSum = 0U;
            for (int source = 0; source < 9; ++source) {
                const quint32 dropped = read32(data + 24 + source * 4);
                record_.eventDroppedBySource.append(dropped);
                droppedSum += dropped;
            }
            const quint32 masks = read32(data + 60);
            const quint32 endEnabledSourceMask = masks & 0x1ffU;
            const quint32 endImplementedSourceMask = (masks >> 9U) & 0x1ffU;
            const quint32 received = static_cast<quint32>(record_.sparseChanges.size());
            const bool statisticsInvalid =
                record_.eventEndReason > static_cast<quint32>(EventEndReason::FatalError) ||
                (record_.eventOverflowMask & ~0x1ffU) != 0U ||
                (masks & 0xfffc0000U) != 0U ||
                droppedSum != record_.eventHardwareDroppedTotal ||
                record_.eventRetainedTotal != record_.eventRetainedCount ||
                record_.eventRetainedTotal != received ||
                record_.eventUploadedTotal != record_.eventRetainedTotal ||
                record_.eventObservedTotal < record_.eventRetainedTotal ||
                endEnabledSourceMask != record_.enabledSourceMask ||
                endImplementedSourceMask != record_.implementedSourceMask;
            const bool eventLoss = record_.eventHardwareDroppedTotal != 0U ||
                                   record_.eventOverflowMask != 0U || eventSequenceGap_;
            const bool reasonIsOverflow =
                record_.eventEndReason == static_cast<quint32>(EventEndReason::Overflow);
            const bool overflowReasonMismatch = reasonIsOverflow != eventLoss;
            if (statisticsInvalid || overflowReasonMismatch || eventLoss) {
                if (error != nullptr) {
                    const QString detail = QStringLiteral(
                        "EVENT_END v4 contract error: reason=%1 overflow=0x%2 observed=%3 "
                        "retained=%4 meta_retained=%5 received=%6 uploaded=%7 dropped=%8 "
                        "drop_sum=%9 enabled=0x%10/0x%11 implemented=0x%12/0x%13")
                        .arg(record_.eventEndReason)
                        .arg(record_.eventOverflowMask, 3, 16, QLatin1Char('0'))
                        .arg(record_.eventObservedTotal).arg(record_.eventRetainedTotal)
                        .arg(record_.eventRetainedCount).arg(received)
                        .arg(record_.eventUploadedTotal).arg(record_.eventHardwareDroppedTotal)
                        .arg(droppedSum)
                        .arg(endEnabledSourceMask, 3, 16, QLatin1Char('0'))
                        .arg(record_.enabledSourceMask, 3, 16, QLatin1Char('0'))
                        .arg(endImplementedSourceMask, 3, 16, QLatin1Char('0'))
                        .arg(record_.implementedSourceMask, 3, 16, QLatin1Char('0'));
                    if (overflowReasonMismatch) {
                        *error = QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR: %1").arg(detail);
                    } else if (eventLoss && !statisticsInvalid) {
                        *error = QStringLiteral("CAPTURE_END_OVERFLOW: %1").arg(detail);
                    } else {
                        *error = detail;
                    }
                }
                return false;
            }
            hasEventEnd_ = true;
            return true;
        }
        record_.eventEndReason = read32(data + 0);
        record_.eventOverflowMask = read32(data + 4);
        record_.eventAcceptedTotal = read32(data + 8);
        record_.eventEmittedTotal = read32(data + 12);
        record_.eventDroppedTotal = read32(data + 16);
        quint32 droppedSum = 0U;
        QStringList droppedCounts;
        for (int protocol = 0; protocol < 9; ++protocol) {
            const quint32 dropped = read32(data + 20 + protocol * 4);
            droppedSum += dropped;
            droppedCounts.append(QString::number(dropped));
        }
        const quint32 endEnabledSourceMask = read32(data + 56);
        const quint32 endImplementedSourceMask = read32(data + 60);
        const bool statisticsInvalid =
            record_.eventEndReason > static_cast<quint32>(EventEndReason::FatalError) ||
            (record_.eventOverflowMask & ~0x1ffU) != 0U ||
            droppedSum != record_.eventDroppedTotal ||
            record_.eventEmittedTotal != static_cast<quint32>(record_.protocolEvents.size()) ||
            record_.eventAcceptedTotal != record_.eventEmittedTotal + record_.eventDroppedTotal ||
            endEnabledSourceMask != record_.enabledSourceMask ||
            endImplementedSourceMask != record_.implementedSourceMask;
        const bool eventLoss = record_.eventDroppedTotal != 0U ||
                               record_.eventOverflowMask != 0U || eventSequenceGap_;
        const bool reasonIsOverflow =
            record_.eventEndReason == static_cast<quint32>(EventEndReason::Overflow);
        const bool overflowReasonMismatch = reasonIsOverflow != eventLoss;
        if (statisticsInvalid || overflowReasonMismatch || eventLoss) {
            if (error != nullptr) {
                const QString detail = QStringLiteral(
                    "EVENT_END 统计、溢出或事件序号不一致: overflow=0x%1, "
                    "accepted=%2, emitted=%3, received=%4, dropped=%5, "
                    "drop_counts=[%6], enabled=0x%7/0x%8, implemented=0x%9/0x%10, "
                    "sequence_gap=%11")
                    .arg(record_.eventOverflowMask, 3, 16, QLatin1Char('0'))
                    .arg(record_.eventAcceptedTotal)
                    .arg(record_.eventEmittedTotal)
                    .arg(record_.protocolEvents.size())
                    .arg(record_.eventDroppedTotal)
                    .arg(droppedCounts.join(QLatin1Char(',')))
                    .arg(endEnabledSourceMask, 3, 16, QLatin1Char('0'))
                    .arg(record_.enabledSourceMask, 3, 16, QLatin1Char('0'))
                    .arg(endImplementedSourceMask, 3, 16, QLatin1Char('0'))
                    .arg(record_.implementedSourceMask, 3, 16, QLatin1Char('0'))
                    .arg(eventSequenceGap_ ? QStringLiteral("true") : QStringLiteral("false"));
                if (overflowReasonMismatch) {
                    *error = QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR: %1").arg(detail);
                } else if (eventLoss && !statisticsInvalid) {
                    *error = QStringLiteral("CAPTURE_END_OVERFLOW: %1").arg(detail);
                } else {
                    *error = detail;
                }
            }
            return false;
        }
        hasEventEnd_ = true;
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
    if (frame.header.type != CaptureFrameType::CaptureEnd || frame.payload.size() != 32 ||
        (hasEventMeta_ && !hasEventEnd_)) {
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
        (v4Frame && record_.actualSampleCount != kCaptureWindowSamplesV4) ||
        sampleBytes != record_.actualSampleCount * record_.sampleBytes || payloadBytes != payloadBytes_ ||
        sampleBytes_.size() != static_cast<int>(sampleBytes)) {
        if (error != nullptr) *error = QStringLiteral("CAPTURE_END 与实际样本流不一致");
        return false;
    }
    for (quint32 index = 0; index < record_.actualSampleCount; ++index) {
        record_.samples.append(sampleBytes_.mid(static_cast<int>(index * record_.sampleBytes),
                                                static_cast<int>(record_.sampleBytes)));
    }
    hasEnd_ = true;
    return true;
}

bool SamplingCaptureAssembler::complete() const
{
    return hasEnd_ && (!hasEventMeta_ || hasEventEnd_);
}
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
    quint32 openIndex = 0U;
    bool hasOpenIndex = false;
};

D3xxRuntime::~D3xxRuntime() { close(); }

bool D3xxRuntime::load(QString* error)
{
    close();
    d_.reset(new Private);
    QString libraryPath = qEnvironmentVariable("LOCKSTEP_FTD3XX_LIBRARY");
    if (libraryPath.isEmpty()) {
        const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("FTD3XX.dll"));
        libraryPath = QFileInfo::exists(bundled) ? bundled : QStringLiteral("FTD3XX");
    }
    d_->library.setFileName(libraryPath);
    if (!d_->library.load()) {
        if (error != nullptr) {
            *error = QStringLiteral("无法加载 FTD3XX 动态库: %1").arg(d_->library.errorString());
        }
        return false;
    }
    d_->createDeviceInfoList = reinterpret_cast<Private::CreateDeviceInfoList>(
        d_->library.resolve("FT_CreateDeviceInfoList"));
    d_->getDeviceInfoDetail = reinterpret_cast<Private::GetDeviceInfoDetail>(
        d_->library.resolve("FT_GetDeviceInfoDetail"));
    d_->create = reinterpret_cast<Private::Create>(d_->library.resolve("FT_Create"));
    d_->close = reinterpret_cast<Private::Close>(d_->library.resolve("FT_Close"));
    d_->readPipe = reinterpret_cast<Private::PipeIo>(d_->library.resolve("FT_ReadPipe"));
    d_->writePipe = reinterpret_cast<Private::PipeIo>(d_->library.resolve("FT_WritePipe"));
    d_->setPipeTimeout = reinterpret_cast<Private::SetPipeTimeout>(
        d_->library.resolve("FT_SetPipeTimeout"));
    if (d_->createDeviceInfoList == nullptr || d_->getDeviceInfoDetail == nullptr ||
        d_->create == nullptr || d_->close == nullptr || d_->readPipe == nullptr ||
        d_->writePipe == nullptr || d_->setPipeTimeout == nullptr) {
        if (error != nullptr) *error = QStringLiteral("FTD3XX 缺少必需的设备、管道或超时接口");
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
    const unsigned long status = d_->createDeviceInfoList(&count);
    if (status != 0U) {
        if (error != nullptr) *error = QStringLiteral("FT601 D3XX 枚举失败: status=%1").arg(status);
        return devices;
    }
    for (unsigned long index = 0; index < count; ++index) {
        char serial[64] = {};
        char description[128] = {};
        unsigned long flags = 0, type = 0, id = 0, location = 0;
        void* handle = nullptr;
        if (d_->getDeviceInfoDetail(index, &flags, &type, &id, &location, serial, description,
                                    &handle) == 0U) {
            D3xxDeviceInfo info;
            info.index = static_cast<quint32>(index);
            info.serialNumber = QString::fromLocal8Bit(serial);
            info.description = QString::fromLocal8Bit(description);
            devices.append(info);
        }
    }
    if (devices.isEmpty() && error != nullptr) *error = QStringLiteral("未枚举到 FT601 D3XX 设备");
    return devices;
}

bool D3xxRuntime::isLoaded() const { return !d_.isNull() && d_->library.isLoaded(); }

bool D3xxRuntime::open(const quint32 index, QString* error)
{
    if (!isLoaded()) {
        if (error != nullptr) *error = QStringLiteral("FTD3XX 尚未加载，无法打开 FT601");
        return false;
    }
    close();
    const unsigned long status = d_->create(
        reinterpret_cast<void*>(static_cast<quintptr>(index)), 0x10U, &d_->handle);
    if (status != 0U || d_->handle == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("无法按索引打开 FT601 D3XX: index=%1, status=%2")
                         .arg(index).arg(status);
        }
        d_->handle = nullptr;
        return false;
    }
    const unsigned long writeTimeoutStatus = d_->setPipeTimeout(d_->handle, 0x02U, 2000U);
    const unsigned long readTimeoutStatus = d_->setPipeTimeout(d_->handle, 0x82U, 2000U);
    if (writeTimeoutStatus != 0U || readTimeoutStatus != 0U) {
        if (error != nullptr) {
            *error = QStringLiteral("无法设置 FT601 管道超时: write_status=%1 read_status=%2")
                         .arg(writeTimeoutStatus).arg(readTimeoutStatus);
        }
        close();
        return false;
    }
    d_->openIndex = index;
    d_->hasOpenIndex = true;
    return true;
}

bool D3xxRuntime::writePipe(const quint8 pipeId, const QByteArray& bytes, int* transferred,
                            QString* error)
{
    unsigned long count = 0;
    const unsigned long status = !isOpen() ? 1U : d_->writePipe(
        d_->handle, pipeId, reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.constData())),
        static_cast<unsigned long>(bytes.size()), &count, nullptr);
    if (transferred != nullptr) *transferred = static_cast<int>(count);
    if (status != 0U || count != static_cast<unsigned long>(bytes.size())) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "FT601 D3XX 写入失败或不完整: status=%1, pipe=0x%2, requested=%3, transferred=%4")
                         .arg(status).arg(pipeId, 2, 16, QLatin1Char('0'))
                         .arg(bytes.size()).arg(count);
        }
        return false;
    }
    return true;
}

QByteArray D3xxRuntime::readPipe(const quint8 pipeId, const int maximumBytes, QString* error)
{
    const CapturePipeReadResult result = readPipeDetailed(pipeId, maximumBytes);
    if ((result.fatalError || result.pendingTimeout) && error != nullptr) *error = result.error;
    return result.data;
}

CapturePipeReadResult D3xxRuntime::readPipeDetailed(const quint8 pipeId, const int maximumBytes)
{
    CapturePipeReadResult result;
    result.pipeId = pipeId;
    result.requestedBytes = maximumBytes;
    if (!isOpen() || maximumBytes <= 0) {
        result.status = 1;
        result.fatalError = true;
        result.error = QStringLiteral("FT601 D3XX 传输未打开或读取长度无效");
        return result;
    }
    QByteArray bytes(maximumBytes, '\0');
    unsigned long count = 0;
    const unsigned long status = d_->readPipe(
        d_->handle, pipeId, reinterpret_cast<unsigned char*>(bytes.data()),
        static_cast<unsigned long>(bytes.size()), &count, nullptr);
    result.status = static_cast<int>(status);
    result.transferredBytes = static_cast<int>(count);
    if (count > 0U) {
        bytes.resize(static_cast<int>(count));
        result.data = bytes;
    }
    if (status != 0U) {
        result.pendingTimeout = status == 19U;
        result.fatalError = !result.pendingTimeout;
        result.error = QStringLiteral(
            "FT601 D3XX 读取失败: status=%1, pipe=0x%2, requested=%3, transferred=%4")
                           .arg(status).arg(pipeId, 2, 16, QLatin1Char('0'))
                           .arg(maximumBytes).arg(count);
    }
    return result;
}

void D3xxRuntime::close()
{
    if (isOpen()) d_->close(d_->handle);
    if (!d_.isNull()) d_->handle = nullptr;
}

bool D3xxRuntime::reopen(QString* error)
{
    if (!isLoaded() || !d_->hasOpenIndex) {
        if (error != nullptr) *error = QStringLiteral("FT601 D3XX 缺少可重连设备索引");
        return false;
    }
    const quint32 index = d_->openIndex;
    close();
    return open(index, error);
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
    const quint64 windowEndIndex = static_cast<quint64>(capture.windowStartIndex) +
        (capture.actualSampleCount == 0U ? 0U : static_cast<quint64>(capture.actualSampleCount - 1U));
    sidecar.insert(QStringLiteral("window_end_index"), QString::number(windowEndIndex));
    sidecar.insert(QStringLiteral("trigger_index"), static_cast<qint64>(capture.triggerIndex));
    sidecar.insert(QStringLiteral("stop_reason"), static_cast<qint64>(capture.stopReason));
    sidecar.insert(QStringLiteral("vcd"), QStringLiteral("waveform/capture.vcd"));
    if (capture.eventTimebaseHz != 0U) {
        QJsonArray events;
        for (const SamplingCaptureRecord::ProtocolEvent& event : capture.protocolEvents) {
            QJsonObject item;
            item.insert(QStringLiteral("timestamp_ticks"), QString::number(event.timestampTicks));
            item.insert(QStringLiteral("capture_id"), static_cast<qint64>(event.captureId));
            item.insert(QStringLiteral("local_sequence"), static_cast<qint64>(event.localSequence));
            item.insert(QStringLiteral("protocol_id"), static_cast<int>(event.protocolId));
            item.insert(QStringLiteral("event_type"), static_cast<int>(event.eventType));
            item.insert(QStringLiteral("source_kind"), static_cast<int>(event.sourceKind));
            item.insert(QStringLiteral("flags"), static_cast<int>(event.flags));
            item.insert(QStringLiteral("event_reason_mask"), static_cast<qint64>(event.eventReasonMask));
            item.insert(QStringLiteral("payload_hex"), QString::fromLatin1(event.payload.toHex()));
            events.append(item);
        }
        QJsonObject eventArchive;
        eventArchive.insert(QStringLiteral("schema"), QStringLiteral("lockstep-protocol-events-v3"));
        eventArchive.insert(QStringLiteral("capture_id"), static_cast<qint64>(capture.captureId));
        eventArchive.insert(QStringLiteral("timebase_hz"), static_cast<qint64>(capture.eventTimebaseHz));
        eventArchive.insert(QStringLiteral("implemented_source_mask"),
                            static_cast<qint64>(capture.implementedSourceMask));
        eventArchive.insert(QStringLiteral("enabled_source_mask"),
                            static_cast<qint64>(capture.enabledSourceMask));
        eventArchive.insert(QStringLiteral("design_gap_mask"), static_cast<qint64>(capture.designGapMask));
        eventArchive.insert(QStringLiteral("end_reason"), static_cast<qint64>(capture.eventEndReason));
        eventArchive.insert(QStringLiteral("end_reason_name"),
                            toString(static_cast<EventEndReason>(capture.eventEndReason)));
        eventArchive.insert(QStringLiteral("overflow_mask"), static_cast<qint64>(capture.eventOverflowMask));
        eventArchive.insert(QStringLiteral("accepted_total"),
                            static_cast<qint64>(capture.eventAcceptedTotal));
        eventArchive.insert(QStringLiteral("emitted_total"),
                            static_cast<qint64>(capture.eventEmittedTotal));
        eventArchive.insert(QStringLiteral("dropped_total"),
                            static_cast<qint64>(capture.eventDroppedTotal));
        eventArchive.insert(QStringLiteral("events"), events);
        const QString eventPath = QDir(evidenceDir).filePath(QStringLiteral("protocol_events.json"));
        if (!saveFile(eventPath, QJsonDocument(eventArchive).toJson(QJsonDocument::Indented), error)) return false;
        sidecar.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-sidecar-v3"));
        sidecar.insert(QStringLiteral("event_timebase_hz"), static_cast<qint64>(capture.eventTimebaseHz));
        sidecar.insert(QStringLiteral("protocol_events"), QStringLiteral("evidence/protocol_events.json"));
    }
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
                                 const quint32 sequence,
                                 const quint16 version = kCaptureProtocolVersion) {
        const QByteArray command = codec.encode(type, payload, sequence, 0U, 0U, version);
        return writeComplete(transport, command, &transferred, error);
    };
    quint32 nextResponseSequence = 0U;
    bool hasResponseSequence = false;
    const auto readResponse = [&](const CaptureFrameType expected, const QString& phase,
                                  CaptureFrame* const matched) {
        QElapsedTimer responseTimer;
        responseTimer.start();
        while (responseTimer.elapsed() < 2'000) {
            const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kCaptureReadChunkBytes);
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
        response.header.version != kCaptureProtocolVersion || response.payload.size() != 32 ||
        read32(response.payload.constData()) != kCaptureProtocolVersion ||
        read32(response.payload.constData() + 4) != kCapturePhysicalChannels ||
        read32(response.payload.constData() + 8) != kCaptureSampleWordBits) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("采集设备 HELLO 能力不匹配");
        return false;
    }
    if (!sendCommand(CaptureFrameType::ConfigCapture, config.toPayload(), 1U) ||
        !readResponse(CaptureFrameType::StatusResponse, QStringLiteral("config"), &response) ||
        response.header.version != kCaptureProtocolVersion || response.payload.size() != 64 ||
        read32(response.payload.constData()) != 2U ||
        read32(response.payload.constData() + 4) != 1U ||
        read32(response.payload.constData() + 24) != 0U ||
        !sendCommand(CaptureFrameType::GetStatus, QByteArray(), 2U) ||
        !readResponse(CaptureFrameType::StatusResponse, QStringLiteral("get_status"), &response) ||
        response.header.version != kCaptureProtocolVersion || response.payload.size() != 64 ||
        read32(response.payload.constData()) != 2U ||
        read32(response.payload.constData() + 4) != 2U ||
        read32(response.payload.constData() + 24) != 0U) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("CONFIG_CAPTURE 未进入 CONFIGURED 状态");
        return false;
    }
    if (!sendCommand(CaptureFrameType::ConfigEvents, config.toEventPayload(), 3U,
                     kCaptureProtocolVersionV3) ||
        !readResponse(CaptureFrameType::StatusResponse, QStringLiteral("config_events"), &response) ||
        response.header.version != kCaptureProtocolVersion || response.payload.size() != 64 ||
        read32(response.payload.constData()) != 2U ||
        read32(response.payload.constData() + 4) != 3U ||
        read32(response.payload.constData() + 24) != 0U) {
        if (error != nullptr && error->isEmpty()) *error = QStringLiteral("CONFIG_EVENTS 未进入 CONFIGURED 状态");
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
    const QByteArray command = codec.encode(CaptureFrameType::GetStatus, QByteArray(), 100U);
    if (!writeComplete(transport, command, &transferred, error)) {
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
        const QByteArray command = codec.encode(CaptureFrameType::StopCapture, QByteArray(), 101U);
        stopSent = writeComplete(transport, command, &transferred, &recoveryDetail);
        if (stopSent) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 2'000 && !endSeen) {
                const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kCaptureReadChunkBytes);
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
    if (!reopened) details.append(reopenError.isEmpty() ? QStringLiteral("传输重连失败") : reopenError);
    if (reopened && !finalStatusRead) details.append(finalQueryError.isEmpty()
        ? QStringLiteral("传输重连后状态查询失败") : finalQueryError);
    if (finalStatusRead && !finalConfigured) {
        details.append(QStringLiteral("传输重连后状态仍为 %1").arg(recovered.state));
    }
    if (error != nullptr) *error = details.join(QStringLiteral("; "));
    return false;
}

CaptureSessionResult SamplingCaptureSession::runDetailed(
    CaptureTransport* const transport, const SamplingCaptureConfig& config,
    const QString& taskRootPath, const int timeoutMs, SamplingCaptureRecord* const record,
    const std::function<bool(quint32, QString*)>& afterArm,
    const std::function<bool()>& cancelled) const
{
    CaptureSessionResult result;
    QString error;
    if (transport == nullptr || !transport->isOpen() || record == nullptr || timeoutMs <= 0) {
        result.phase = QStringLiteral("validation");
        result.message = QStringLiteral("采集会话参数无效");
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    const auto cancellationRequested = [&cancelled]() {
        return cancelled && cancelled();
    };
    if (cancellationRequested()) {
        result.phase = QStringLiteral("cancelled");
        result.message = QStringLiteral("[CAPTURE_CANCELLED] 采集线程已取消");
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    if (!configure(transport, config, &error)) {
        result.phase = QStringLiteral("configure");
        result.message = error;
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    if (cancellationRequested()) {
        result.phase = QStringLiteral("cancelled");
        result.message = QStringLiteral("[CAPTURE_CANCELLED] 采集线程已取消");
        saveSessionEvidence(taskRootPath, result);
        return result;
    }
    quint32 captureId = 0U;
    const bool armOk = armAndWaitAccepted(transport, 4U, &captureId, &error);
    if (!armOk) {
        result.phase = QStringLiteral("arm");
    } else if (cancellationRequested()) {
        error = QStringLiteral("[CAPTURE_CANCELLED] 采集线程已取消");
        result.phase = QStringLiteral("cancelled");
    } else if (!startEventStream(transport, 5U, &error)) {
        result.phase = QStringLiteral("start_event_stream");
    }
    if (armOk && result.phase.isEmpty() && cancellationRequested()) {
        error = QStringLiteral("[CAPTURE_CANCELLED] 采集线程已取消");
        result.phase = QStringLiteral("cancelled");
    } else if (armOk && result.phase.isEmpty() && afterArm && !afterArm(captureId, &error)) {
        result.phase = QStringLiteral("after_arm");
    } else if (armOk && result.phase.isEmpty() &&
               !collect(transport, taskRootPath, timeoutMs, record, &error,
                        config.eventEnableMask, cancelled)) {
        result.phase = QStringLiteral("collect");
    } else if (armOk && result.phase.isEmpty()) {
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

bool SamplingCaptureSession::startEventStream(
    CaptureTransport* const transport,
    const quint32 commandSequence,
    QString* const error) const
{
    if (transport == nullptr || !transport->isOpen()) {
        if (error != nullptr) *error = QStringLiteral("capture transport is not open");
        return false;
    }
    CaptureFrameCodec codec;
    const QByteArray release = codec.encode(
        CaptureFrameType::StartEventStream, QByteArray(), commandSequence,
        0U, 0U, kCaptureProtocolVersionV3);
    int transferred = 0;
    if (writeComplete(transport, release, &transferred, error)) return true;
    if (error != nullptr && error->isEmpty()) {
        *error = QStringLiteral("START_EVENT_STREAM 写入不完整");
    }
    return false;
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
    const QByteArray command = codec.encode(CaptureFrameType::ArmCapture, QByteArray(), commandSequence);
    if (!writeComplete(transport, command, &transferred, error)) return false;
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
            if (frame.header.type != CaptureFrameType::StatusResponse) continue;
            CaptureStatusV2 status;
            if (!parseStatusV2(frame, &status, error)) return false;
            const quint32 state = status.state;
            if (state != 3U && state != 4U) {
                if (error != nullptr) *error = QStringLiteral("ARM_CAPTURE was not accepted, state=%1").arg(state);
                return false;
            }
            if (status.requestSequence != commandSequence) {
                if (error != nullptr) *error = QStringLiteral("ARM 状态响应请求序号不一致");
                return false;
            }
            if (captureId != nullptr) *captureId = status.captureId;
            return true;
        }
    }
    if (error != nullptr) *error = QStringLiteral("timed out waiting for ARM_CAPTURE acceptance");
    return false;
}

bool SamplingCaptureSession::collect(CaptureTransport* transport, const QString& taskRootPath,
                                     const int timeoutMs, SamplingCaptureRecord* record,
                                     QString* error,
                                     const quint32 expectedEnabledSourceMask,
                                     const std::function<bool()>& cancelled) const
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
    SamplingCaptureAssembler assembler(expectedEnabledSourceMask);
    bool captureStarted = false;
    QElapsedTimer timer;
    timer.start();
    while (!assembler.complete() && timer.elapsed() < timeoutMs) {
        if (cancelled && cancelled()) {
            persistRawCapture();
            if (error != nullptr) *error = QStringLiteral("[CAPTURE_CANCELLED] 采集线程已取消");
            return false;
        }
        const CapturePipeReadResult read = transport->readPipeDetailed(0x82U, kCaptureReadChunkBytes);
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
            if (frame.header.type == CaptureFrameType::CaptureMeta ||
                frame.header.type == CaptureFrameType::EventMeta) captureStarted = true;
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
    if (!validateCaptureCompletion(*record, error)) return false;
    return exportScalarVcd(*record, taskRootPath, nullptr, nullptr, error);
}

}  // namespace lockstep::acquisition
