/**********************************************************
* 文件名: lockstep_trace_analysis_test.cpp
* 日期: 2026-07-17
* 版本: v2.0
* 更新记录: 删除 512-bit 历史 fixture，仅保留 1024-bit 产品合同。
* 描述: 验证九协议事务、mismatch 和共享时间轴进入展示模型。
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

    if (!expect(writeWideProtocolFixture(taskRootPath), QStringLiteral("1024-bit trace fixture can be written"))) {
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
    for (const QJsonValue& value : protocolEvents) {
        const QJsonObject event = value.toObject();
        const QString groupId = event.value(QStringLiteral("group_id")).toString();
        decodedGroups.insert(groupId);
        eventSummaries.append(event.value(QStringLiteral("summary")).toString());
        if (groupId == QStringLiteral("ahb")) ++ahbEventCount;
        if (groupId == QStringLiteral("i2c")) ++i2cEventCount;
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
