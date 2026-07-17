/**********************************************************
* 文件名: workbench_controller.h
* 日期: 2026-07-14
* 版本: v1.2
* 更新记录: 增加报告模型构建、生命周期刷新和报告文件操作
* 描述: 声明 UI 与工作区、目标控制、报告及日志模块的适配控制器
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_
#define LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_

#include <memory>

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "error_registry.h"
#include "fault_injection_orchestrator.h"
#include "main_window_shell.h"
#include "protocol_analysis.h"
#include "report_generator.h"
#include "resource_manager.h"
#include "target_control.h"
#include "waveform_trace_viewer.h"
#include "workflow_engine.h"
#include "workspace_manager.h"

namespace lockstep::apps {

class WorkbenchController final : public QObject {
public:
    WorkbenchController(
        lockstep::ui::MainWindowShell* window,
        lockstep::ui::UiMode mode,
        QObject* parent = nullptr);

    bool initialize(const QString& workspaceRootPath);

private:
    void handleAction(const lockstep::ui::UiActionRequest& request);
    bool ensureWorkspace();
    bool ensureTask();
    bool createTask();
    bool saveCurrentTask();
    void loadTaskToWorkbench(const QString& taskId);
    void startEditTask(const QString& taskId);
    void saveTaskMetadataEdit(
        const QString& taskId,
        const QString& taskName,
        const QString& description);
    void cancelTaskMetadataEdit(const QString& taskId);
    void deleteTask(const QString& taskId);
    void loadResourcePackIfAvailable();
    void startDebugService();
    void stopDebugService();
    void refreshSerialPorts();
    bool openSelectedSerialPort();
    void toggleSerialMonitor();
    void clearSerialOutput();
    void sendSerialData(const QString& text);
    bool saveSamplingConfig(const QVariantMap& parameters, bool requestHardwareSend);
    void startSamplingCapture(const QVariantMap& parameters);
    void browseProgramImage();
    void programImage();
    void verifyReadback();
    void beginHardwareOperation(lockstep::target_control::ProgramOperation operation, const QString& name);
    void endHardwareOperation();
    void updateProgramActionAvailability();
    void updateWriteOperationProgress(quint64 completedBytes, quint64 totalBytes, const QString& message);
    void updateReadbackOperationProgress(quint64 completedBytes, quint64 totalBytes, const QString& message);
    void runProgram();
    void stopProgram();
    void appendProgramSerialOutput(const QString& text);
    void updateRunButtonText();
    void refreshRunSummary(const QString& title, int runProgressPercent, int stopProgressPercent);
    lockstep::reporting::ReportDocumentModel buildReportModel() const;
    void refreshReportView(const QString& generationError = QString());
    void generateReport();
    void openReportHtml();
    void openReportDirectory();
    void copyReportPath();
    void openReportArtifact(const QString& relativePath);
    void showVerifySummary();
    void showRunSummary();
    void importWaveformFile();
    void refreshWaveformView();
    void refreshWaveformViewWithAutoAnalysis();
    void analyzeCurrentTrace(bool refreshAfterAnalysis = true);
    void updateProjectView();
    void updateTaskDetail();
    void updateTopStatus();
    void setRamSummaryFromCurrentState(const QString& title);
    void setRunSummaryFromCurrentState(const QString& title, int runProgressPercent, int stopProgressPercent);
    void resetExecutionState();
    bool saveProgramInputForCurrentTask(workspace::TaskInputSet* inputs);
    void restoreExecutionEvidenceForCurrentTask();
    void logInfo(const QString& source, const QString& message) const;
    void logWarning(const QString& source, const QString& message) const;
    void logError(const QString& source, const QString& message);
    void recordError(
        const QString& code,
        lockstep::error_handling::ErrorSeverity severity,
        const QString& source,
        const QString& message,
        const QString& detail);
    bool writeEvidenceJson(
        const QString& fileName,
        const QJsonObject& object,
        QString* relativePath,
        QString* errorMessage);
    bool configureDebugServiceAccess(
        const lockstep::resources::BoardProfile& profile,
        const QString& resourceRootPath);
    void updateConnectionDiagnostics(const QString& serviceState);
    lockstep::target_control::DebugAccess* debugAccess() const;
    QJsonObject resourceSnapshotJson() const;
    lockstep::workspace::WorkspaceMode workspaceMode() const;
    lockstep::workflow::FlowMode flowMode() const;

    lockstep::ui::MainWindowShell* window_;
    lockstep::ui::UiMode mode_;
    lockstep::workspace::WorkspaceManager workspace_;
    lockstep::resources::ResourceManager resources_;
    lockstep::workflow::WorkflowEngine workflow_;
    lockstep::target_control::TargetConnectionService connectionService_;
    lockstep::target_control::ProgramController programController_;
    lockstep::reporting::ReportGenerator reportGenerator_;
    lockstep::error_handling::ErrorRegistry errorRegistry_;
    lockstep::protocol_analyzer::ProtocolAnalyzer protocolAnalyzer_;
    lockstep::waveform_viewer::WaveformTraceViewer waveformViewer_;
    std::unique_ptr<lockstep::target_control::DebugAccess> debugAccess_;
    lockstep::target_control::DebugServiceConfig debugConfig_;

    QString workspaceRootPath_;
    QString selectedTaskId_;
    bool workspaceReady_;
    bool resourcePackReady_;
    bool hasTask_;
    bool hasImage_;
    bool hasConnection_;
    bool hasPrecheck_;
    bool hasWriteRecord_;
    bool hasVerifyRecord_;
    bool hasRunRecord_;
    bool hasHaltRecord_;
    bool hardwareOperationBusy_;
    bool targetConnectionBusy_;

    lockstep::workspace::TaskContext currentTask_;
    lockstep::workflow::FlowState flowState_;
    lockstep::target_control::DebugProfile debugProfile_;
    lockstep::target_control::ConnectionRecord connectionRecord_;
    lockstep::target_control::PrecheckRecord precheckRecord_;
    lockstep::target_control::ProgramImageInfo imageInfo_;
    lockstep::target_control::WriteRecord writeRecord_;
    lockstep::target_control::ReadbackVerifyRecord verifyRecord_;
    lockstep::target_control::RunControlRecord runRecord_;
    lockstep::target_control::RunControlRecord haltRecord_;
    std::unique_ptr<QSerialPort> serialPort_;
    QStringList serialPorts_;
    QString runSerialOutput_;
    QString latestRunInstruction_;
    bool runOutputConfirmed_;
    bool serialOpen_;
    bool runSerialCaptureActive_;
    bool runButtonResetMode_;
    bool reportGenerationBusy_;
    bool samplingCaptureBusy_;
    lockstep::target_control::ProgramOperation hardwareOperation_;
    QString hardwareOperationName_;
    int currentWriteProgress_;
    int currentReadbackProgress_;
    int currentRunProgress_;
    int currentStopProgress_;
};

}  // namespace lockstep::apps

#endif  // LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_
