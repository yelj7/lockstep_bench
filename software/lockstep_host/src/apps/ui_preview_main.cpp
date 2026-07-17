/**********************************************************
* 文件名: ui_preview_main.cpp
* 日期: 2026-07-13
* 版本: v2.1
* 更新记录: 增加唯一 EXE 的纯 C++ FT601 实时采集模式
* 描述: 按参数启动界面、调试服务、离线回放或实时采集模式。
**********************************************************/

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "debug_service_entry.h"
#include "protocol_analysis.h"
#include "report_generator.h"
#include "sampling_capture.h"
#include "main_window_shell.h"
#include "splash_widget.h"
#include "ui_contract.h"
#include "ui_types.h"
#include "workbench_controller.h"
#include "workspace_selection_dialog.h"

namespace {

constexpr int kSplashDurationMs = 1000;

void writeLiveCaptureFailure(
    const QString& taskRoot,
    const QString& phase,
    const QString& message,
    const int exitCode)
{
    const QString evidenceDir = QDir(taskRoot).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidenceDir)) return;

    QJsonObject object;
    object.insert(QStringLiteral("schema"), QStringLiteral("lockstep-live-capture-error-v1"));
    object.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("phase"), phase);
    object.insert(QStringLiteral("message"), message);
    object.insert(QStringLiteral("exit_code"), exitCode);

    QSaveFile output(QDir(evidenceDir).filePath(QStringLiteral("capture_error.json")));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    output.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    output.commit();
}

QString argumentValue(const QStringList& arguments, const QString& name)
{
    const int index = arguments.indexOf(name);
    if (index >= 0 && index + 1 < arguments.size()) return arguments.at(index + 1);
    const QString prefix = name + QLatin1Char('=');
    for (const QString& argument : arguments) {
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
    }
    return QString();
}

quint32 unsignedArgument(const QStringList& arguments, const QString& name, const quint32 fallback,
                         bool* const valid)
{
    const QString text = argumentValue(arguments, name);
    if (text.isEmpty()) {
        if (valid != nullptr) *valid = true;
        return fallback;
    }
    bool ok = false;
    const quint32 value = text.toUInt(&ok, 0);
    if (valid != nullptr) *valid = ok;
    return ok ? value : fallback;
}

int finalizeCaptureTask(const QString& taskRoot, const QString& taskId)
{
    const QString evidenceDir = QDir(taskRoot).filePath(QStringLiteral("evidence"));
    QDir().mkpath(evidenceDir);
    QDir().mkpath(QDir(taskRoot).filePath(QStringLiteral("waveform")));
    QJsonObject schema;
    schema.insert(QStringLiteral("schema_version"), QStringLiteral("2.0"));
    schema.insert(QStringLiteral("task_id"), taskId);
    schema.insert(QStringLiteral("sample_signal"), QStringLiteral("CH0..CH1023"));
    schema.insert(QStringLiteral("sample_width"), 1024);
    schema.insert(QStringLiteral("physical_channels"), 1024);
    schema.insert(QStringLiteral("trace_profile_id"), QStringLiteral("trace.noelv.lockstep_1024"));
    QSaveFile schemaOutput(QDir(taskRoot).filePath(QStringLiteral("waveform/capture_schema.json")));
    const QByteArray schemaBytes = QJsonDocument(schema).toJson(QJsonDocument::Indented);
    if (!schemaOutput.open(QIODevice::WriteOnly) || schemaOutput.write(schemaBytes) != schemaBytes.size() ||
        !schemaOutput.commit()) return 9;

    lockstep::protocol_analyzer::ProtocolAnalyzer analyzer;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = taskRoot;
    request.taskId = taskId;
    request.reportDiagnosticsToErrorRegistry = false;
    if (!analyzer.analyzeTask(request).success) return 10;

    QJsonArray artifacts;
    const auto artifact = [](const QString& name, const QString& path) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("relative_path"), path);
        return object;
    };
    artifacts.append(artifact(QStringLiteral("acquisition_raw"), QStringLiteral("evidence/raw_capture.dat")));
    artifacts.append(artifact(QStringLiteral("capture_sidecar"), QStringLiteral("evidence/capture_sidecar.json")));
    artifacts.append(artifact(QStringLiteral("capture_vcd"), QStringLiteral("waveform/capture.vcd")));
    artifacts.append(artifact(QStringLiteral("protocol_analysis"), QStringLiteral("evidence/protocol_analysis.json")));
    QJsonObject artifactRoot;
    artifactRoot.insert(QStringLiteral("schema"), QStringLiteral("lockstep-artifacts-v1"));
    artifactRoot.insert(QStringLiteral("artifacts"), artifacts);
    QSaveFile artifactOutput(QDir(evidenceDir).filePath(QStringLiteral("artifacts.json")));
    const QByteArray artifactBytes = QJsonDocument(artifactRoot).toJson(QJsonDocument::Indented);
    if (!artifactOutput.open(QIODevice::WriteOnly) || artifactOutput.write(artifactBytes) != artifactBytes.size() ||
        !artifactOutput.commit()) return 11;

    lockstep::reporting::ReportDocumentModel reportContext;
    reportContext.taskId = taskId;
    reportContext.taskName = taskId.isEmpty() ? QStringLiteral("sampling_capture") : taskId;
    reportContext.mode = QStringLiteral("test");
    reportContext.targetSummary = QStringLiteral("NOEL-V ZCU102 1024-bit FT601 C++ capture");
    lockstep::reporting::ReportGenerator reportGenerator;
    const lockstep::reporting::ReportDocumentModel reportModel =
        reportGenerator.buildModelFromTask(taskRoot, reportContext);
    return reportGenerator.generateReport(taskRoot, reportModel).success ? 0 : 13;
}

