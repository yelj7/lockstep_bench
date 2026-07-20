/**********************************************************
* 文件名: sampling_capture_test.cpp
* 日期: 2026-07-16
* 版本: 3.1
* 更新记录: 主分支恢复 D3XX 后更新传输无关的恢复门槛断言。
* 描述: 验证 v2/v3、恢复门槛、1024-bit VCD 和稀疏事件合同。
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

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

class FakeCaptureTransport final : public lockstep::acquisition::CaptureTransport {
public:
    bool writePipe(quint8 pipeId, const QByteArray& bytes, int* transferred, QString* error) override
    {
        Q_UNUSED(error);
        if (pipeId != 0x02U) return false;
        if (transferred != nullptr) *transferred = bytes.size();
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
    QString error;
    if (!expect(config.validate(&error), "default 1024 capture config is valid") ||
        !expect(config.sampleRateHz == 120'000'000U, "default config matches live 120 MHz hardware") ||
        !expect(config.toPayload().size() == 52, "capture config payload preserves v2 52-byte wire contract") ||
        !expect(config.toEventPayload().size() == 16, "event config uses explicit v3 16-byte wire contract")) return 1;

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

    QTemporaryDir afterArmTask;
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

    QTemporaryDir recoveryTask;
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

    QTemporaryDir failedRecoveryTask;
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
    QTemporaryDir earlyEventTask;
    FakeCaptureTransport earlyEventTransport;
    earlyEventTransport.queueIncoming(v3Stream);
    SamplingCaptureRecord earlyEventRecord;
    if (!expect(session.collect(&earlyEventTransport, earlyEventTask.path(), 1000,
                                &earlyEventRecord, &error) &&
                    earlyEventRecord.protocolEvents.size() == 1 &&
                    earlyEventRecord.samples.size() == 2,
                "collect preserves EVENT_META/EVENT_DATA before CAPTURE_META")) return 1;
    QTemporaryDir v3Task;
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

    QTemporaryDir task;
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
    if (!expect(sidecarObject.value("sample_word_bits").toInt() == 1024, "sidecar fixes 1024-bit contract")) return 1;

    if (argc >= 2) {
        QTemporaryDir replayTask;
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
        QTemporaryDir subsetReplayTask;
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
