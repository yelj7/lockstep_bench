/**********************************************************
* 文件名: sampling_capture_test.cpp
* 日期: 2026-07-16
* 版本: 1.2
* 更新记录: 对齐 CONFIG_CAPTURE 与 GET_STATUS 均返回 STATUS_RSP 的硬件合同。
* 描述: 验证 FT601 帧、配置握手、采集结束门槛和 1024-bit VCD 导出。
**********************************************************/

/**********************************************************
* 文件名: sampling_capture_test.cpp
* 日期: 2026-07-14
* 版本: v1.1
* 更新记录: 增加纯 C++ HELLO/CONFIG/STATUS 握手测试
* 描述: 验证帧 CRC、分片接收、采集结束门槛和标量 VCD/sidecar 导出。
**********************************************************/

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
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
                request.header.sequence + responseSequenceDelta);
        } else if (request.header.type == lockstep::acquisition::CaptureFrameType::ConfigCapture ||
                   request.header.type == lockstep::acquisition::CaptureFrameType::GetStatus ||
                   request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture) {
            const quint32 responseState = request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture
                ? armState : configuredState;
            append32(&payload, responseState);
            append32(&payload, request.header.sequence);
            append32(&payload, request.header.type == lockstep::acquisition::CaptureFrameType::ArmCapture ? 42U : 0U);
            for (int index = 0; index < 2; ++index) append32(&payload, 0U);
            append32(&payload, 0U);
            append32(&payload, lastError);
            for (int index = 0; index < 9; ++index) append32(&payload, 0U);
            pending_ += responseCodec_.encode(
                lockstep::acquisition::CaptureFrameType::StatusResponse, payload,
                request.header.sequence + responseSequenceDelta +
                    ((request.header.type == lockstep::acquisition::CaptureFrameType::ConfigCapture)
                         ? configResponseSequenceGap
                         : 0U));
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

    bool isOpen() const override { return true; }

    QList<lockstep::acquisition::CaptureFrameType> commands;
    quint32 protocolVersion = lockstep::acquisition::kCaptureProtocolVersion;
    quint32 responseSequenceDelta = 0U;
    quint32 configResponseSequenceGap = 0U;
    quint32 configuredState = 2U;
    quint32 armState = 4U;
    quint32 lastError = 0U;
    int pendingTimeoutReads = 0;

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
        !expect(config.toPayload().size() == 52, "capture config payload preserves v2 52-byte wire contract")) return 1;

    FakeCaptureTransport transport;
    transport.pendingTimeoutReads = 2;
    SamplingCaptureSession session;
    if (!expect(session.configure(&transport, config, &error), "C++ CONFIG handshake reaches CONFIGURED") ||
        !expect(transport.commands == QList<CaptureFrameType>{
                    CaptureFrameType::HelloRequest, CaptureFrameType::ConfigCapture, CaptureFrameType::GetStatus},
                "C++ handshake is HELLO -> CONFIG -> GET_STATUS")) return 1;
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
        if (!expect(rawFile.open(QIODevice::WriteOnly) && rawFile.write(stream) == stream.size(),
                    "offline fixture written")) return 1;
        rawFile.close();
        QProcess replay;
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