int runOfflineCapture(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const QString inputPath = argumentValue(arguments, QStringLiteral("--offline-capture"));
    const QString taskRoot = argumentValue(arguments, QStringLiteral("--task-root"));
    const QString taskId = argumentValue(arguments, QStringLiteral("--task-id"));
    if (inputPath.isEmpty() || taskRoot.isEmpty()) return 2;
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) return 3;
    const QByteArray raw = input.readAll();
    lockstep::acquisition::CaptureFrameCodec codec;
    const lockstep::acquisition::CaptureDecodeResult decoded = codec.feed(raw);
    if (!decoded.success) return 4;
    lockstep::acquisition::SamplingCaptureAssembler assembler;
    QString error;
    bool captureStarted = false;
    for (const lockstep::acquisition::CaptureFrame& frame : decoded.frames) {
        if (frame.header.type == lockstep::acquisition::CaptureFrameType::CaptureMeta) captureStarted = true;
        if (assembler.complete()) {
            if (frame.header.type == lockstep::acquisition::CaptureFrameType::ErrorResponse) return 12;
            break;
        }
        if (captureStarted && !assembler.append(frame, &error)) return 5;
    }
    if (!assembler.complete()) return 6;
    const QString evidenceDir = QDir(taskRoot).filePath(QStringLiteral("evidence"));
    QDir().mkpath(evidenceDir);
    QSaveFile rawOutput(QDir(evidenceDir).filePath(QStringLiteral("raw_capture.dat")));
    if (!rawOutput.open(QIODevice::WriteOnly) || rawOutput.write(raw) != raw.size() || !rawOutput.commit()) return 7;
    QString vcdPath;
    QString sidecarPath;
    if (!lockstep::acquisition::exportScalarVcd(assembler.record(), taskRoot, &vcdPath, &sidecarPath, &error)) return 8;

    return finalizeCaptureTask(taskRoot, taskId);
}

