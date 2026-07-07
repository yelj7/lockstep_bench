/*****************************************************************************
*  @file      workspace_manager.h
*  @brief     工作区与任务管理模块接口
*  Details.   声明工作区与任务管理模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_WORKSPACE_WORKSPACE_MANAGER_H_
#define LOCKSTEP_HOST_SRC_WORKSPACE_WORKSPACE_MANAGER_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::workspace {

enum class WorkspaceMode : unsigned char {
    Research = 0U,
    Test = 1U
};

enum class TaskStatus : unsigned char {
    Draft = 0U,
    Ready = 1U,
    Running = 2U,
    Completed = 3U,
    Failed = 4U,
    Aborted = 5U,
    Stale = 6U
};

enum class ArtifactKind : unsigned char {
    Log = 0U,
    Evidence = 1U,
    Waveform = 2U,
    Report = 3U,
    Other = 4U
};

enum class InputChangeAction : unsigned char {
    OverwriteCurrentTask = 0U,
    CreateNewTask = 1U,
    Cancel = 2U
};

enum class TaskInputFileKind : unsigned char {
    ProgramFile = 0U,
    ProgramManifest = 1U,
    SamplingConfig = 2U,
    FaultInjectionConfig = 3U
};

struct TaskInputItem final {
    QString id;
    QString relativePath;
    QString originalFileName;
    QString sha256;
    qint64 sizeBytes = 0;
    QString importedAt;
};

struct TaskInputSet final {
    TaskInputItem programFile;
    TaskInputItem programManifest;
    TaskInputItem samplingConfig;
    TaskInputItem faultInjectionConfig;
    QJsonObject resourceSnapshot;
};

struct TaskPaths final {
    QString taskId;
    QString taskRootRelativePath;
    QString taskRootPath;
    QString taskJsonPath;
    QString inputsPath;
    QString programPath;
    QString programManifestPath;
    QString samplingConfigPath;
    QString faultInjectionConfigPath;
    QString logsPath;
    QString evidencePath;
    QString artifactIndexPath;
    QString waveformPath;
    QString reportsPath;
};

struct TaskSummary final {
    QString taskId;
    QString taskName;
    QString description;
    WorkspaceMode mode = WorkspaceMode::Research;
    TaskStatus status = TaskStatus::Draft;
    QString relativePath;
    QString createdAt;
    QString updatedAt;
};

struct TaskContext final {
    TaskSummary summary;
    TaskInputSet inputs;
    TaskPaths paths;
    QString stageStatus;
    QString lastErrorId;
};

struct TaskCreateOptions final {
    QString taskName;
    QString description;
    TaskInputSet inputs;
    QString stageStatus;
    QString lastErrorId;
};

struct ArtifactRecord final {
    QString artifactId;
    ArtifactKind kind = ArtifactKind::Other;
    QString relativePath;
    QString name;
    QString sha256;
    qint64 sizeBytes = 0;
    QString createdAt;
    QJsonObject metadata;
};

struct TaskInputImportRequest final {
    WorkspaceMode mode = WorkspaceMode::Research;
    QString taskId;
    TaskInputFileKind kind = TaskInputFileKind::ProgramFile;
    QString sourceFilePath;
    QString targetFileName;
};

struct InputChangeRequest final {
    WorkspaceMode mode = WorkspaceMode::Research;
    QString taskId;
    TaskInputSet changedInputs;
    InputChangeAction action = InputChangeAction::Cancel;
    QString newTaskName;
};

struct InputChangeResult final {
    bool success = false;
    bool canceled = false;
    bool createdNewTask = false;
    TaskContext task;
    QStringList removedPaths;
    QString errorMessage;
};

QString toString(WorkspaceMode mode);
QString toStorageName(WorkspaceMode mode);
QString toModeCode(WorkspaceMode mode);
bool parseWorkspaceMode(const QString& text, WorkspaceMode* mode);

QString toString(TaskStatus status);
bool parseTaskStatus(const QString& text, TaskStatus* status);

QString toString(ArtifactKind kind);
bool parseArtifactKind(const QString& text, ArtifactKind* kind);

class WorkspaceManager final {
public:
    WorkspaceManager() = default;

    bool createWorkspace(const QString& rootPath, QString* errorMessage = nullptr);
    bool openWorkspace(const QString& rootPath, QString* errorMessage = nullptr);

    bool switchMode(WorkspaceMode mode, QString* errorMessage = nullptr);
    WorkspaceMode currentMode() const;

    QString workspaceRootPath() const;
    QString workspaceDisplayName() const;

    bool listTasks(
        WorkspaceMode mode,
        QList<TaskSummary>* tasks,
        QString* errorMessage = nullptr) const;

    bool createTask(
        WorkspaceMode mode,
        const TaskCreateOptions& options,
        TaskContext* context,
        QString* errorMessage = nullptr);

    bool loadTask(
        WorkspaceMode mode,
        const QString& taskId,
        TaskContext* context,
        QString* errorMessage = nullptr) const;

    bool renameTask(
        WorkspaceMode mode,
        const QString& taskId,
        const QString& newTaskName,
        QString* errorMessage = nullptr);

    bool updateTaskMetadata(
        WorkspaceMode mode,
        const QString& taskId,
        const QString& newTaskName,
        const QString& description,
        QString* errorMessage = nullptr);

    bool deleteTask(
        WorkspaceMode mode,
        const QString& taskId,
        QString* errorMessage = nullptr);

    bool saveTaskInputs(
        WorkspaceMode mode,
        const QString& taskId,
        const TaskInputSet& inputs,
        TaskContext* context,
        QString* errorMessage = nullptr);

    bool importTaskInputFile(
        const TaskInputImportRequest& request,
        TaskInputItem* importedItem,
        QString* errorMessage = nullptr);

    bool handleInputChange(
        const InputChangeRequest& request,
        InputChangeResult* result,
        QString* errorMessage = nullptr);

    bool attachArtifact(
        WorkspaceMode mode,
        const QString& taskId,
        const ArtifactRecord& artifact,
        QString* errorMessage = nullptr);

    bool getTaskPaths(
        WorkspaceMode mode,
        const QString& taskId,
        TaskPaths* paths,
        QString* errorMessage = nullptr) const;

private:
    QString rootPath_;
    WorkspaceMode currentMode_ = WorkspaceMode::Research;
    bool hasWorkspace_ = false;
};

}  // namespace lockstep::workspace

#endif  // LOCKSTEP_HOST_SRC_WORKSPACE_WORKSPACE_MANAGER_H_
