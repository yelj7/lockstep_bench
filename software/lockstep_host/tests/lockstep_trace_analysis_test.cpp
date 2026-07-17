/**********************************************************
* 文件名: lockstep_trace_analysis_test.cpp
* 日期: 2026-07-08
* 版本: v1.1
* 更新记录: 增加协议组事务汇总到协议页的回归测试
* 描述: 验证协议事务、mismatch 和共享时间轴能完整进入展示模型
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

QString packedSampleWithMask(const quint8 mismatchMask)
{
    QString bits(512, QLatin1Char('0'));
    for (int bit = 0; bit <= 4; ++bit) {
        if ((mismatchMask & static_cast<quint8>(1U << bit)) != 0U) {
            bits[511 - (502 + bit)] = QLatin1Char('1');
        }
    }
    return bits;
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

QString packedAhbSample(const quint8 mismatchMask, const bool ready)
{
    QString bits = packedSampleWithMask(mismatchMask);
    setPackedValue(&bits, 0, 32, 0x80001000U);
    setPackedValue(&bits, 32, 2, 2U);
    setPackedValue(&bits, 34, 1, 1U);
    setPackedValue(&bits, 41, 1, ready ? 1U : 0U);
    setPackedValue(&bits, 44, 32, 0x1234ABCDU);
    setPackedValue(&bits, 108, 1, 1U);
    setPackedValue(&bits, 111, 1, 1U);
    return bits;
}

bool writeTraceFixture(const QString& taskRootPath)
{
    const QString waveformPath = QDir(taskRootPath).filePath(QStringLiteral("waveform"));
    QDir dir;
    if (!dir.mkpath(waveformPath)) {
        return false;
    }

    const QString vcd = QStringLiteral(
        "$date\n"
        "  test\n"
        "$end\n"
        "$version\n"
        "  lockstep test\n"
        "$end\n"
        "$timescale\n"
        "  10 ns\n"
        "$end\n"
        "$scope module logic $end\n"
        "$var wire 512 ! lockstep_trace_sample [511:0] $end\n"
        "$upscope $end\n"
        "$enddefinitions $end\n"
        "#0\n"
        "b%1 !\n"
        "#10\n"
        "b%2 !\n"
        "#20\n"
        "b%3 !\n"
        "#30\n"
        "b%4 !\n"
        "#40\n"
        "b%5 !\n"
        "#55\n"
        "b%6 !\n")
        .arg(packedSampleWithMask(0U),
             packedAhbSample(static_cast<quint8>((1U << 4) | (1U << 2)), false),
             packedAhbSample(static_cast<quint8>((1U << 4) | (1U << 2) | (1U << 0)), true),
             packedSampleWithMask(0U),
             packedSampleWithMask(static_cast<quint8>(1U << 0)),
             packedSampleWithMask(0U));

    if (!writeTextFile(QDir(waveformPath).filePath(QStringLiteral("lockstep_trace.vcd")), vcd)) {
        return false;
    }

    const QString schema = QStringLiteral(
        "{\n"
        "  \"schema_version\": \"1.0\",\n"
        "  \"task_id\": \"trace_test\",\n"
        "  \"sample_signal\": \"lockstep_trace_sample\",\n"
        "  \"sample_width\": 512,\n"
        "  \"timescale\": \"1 ns\",\n"
        "  \"trace_profile_id\": \"trace.noelv.lockstep_512\"\n"
        "}\n");
    return writeTextFile(QDir(waveformPath).filePath(QStringLiteral("lockstep_trace_schema.json")), schema);
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

    if (!expect(writeTraceFixture(taskRootPath), QStringLiteral("trace fixture can be written"))) {
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

    const QJsonArray behaviors = result.analysis.value(QStringLiteral("key_behaviors")).toArray();
    if (!expect(behaviors.size() == 2, QStringLiteral("analysis has two mismatch behavior windows"))) {
        return 1;
    }
    const QJsonObject firstEvent = behaviors.at(0).toObject();
    if (!expect(firstEvent.value(QStringLiteral("type")).toString() == QStringLiteral("mismatch_event"), QStringLiteral("first event is mismatch_event"))) {
        return 1;
    }
    const QJsonArray firstItems = firstEvent.value(QStringLiteral("items")).toArray();
    if (!expect(firstItems.size() == 3, QStringLiteral("first mismatch event keeps all simultaneous items"))) {
        return 1;
    }

    QList<lockstep::error_handling::ErrorRecord> records;
    QString error;
    if (!expect(registry.loadTaskErrors(taskRootPath, &records, &error), QStringLiteral("M14 task errors can be loaded"))) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    if (!expect(!records.isEmpty(), QStringLiteral("diagnostics are reported to M14"))) {
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel model = viewer.loadTask(taskRootPath);
    if (!expect(model.hasVcd && model.hasAnalysis, QStringLiteral("M11 sees VCD and analysis"))) {
        return 1;
    }
    if (!expect(model.groups.size() == 9, QStringLiteral("M11 exposes 9 display groups"))) {
        return 1;
    }
    if (!expect(model.samples.size() == 6, QStringLiteral("M11 exposes real packed VCD samples"))) {
        return 1;
    }
    if (!expect(model.timeRangeText == QStringLiteral("0 .. 550 ns"),
                QStringLiteral("multi-line VCD timescale multiplier is applied to the shared axis"))) {
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

    QTemporaryDir wideTask(QDir::tempPath() + QStringLiteral("/lockstep_protocol_1024_XXXXXX"));
    if (!expect(wideTask.isValid() && writeWideProtocolFixture(wideTask.path()),
                QStringLiteral("1024-bit multi-protocol fixture is writable"))) return 1;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest wideRequest;
    wideRequest.taskRootPath = wideTask.path();
    wideRequest.taskId = QStringLiteral("protocol_1024_test");
    wideRequest.reportDiagnosticsToErrorRegistry = false;
    const auto wideResult = analyzer.analyzeTask(wideRequest);
    if (!expect(wideResult.success, QStringLiteral("1024-bit multi-protocol analysis succeeds"))) {
        QTextStream(stderr) << wideResult.errorMessage << '\n';
        return 1;
    }
    const QJsonArray protocolEvents =
        wideResult.analysis.value(QStringLiteral("protocol_events")).toArray();
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
