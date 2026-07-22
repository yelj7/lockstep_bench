/**********************************************************
* 文件名: ui_preview_main.cpp
* 日期: 2026-07-13
* 版本: v2.6
* 更新记录: 诊断 smoke 补齐事件流启动并使用生产默认 watchdog。
* 描述: 按参数启动界面、调试服务、离线回放或实时采集模式。
**********************************************************/

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSerialPort>
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

QString programDoneMarker(const QByteArray& output)
{
    const QByteArray normalized = output.toLower();
    if (normalized.contains(QByteArrayLiteral("program_run_done"))) {
        return QStringLiteral("PROGRAM_RUN_DONE");
    }
    if (normalized.contains(QByteArrayLiteral("lockstep_run_done"))) {
        return QStringLiteral("LOCKSTEP_RUN_DONE");
    }
    return QString();
}

bool saveSerialEvidence(const QString& taskRoot, const QString& portName, const quint32 baudRate,
                        const QByteArray& output, const QString& openError, QString* const error)
{
    const QString evidenceDir = QDir(taskRoot).filePath(QStringLiteral("evidence"));
    if (!QDir().mkpath(evidenceDir)) {
        if (error != nullptr) *error = QStringLiteral("无法创建 UART 证据目录");
        return false;
    }
    QSaveFile transcript(QDir(evidenceDir).filePath(QStringLiteral("uart_serial.txt")));
    if (!transcript.open(QIODevice::WriteOnly) || transcript.write(output) != output.size() ||
        !transcript.commit()) {
        if (error != nullptr) *error = QStringLiteral("无法写入 UART 串口文本");
        return false;
    }
    const QString marker = programDoneMarker(output);
    QJsonObject status;
    status.insert(QStringLiteral("schema"), QStringLiteral("lockstep-uart-serial-evidence-v1"));
    status.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    status.insert(QStringLiteral("port"), portName);
    status.insert(QStringLiteral("baud"), static_cast<qint64>(baudRate));
    status.insert(QStringLiteral("format"), QStringLiteral("8N1"));
    status.insert(QStringLiteral("bytes"), output.size());
    status.insert(QStringLiteral("opened"), openError.isEmpty());
    if (!openError.isEmpty()) status.insert(QStringLiteral("open_error"), openError);
    status.insert(QStringLiteral("program_done_marker"), marker);
    status.insert(QStringLiteral("program_done_marker_detected"), !marker.isEmpty());
    QSaveFile statusFile(QDir(evidenceDir).filePath(QStringLiteral("uart_serial_status.json")));
    const QByteArray statusBytes = QJsonDocument(status).toJson(QJsonDocument::Indented);
    if (!statusFile.open(QIODevice::WriteOnly) || statusFile.write(statusBytes) != statusBytes.size() ||
        !statusFile.commit()) {
        if (error != nullptr) *error = QStringLiteral("无法写入 UART 串口状态");
        return false;
    }
    return true;
}

