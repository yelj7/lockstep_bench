/*****************************************************************************
*  @file      workbench_controller.h
*  @brief     UI与底层模块适配控制器接口
*  Details.   声明UI动作到工作区、资源、流程、目标控制、报告和错误日志模块的适配接口。
*
*  @version   1.0.0.1
*
*----------------------------------------------------------------------------*
*  Change History :
*  <Version> | <Description>
*----------------------------------------------------------------------------*
*   1.0.0.1   | Create file
*----------------------------------------------------------------------------*
*
*****************************************************************************/

#ifndef LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_
#define LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_

#include <QObject>
#include <QString>

#include "error_registry.h"
#include "main_window_shell.h"
#include "report_generator.h"
#include "resource_manager.h"
#include "target_control.h"
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
    void browseProgramImage();
    void programImage();
    void verifyReadback();
    void runProgram();
    void stopProgram();
    void generateReport();
    void showVerifySummary();
    void showRunSummary();
    void updateProjectView();
    void updateTaskDetail();
    void updateTopStatus();
    void setRamSummaryFromCurrentState(const QString& title);
    void resetExecutionState();
    bool saveProgramInputForCurrentTask(workspace::TaskInputSet* inputs);
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
    lockstep::target_control::InMemoryDebugAccess debugAccess_;

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

    lockstep::workspace::TaskContext currentTask_;
    lockstep::workflow::FlowState flowState_;
    lockstep::target_control::DebugProfile debugProfile_;
    lockstep::target_control::ConnectionRecord connectionRecord_;
    lockstep::target_control::PrecheckRecord precheckRecord_;
    lockstep::target_control::ProgramImageInfo imageInfo_;
    lockstep::target_control::WriteRecord writeRecord_;
    lockstep::target_control::ReadbackVerifyRecord verifyRecord_;
    lockstep::target_control::RunControlRecord runRecord_;
};

}  // namespace lockstep::apps

#endif  // LOCKSTEP_HOST_SRC_APPS_WORKBENCH_CONTROLLER_H_
