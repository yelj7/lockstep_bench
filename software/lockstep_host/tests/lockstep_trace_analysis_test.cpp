/**********************************************************
* 文件名: lockstep_trace_analysis_test.cpp
* 日期: 2026-07-08
* 版本: v1.0
* 更新记录: 初版创建固定 trace VCD 协议解析集成测试
* 描述: 验证 M12 能解析 512-bit VCD mismatch 复合事件，M11 能读取 analysis 展示模型
**********************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
        "$timescale 1 ns $end\n"
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
             packedSampleWithMask(static_cast<quint8>((1U << 4) | (1U << 2))),
             packedSampleWithMask(static_cast<quint8>((1U << 4) | (1U << 2) | (1U << 0))),
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
    if (!expect(writeTraceFixture(taskRootPath), QStringLiteral("trace fixture can be written"))) {
        return 1;
    }

    lockstep::waveform_viewer::WaveformTraceViewer viewer;
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
    if (!expect(!model.keyBehaviors.isEmpty(), QStringLiteral("M11 exposes key behaviors"))) {
        return 1;
    }

    QTextStream(stdout) << "PASS lockstep_trace_analysis_test\n";
    return 0;
}