int runLiveCapture(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const QString taskRoot = argumentValue(arguments, QStringLiteral("--task-root"));
    const QString taskId = argumentValue(arguments, QStringLiteral("--task-id"));
    if (taskRoot.isEmpty()) return 20;
    const QString libraryPath = argumentValue(arguments, QStringLiteral("--ftd3xx"));
    if (!libraryPath.isEmpty()) qputenv("LOCKSTEP_FTD3XX_LIBRARY", libraryPath.toLocal8Bit());

    bool argumentsValid = true;
    lockstep::acquisition::SamplingCaptureConfig config;
    config.sampleRateHz = unsignedArgument(arguments, QStringLiteral("--sample-rate"), config.sampleRateHz,
                                           &argumentsValid);
    if (!argumentsValid) return 21;
    config.sampleCount = unsignedArgument(arguments, QStringLiteral("--sample-count"), config.sampleCount,
                                          &argumentsValid);
    if (!argumentsValid) return 21;
    config.pretriggerCount = unsignedArgument(arguments, QStringLiteral("--pretrigger"),
                                              config.pretriggerCount, &argumentsValid);
    if (!argumentsValid || config.pretriggerCount > config.sampleCount) return 21;
    config.posttriggerCount = config.sampleCount - config.pretriggerCount;
    config.triggerMask = unsignedArgument(arguments, QStringLiteral("--trigger-mask"), 0U, &argumentsValid);
    if (!argumentsValid) return 21;
    config.triggerValue = unsignedArgument(arguments, QStringLiteral("--trigger-value"), 0U, &argumentsValid);
    if (!argumentsValid) return 21;
    config.triggerEdgeRise = unsignedArgument(arguments, QStringLiteral("--trigger-edge-rise"), 0U,
                                              &argumentsValid);
    if (!argumentsValid) return 21;
    config.triggerEdgeFall = unsignedArgument(arguments, QStringLiteral("--trigger-edge-fall"), 0U,
                                              &argumentsValid);
    if (!argumentsValid) return 21;

    QString error;
    lockstep::acquisition::D3xxRuntime transport;
    if (!transport.load(&error)) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("d3xx_load"), error, 22);
        QTextStream(stderr) << QStringLiteral("FT601 D3XX 加载失败: ") << error << Qt::endl;
        return 22;
    }
    const QList<lockstep::acquisition::D3xxDeviceInfo> devices = transport.enumerate(&error);
    if (devices.isEmpty()) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("d3xx_enumerate"), error, 23);
        QTextStream(stderr) << QStringLiteral("FT601 枚举失败: ") << error << Qt::endl;
        return 23;
    }
    const quint32 deviceIndex = unsignedArgument(arguments, QStringLiteral("--device-index"),
                                                 devices.first().index, &argumentsValid);
    if (!argumentsValid || !transport.open(deviceIndex, &error)) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("d3xx_open"), error, 24);
        QTextStream(stderr) << QStringLiteral("FT601 打开失败: ") << error << Qt::endl;
        return 24;
    }
    lockstep::acquisition::SamplingCaptureRecord record;
    lockstep::acquisition::SamplingCaptureSession session;
    const bool captured = session.run(&transport, config, taskRoot, 120000, &record, &error);
    transport.close();
    if (!captured) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("capture_session"), error, 25);
        QTextStream(stderr) << QStringLiteral("FT601 实时采集失败: ") << error << Qt::endl;
        return 25;
    }
    return finalizeCaptureTask(taskRoot, taskId);
}

lockstep::ui::UiMode parseMode(const QStringList& arguments)
{
    lockstep::ui::UiMode mode = lockstep::ui::UiMode::Test;

    if (arguments.contains(QStringLiteral("--research"))) {
        mode = lockstep::ui::UiMode::Research;
    }

    return mode;
}

}  // namespace

int main(int argc, char* argv[])
{
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--offline-capture") || argument.startsWith(QStringLiteral("--offline-capture="))) {
            return runOfflineCapture(argc, argv);
        }
        if (argument == QStringLiteral("--live-capture")) {
            return runLiveCapture(argc, argv);
        }
    }
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--local-server") ||
            argument == QStringLiteral("--server") ||
            argument == QStringLiteral("--request") ||
            argument.startsWith(QStringLiteral("--request="))) {
            return runDebugServiceMode(argc, argv);
        }
    }

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lockstep_ui_preview"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(LOCKSTEP_APP_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("锁步研发测试系统"));
    application.setQuitOnLastWindowClosed(false);

    const lockstep::ui::UiMode mode = parseMode(application.arguments());
    lockstep::ui::MainWindowShell window;
    lockstep::ui::SplashWidget splash;
    lockstep::apps::WorkbenchController controller(&window, mode, &application);

    lockstep::ui::UiWorkbenchState state = lockstep::ui::makeDefaultWorkbenchState(mode);
    state.topStatus.taskStatusText = QStringLiteral("任务: 未选择");
    state.topStatus.targetStatusText = QStringLiteral("目标: 未连接");
    state.topStatus.programStatusText = QStringLiteral("程序: 未选择");
    window.setWorkbenchState(state);

    splash.resize(860, 500);
    splash.show();
    QTimer::singleShot(kSplashDurationMs, &application, [&application, &splash, &window, &controller]() {
        splash.close();
        QString workspaceRoot;
        if (!lockstep::ui::WorkspaceSelectionDialog::selectWorkspaceRoot(nullptr, &workspaceRoot)) {
            application.quit();
            return;
        }

        if (!controller.initialize(workspaceRoot)) {
            application.quit();
            return;
        }

        window.appendLog(
            lockstep::ui::LogChannel::Operation,
            lockstep::ui::LogLevel::Info,
            QStringLiteral("UI"),
            QStringLiteral("上位机主窗口已启动"));

        application.setQuitOnLastWindowClosed(true);
        window.show();
    });

    return application.exec();
}
