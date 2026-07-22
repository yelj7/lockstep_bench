/**********************************************************
* 文件名: sampling_capture_test.cpp
* 日期: 2026-07-22
* 版本: 4.0
* 更新记录: 增加 v4 统一时间轴、稀疏状态链与结束统计正反例。
* 描述: 验证 v2/v3/v4 采集组装、恢复门槛及稀疏事件合同。
**********************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>

#include "sampling_capture.h"
#include "test_temp_directory.h"

namespace {

void append32(QByteArray* bytes, quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char*>(&little), sizeof(little));
}

void append64(QByteArray* bytes, quint64 value)
{
    const quint64 little = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char*>(&little), sizeof(little));
}

void overwrite32(QByteArray* bytes, const int offset, const quint32 value)
{
    const quint32 little = qToLittleEndian(value);
    bytes->replace(offset, sizeof(little), reinterpret_cast<const char*>(&little), sizeof(little));
}

quint32 read32(const QByteArray& bytes, const int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

struct V4Fixture final {
    static constexpr quint32 kCaptureId = 77U;
    static constexpr quint64 kOrigin = 0x00000001ffffff00ULL;
    static constexpr quint64 kTrigger = kOrigin + 2047U;
    static constexpr quint64 kEndExclusive = kOrigin + 4096U;

    QByteArray captureMeta;
    QByteArray eventMeta;
    QByteArray firstChange;
    QByteArray secondChange;
    QByteArray eventEnd;
    QByteArray samples;
    QByteArray captureEnd;

    V4Fixture()
    {
        append32(&captureMeta, 120'000'000U);
        append32(&captureMeta, lockstep::acquisition::kCaptureWindowSamplesV4);
        append32(&captureMeta, 2047U);
        append32(&captureMeta, 2049U);
        append32(&captureMeta, 0x19fU);
        append32(&captureMeta, 0U);
        append32(&captureMeta, lockstep::acquisition::kCapturePhysicalChannelsV4);
        append32(&captureMeta, lockstep::acquisition::kCaptureSampleWordBitsV4);
        append32(&captureMeta, static_cast<quint32>(kOrigin));
        append32(&captureMeta, static_cast<quint32>(kTrigger));
        append64(&captureMeta, kOrigin);
        append64(&captureMeta, kTrigger);
        append64(&captureMeta, kEndExclusive);

        append32(&eventMeta, 120'000'000U);
        append32(&eventMeta, 0x09eU);
        append32(&eventMeta, 0x09eU);
        append32(&eventMeta, 0x060U);
        append32(&eventMeta, lockstep::acquisition::kCaptureEventRecordBytesV4);
        append32(&eventMeta, lockstep::acquisition::kCaptureEventLayoutV4);
        append32(&eventMeta, 123U);
        append32(&eventMeta, 456U);
        append32(&eventMeta, 0x0001U);
        append64(&eventMeta, kOrigin);
        append64(&eventMeta, kTrigger);
        append64(&eventMeta, kEndExclusive);
        append32(&eventMeta, 2U);

        append32(&firstChange, static_cast<quint32>(kOrigin + 3U));
        append32(&firstChange, 41U);
        append32(&firstChange, 0x1003U | (0x82U << 20U));
        append32(&firstChange, 0x1002U);

        append32(&secondChange, static_cast<quint32>(kOrigin + 0x110U));
        append32(&secondChange, 42U);
        append32(&secondChange, 0x1513U | (0x1cU << 20U));
        append32(&secondChange, 0x0510U);

        append32(&eventEnd, 0U);
        append32(&eventEnd, 0U);
        append32(&eventEnd, 2U);
        append32(&eventEnd, 2U);
        append32(&eventEnd, 2U);
        append32(&eventEnd, 0U);
        for (int source = 0; source < 9; ++source) append32(&eventEnd, 0U);
        append32(&eventEnd, 0x09eU | (0x09eU << 9U));

        samples = QByteArray(
            static_cast<int>(lockstep::acquisition::kCaptureWindowSamplesV4 *
                             lockstep::acquisition::kCaptureSampleBytesV4), '\0');
        samples[0] = char(0x5a);
        samples[samples.size() - 1] = char(0xa5);

        append32(&captureEnd, lockstep::acquisition::kCaptureWindowSamplesV4);
        append32(&captureEnd, static_cast<quint32>(kOrigin));
        append32(&captureEnd, static_cast<quint32>(kTrigger));
        append32(&captureEnd, 0U);
        append32(&captureEnd, 0U);
        append32(&captureEnd, static_cast<quint32>(samples.size()));
        append32(&captureEnd, static_cast<quint32>(samples.size()));
        append32(&captureEnd, 0U);
    }
};

lockstep::acquisition::CaptureFrame v4Frame(
    const lockstep::acquisition::CaptureFrameType type, const QByteArray& payload,
    const quint32 sequence)
{
    lockstep::acquisition::CaptureFrame frame;
    frame.header.version = lockstep::acquisition::kCaptureProtocolVersionV4;
    frame.header.type = type;
    frame.header.sequence = sequence;
    frame.header.captureId = V4Fixture::kCaptureId;
    frame.payload = payload;
    return frame;
}

QList<lockstep::acquisition::CaptureFrame> v4Frames(
    const V4Fixture& fixture, const bool eventMetaFirst)
{
    using lockstep::acquisition::CaptureFrame;
    using lockstep::acquisition::CaptureFrameType;
    QList<CaptureFrame> frames;
    quint32 sequence = 300U;
    if (!eventMetaFirst) {
        frames.append(v4Frame(CaptureFrameType::CaptureMeta, fixture.captureMeta, sequence++));
    }
    frames.append(v4Frame(CaptureFrameType::EventMeta, fixture.eventMeta, sequence++));
    frames.append(v4Frame(CaptureFrameType::EventData, fixture.firstChange, sequence++));
    frames.append(v4Frame(CaptureFrameType::EventData, fixture.secondChange, sequence++));
    if (eventMetaFirst) {
        frames.append(v4Frame(CaptureFrameType::CaptureMeta, fixture.captureMeta, sequence++));
    }
    frames.append(v4Frame(CaptureFrameType::EventEnd, fixture.eventEnd, sequence++));
    frames.append(v4Frame(CaptureFrameType::SampleData, fixture.samples, sequence++));
    frames.append(v4Frame(CaptureFrameType::CaptureEnd, fixture.captureEnd, sequence));
    return frames;
}

class FakeCaptureTransport final : public lockstep::acquisition::CaptureTransport {
public:
    bool writePipe(quint8 pipeId, const QByteArray& bytes, int* transferred, QString* error) override
    {
        Q_UNUSED(error);
        if (pipeId != 0x02U) return false;
        if (transferred != nullptr) *transferred = qMax(0, bytes.size() - shortWriteBytes);
        const lockstep::acquisition::CaptureDecodeResult decoded = requestCodec_.feed(bytes);
        if (!decoded.success || decoded.frames.size() != 1) return false;
        const lockstep::acquisition::CaptureFrame& request = decoded.frames.first();
        commands.append(request.header.type);
        QByteArray payload;
        if (request.header.type == lockstep::acquisition::CaptureFrameType::HelloRequest) {
            append32(&payload, protocolVersion);
            append32(&payload, lockstep::acquisition::kCapturePhysicalChannels);
            append32(&payload, lockstep::acquisition::kCaptureSampleWordBits);
            for (int index = 0; index < 5; ++index) append32(&payload, 0U);
            pending_ += responseCodec_.encode(
                lockstep::acquisition::CaptureFrameType::HelloResponse, payload,
                request.header.sequence + responseSequenceDelta, 0U, 0U, responseFrameVersion);
        } else if (request.header.type == lockstep::acquisition::CaptureFrameType::ConfigCapture ||
                   request.header.type == lockstep::acquisition::CaptureFrameType::ConfigEvents ||
                   request.header.type == lockstep::acquisition::CaptureFrameType::GetStatus ||
                   request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture) {
            if (request.header.type == lockstep::acquisition::CaptureFrameType::ConfigCapture) {
                currentState = configuredState;
            } else if (request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture) {
                currentState = armState;
            }
            append32(&payload, currentState);
            append32(&payload, request.header.sequence);
            append32(&payload, request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture ? 42U : 0U);
            for (int index = 0; index < 2; ++index) append32(&payload, 0U);
            append32(&payload, 0U);
            append32(&payload, lastError);
            for (int index = 0; index < 9; ++index) append32(&payload, 0U);
            payload.append(QByteArray(statusExtraBytes, '\0'));
            pending_ += responseCodec_.encode(
                lockstep::acquisition::CaptureFrameType::StatusResponse, payload,
                request.header.sequence + responseSequenceDelta +
                    ((request.header.type == lockstep::acquisition::CaptureFrameType::ConfigCapture)
                          ? configResponseSequenceGap
                          : 0U), 0U, 0U, responseFrameVersion);
        } else if (request.header.type == lockstep::acquisition::CaptureFrameType::StopCapture) {
            currentState = configuredState;
            for (int index = 0; index < 8; ++index) append32(&payload, index == 3 ? 2U : 0U);
            pending_ += responseCodec_.encode(
                lockstep::acquisition::CaptureFrameType::CaptureEnd, payload,
                request.header.sequence + responseSequenceDelta, 42U);
        }
        return true;
    }

    QByteArray readPipe(quint8 pipeId, int maximumBytes, QString* error) override
    {
        Q_UNUSED(error);
        if (pipeId != 0x82U || maximumBytes <= 0) return QByteArray();
        const QByteArray result = pending_.left(maximumBytes);
        pending_.remove(0, result.size());
        return result;
    }

    lockstep::acquisition::CapturePipeReadResult readPipeDetailed(
        quint8 pipeId, int maximumBytes) override
    {
        lockstep::acquisition::CapturePipeReadResult result;
        result.pipeId = pipeId;
        result.requestedBytes = maximumBytes;
        if (pendingTimeoutReads > 0) {
            --pendingTimeoutReads;
            result.pendingTimeout = true;
            result.status = 19U;
            return result;
        }
        result.data = readPipe(pipeId, maximumBytes, &result.error);
        result.transferredBytes = result.data.size();
        result.fatalError = result.data.isEmpty() && !result.error.isEmpty();
        return result;
    }

    bool reopen(QString* error) override
    {
        ++reopenCount;
        if (!reopenSucceeds && error != nullptr) *error = QStringLiteral("fake reopen failed");
        return reopenSucceeds;
    }

    bool isOpen() const override { return true; }

    void queueIncoming(const QByteArray& bytes) { pending_ += bytes; }

    QList<lockstep::acquisition::CaptureFrameType> commands;
    quint32 protocolVersion = lockstep::acquisition::kCaptureProtocolVersion;
    quint16 responseFrameVersion = lockstep::acquisition::kCaptureProtocolVersion;
    int statusExtraBytes = 0;
    quint32 responseSequenceDelta = 0U;
    quint32 configResponseSequenceGap = 0U;
    quint32 configuredState = 2U;
    quint32 armState = 4U;
    quint32 currentState = 2U;
    quint32 lastError = 0U;
    int pendingTimeoutReads = 0;
    int reopenCount = 0;
    bool reopenSucceeds = true;
    int shortWriteBytes = 0;

private:
    lockstep::acquisition::CaptureFrameCodec requestCodec_;
    lockstep::acquisition::CaptureFrameCodec responseCodec_;
    QByteArray pending_;
};

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace lockstep::acquisition;

    SamplingCaptureConfig config;
    const QJsonObject configJson = config.toJson();
    if (!expect(configJson.value(QStringLiteral("sample_rate_hz")).toInt() == 120'000'000 &&
                    configJson.value(QStringLiteral("sample_count")).toInt() == 4096 &&
                    configJson.value(QStringLiteral("protocol_group_mask")).toInt() == 0x1ff &&
                    configJson.value(QStringLiteral("input_invert_mask")).toInt() == 0 &&
                    configJson.value(QStringLiteral("trigger_timeout_samples")).toInt() == 1'200'000'000 &&
                    configJson.value(QStringLiteral("event_enable_mask")).toInt() == 0x19f &&
                    configJson.value(QStringLiteral("event_limit")).toInt() == 0 &&
                    configJson.value(QStringLiteral("event_watchdog_ticks")).toInt() == 12'000'000 &&
                    configJson.value(QStringLiteral("event_hard_timeout_ticks")).toInt() == 240'000'000,
                "persisted JSON includes every hardware and event configuration field")) return 1;
    QString error;
    const QByteArray capturePayload = config.toPayload();
    const QByteArray eventPayload = config.toEventPayload();
    if (!expect(config.validate(&error), "default 1024 capture config is valid") ||
        !expect(config.sampleRateHz == 120'000'000U, "default config matches live 120 MHz hardware") ||
        !expect(config.eventLimit == 0U, "default event stream is independent of the 4096-sample window") ||
        !expect(capturePayload.size() == 52, "capture config payload preserves v2 52-byte wire contract") ||
        !expect(eventPayload.size() == 16, "event config uses explicit v3 16-byte wire contract") ||
        !expect(read32(capturePayload, 0) == config.sampleRateHz &&
                    read32(capturePayload, 4) == config.sampleCount &&
                    read32(capturePayload, 8) == config.pretriggerCount &&
                    read32(capturePayload, 12) == config.posttriggerCount &&
                    read32(capturePayload, 16) == config.protocolGroupMask &&
                    read32(capturePayload, 20) == config.inputInvertMask &&
                    read32(capturePayload, 24) == 0x04000400U &&
                    read32(capturePayload, 28) == config.triggerMask &&
                    read32(capturePayload, 32) == config.triggerValue &&
                    read32(capturePayload, 36) == config.triggerEdgeRise &&
                    read32(capturePayload, 40) == config.triggerEdgeFall &&
                    read32(capturePayload, 44) == config.mode &&
                    read32(capturePayload, 48) == config.triggerTimeoutSamples,
                "capture config serializes every v2 field at the fixed wire offset") ||
        !expect(read32(eventPayload, 0) == config.eventEnableMask &&
                    read32(eventPayload, 4) == config.eventLimit &&
                    read32(eventPayload, 8) == config.eventWatchdogTicks &&
                    read32(eventPayload, 12) == config.eventHardTimeoutTicks,
                "event config serializes every v3 field at the fixed wire offset")) return 1;

    const auto expectInvalidConfig = [&](const SamplingCaptureConfig& invalid,
                                         const char* const message) {
        QString validationError;
        return expect(!invalid.validate(&validationError), message);
    };
    SamplingCaptureConfig invalid = config;
    invalid.sampleRateHz = 50'000'000U;
    if (!expectInvalidConfig(invalid, "unsupported sample rate is rejected")) return 1;
    invalid = config;
    invalid.sampleCount = 2048U;
    invalid.pretriggerCount = 1023U;
    invalid.posttriggerCount = 1025U;
    if (!expectInvalidConfig(invalid, "unsupported sample window is rejected")) return 1;
    invalid = config;
    invalid.inputInvertMask = 1U;
    if (!expectInvalidConfig(invalid, "unimplemented input inversion is rejected")) return 1;
    invalid = config;
    invalid.triggerEdgeRise = 2U;
    if (!expectInvalidConfig(invalid, "unsupported trigger rise bits are rejected")) return 1;
    invalid = config;
    invalid.triggerEdgeFall = 1U;
    if (!expectInvalidConfig(invalid, "unimplemented falling-edge trigger is rejected")) return 1;
    invalid = config;
    invalid.protocolGroupMask = 0x200U;
    if (!expectInvalidConfig(invalid, "unsupported protocol group bits are rejected")) return 1;
    invalid = config;
    invalid.mode = 1U;
    if (!expectInvalidConfig(invalid, "unsupported capture mode is rejected")) return 1;
    invalid = config;
    invalid.triggerTimeoutSamples = 0U;
    if (!expectInvalidConfig(invalid, "zero trigger timeout is rejected")) return 1;
    invalid = config;
    invalid.eventEnableMask = 0x200U;
    if (!expectInvalidConfig(invalid, "unimplemented event sources are rejected")) return 1;
    invalid = config;
    invalid.eventLimit = 1U;
    if (!expectInvalidConfig(invalid, "unsupported finite event limit is rejected")) return 1;
    invalid = config;
    invalid.eventWatchdogTicks = 0U;
    if (!expectInvalidConfig(invalid, "zero event watchdog is rejected")) return 1;
    invalid = config;
    invalid.eventHardTimeoutTicks = invalid.eventWatchdogTicks - 1U;
    if (!expectInvalidConfig(invalid, "hard timeout shorter than watchdog is rejected")) return 1;

    SamplingCaptureRecord missingEventClosure;
    if (!expect(!validateCaptureCompletion(missingEventClosure, &error) &&
                    error.contains(QStringLiteral("event stream")),
                "capture without EVENT_META/EVENT_END cannot pass")) return 1;

    FakeCaptureTransport transport;
    transport.pendingTimeoutReads = 2;
    SamplingCaptureSession session;
    if (!expect(session.configure(&transport, config, &error), "C++ CONFIG handshake reaches CONFIGURED") ||
        !expect(transport.commands == QList<CaptureFrameType>{
                    CaptureFrameType::HelloRequest, CaptureFrameType::ConfigCapture,
                    CaptureFrameType::GetStatus, CaptureFrameType::ConfigEvents},
                "C++ handshake is HELLO -> CONFIG -> GET_STATUS -> CONFIG_EVENTS")) return 1;
    quint32 acceptedCaptureId = 0U;
    if (!expect(session.armAndWaitAccepted(&transport, 3U, &acceptedCaptureId, &error) &&
                    acceptedCaptureId == 42U,
                "ARM waits for an explicit armed status response")) return 1;
    FakeCaptureTransport rejectedArmTransport;
    rejectedArmTransport.armState = 2U;
    if (!expect(!session.armAndWaitAccepted(&rejectedArmTransport, 3U, nullptr, &error),
                "ARM rejects a response that remains CONFIGURED")) return 1;
    FakeCaptureTransport rejectedTransport;
    rejectedTransport.configuredState = 1U;
    if (!expect(!session.configure(&rejectedTransport, config, &error),
                "configuration is rejected unless hardware reports CONFIGURED")) return 1;
    FakeCaptureTransport staleResponseTransport;
    staleResponseTransport.configResponseSequenceGap = 1U;
    if (!expect(!session.configure(&staleResponseTransport, config, &error),
                "configuration rejects a stale response sequence")) return 1;
    FakeCaptureTransport incompatibleTransport;
    incompatibleTransport.protocolVersion += 1U;
    if (!expect(!session.configure(&incompatibleTransport, config, &error),
                "configuration rejects an incompatible protocol version")) return 1;

    FakeCaptureTransport v3ResponseTransport;
    v3ResponseTransport.responseFrameVersion = kCaptureProtocolVersionV3;
    if (!expect(!session.configure(&v3ResponseTransport, config, &error),
                "configuration rejects v3 control responses on the v2 response contract")) return 1;

    FakeCaptureTransport oversizedStatusTransport;
    oversizedStatusTransport.statusExtraBytes = 4;
    if (!expect(!session.configure(&oversizedStatusTransport, config, &error),
                "configuration rejects an oversized STATUS_RSP")) return 1;

    FakeCaptureTransport shortWriteTransport;
    shortWriteTransport.shortWriteBytes = 4;
    if (!expect(!session.configure(&shortWriteTransport, config, &error) &&
                    error.contains(QStringLiteral("short write")),
                "configuration rejects a transport short write")) return 1;

    QTemporaryDir afterArmTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_after_arm")));
    FakeCaptureTransport afterArmTransport;
    SamplingCaptureRecord afterArmRecord;
    bool afterArmCalled = false;
    quint32 afterArmCaptureId = 0U;
    const CaptureSessionResult afterArmFailure = session.runDetailed(
        &afterArmTransport, config, afterArmTask.path(), 100, &afterArmRecord,
        [&](const quint32 captureId, QString* callbackError) {
            afterArmCalled = true;
            afterArmCaptureId = captureId;
            if (callbackError != nullptr) *callbackError = QStringLiteral("run request failed");
            return false;
        });
    if (!expect(afterArmCalled && afterArmCaptureId == 42U && !afterArmFailure.success &&
                    afterArmFailure.phase == QStringLiteral("after_arm") &&
                    afterArmFailure.recoverySucceeded,
                "after-ARM callback runs after ACK and preserves STOP recovery on failure") ||
        !expect(afterArmTransport.commands.indexOf(CaptureFrameType::StartEventStream) ==
                    afterArmTransport.commands.indexOf(CaptureFrameType::ArmCapture) + 1,
                "START_EVENT_STREAM is sent immediately after ARM ACK")) return 1;

    QTemporaryDir cancelledTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_cancelled")));
    FakeCaptureTransport cancelledTransport;
    SamplingCaptureRecord cancelledRecord;
    int cancellationChecks = 0;
    const CaptureSessionResult cancelledResult = session.runDetailed(
        &cancelledTransport, config, cancelledTask.path(), 100, &cancelledRecord, {},
        [&cancellationChecks]() { return ++cancellationChecks >= 3; });
    if (!expect(!cancelledResult.success && cancelledResult.phase == QStringLiteral("cancelled") &&
                    cancelledResult.message.contains(QStringLiteral("CAPTURE_CANCELLED")) &&
                    cancelledResult.recoverySucceeded,
                "thread cancellation between ARM and START performs STOP recovery")) return 1;

    QTemporaryDir recoveryTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_recovery")));
    FakeCaptureTransport timeoutTransport;
    SamplingCaptureRecord timeoutRecord;
    const CaptureSessionResult recoveredTimeout = session.runDetailed(
        &timeoutTransport, config, recoveryTask.path(), 1, &timeoutRecord);
    if (!expect(!recoveredTimeout.success && recoveredTimeout.phase == QStringLiteral("collect") &&
                    recoveredTimeout.recoveryAttempted && recoveredTimeout.recoverySucceeded,
                "collect timeout performs STOP recovery") ||
        !expect(timeoutTransport.commands.contains(CaptureFrameType::StopCapture) &&
                    timeoutTransport.reopenCount == 1,
                "STOP recovery reopens transport exactly once") ||
        !expect(QFileInfo::exists(QDir(recoveryTask.path()).filePath(QStringLiteral("evidence/raw_capture.dat"))) &&
                    QFileInfo::exists(QDir(recoveryTask.path()).filePath(QStringLiteral("evidence/capture_status.json"))),
                "failed capture preserves raw and status evidence")) return 1;

    QTemporaryDir failedRecoveryTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_failed_recovery")));
    FakeCaptureTransport failedRecoveryTransport;
    failedRecoveryTransport.reopenSucceeds = false;
    SamplingCaptureRecord failedRecoveryRecord;
    const CaptureSessionResult failedRecovery = session.runDetailed(
        &failedRecoveryTransport, config, failedRecoveryTask.path(), 1, &failedRecoveryRecord);
    if (!expect(!failedRecovery.success && failedRecovery.recoveryAttempted &&
                    !failedRecovery.recoverySucceeded &&
                    failedRecovery.message.contains(QStringLiteral("CAPTURE_RECOVERY_FAILED")),
                "transport reopen failure is reported as CAPTURE_RECOVERY_FAILED")) return 1;

    CaptureFrameCodec encoder;
    QByteArray meta;
    append32(&meta, config.sampleRateHz);
    append32(&meta, 2);
    append32(&meta, 1);
    append32(&meta, 1);
    append32(&meta, config.protocolGroupMask);
    append32(&meta, 0);
    append32(&meta, kCapturePhysicalChannels);
    append32(&meta, kCaptureSampleWordBits);
    append32(&meta, 10);
    append32(&meta, 11);

    QByteArray samples(static_cast<int>(2 * kCaptureSampleBytes), '\0');
    samples[0] = 1;
    samples[static_cast<int>(kCaptureSampleBytes + 63)] = static_cast<char>(0x40); // CH510 mismatch
    QByteArray end;
    append32(&end, 2);
    append32(&end, 10);
    append32(&end, 11);
    append32(&end, 0);
    append32(&end, 0);
    append32(&end, static_cast<quint32>(samples.size()));
    append32(&end, static_cast<quint32>(samples.size()));
    append32(&end, 0);

    const QByteArray stream = encoder.encode(CaptureFrameType::CaptureMeta, meta, 0, 42) +
                              encoder.encode(CaptureFrameType::SampleData, samples, 1, 42) +
                              encoder.encode(CaptureFrameType::CaptureEnd, end, 2, 42);
    CaptureFrameCodec disabledCrcDecoder;
    const CaptureDecodeResult disabledCrc = disabledCrcDecoder.feed(
        encoder.encode(CaptureFrameType::HelloResponse, QByteArray("abc"), 9U, 0U, 1U << 1U));
    if (!expect(disabledCrc.success && disabledCrc.frames.size() == 1,
                "CRC-disabled frame remains decodable")) return 1;
    CaptureFrameCodec resyncDecoder;
    const QByteArray staleWords(32, '\x10');
    const CaptureDecodeResult resynced = resyncDecoder.feed(
        staleWords + encoder.encode(CaptureFrameType::HelloResponse, QByteArray("abc"), 10U));
    if (!expect(resynced.success && resynced.frames.size() == 1 &&
                    resynced.frames.first().header.sequence == 10U,
                "decoder resynchronizes after stale FT601 endpoint words")) return 1;
    QByteArray corrupted = encoder.encode(CaptureFrameType::HelloResponse, QByteArray("abc"), 9U);
    corrupted[32] = static_cast<char>(corrupted.at(32) ^ 0x01);
    CaptureFrameCodec corruptedDecoder;
    if (!expect(!corruptedDecoder.feed(corrupted).success,
                "CRC-enabled frame rejects corrupted payload")) return 1;
    CaptureFrameCodec decoder;
    QList<CaptureFrame> frames;
    for (int offset = 0; offset < stream.size(); offset += 17) {
        const CaptureDecodeResult decoded = decoder.feed(stream.mid(offset, 17));
        if (!expect(decoded.success, "fragmented frame stream passes CRC")) return 1;
        frames.append(decoded.frames);
    }
    if (!expect(frames.size() == 3, "three capture frames decoded")) return 1;

    SamplingCaptureAssembler assembler;
    for (const CaptureFrame& frame : frames) {
        if (!expect(assembler.append(frame, &error), "capture frame accepted")) return 1;
    }
    if (!expect(assembler.complete(), "CAPTURE_END closes capture")) return 1;
    const SamplingCaptureRecord record = assembler.record();
    if (!expect(record.samples.size() == 2 && record.captureId == 42, "1024-bit samples assembled")) return 1;

    QByteArray eventMeta;
    append32(&eventMeta, 120'000'000U);
    append32(&eventMeta, 0x19fU);
    append32(&eventMeta, 0x19fU);
    append32(&eventMeta, 0x060U);
    append32(&eventMeta, 64U);
    for (int index = 0; index < 11; ++index) append32(&eventMeta, 0U);
    QByteArray eventData;
    append64(&eventData, 1234U);
    append32(&eventData, 42U);
    append32(&eventData, 0U);
    eventData.append(char(1));
    eventData.append(char(2));
    eventData.append(char(0));
    eventData.append(char(0));
    append32(&eventData, 1U << 1U);
    append32(&eventData, 2U);
    append32(&eventData, 0U);
    eventData.append(char(0x55));
    eventData.append(char(0xaa));
    eventData.append(QByteArray(30, '\0'));
    QByteArray eventEnd;
    append32(&eventEnd, 0U);
    append32(&eventEnd, 0U);
    append32(&eventEnd, 1U);
    append32(&eventEnd, 1U);
    append32(&eventEnd, 0U);
    for (int protocol = 0; protocol < 9; ++protocol) append32(&eventEnd, 0U);
    append32(&eventEnd, 0x19fU);
    append32(&eventEnd, 0x19fU);
    const QByteArray v3Stream =
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 20U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, eventData, 21U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::CaptureMeta, meta, 22U, 42U, 0U, kCaptureProtocolVersion) +
        encoder.encode(CaptureFrameType::SampleData, samples, 23U, 42U, 0U, kCaptureProtocolVersion) +
        encoder.encode(CaptureFrameType::EventEnd, eventEnd, 24U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::CaptureEnd, end, 25U, 42U, 0U, kCaptureProtocolVersion);
    CaptureFrameCodec v3Decoder;
    const CaptureDecodeResult v3Decoded = v3Decoder.feed(v3Stream);
    if (!expect(v3Decoded.success && v3Decoded.frames.size() == 6,
                "v3 codec accepts negotiated event frames")) return 1;
    SamplingCaptureAssembler v3Assembler;
    for (const CaptureFrame& frame : v3Decoded.frames) {
        if (!expect(v3Assembler.append(frame, &error), "v3 capture/event frame accepted")) return 1;
    }
    const SamplingCaptureRecord v3Record = v3Assembler.record();
    if (!expect(v3Assembler.complete() && v3Record.protocolEvents.size() == 1 &&
                    v3Record.protocolEvents.first().timestampTicks == 1234U &&
                    v3Record.protocolEvents.first().payload == QByteArray::fromHex("55aa") &&
                    v3Record.eventEmittedTotal == 1U && v3Record.eventDroppedTotal == 0U,
                "v3 event record and EVENT_END statistics assemble exactly")) return 1;

    const V4Fixture v4Fixture;
    for (const bool eventMetaFirst : {false, true}) {
        QList<CaptureFrame> inputFrames = v4Frames(v4Fixture, eventMetaFirst);
        if (!eventMetaFirst) {
            QByteArray encodedV4Stream;
            for (const CaptureFrame& frame : inputFrames) {
                encodedV4Stream += encoder.encode(
                    frame.header.type, frame.payload, frame.header.sequence,
                    frame.header.captureId, 0U, frame.header.version);
            }
            CaptureFrameCodec v4Decoder;
            const CaptureDecodeResult decodedV4 = v4Decoder.feed(encodedV4Stream);
            if (!expect(decodedV4.success && decodedV4.frames.size() == inputFrames.size(),
                        "v4 encoded stream passes frame CRC and payload decoding")) return 1;
            inputFrames = decodedV4.frames;
        }
        SamplingCaptureAssembler v4Assembler;
        for (const CaptureFrame& frame : inputFrames) {
            if (!expect(v4Assembler.append(frame, &error),
                        "v4 capture/event frame accepted")) return 1;
        }
        const SamplingCaptureRecord v4Record = v4Assembler.record();
        if (!expect(v4Assembler.complete() &&
                        v4Record.contractVersion == kCaptureProtocolVersionV4 &&
                        v4Record.physicalChannels == kCapturePhysicalChannelsV4 &&
                        v4Record.sampleWordBits == kCaptureSampleWordBitsV4 &&
                        v4Record.sampleBytes == kCaptureSampleBytesV4 &&
                        v4Record.windowOriginTicks == V4Fixture::kOrigin &&
                        v4Record.triggerTicks == V4Fixture::kTrigger &&
                        v4Record.windowEndExclusiveTicks == V4Fixture::kEndExclusive &&
                        v4Record.samples.size() == static_cast<int>(kCaptureWindowSamplesV4) &&
                        v4Record.samples.first().size() == static_cast<int>(kCaptureSampleBytesV4),
                    "v4 META order and 512-bit continuous window assemble exactly")) return 1;
        if (!expect(v4Record.eventInitialState == 0x0001U &&
                        v4Record.eventWatchdogTicks == 123U &&
                        v4Record.eventHardTimeoutTicks == 456U &&
                        v4Record.sparseChanges.size() == 2 &&
                        v4Record.sparseChanges.at(0).globalSequence == 41U &&
                        v4Record.sparseChanges.at(0).stateAfter == 0x1003U &&
                        v4Record.sparseChanges.at(0).sourceMask == 0x82U &&
                        v4Record.sparseChanges.at(1).absoluteIndex ==
                            V4Fixture::kOrigin + 0x110U &&
                        v4Record.sparseChanges.at(1).absoluteIndexLow == 0x10U &&
                        v4Record.sparseChanges.at(1).stateAfter == 0x1513U &&
                        v4Record.eventObservedTotal == 2U &&
                        v4Record.eventRetainedTotal == 2U &&
                        v4Record.eventUploadedTotal == 2U,
                    "v4 sparse state chain, source mask, sequence and wrapped index are preserved")) {
            return 1;
        }
    }
    const auto expectV4Rejected = [&](const QList<CaptureFrame>& candidate,
                                      const QString& errorPrefix,
                                      const char* message) {
        SamplingCaptureAssembler invalidAssembler;
        bool rejected = false;
        error.clear();
        for (const CaptureFrame& frame : candidate) {
            if (!invalidAssembler.append(frame, &error)) {
                rejected = true;
                break;
            }
        }
        return expect(rejected && (errorPrefix.isEmpty() || error.startsWith(errorPrefix)), message);
    };

    QList<CaptureFrame> mixedFrames = v4Frames(v4Fixture, false);
    mixedFrames[1].header.version = kCaptureProtocolVersionV3;
    if (!expectV4Rejected(mixedFrames, QString(),
                          "mixed legacy and v4 stream is rejected")) return 1;

    V4Fixture stateReservedFixture;
    overwrite32(&stateReservedFixture.firstChange, 8,
                read32(stateReservedFixture.firstChange, 8) | 0x00010000U);
    if (!expectV4Rejected(v4Frames(stateReservedFixture, false), QString(),
                          "v4 state reserved bits are rejected")) return 1;

    V4Fixture initialStateReservedFixture;
    overwrite32(&initialStateReservedFixture.eventMeta, 32, 0x00010001U);
    if (!expectV4Rejected(v4Frames(initialStateReservedFixture, false), QString(),
                          "v4 initial-state reserved bits are rejected")) return 1;

    V4Fixture changeReservedFixture;
    overwrite32(&changeReservedFixture.firstChange, 12,
                read32(changeReservedFixture.firstChange, 12) | 0x00010000U);
    if (!expectV4Rejected(v4Frames(changeReservedFixture, false), QString(),
                          "v4 change reserved bits are rejected")) return 1;

    V4Fixture zeroChangeFixture;
    overwrite32(&zeroChangeFixture.firstChange, 12, 0U);
    if (!expectV4Rejected(v4Frames(zeroChangeFixture, false), QString(),
                          "v4 zero change is rejected")) return 1;

    V4Fixture wrongSourceFixture;
    overwrite32(&wrongSourceFixture.firstChange, 8, 0x1003U | (0x02U << 20U));
    if (!expectV4Rejected(v4Frames(wrongSourceFixture, false), QString(),
                          "v4 source mask must exactly match changed protocols")) return 1;

    V4Fixture brokenChainFixture;
    overwrite32(&brokenChainFixture.firstChange, 8, 0x1002U | (0x82U << 20U));
    if (!expectV4Rejected(v4Frames(brokenChainFixture, false), QString(),
                          "v4 broken state chain is rejected")) return 1;

    V4Fixture sequenceGapFixture;
    overwrite32(&sequenceGapFixture.secondChange, 4, 43U);
    if (!expectV4Rejected(v4Frames(sequenceGapFixture, false), QString(),
                          "v4 capture-local sequence gap is rejected")) return 1;

    V4Fixture duplicateIndexFixture;
    overwrite32(&duplicateIndexFixture.secondChange, 0,
                read32(duplicateIndexFixture.firstChange, 0));
    if (!expectV4Rejected(v4Frames(duplicateIndexFixture, false), QString(),
                          "v4 duplicate absolute index is rejected")) return 1;

    V4Fixture decreasingIndexFixture;
    overwrite32(&decreasingIndexFixture.secondChange, 0,
                static_cast<quint32>(V4Fixture::kOrigin + 2U));
    if (!expectV4Rejected(v4Frames(decreasingIndexFixture, false), QString(),
                          "v4 decreasing absolute index is rejected")) return 1;

    V4Fixture outsideWindowFixture;
    overwrite32(&outsideWindowFixture.secondChange, 0,
                static_cast<quint32>(V4Fixture::kEndExclusive));
    if (!expectV4Rejected(v4Frames(outsideWindowFixture, false), QString(),
                          "v4 out-of-window absolute index is rejected")) return 1;

    V4Fixture retainedMismatchFixture;
    overwrite32(&retainedMismatchFixture.eventMeta, 60, 3U);
    if (!expectV4Rejected(v4Frames(retainedMismatchFixture, false), QString(),
                          "v4 EVENT_META retained count mismatch is rejected")) return 1;

    V4Fixture observedTooSmallFixture;
    overwrite32(&observedTooSmallFixture.eventEnd, 8, 1U);
    if (!expectV4Rejected(v4Frames(observedTooSmallFixture, false), QString(),
                          "v4 observed count below retained count is rejected")) return 1;

    V4Fixture dropSumFixture;
    overwrite32(&dropSumFixture.eventEnd, 0,
                static_cast<quint32>(EventEndReason::Overflow));
    overwrite32(&dropSumFixture.eventEnd, 20, 1U);
    if (!expectV4Rejected(v4Frames(dropSumFixture, false), QString(),
                          "v4 per-source drop sum mismatch is rejected")) return 1;

    V4Fixture lossWithoutReasonFixture;
    overwrite32(&lossWithoutReasonFixture.eventEnd, 8, 3U);
    overwrite32(&lossWithoutReasonFixture.eventEnd, 20, 1U);
    overwrite32(&lossWithoutReasonFixture.eventEnd, 24, 1U);
    if (!expectV4Rejected(
            v4Frames(lossWithoutReasonFixture, false),
            QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR"),
            "v4 loss without overflow reason is rejected bidirectionally")) return 1;

    V4Fixture reasonWithoutLossFixture;
    overwrite32(&reasonWithoutLossFixture.eventEnd, 0,
                static_cast<quint32>(EventEndReason::Overflow));
    if (!expectV4Rejected(
            v4Frames(reasonWithoutLossFixture, false),
            QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR"),
            "v4 overflow reason without loss is rejected bidirectionally")) return 1;

    V4Fixture ringOverwriteFixture;
    overwrite32(&ringOverwriteFixture.eventEnd, 8, 9U);
    SamplingCaptureAssembler ringOverwriteAssembler;
    for (const CaptureFrame& frame : v4Frames(ringOverwriteFixture, false)) {
        if (!expect(ringOverwriteAssembler.append(frame, &error),
                    "v4 ring overwrite is not reported as event loss")) return 1;
    }

    V4Fixture spiModeFixture;
    overwrite32(&spiModeFixture.eventMeta, 20,
                kCaptureEventLayoutV4 | (3U << 8U) | (1U << 10U));
    SamplingCaptureAssembler spiModeAssembler;
    for (const CaptureFrame& frame : v4Frames(spiModeFixture, false)) {
        if (!expect(spiModeAssembler.append(frame, &error),
                    "v4 EVENT_META accepts declared SPI mode bits")) return 1;
    }
    if (!expect(spiModeAssembler.record().eventSpiMode == 3U &&
                    spiModeAssembler.record().eventSpiModeValid,
                "v4 EVENT_META preserves SPI mode audit fields")) return 1;

    V4Fixture shortWindowFixture;
    shortWindowFixture.samples.chop(static_cast<int>(kCaptureSampleBytesV4));
    overwrite32(&shortWindowFixture.captureEnd, 0, kCaptureWindowSamplesV4 - 1U);
    overwrite32(&shortWindowFixture.captureEnd, 20,
                static_cast<quint32>(shortWindowFixture.samples.size()));
    overwrite32(&shortWindowFixture.captureEnd, 24,
                static_cast<quint32>(shortWindowFixture.samples.size()));
    if (!expectV4Rejected(v4Frames(shortWindowFixture, false), QString(),
                          "v4 continuous window must contain exactly 4096 samples")) return 1;
    QByteArray overflowEnd = eventEnd;
    overwrite32(&overflowEnd, 0, 3U);
    overwrite32(&overflowEnd, 4, 1U);
    overwrite32(&overflowEnd, 8, 2U);
    overwrite32(&overflowEnd, 16, 1U);
    overwrite32(&overflowEnd, 20, 1U);
    const QByteArray overflowStream =
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 20U, 42U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, eventData, 21U, 42U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventEnd, overflowEnd, 22U, 42U, 0U,
                       kCaptureProtocolVersionV3);
    CaptureFrameCodec overflowDecoder;
    const CaptureDecodeResult overflowDecoded = overflowDecoder.feed(overflowStream);
    SamplingCaptureAssembler overflowAssembler;
    if (!expect(overflowDecoded.success &&
                    overflowAssembler.append(overflowDecoded.frames.at(0), &error) &&
                    overflowAssembler.append(overflowDecoded.frames.at(1), &error) &&
                    !overflowAssembler.append(overflowDecoded.frames.at(2), &error) &&
                    error.startsWith(QStringLiteral("CAPTURE_END_OVERFLOW")),
                "EVENT_END loss is classified as CAPTURE_END_OVERFLOW")) return 1;
    QByteArray mismatchedOverflowEnd = overflowEnd;
    overwrite32(&mismatchedOverflowEnd, 0, 4U);
    QByteArray mismatchedOverflowData = eventData;
    overwrite32(&mismatchedOverflowData, 8, 43U);
    const QByteArray mismatchedOverflowStream =
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 23U, 43U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, mismatchedOverflowData, 24U, 43U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventEnd, mismatchedOverflowEnd, 25U, 43U, 0U,
                       kCaptureProtocolVersionV3);
    CaptureFrameCodec mismatchedOverflowDecoder;
    const CaptureDecodeResult mismatchedOverflowDecoded =
        mismatchedOverflowDecoder.feed(mismatchedOverflowStream);
    SamplingCaptureAssembler mismatchedOverflowAssembler;
    if (!expect(mismatchedOverflowDecoded.success &&
                    mismatchedOverflowAssembler.append(mismatchedOverflowDecoded.frames.at(0), &error) &&
                    mismatchedOverflowAssembler.append(mismatchedOverflowDecoded.frames.at(1), &error) &&
                    !mismatchedOverflowAssembler.append(mismatchedOverflowDecoded.frames.at(2), &error) &&
                    error.startsWith(QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR")),
                "EVENT_END loss with non-overflow reason is rejected as a contract error")) return 1;
    QByteArray emptyOverflowEnd = eventEnd;
    overwrite32(&emptyOverflowEnd, 0, 3U);
    QByteArray emptyOverflowData = eventData;
    overwrite32(&emptyOverflowData, 8, 44U);
    const QByteArray emptyOverflowStream =
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 26U, 44U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, emptyOverflowData, 27U, 44U, 0U,
                       kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventEnd, emptyOverflowEnd, 28U, 44U, 0U,
                       kCaptureProtocolVersionV3);
    CaptureFrameCodec emptyOverflowDecoder;
    const CaptureDecodeResult emptyOverflowDecoded = emptyOverflowDecoder.feed(emptyOverflowStream);
    SamplingCaptureAssembler emptyOverflowAssembler;
    if (!expect(emptyOverflowDecoded.success &&
                    emptyOverflowAssembler.append(emptyOverflowDecoded.frames.at(0), &error) &&
                    emptyOverflowAssembler.append(emptyOverflowDecoded.frames.at(1), &error) &&
                    !emptyOverflowAssembler.append(emptyOverflowDecoded.frames.at(2), &error) &&
                    error.startsWith(QStringLiteral("CAPTURE_END_OVERFLOW_CONTRACT_ERROR")),
                "EVENT_END overflow reason without loss is rejected as a contract error")) return 1;
    for (const quint32 reason : {1U, 2U, 4U, 5U}) {
        QByteArray reasonEnd = eventEnd;
        overwrite32(&reasonEnd, 0, reason);
        const QByteArray reasonStream =
            encoder.encode(CaptureFrameType::EventMeta, eventMeta, 20U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::EventData, eventData, 21U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureMeta, meta, 22U, 42U) +
            encoder.encode(CaptureFrameType::SampleData, samples, 23U, 42U) +
            encoder.encode(CaptureFrameType::EventEnd, reasonEnd, 24U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureEnd, end, 25U, 42U);
        QTemporaryDir reasonTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_end_reason")));
        FakeCaptureTransport reasonTransport;
        reasonTransport.queueIncoming(reasonStream);
        SamplingCaptureRecord reasonRecord;
        if (!expect(!session.collect(&reasonTransport, reasonTask.path(), 1000,
                                     &reasonRecord, &error),
                    "non-program EVENT_END reason cannot be reported as capture success")) return 1;
        if (!expect(error.contains(QStringLiteral("CAPTURE_END_")),
                    "EVENT_END reason is preserved in the failure diagnostic")) return 1;
    }
    QTemporaryDir earlyEventTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_early_event")));
    FakeCaptureTransport earlyEventTransport;
    earlyEventTransport.queueIncoming(v3Stream);
    SamplingCaptureRecord earlyEventRecord;
    if (!expect(session.collect(&earlyEventTransport, earlyEventTask.path(), 1000,
                                &earlyEventRecord, &error) &&
                    earlyEventRecord.protocolEvents.size() == 1 &&
                    earlyEventRecord.samples.size() == 2,
                "collect preserves EVENT_META/EVENT_DATA before CAPTURE_META")) return 1;
    QTemporaryDir v3Task(lockstepTestTemporaryTemplate(QStringLiteral("capture_v3")));
    if (!expect(exportScalarVcd(v3Record, v3Task.path(), nullptr, nullptr, &error),
                "v3 capture exports continuous VCD and sparse events") ||
        !expect(QFileInfo::exists(QDir(v3Task.path()).filePath(QStringLiteral("evidence/protocol_events.json"))),
                "v3 sparse event archive exists")) return 1;
    QFile eventArchive(QDir(v3Task.path()).filePath(QStringLiteral("evidence/protocol_events.json")));
    if (!expect(eventArchive.open(QIODevice::ReadOnly), "v3 event archive readable")) return 1;
    const QJsonObject eventArchiveObject = QJsonDocument::fromJson(eventArchive.readAll()).object();
    if (!expect(eventArchiveObject.value(QStringLiteral("schema")).toString() ==
                    QStringLiteral("lockstep-protocol-events-v3") &&
                    eventArchiveObject.value(QStringLiteral("events")).toArray().size() == 1,
                "v3 event archive preserves contract and event count")) return 1;

    QByteArray badEventData = eventData;
    badEventData[20] = 0;
    const QByteArray badEventStream =
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 30U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, badEventData, 31U, 42U, 0U, kCaptureProtocolVersionV3);
    CaptureFrameCodec badEventDecoder;
    const CaptureDecodeResult badEventDecoded = badEventDecoder.feed(badEventStream);
    SamplingCaptureAssembler badEventAssembler;
    if (!expect(badEventDecoded.success && badEventAssembler.append(badEventDecoded.frames.at(0), &error) &&
                    !badEventAssembler.append(badEventDecoded.frames.at(1), &error),
                "event record without its protocol reason bit is rejected")) return 1;

    QByteArray wrongCapabilitiesMeta = eventMeta;
    overwrite32(&wrongCapabilitiesMeta, 8, 0x183U);
    CaptureFrameCodec wrongCapabilitiesDecoder;
    const CaptureDecodeResult wrongCapabilitiesDecoded = wrongCapabilitiesDecoder.feed(
        encoder.encode(CaptureFrameType::EventMeta, wrongCapabilitiesMeta, 40U, 42U, 0U,
                       kCaptureProtocolVersionV3));
    SamplingCaptureAssembler wrongCapabilitiesAssembler;
    if (!expect(wrongCapabilitiesDecoded.success &&
                    !wrongCapabilitiesAssembler.append(wrongCapabilitiesDecoded.frames.first(), &error),
                "EVENT_META rejects an enabled mask different from the request")) return 1;

    QByteArray invalidSourceKindData = eventData;
    invalidSourceKindData[18] = char(4);
    CaptureFrameCodec invalidSourceKindDecoder;
    const CaptureDecodeResult invalidSourceKindDecoded = invalidSourceKindDecoder.feed(
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 50U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, invalidSourceKindData, 51U, 42U, 0U,
                       kCaptureProtocolVersionV3));
    SamplingCaptureAssembler invalidSourceKindAssembler;
    if (!expect(invalidSourceKindDecoded.success &&
                    invalidSourceKindAssembler.append(invalidSourceKindDecoded.frames.at(0), &error) &&
                    !invalidSourceKindAssembler.append(invalidSourceKindDecoded.frames.at(1), &error),
                "event record rejects an unknown source kind")) return 1;

    QByteArray invalidPaddingData = eventData;
    invalidPaddingData[34] = char(1);
    CaptureFrameCodec invalidPaddingDecoder;
    const CaptureDecodeResult invalidPaddingDecoded = invalidPaddingDecoder.feed(
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 60U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, invalidPaddingData, 61U, 42U, 0U,
                       kCaptureProtocolVersionV3));
    SamplingCaptureAssembler invalidPaddingAssembler;
    if (!expect(invalidPaddingDecoded.success &&
                    invalidPaddingAssembler.append(invalidPaddingDecoded.frames.at(0), &error) &&
                    !invalidPaddingAssembler.append(invalidPaddingDecoded.frames.at(1), &error),
                "event record rejects non-zero unused payload bytes")) return 1;

    QByteArray droppedEventEnd = eventEnd;
    overwrite32(&droppedEventEnd, 8, 2U);
    overwrite32(&droppedEventEnd, 16, 1U);
    overwrite32(&droppedEventEnd, 20, 1U);
    CaptureFrameCodec droppedEventDecoder;
    const CaptureDecodeResult droppedEventDecoded = droppedEventDecoder.feed(
        encoder.encode(CaptureFrameType::EventMeta, eventMeta, 70U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventData, eventData, 71U, 42U, 0U, kCaptureProtocolVersionV3) +
        encoder.encode(CaptureFrameType::EventEnd, droppedEventEnd, 72U, 42U, 0U,
                       kCaptureProtocolVersionV3));
    SamplingCaptureAssembler droppedEventAssembler;
    if (!expect(droppedEventDecoded.success &&
                    droppedEventAssembler.append(droppedEventDecoded.frames.at(0), &error) &&
                    droppedEventAssembler.append(droppedEventDecoded.frames.at(1), &error) &&
                    !droppedEventAssembler.append(droppedEventDecoded.frames.at(2), &error),
                "EVENT_END rejects any reported event loss") ||
        !expect(error.contains(QStringLiteral("overflow=0x000")) &&
                    error.contains(QStringLiteral("received=1")) &&
                    error.contains(QStringLiteral("drop_counts=[1,0,0,0,0,0,0,0,0]")),
                "EVENT_END loss diagnostic preserves exact counters")) return 1;

    QTemporaryDir task(lockstepTestTemporaryTemplate(QStringLiteral("capture_main")));
    QString vcdPath;
    QString sidecarPath;
    if (!expect(exportScalarVcd(record, task.path(), &vcdPath, &sidecarPath, &error), "VCD export succeeds")) return 1;
    QFile vcd(vcdPath);
    if (!expect(vcd.open(QIODevice::ReadOnly), "VCD readable")) return 1;
    const QByteArray text = vcd.readAll();
    if (!expect(text.count("$var wire 1 ") == 1024, "VCD has exactly 1024 scalar variables") ||
        !expect(text.contains(" CH0 $end") && text.contains(" CH1023 $end"), "VCD uses CH0..CH1023") ||
        !expect(text.contains("$timescale 1 ps $end") && text.contains("#8333\n"),
                "120 MHz samples use a picosecond time axis")) return 1;
    QFile sidecar(sidecarPath);
    if (!expect(sidecar.open(QIODevice::ReadOnly), "sidecar readable")) return 1;
    const QJsonObject sidecarObject = QJsonDocument::fromJson(sidecar.readAll()).object();
    if (!expect(sidecarObject.value("sample_word_bits").toInt() == 1024,
                "sidecar fixes 1024-bit contract") ||
        !expect(sidecarObject.value("window_start_index").toInteger() == 10 &&
                    sidecarObject.value("window_end_index").toString() == QStringLiteral("11"),
                "sidecar preserves the inclusive absolute capture window")) return 1;

    if (argc >= 2) {
        QTemporaryDir replayTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_replay")));
        const QString rawPath = QDir(replayTask.path()).filePath(QStringLiteral("fixture.dat"));
        QFile rawFile(rawPath);
        QProcess replay;
        const QByteArray replayStream = v3Stream;
        if (!expect(rawFile.open(QIODevice::WriteOnly) &&
                        rawFile.write(replayStream) == replayStream.size(),
                    "v3 early-event offline fixture written")) return 1;
        rawFile.close();
        replay.start(QString::fromLocal8Bit(argv[1]), {
            QStringLiteral("--offline-capture"), rawPath,
            QStringLiteral("--task-root"), replayTask.path(),
            QStringLiteral("--task-id"), QStringLiteral("offline_fixture")});
        if (!expect(replay.waitForFinished(30'000) && replay.exitCode() == 0,
                    "unique EXE completes offline capture replay")) return 1;
        const QStringList outputs = {
            QStringLiteral("evidence/raw_capture.dat"),
            QStringLiteral("evidence/capture_sidecar.json"),
            QStringLiteral("waveform/capture.vcd"),
            QStringLiteral("evidence/protocol_analysis.json"),
            QStringLiteral("evidence/artifacts.json"),
            QStringLiteral("reports/report.json"),
            QStringLiteral("reports/report.html")};
        for (const QString& output : outputs) {
            if (!expect(QFileInfo::exists(QDir(replayTask.path()).filePath(output)),
                        "offline replay output exists")) return 1;
        }

        QByteArray watchdogEventEnd = eventEnd;
        overwrite32(&watchdogEventEnd, 0, static_cast<quint32>(EventEndReason::Watchdog));
        const QByteArray watchdogStream =
            encoder.encode(CaptureFrameType::EventMeta, eventMeta, 20U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::EventData, eventData, 21U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureMeta, meta, 22U, 42U) +
            encoder.encode(CaptureFrameType::SampleData, samples, 23U, 42U) +
            encoder.encode(CaptureFrameType::EventEnd, watchdogEventEnd, 24U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureEnd, end, 25U, 42U);
        QTemporaryDir watchdogReplayTask(
            lockstepTestTemporaryTemplate(QStringLiteral("capture_watchdog_replay")));
        const QString watchdogRawPath =
            QDir(watchdogReplayTask.path()).filePath(QStringLiteral("watchdog_fixture.dat"));
        QFile watchdogRaw(watchdogRawPath);
        if (!expect(watchdogRaw.open(QIODevice::WriteOnly) &&
                        watchdogRaw.write(watchdogStream) == watchdogStream.size(),
                    "watchdog offline fixture written")) return 1;
        watchdogRaw.close();
        QProcess watchdogReplay;
        watchdogReplay.start(QString::fromLocal8Bit(argv[1]), {
            QStringLiteral("--offline-capture"), watchdogRawPath,
            QStringLiteral("--task-root"), watchdogReplayTask.path(),
            QStringLiteral("--task-id"), QStringLiteral("offline_watchdog_fixture")});
        if (!expect(watchdogReplay.waitForFinished(30'000) && watchdogReplay.exitCode() == 13,
                    "offline replay rejects a watchdog-ended capture")) return 1;

        QByteArray subsetEventMeta = eventMeta;
        overwrite32(&subsetEventMeta, 8, 0x183U);
        QByteArray subsetEventEnd = eventEnd;
        overwrite32(&subsetEventEnd, 56, 0x183U);
        const QByteArray subsetStream =
            encoder.encode(CaptureFrameType::EventMeta, subsetEventMeta, 20U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::EventData, eventData, 21U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureMeta, meta, 22U, 42U, 0U,
                           kCaptureProtocolVersion) +
            encoder.encode(CaptureFrameType::SampleData, samples, 23U, 42U, 0U,
                           kCaptureProtocolVersion) +
            encoder.encode(CaptureFrameType::EventEnd, subsetEventEnd, 24U, 42U, 0U,
                           kCaptureProtocolVersionV3) +
            encoder.encode(CaptureFrameType::CaptureEnd, end, 25U, 42U, 0U,
                           kCaptureProtocolVersion);
        QTemporaryDir subsetReplayTask(lockstepTestTemporaryTemplate(QStringLiteral("capture_subset_replay")));
        const QString subsetRawPath = QDir(subsetReplayTask.path()).filePath(
            QStringLiteral("subset_fixture.dat"));
        QFile subsetRaw(subsetRawPath);
        if (!expect(subsetRaw.open(QIODevice::WriteOnly) &&
                        subsetRaw.write(subsetStream) == subsetStream.size(),
                    "subset-mask offline fixture written")) return 1;
        subsetRaw.close();
        QProcess subsetReplay;
        subsetReplay.start(QString::fromLocal8Bit(argv[1]), {
            QStringLiteral("--offline-capture"), subsetRawPath,
            QStringLiteral("--event-enable-mask"), QStringLiteral("0x183"),
            QStringLiteral("--task-root"), subsetReplayTask.path(),
            QStringLiteral("--task-id"), QStringLiteral("offline_subset_fixture")});
        if (!expect(subsetReplay.waitForFinished(30'000) && subsetReplay.exitCode() == 0,
                    "offline replay accepts the explicitly requested event subset")) return 1;

        QFile trailingRaw(rawPath);
        const QByteArray trailingStream = replayStream +
            encoder.encode(CaptureFrameType::HelloResponse, QByteArray(), 26U);
        if (!expect(trailingRaw.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                        trailingRaw.write(trailingStream) == trailingStream.size(),
                    "trailing-frame offline fixture written")) return 1;
        trailingRaw.close();
        QProcess trailingReplay;
        trailingReplay.start(QString::fromLocal8Bit(argv[1]), {
            QStringLiteral("--offline-capture"), rawPath,
            QStringLiteral("--task-root"), replayTask.path(),
            QStringLiteral("--task-id"), QStringLiteral("offline_trailing_fixture")});
        if (!expect(trailingReplay.waitForFinished(30'000) && trailingReplay.exitCode() == 5,
                    "offline replay rejects frames after CAPTURE_END")) return 1;

        QProcess invalidLiveCapture;
        invalidLiveCapture.start(QString::fromLocal8Bit(argv[1]), {
            QStringLiteral("--live-capture"),
            QStringLiteral("--task-root"), replayTask.path(),
            QStringLiteral("--sample-rate"), QStringLiteral("invalid")});
        if (!expect(invalidLiveCapture.waitForFinished(10'000) &&
                        invalidLiveCapture.exitStatus() == QProcess::NormalExit &&
                        invalidLiveCapture.exitCode() == 21,
                    "live capture validates arguments before loading the FT601 runtime")) return 1;
    }
    return 0;
}