bool protocolAnalysisHasProgramDoneMarker(const QString& taskRoot)
{
    QFile file(QDir(taskRoot).filePath(QStringLiteral("evidence/protocol_analysis.json")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    return parseError.error == QJsonParseError::NoError && document.isObject() &&
        document.object().value(QStringLiteral("program_done_marker_detected")).toBool();
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
    const auto appendIfPresent = [&artifacts, &artifact, &taskRoot](const QString& name, const QString& path) {
        if (QFileInfo::exists(QDir(taskRoot).filePath(path))) artifacts.append(artifact(name, path));
    };
    appendIfPresent(QStringLiteral("capture_status"), QStringLiteral("evidence/capture_status.json"));
    appendIfPresent(QStringLiteral("capture_error"), QStringLiteral("evidence/capture_error.json"));
    appendIfPresent(QStringLiteral("fault_injection"), QStringLiteral("evidence/fault_injection.json"));
    appendIfPresent(QStringLiteral("uart_serial"), QStringLiteral("evidence/uart_serial.txt"));
    appendIfPresent(QStringLiteral("uart_serial_status"), QStringLiteral("evidence/uart_serial_status.json"));
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
    const QString versionsDir = QDir(taskRoot).filePath(QStringLiteral("reports/versions"));
    reportContext.reportId = QStringLiteral("report");
    for (int suffix = 1; QFileInfo::exists(QDir(versionsDir).filePath(reportContext.reportId)); ++suffix) {
        reportContext.reportId = QStringLiteral("report-%1").arg(suffix);
    }
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
    bool argumentsValid = true;
    const quint32 eventEnableMask = unsignedArgument(
        arguments, QStringLiteral("--event-enable-mask"), 0x19fU, &argumentsValid);
    if (inputPath.isEmpty() || taskRoot.isEmpty() || !argumentsValid ||
        (eventEnableMask & ~0x19fU) != 0U) return 2;
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) return 3;
    const QByteArray raw = input.readAll();
    lockstep::acquisition::CaptureFrameCodec codec;
    const lockstep::acquisition::CaptureDecodeResult decoded = codec.feed(raw);
    if (!decoded.success) return 4;
    lockstep::acquisition::SamplingCaptureAssembler assembler(eventEnableMask);
    QString error;
    bool captureStarted = false;
    for (const lockstep::acquisition::CaptureFrame& frame : decoded.frames) {
        if (frame.header.type == lockstep::acquisition::CaptureFrameType::CaptureMeta ||
            frame.header.type == lockstep::acquisition::CaptureFrameType::EventMeta) {
            captureStarted = true;
        }
        if (assembler.complete()) {
            if (frame.header.type == lockstep::acquisition::CaptureFrameType::ErrorResponse) return 12;
            return 5;
        }
        if (captureStarted && !assembler.append(frame, &error)) return 5;
    }
    if (!assembler.complete()) return 6;
    const lockstep::acquisition::SamplingCaptureRecord record = assembler.record();
    if (!lockstep::acquisition::validateCaptureCompletion(record, &error)) return 13;
    const QString evidenceDir = QDir(taskRoot).filePath(QStringLiteral("evidence"));
    QDir().mkpath(evidenceDir);
    QSaveFile rawOutput(QDir(evidenceDir).filePath(QStringLiteral("raw_capture.dat")));
    if (!rawOutput.open(QIODevice::WriteOnly) || rawOutput.write(raw) != raw.size() || !rawOutput.commit()) return 7;
    QString vcdPath;
    QString sidecarPath;
    if (!lockstep::acquisition::exportScalarVcd(record, taskRoot, &vcdPath, &sidecarPath, &error)) return 8;

    return finalizeCaptureTask(taskRoot, taskId);
}

int runLiveCapture(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const QString taskRoot = argumentValue(arguments, QStringLiteral("--task-root"));
    const QString taskId = argumentValue(arguments, QStringLiteral("--task-id"));
    const QString runRequest = argumentValue(arguments, QStringLiteral("--run-request"));
    if (taskRoot.isEmpty()) return 20;
    if (!QDir().mkpath(taskRoot)) return 20;
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
    config.triggerTimeoutSamples = unsignedArgument(
        arguments, QStringLiteral("--trigger-timeout-samples"), config.triggerTimeoutSamples,
        &argumentsValid);
    if (!argumentsValid) return 21;
    config.eventLimit = unsignedArgument(
        arguments, QStringLiteral("--event-limit"), config.eventLimit, &argumentsValid);
    if (!argumentsValid) return 21;
    const QString serialPortName = argumentValue(arguments, QStringLiteral("--serial-port"));
    const quint32 serialBaud = unsignedArgument(
        arguments, QStringLiteral("--serial-baud"), 115'200U, &argumentsValid);
    if (!argumentsValid || serialBaud == 0U) return 21;
    const bool requireProgramDoneMarker =
        arguments.contains(QStringLiteral("--require-program-done-marker"));
    const bool requireFtUartMarker =
        arguments.contains(QStringLiteral("--require-ft-uart-marker"));

    QSerialPort serialPort;
    QString serialOpenError;
    if (!serialPortName.isEmpty()) {
        serialPort.setPortName(serialPortName);
        serialPort.setBaudRate(static_cast<qint32>(serialBaud));
        serialPort.setDataBits(QSerialPort::Data8);
        serialPort.setParity(QSerialPort::NoParity);
        serialPort.setStopBits(QSerialPort::OneStop);
        serialPort.setFlowControl(QSerialPort::NoFlowControl);
        if (!serialPort.open(QIODevice::ReadOnly)) {
            serialOpenError = serialPort.errorString();
            if (requireProgramDoneMarker) {
                writeLiveCaptureFailure(taskRoot, QStringLiteral("uart_open"), serialOpenError, 26);
                return 26;
            }
        } else {
            serialPort.clear(QSerialPort::Input);
        }
    } else if (requireProgramDoneMarker) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("uart_config"),
                                QStringLiteral("要求程序结束标志但未指定 --serial-port"), 26);
        return 26;
    }

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
    QProcess runRequestProcess;
    bool runRequestStarted = false;
    std::function<bool(quint32, QString*)> afterArm;
    if (!runRequest.isEmpty()) {
        afterArm = [&](const quint32 captureId, QString* callbackError) {
            runRequestProcess.setProgram(QCoreApplication::applicationFilePath());
            runRequestProcess.setArguments({QStringLiteral("--request"), runRequest});
            runRequestProcess.setWorkingDirectory(QDir::currentPath());
            runRequestProcess.setStandardOutputFile(
                QDir(taskRoot).filePath(QStringLiteral("run_request_stdout.txt")));
            runRequestProcess.setStandardErrorFile(
                QDir(taskRoot).filePath(QStringLiteral("run_request_stderr.txt")));
            runRequestProcess.start();
            if (!runRequestProcess.waitForStarted(5'000)) {
                if (callbackError != nullptr) {
                    *callbackError = QStringLiteral(
                        "ARM 后运行请求启动失败: capture_id=%1, error=%2")
                                         .arg(captureId).arg(runRequestProcess.errorString());
                }
                return false;
            }
            runRequestStarted = true;
            return true;
        };
    }
    const lockstep::acquisition::CaptureSessionResult captureResult =
        session.runDetailed(&transport, config, taskRoot, 120000, &record, afterArm);
    if (!captureResult.success && runRequestProcess.state() != QProcess::NotRunning) {
        runRequestProcess.terminate();
        if (!runRequestProcess.waitForFinished(5'000)) {
            runRequestProcess.kill();
            runRequestProcess.waitForFinished(5'000);
        }
    }
    QString runRequestError;
    if (captureResult.success && runRequestStarted) {
        const bool finished = runRequestProcess.state() == QProcess::NotRunning ||
                              runRequestProcess.waitForFinished(60'000);
        if (!finished || runRequestProcess.exitStatus() != QProcess::NormalExit ||
            runRequestProcess.exitCode() != 0) {
            runRequestError = QStringLiteral("板上运行请求失败: exit_code=%1, error=%2")
                                  .arg(runRequestProcess.exitCode())
                                  .arg(runRequestProcess.errorString());
            if (runRequestProcess.state() != QProcess::NotRunning) {
                runRequestProcess.terminate();
                if (!runRequestProcess.waitForFinished(5'000)) {
                    runRequestProcess.kill();
                    runRequestProcess.waitForFinished(5'000);
                }
            }
        }
    }
    QByteArray serialOutput;
    if (serialPort.isOpen()) {
        QElapsedTimer quietTimer;
        quietTimer.start();
        while (quietTimer.elapsed() < 500) {
            if (serialPort.waitForReadyRead(100)) {
                const QByteArray chunk = serialPort.readAll();
                if (!chunk.isEmpty()) {
                    serialOutput.append(chunk);
                    quietTimer.restart();
                }
            }
        }
        serialOutput.append(serialPort.readAll());
        serialPort.close();
    }
    transport.close();
    QString serialEvidenceError;
    if (!serialPortName.isEmpty() &&
        !saveSerialEvidence(taskRoot, serialPortName, serialBaud, serialOutput,
                            serialOpenError, &serialEvidenceError)) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("uart_evidence"), serialEvidenceError, 26);
        return 26;
    }
    if (!captureResult.success) {
        writeLiveCaptureFailure(taskRoot, captureResult.phase, captureResult.message, 25);
        QTextStream(stderr) << QStringLiteral("FT601 实时采集失败: ")
                            << captureResult.message << Qt::endl;
        return 25;
    }
    if (!runRequestError.isEmpty()) {
        writeLiveCaptureFailure(taskRoot, QStringLiteral("after_arm_run"), runRequestError, 25);
        QTextStream(stderr) << runRequestError << Qt::endl;
        return 25;
    }
    const int finalizeResult = finalizeCaptureTask(taskRoot, taskId);
    if (finalizeResult != 0) return finalizeResult;
    if (requireFtUartMarker && !protocolAnalysisHasProgramDoneMarker(taskRoot)) {
        const QString message = QStringLiteral("FT601 UART 稀疏事件未恢复出程序结束标志");
        writeLiveCaptureFailure(taskRoot, QStringLiteral("ft_uart_marker"), message, 27);
        QTextStream(stderr) << message << Qt::endl;
        return 27;
    }
    if (requireProgramDoneMarker && programDoneMarker(serialOutput).isEmpty()) {
        const QString message = QStringLiteral("UART 未检测到 PROGRAM_RUN_DONE/LOCKSTEP_RUN_DONE");
        writeLiveCaptureFailure(taskRoot, QStringLiteral("uart_marker"), message, 26);
        QTextStream(stderr) << message << Qt::endl;
        return 26;
    }
    return 0;
}

QJsonObject captureStatusJson(const lockstep::acquisition::CaptureStatusV2& status)
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

int runCaptureDiagnostic(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    QJsonObject output;
    output.insert(QStringLiteral("schema"), QStringLiteral("lockstep-capture-diagnostic-v2"));
    output.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QString error;
    lockstep::acquisition::D3xxRuntime transport;
    if (!transport.load(&error)) {
        output.insert(QStringLiteral("success"), false);
        output.insert(QStringLiteral("error"), error);
        QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact) << Qt::endl;
        return 30;
    }
    const QList<lockstep::acquisition::D3xxDeviceInfo> devices = transport.enumerate(&error);
    bool argumentValid = true;
    const quint32 deviceIndex = unsignedArgument(
        arguments, QStringLiteral("--device-index"), devices.isEmpty() ? 0U : devices.first().index,
        &argumentValid);
    if (!argumentValid || devices.isEmpty() || !transport.open(deviceIndex, &error)) {
        output.insert(QStringLiteral("success"), false);
        output.insert(QStringLiteral("error"), error.isEmpty() ? QStringLiteral("未枚举到 FT601 设备") : error);
        QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact) << Qt::endl;
        return 31;
    }

    lockstep::acquisition::SamplingCaptureSession session;
    lockstep::acquisition::CaptureStatusV2 status;
    bool success = false;
    if (arguments.contains(QStringLiteral("--capture-status"))) {
        output.insert(QStringLiteral("operation"), QStringLiteral("status"));
        success = session.queryStatus(&transport, &status, &error);
    } else if (arguments.contains(QStringLiteral("--capture-stop"))) {
        output.insert(QStringLiteral("operation"), QStringLiteral("stop"));
        success = session.stopAndRecover(&transport, &status, &error);
    } else {
        output.insert(QStringLiteral("operation"), QStringLiteral("smoke"));
        lockstep::acquisition::SamplingCaptureConfig config;
        config.triggerTimeoutSamples = unsignedArgument(
            arguments, QStringLiteral("--trigger-timeout-samples"), config.triggerTimeoutSamples,
            &argumentValid);
        quint32 captureId = 0U;
        success = argumentValid && session.configure(&transport, config, &error) &&
            session.armAndWaitAccepted(&transport, 4U, &captureId, &error) &&
            session.startEventStream(&transport, 5U, &error) &&
            session.queryStatus(&transport, &status, &error);
        output.insert(QStringLiteral("capture_id"), static_cast<double>(captureId));
        if (success) success = session.stopAndRecover(&transport, &status, &error);
    }
    output.insert(QStringLiteral("success"), success);
    output.insert(QStringLiteral("status"), captureStatusJson(status));
    if (!error.isEmpty()) output.insert(QStringLiteral("error"), error);
    transport.close();
    QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact) << Qt::endl;
    return success ? 0 : 32;
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
        if (argument == QStringLiteral("--capture-status") ||
            argument == QStringLiteral("--capture-stop") ||
            argument == QStringLiteral("--capture-smoke")) {
            return runCaptureDiagnostic(argc, argv);
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
