/**********************************************************
* 文件名: lockstep_trace_analysis_test.cpp
* 日期: 2026-07-19
* 版本: v3.0
* 更新记录: 增加稀疏事件时间轴合并和 ETH/USB design_gap mask 回归。
* 描述: 验证九协议、稀疏事件、mismatch 和共享时间轴进入展示模型。
**********************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include "error_registry.h"
#include "protocol_analysis.h"
#include "waveform_trace_viewer.h"

namespace {

bool writeTextFile(const QString& path, const QString& text)
{
    const QFileInfo info(path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray payload = text.toUtf8();
    if (file.write(payload) != payload.size()) {
        return false;
    }
    return file.commit();
}

void setPackedValue(QString* const bits, const int lsb, const int width, const quint64 value)
{
    if (bits == nullptr) {
        return;
    }
    for (int bit = 0; bit < width; ++bit) {
        (*bits)[bits->size() - 1 - (lsb + bit)] = ((value >> bit) & 1U) != 0U
            ? QLatin1Char('1') : QLatin1Char('0');
    }
}

bool writeWideProtocolFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    QString bits(1024, QLatin1Char('0'));
    for (const int realSourceBit : {768, 770, 773, 776, 781, 782}) {
        setPackedValue(&bits, realSourceBit, 1, 1U);
    }
    setPackedValue(&bits, 512, 1, 1U);
    setPackedValue(&bits, 513, 1, 1U);
    setPackedValue(&bits, 547, 1, 1U);
    setPackedValue(&bits, 576, 1, 1U);
    setPackedValue(&bits, 577, 1, 1U);
    setPackedValue(&bits, 608, 1, 1U);
    setPackedValue(&bits, 609, 1, 1U);

    QString vcd = QStringLiteral(
        "$timescale 1 ns $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    int time = 0;
    const auto emitSample = [&vcd, &time](const QString& value) {
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(time).arg(value);
        time += 10;
    };
    emitSample(bits);

    setPackedValue(&bits, 32, 32, 0x80001000U);
    setPackedValue(&bits, 416, 1, 1U);
    setPackedValue(&bits, 417, 2, 2U);
    setPackedValue(&bits, 429, 1, 1U);
    setPackedValue(&bits, 320, 16, 1U);
    emitSample(bits);
    setPackedValue(&bits, 32, 32, 0x80002000U);
    emitSample(bits);
    setPackedValue(&bits, 417, 2, 0U);
    emitSample(bits);

    setPackedValue(&bits, 516, 1, 1U);
    setPackedValue(&bits, 520, 8, 0x55U);
    setPackedValue(&bits, 518, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 518, 1, 0U);
    emitSample(bits);

    setPackedValue(&bits, 547, 1, 0U);
    emitSample(bits);
    const quint8 spiTx = 0xA5U;
    const quint8 spiRx = 0x3CU;
    for (int bit = 7; bit >= 0; --bit) {
        setPackedValue(&bits, 544, 1, 0U);
        setPackedValue(&bits, 545, 1, (spiTx >> bit) & 1U);
        setPackedValue(&bits, 546, 1, (spiRx >> bit) & 1U);
        emitSample(bits);
        setPackedValue(&bits, 544, 1, 1U);
        emitSample(bits);
    }
    setPackedValue(&bits, 547, 1, 1U);
    emitSample(bits);

    setPackedValue(&bits, 578, 1, 1U);
    setPackedValue(&bits, 584, 11, 0x123U);
    emitSample(bits);
    setPackedValue(&bits, 578, 1, 0U);
    setPackedValue(&bits, 579, 1, 1U);
    setPackedValue(&bits, 595, 4, 0xAU);
    emitSample(bits);
    setPackedValue(&bits, 579, 1, 0U);

    setPackedValue(&bits, 609, 1, 0U);
    emitSample(bits);
    const QList<quint8> i2cBytes = {quint8(0xA0U), quint8(0x5AU)};
    for (int byteIndex = 0; byteIndex < i2cBytes.size(); ++byteIndex) {
        const quint8 byte = i2cBytes.at(byteIndex);
        for (int bit = 7; bit >= 0; --bit) {
            setPackedValue(&bits, 608, 1, 0U);
            setPackedValue(&bits, 609, 1, (byte >> bit) & 1U);
            emitSample(bits);
            setPackedValue(&bits, 608, 1, 1U);
            emitSample(bits);
        }
        setPackedValue(&bits, 608, 1, 0U);
        setPackedValue(&bits, 609, 1, 0U);
        emitSample(bits);
        setPackedValue(&bits, 608, 1, 1U);
        emitSample(bits);
        if (byteIndex == 0) {
            setPackedValue(&bits, 608, 1, 0U);
            setPackedValue(&bits, 609, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 608, 1, 1U);
            emitSample(bits);
            setPackedValue(&bits, 609, 1, 0U);
            emitSample(bits);
        }
    }
    setPackedValue(&bits, 608, 1, 0U);
    setPackedValue(&bits, 609, 1, 0U);
    emitSample(bits);
    setPackedValue(&bits, 608, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 609, 1, 1U);
    emitSample(bits);

    setPackedValue(&bits, 646, 1, 1U);
    setPackedValue(&bits, 688, 16, 0x0800U);
    emitSample(bits);
    setPackedValue(&bits, 646, 1, 0U);
    setPackedValue(&bits, 647, 1, 1U);
    emitSample(bits);
    setPackedValue(&bits, 647, 1, 0U);

    setPackedValue(&bits, 737, 1, 1U);
    setPackedValue(&bits, 738, 1, 1U);
    setPackedValue(&bits, 736, 1, 1U);
    emitSample(bits);

    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024,\"trace_profile_id\":"
        "\"trace.noelv.lockstep_1024\"}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool writeSparseEventFixture(const QString& taskRootPath)
{
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidencePath)) return false;
    const QString sidecar = QStringLiteral(
        "{\"schema\":\"lockstep-capture-sidecar-v3\",\"sample_rate_hz\":1000000000,"
        "\"window_start_index\":1000,\"protocol_events\":\"evidence/protocol_events.json\"}\n");
    const QString events = QStringLiteral(
        "{\"schema\":\"lockstep-protocol-events-v3\",\"capture_id\":42,"
        "\"timebase_hz\":1000000000,\"implemented_source_mask\":415,"
        "\"enabled_source_mask\":387,\"design_gap_mask\":96,\"events\":[{"
        "\"timestamp_ticks\":\"1250\",\"capture_id\":42,\"local_sequence\":7,"
        "\"protocol_id\":1,\"event_type\":1,\"source_kind\":0,\"flags\":0,"
        "\"event_reason_mask\":2,\"payload_hex\":\"41\"}]}\n");
    return writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")), sidecar) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("protocol_events.json")), events);
}

bool writeRawUartFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    const QString evidencePath = QDir(taskRootPath).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(waveformPath) || !QDir().mkpath(evidencePath)) return false;
    QString bits(1024, QLatin1Char('0'));
    setPackedValue(&bits, 512, 1, 1U);
    setPackedValue(&bits, 513, 1, 1U);
    setPackedValue(&bits, 768, 1, 1U);
    QString vcd = QStringLiteral(
        "$timescale 1 ps $end\n$scope module logic $end\n"
        "$var wire 1024 ! lockstep_trace_sample [1023:0] $end\n"
        "$upscope $end\n$enddefinitions $end\n");
    constexpr int bitSamples = 9;
    constexpr int startSample = 2;
    constexpr quint8 value = 0x55U;
    for (int sample = 0; sample < 110; ++sample) {
        bool tx = true;
        if (sample >= startSample && sample < startSample + bitSamples) {
            tx = false;
        } else if (sample >= startSample + bitSamples &&
                   sample < startSample + bitSamples * 9) {
            const int dataBit = (sample - startSample - bitSamples) / bitSamples;
            tx = ((value >> dataBit) & 1U) != 0U;
        }
        setPackedValue(&bits, 512, 1, tx ? 1U : 0U);
        vcd += QStringLiteral("#%1\nb%2 !\n").arg(sample * 1'000'000).arg(bits);
    }
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    const QString sidecar = QStringLiteral(
        "{\"schema\":\"lockstep-capture-sidecar-v2\","
        "\"sample_rate_hz\":1000000}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema) &&
        writeTextFile(QDir(evidencePath).filePath(QStringLiteral("capture_sidecar.json")), sidecar);
}

bool writeTrustedScalarOrderFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    if (!QDir().mkpath(waveformPath)) return false;
    const QHash<int, QString> anchors = {
        {0, QStringLiteral("sample_abs_index[0]")},
        {32, QStringLiteral("ahb_haddr[0]")},
        {417, QStringLiteral("ahb_htrans[0]")},
        {512, QStringLiteral("UART_TX")},
        {544, QStringLiteral("SPI_SCLK")},
        {608, QStringLiteral("I2C_SCL")},
        {736, QStringLiteral("JTAG_TCK")},
        {1023, QStringLiteral("NOELV_1024_RESERVED[239]")}
    };
    QString vcd = QStringLiteral(
        "$version\n  host export\n$end\n$timescale 1 ns $end\n$scope module logic $end\n");
    for (int channel = 0; channel < 1024; ++channel) {
        const QString reference = anchors.value(
            channel, QStringLiteral("product_semantic_%1").arg(channel));
        vcd += QStringLiteral("$var wire 1 s%1 %2 $end\n").arg(channel).arg(reference);
    }
    vcd += QStringLiteral("$upscope $end\n$enddefinitions $end\n#0\n");
    for (int channel = 0; channel < 1024; ++channel) {
        vcd += QStringLiteral("0s%1\n").arg(channel);
    }
    vcd += QStringLiteral("#10\n1s506\n");
    const QString schema = QStringLiteral(
        "{\"schema_version\":\"2.0\",\"sample_width\":1024,"
        "\"physical_channels\":1024}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture.vcd")), vcd) &&
        writeTextFile(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json")), schema);
}

bool expect(bool condition, const QString& message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTemporaryDir tempDir(QDir::tempPath() + QStringLiteral("/lockstep_trace_test_XXXXXX"));
    if (!expect(tempDir.isValid(), QStringLiteral("temporary directory is valid"))) {
        return 1;
    }

    const QString taskRootPath = tempDir.path();

    lockstep::waveform_viewer::WaveformTraceViewer viewer;
    const lockstep::waveform_viewer::WaveformViewModel emptyModel = viewer.loadTask(taskRootPath);
    if (!expect(emptyModel.groups.size() == 9, QStringLiteral("M11 exposes 9 protocol bundles without VCD"))) {
        return 1;
    }
    for (const lockstep::waveform_viewer::WaveformGroupView& group : emptyModel.groups) {
        if (!expect(!group.fields.isEmpty(), QStringLiteral("every default protocol bundle has collapsed member fields"))) {
            return 1;
        }
    }

    if (!expect(writeWideProtocolFixture(taskRootPath) && writeSparseEventFixture(taskRootPath),
                QStringLiteral("1024-bit trace and sparse event fixtures can be written"))) {
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel missingAnalysisModel = viewer.loadTask(taskRootPath);
    if (!expect(missingAnalysisModel.hasVcd && !missingAnalysisModel.hasAnalysis, QStringLiteral("M11 detects VCD without analysis"))) {
        return 1;
    }
    if (!expect(missingAnalysisModel.status == QStringLiteral("analysis_missing"), QStringLiteral("M11 marks missing analysis"))) {
        return 1;
    }

    lockstep::error_handling::ErrorRegistry registry;
    lockstep::protocol_analyzer::ProtocolAnalyzer analyzer;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = taskRootPath;
    request.taskId = QStringLiteral("trace_test");
    request.errorRegistry = &registry;
    request.reportDiagnosticsToErrorRegistry = true;

    const lockstep::protocol_analyzer::ProtocolAnalysisResult result = analyzer.analyzeTask(request);
    if (!expect(result.success, QStringLiteral("analysis succeeds"))) {
        QTextStream(stderr) << result.errorMessage << '\n';
        return 1;
    }
    if (!expect(QFileInfo::exists(result.analysisPath), QStringLiteral("analysis file exists"))) {
        return 1;
    }

    const QJsonArray groups = result.analysis.value(QStringLiteral("groups")).toArray();
    if (!expect(groups.size() == 9, QStringLiteral("analysis has 9 protocol groups"))) {
        return 1;
    }
    bool sparseUartSeen = false;
    for (const QJsonValue& value : result.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        sparseUartSeen = sparseUartSeen ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("uart") &&
             event.value(QStringLiteral("type")).toString() == QStringLiteral("raw_line_event") &&
             event.value(QStringLiteral("start_time")).toInteger() == 250);
    }
    if (!expect(sparseUartSeen, QStringLiteral("sparse raw UART event joins the VCD timeline"))) {
        return 1;
    }
    for (const QJsonValue& value : groups) {
        const QJsonObject group = value.toObject();
        const QString id = group.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("eth") || id == QStringLiteral("usb")) {
            if (!expect(group.value(QStringLiteral("status")).toString() == QStringLiteral("unavailable") &&
                            group.value(QStringLiteral("reason")).toString().contains(
                                QStringLiteral("unavailable/design_gap")),
                        QStringLiteral("event design_gap mask forces ETH/USB unavailable"))) {
                return 1;
            }
        }
    }

    lockstep::error_handling::ErrorEvent recoveryEvent;
    recoveryEvent.code = QStringLiteral("CAPTURE_RECOVERY_FAILED");
    recoveryEvent.source = QStringLiteral("Sampling");
    recoveryEvent.message = QStringLiteral("capture recovery failed");
    lockstep::error_handling::ErrorRecord recoveryRecord;
    QString error;
    if (!expect(registry.appendTaskError(taskRootPath, recoveryEvent, &recoveryRecord, &error),
                QStringLiteral("diagnostic can be appended to M14"))) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    if (!expect(recoveryRecord.context.value(QStringLiteral("suggestion")).toString()
                    .contains(QStringLiteral("PL 复位")),
                QStringLiteral("machine-readable diagnostic suggestion reaches ErrorRegistry"))) {
        return 1;
    }
    QList<lockstep::error_handling::ErrorRecord> records;
    if (!expect(registry.loadTaskErrors(taskRootPath, &records, &error) && !records.isEmpty(),
                QStringLiteral("M14 task errors can be loaded"))) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel model = viewer.loadTask(taskRootPath);
    if (!expect(model.hasVcd && model.hasAnalysis, QStringLiteral("M11 sees VCD and analysis"))) {
        return 1;
    }
    if (!expect(model.groups.size() == 9, QStringLiteral("M11 exposes 9 display groups"))) {
        return 1;
    }
    if (!expect(!model.samples.isEmpty(), QStringLiteral("M11 exposes real 1024-bit VCD samples"))) {
        return 1;
    }
    if (!expect(model.timeRangeText.startsWith(QStringLiteral("0 .. ")),
                QStringLiteral("VCD timescale is applied to the shared axis"))) {
        return 1;
    }
    const lockstep::waveform_viewer::WaveformGroupView& ahbGroup = model.groups.first();
    if (!expect(!ahbGroup.fields.isEmpty() && ahbGroup.fields.first().width == 32,
                QStringLiteral("same-name AHB address bits are merged into one hexadecimal bus"))) {
        return 1;
    }
    if (!expect(!ahbGroup.transactions.isEmpty() &&
                    ahbGroup.transactions.first().contains(QStringLiteral("AHB WRITE")) &&
                    ahbGroup.transactions.first().contains(QStringLiteral("0x80001000")),
                QStringLiteral("AHB activity is decoded into a hexadecimal event on the shared timeline"))) {
        return 1;
    }
    if (!expect(!model.keyBehaviors.isEmpty(), QStringLiteral("M11 exposes key behaviors"))) {
        return 1;
    }
    if (!expect(model.keyBehaviors.join(QLatin1Char('\n')).contains(QStringLiteral("AHB WRITE")),
                QStringLiteral("protocol page receives group transactions, not only mismatch events"))) {
        return 1;
    }

    const QJsonArray protocolEvents =
        result.analysis.value(QStringLiteral("protocol_events")).toArray();
    QSet<QString> decodedGroups;
    QStringList eventSummaries;
    int ahbEventCount = 0;
    int i2cEventCount = 0;
    bool hasI2cAckEvent = false;
    bool hasSpiModeField = false;
    for (const QJsonValue& value : protocolEvents) {
        const QJsonObject event = value.toObject();
        const QString groupId = event.value(QStringLiteral("group_id")).toString();
        decodedGroups.insert(groupId);
        eventSummaries.append(event.value(QStringLiteral("summary")).toString());
        if (groupId == QStringLiteral("ahb")) ++ahbEventCount;
        if (groupId == QStringLiteral("i2c")) ++i2cEventCount;
        hasI2cAckEvent = hasI2cAckEvent || event.value(QStringLiteral("type")).toString() == QStringLiteral("i2c_ack");
        hasSpiModeField = hasSpiModeField ||
            (groupId == QStringLiteral("spi") && event.value(QStringLiteral("fields")).toObject().contains(QStringLiteral("mode")));
    }
    for (const QString& required : {QStringLiteral("ahb"), QStringLiteral("uart"),
                                    QStringLiteral("spi"), QStringLiteral("can"),
                                    QStringLiteral("i2c"), QStringLiteral("eth"),
                                    QStringLiteral("jtag")}) {
        if (!expect(decodedGroups.contains(required),
                    QStringLiteral("fixture decodes %1 events").arg(required))) return 1;
    }
    if (!expect(!decodedGroups.contains(QStringLiteral("usb")),
                QStringLiteral("USB design gap never produces synthetic transactions"))) return 1;
    if (!expect(ahbEventCount == 2 &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("0x80001000")) &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("0x80002000")),
                QStringLiteral("AHB back-to-back address and data phases remain paired")) ||
                !expect(i2cEventCount >= 2 &&
                    eventSummaries.join(QLatin1Char('\n')).contains(QStringLiteral("REPEATED_START")),
                QStringLiteral("I2C repeated START preserves the preceding segment"))) return 1;
    if (!expect(hasI2cAckEvent, QStringLiteral("I2C ACK/NACK is emitted as a first-class event")) ||
        !expect(hasSpiModeField, QStringLiteral("SPI transfer carries mode metadata"))) return 1;
    const QJsonArray keyBehaviors = result.analysis.value(QStringLiteral("key_behaviors")).toArray();
    if (!expect(keyBehaviors.size() >= protocolEvents.size(),
                QStringLiteral("key_behaviors includes protocol events, not only mismatch")) ||
        !expect(!keyBehaviors.isEmpty() &&
                    keyBehaviors.first().toObject().value(QStringLiteral("start_time")).toInteger() <=
                    keyBehaviors.last().toObject().value(QStringLiteral("start_time")).toInteger(),
                QStringLiteral("key_behaviors are ordered by capture time"))) return 1;

    QTemporaryDir rawUartTask(QDir::tempPath() + QStringLiteral("/lockstep_raw_uart_XXXXXX"));
    if (!expect(rawUartTask.isValid() && writeRawUartFixture(rawUartTask.path()),
                QStringLiteral("raw UART fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest rawUartRequest;
    rawUartRequest.taskRootPath = rawUartTask.path();
    rawUartRequest.taskId = QStringLiteral("raw_uart_test");
    rawUartRequest.reportDiagnosticsToErrorRegistry = false;
    const auto rawUartResult = analyzer.analyzeTask(rawUartRequest);
    bool rawUartDecoded = false;
    for (const QJsonValue& value : rawUartResult.analysis.value(QStringLiteral("protocol_events")).toArray()) {
        const QJsonObject event = value.toObject();
        const QJsonObject fields = event.value(QStringLiteral("fields")).toObject();
        rawUartDecoded = rawUartDecoded ||
            (event.value(QStringLiteral("group_id")).toString() == QStringLiteral("uart") &&
             fields.value(QStringLiteral("data")).toString() == QStringLiteral("0x55") &&
             !fields.value(QStringLiteral("frame_error")).toBool());
    }
    if (!expect(rawUartResult.success && rawUartDecoded,
                QStringLiteral("raw 115200 8N1 UART is decoded from hardware TX samples"))) return 1;
    const auto rawUartModel = viewer.loadTask(rawUartTask.path());
    if (!expect(rawUartModel.timeRangeText == QStringLiteral("0 .. 109000 ns"),
                QStringLiteral("picosecond UI axis truncates the final three digits to ns")) ||
        !expect(!rawUartModel.samples.isEmpty() && rawUartModel.samples.last().time == 109000,
                QStringLiteral("sample and event coordinates use the same ns scale"))) return 1;

    QTemporaryDir invalidEventTask(
        QDir::tempPath() + QStringLiteral("/lockstep_invalid_event_XXXXXX"));
    const QString invalidEvidence = QDir(invalidEventTask.path()).filePath(QStringLiteral("evidence"));
    if (!expect(invalidEventTask.isValid() && writeWideProtocolFixture(invalidEventTask.path()) &&
                    QDir().mkpath(invalidEvidence) &&
                    writeTextFile(QDir(invalidEvidence).filePath(QStringLiteral("capture_sidecar.json")),
                                  QStringLiteral("{\"sample_rate_hz\":1000000000,"
                                                 "\"protocol_events\":\"evidence/missing.json\"}")),
                QStringLiteral("invalid sparse event fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest invalidEventRequest;
    invalidEventRequest.taskRootPath = invalidEventTask.path();
    invalidEventRequest.taskId = QStringLiteral("invalid_event_test");
    invalidEventRequest.reportDiagnosticsToErrorRegistry = false;
    const auto invalidEventResult = analyzer.analyzeTask(invalidEventRequest);
    if (!expect(!invalidEventResult.success && invalidEventResult.wroteAnalysis &&
                    invalidEventResult.status == QStringLiteral("failed") &&
                    invalidEventResult.analysis.value(QStringLiteral("status")).toString() ==
                        QStringLiteral("failed"),
                QStringLiteral("missing sparse event evidence fails protocol analysis"))) return 1;

    QTemporaryDir trustedOrderTask(
        QDir::tempPath() + QStringLiteral("/lockstep_trusted_scalar_XXXXXX"));
    if (!expect(trustedOrderTask.isValid() &&
                    writeTrustedScalarOrderFixture(trustedOrderTask.path()),
                QStringLiteral("trusted product scalar-order fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest trustedOrderRequest;
    trustedOrderRequest.taskRootPath = trustedOrderTask.path();
    trustedOrderRequest.taskId = QStringLiteral("trusted_scalar_order_test");
    trustedOrderRequest.reportDiagnosticsToErrorRegistry = false;
    const auto trustedOrderResult = analyzer.analyzeTask(trustedOrderRequest);
    if (!expect(trustedOrderResult.success &&
                    trustedOrderResult.analysis.value(QStringLiteral("key_behaviors"))
                        .toArray().size() == 1,
                QStringLiteral("verified product generator anchors allow declaration-order mapping"))) {
        return 1;
    }

    QTextStream(stdout) << "PASS lockstep_trace_analysis_test\n";
    return 0;
}
