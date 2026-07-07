/*****************************************************************************
*  @file      workspace_manager.cpp
*  @brief     工作区与任务管理模块实现
*  Details.   实现工作区与任务管理模块的业务逻辑、状态转换和文件访问流程。
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

#include "workspace_manager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace lockstep::workspace {
namespace {

constexpr char kResearchWorkspaceName[] = "research_workspace";
constexpr char kTestWorkspaceName[] = "test_workspace";
constexpr char kWorkspaceIndexName[] = "workspace_index.json";
constexpr char kModeConfigName[] = "mode_config.json";
constexpr char kSystemLogsName[] = "system_logs";
constexpr char kTasksName[] = "tasks";
constexpr char kTaskJsonName[] = "task.json";
constexpr char kInputsName[] = "inputs";
constexpr char kProgramName[] = "program";
constexpr char kProgramManifestName[] = "program_manifest.json";
constexpr char kSamplingConfigName[] = "sampling_config.json";
constexpr char kFaultInjectionConfigName[] = "fault_injection_config.json";
constexpr char kLogsName[] = "logs";
constexpr char kEvidenceName[] = "evidence";
constexpr char kArtifactsName[] = "artifacts.json";
constexpr char kWaveformName[] = "waveform";
constexpr char kReportsName[] = "reports";

QString currentTimeText()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void setError(QString* const errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

bool ensureDir(const QString& path, QString* const errorMessage)
{
    QDir dir;
    if (dir.mkpath(path)) {
        return true;
    }

    setError(errorMessage, QStringLiteral("无法创建目录: %1").arg(path));
    return false;
}

bool readJsonObject(
    const QString& path,
    QJsonObject* const object,
    QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取 JSON 文件: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(
            errorMessage,
            QStringLiteral("JSON 格式错误: %1").arg(path));
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(
    const QString& path,
    const QJsonObject& object,
    QString* const errorMessage)
{
    const QFileInfo info(path);
    if (!ensureDir(info.absolutePath(), errorMessage)) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("无法写入 JSON 文件: %1").arg(path));
        return false;
    }

    const QJsonDocument document(object);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        setError(errorMessage, QStringLiteral("JSON 写入不完整: %1").arg(path));
        return false;
    }

    if (!file.commit()) {
        setError(errorMessage, QStringLiteral("JSON 提交失败: %1").arg(path));
        return false;
    }

    return true;
}

QString normalizeTaskName(const QString& name)
{
    return name.trimmed().toCaseFolded();
}

QString subWorkspacePath(const QString& rootPath, const WorkspaceMode mode)
{
    return QDir(rootPath).filePath(toStorageName(mode));
}

QString indexPath(const QString& rootPath, const WorkspaceMode mode)
{
    return QDir(subWorkspacePath(rootPath, mode)).filePath(QString::fromLatin1(kWorkspaceIndexName));
}

QString tasksRootPath(const QString& rootPath, const WorkspaceMode mode)
{
    return QDir(subWorkspacePath(rootPath, mode)).filePath(QString::fromLatin1(kTasksName));
}

QJsonObject taskInputItemToJson(const TaskInputItem& item)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("relative_path"), item.relativePath);
    object.insert(QStringLiteral("original_file_name"), item.originalFileName);
    object.insert(QStringLiteral("sha256"), item.sha256);
    object.insert(QStringLiteral("size_bytes"), QString::number(item.sizeBytes));
    object.insert(QStringLiteral("imported_at"), item.importedAt);
    return object;
}

TaskInputItem taskInputItemFromJson(const QJsonObject& object)
{
    TaskInputItem item;
    item.id = object.value(QStringLiteral("id")).toString();
    item.relativePath = object.value(QStringLiteral("relative_path")).toString();
    item.originalFileName = object.value(QStringLiteral("original_file_name")).toString();
    item.sha256 = object.value(QStringLiteral("sha256")).toString();
    item.sizeBytes = object.value(QStringLiteral("size_bytes")).toString().toLongLong();
    item.importedAt = object.value(QStringLiteral("imported_at")).toString();
    return item;
}

QJsonObject taskInputsToJson(const TaskInputSet& inputs)
{
    QJsonObject object;
    object.insert(QStringLiteral("program_file"), taskInputItemToJson(inputs.programFile));
    object.insert(QStringLiteral("program_manifest"), taskInputItemToJson(inputs.programManifest));
    object.insert(QStringLiteral("sampling_config"), taskInputItemToJson(inputs.samplingConfig));
    object.insert(QStringLiteral("fault_injection_config"), taskInputItemToJson(inputs.faultInjectionConfig));
    object.insert(QStringLiteral("resource_snapshot"), inputs.resourceSnapshot);
    return object;
}

TaskInputSet taskInputsFromJson(const QJsonObject& object)
{
    TaskInputSet inputs;
    inputs.programFile =
        taskInputItemFromJson(object.value(QStringLiteral("program_file")).toObject());
    inputs.programManifest =
        taskInputItemFromJson(object.value(QStringLiteral("program_manifest")).toObject());
    inputs.samplingConfig =
        taskInputItemFromJson(object.value(QStringLiteral("sampling_config")).toObject());
    inputs.faultInjectionConfig =
        taskInputItemFromJson(object.value(QStringLiteral("fault_injection_config")).toObject());
    inputs.resourceSnapshot = object.value(QStringLiteral("resource_snapshot")).toObject();
    return inputs;
}

QJsonObject taskSummaryToJson(const TaskSummary& summary)
{
    QJsonObject object;
    object.insert(QStringLiteral("task_id"), summary.taskId);
    object.insert(QStringLiteral("task_name"), summary.taskName);
    object.insert(QStringLiteral("description"), summary.description);
    object.insert(QStringLiteral("mode"), toString(summary.mode));
    object.insert(QStringLiteral("status"), toString(summary.status));
    object.insert(QStringLiteral("relative_path"), summary.relativePath);
    object.insert(QStringLiteral("created_at"), summary.createdAt);
    object.insert(QStringLiteral("updated_at"), summary.updatedAt);
    return object;
}

bool taskSummaryFromJson(const QJsonObject& object, TaskSummary* const summary)
{
    TaskSummary parsed;
    WorkspaceMode mode = WorkspaceMode::Research;
    TaskStatus status = TaskStatus::Draft;

    if (!parseWorkspaceMode(object.value(QStringLiteral("mode")).toString(), &mode)) {
        return false;
    }
    if (!parseTaskStatus(object.value(QStringLiteral("status")).toString(), &status)) {
        return false;
    }

    parsed.taskId = object.value(QStringLiteral("task_id")).toString();
    parsed.taskName = object.value(QStringLiteral("task_name")).toString();
    parsed.description = object.value(QStringLiteral("description")).toString();
    parsed.mode = mode;
    parsed.status = status;
    parsed.relativePath = object.value(QStringLiteral("relative_path")).toString();
    parsed.createdAt = object.value(QStringLiteral("created_at")).toString();
    parsed.updatedAt = object.value(QStringLiteral("updated_at")).toString();
    *summary = parsed;
    return !parsed.taskId.isEmpty();
}

QJsonObject artifactToJson(const ArtifactRecord& artifact)
{
    QJsonObject object;
    object.insert(QStringLiteral("artifact_id"), artifact.artifactId);
    object.insert(QStringLiteral("kind"), toString(artifact.kind));
    object.insert(QStringLiteral("relative_path"), artifact.relativePath);
    object.insert(QStringLiteral("name"), artifact.name);
    object.insert(QStringLiteral("sha256"), artifact.sha256);
    object.insert(QStringLiteral("size_bytes"), QString::number(artifact.sizeBytes));
    object.insert(QStringLiteral("created_at"), artifact.createdAt);
    object.insert(QStringLiteral("metadata"), artifact.metadata);
    return object;
}

bool loadIndex(
    const QString& rootPath,
    const WorkspaceMode mode,
    QJsonObject* const index,
    QString* const errorMessage)
{
    const QString path = indexPath(rootPath, mode);
    if (!QFileInfo::exists(path)) {
        QJsonObject object;
        object.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
        object.insert(QStringLiteral("mode"), toString(mode));
        object.insert(QStringLiteral("tasks"), QJsonArray());
        *index = object;
        return true;
    }

    return readJsonObject(path, index, errorMessage);
}

bool saveIndex(
    const QString& rootPath,
    const WorkspaceMode mode,
    const QJsonObject& index,
    QString* const errorMessage)
{
    return writeJsonObject(indexPath(rootPath, mode), index, errorMessage);
}

bool taskNameExists(
    const QJsonArray& tasks,
    const QString& taskName,
    const QString& ignoredTaskId)
{
    const QString normalized = normalizeTaskName(taskName);
    for (const QJsonValue& value : tasks) {
        const QJsonObject object = value.toObject();
        const QString existingId = object.value(QStringLiteral("task_id")).toString();
        const QString existingName = object.value(QStringLiteral("task_name")).toString();
        if (existingId != ignoredTaskId && normalizeTaskName(existingName) == normalized) {
            return true;
        }
    }

    return false;
}

QString generateTaskId(const QJsonArray& tasks, const WorkspaceMode mode)
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss"));
    const QString prefix = QStringLiteral("t_%1_%2_").arg(toModeCode(mode), stamp);
    int sequence = 1;

    bool used = true;
    while (used) {
        used = false;
        const QString candidate = QStringLiteral("%1%2")
            .arg(prefix, QStringLiteral("%1").arg(sequence, 4, 10, QLatin1Char('0')));
        for (const QJsonValue& value : tasks) {
            if (value.toObject().value(QStringLiteral("task_id")).toString() == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return candidate;
        }
        ++sequence;
    }

    return prefix + QStringLiteral("0001");
}

QByteArray sha256File(const QString& path, QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取文件摘要: %1").arg(path));
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }

    return hash.result().toHex();
}

bool createTaskFolders(const TaskPaths& paths, QString* const errorMessage)
{
    const QStringList folders = {
        paths.taskRootPath,
        paths.inputsPath,
        paths.programPath,
        paths.logsPath,
        paths.evidencePath,
        paths.waveformPath,
        paths.reportsPath
    };

    for (const QString& folder : folders) {
        if (!ensureDir(folder, errorMessage)) {
            return false;
        }
    }

    return true;
}

bool clearFolder(const QString& path, QStringList* const removedPaths, QString* const errorMessage)
{
    QDir dir(path);
    if (!dir.exists()) {
        return ensureDir(path, errorMessage);
    }

    if (removedPaths != nullptr) {
        removedPaths->append(path);
    }

    if (!dir.removeRecursively()) {
        setError(errorMessage, QStringLiteral("无法清理旧产物目录: %1").arg(path));
        return false;
    }

    return ensureDir(path, errorMessage);
}

bool isTaskRootPath(const QString& tasksRoot, const QString& taskRoot)
{
    const QString cleanTasksRoot = QDir::cleanPath(QDir::fromNativeSeparators(tasksRoot));
    const QString cleanTaskRoot = QDir::cleanPath(QDir::fromNativeSeparators(taskRoot));
    return (cleanTaskRoot != cleanTasksRoot) &&
        cleanTaskRoot.startsWith(cleanTasksRoot + QLatin1Char('/'));
}

}  // namespace

QString toString(const WorkspaceMode mode)
{
    return (mode == WorkspaceMode::Test) ? QStringLiteral("test") : QStringLiteral("research");
}

QString toStorageName(const WorkspaceMode mode)
{
    return (mode == WorkspaceMode::Test)
        ? QString::fromLatin1(kTestWorkspaceName)
        : QString::fromLatin1(kResearchWorkspaceName);
}

QString toModeCode(const WorkspaceMode mode)
{
    return (mode == WorkspaceMode::Test) ? QStringLiteral("test") : QStringLiteral("research");
}

bool parseWorkspaceMode(const QString& text, WorkspaceMode* const mode)
{
    if (mode == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("test")) {
        *mode = WorkspaceMode::Test;
        return true;
    }
    if (normalized == QStringLiteral("research")) {
        *mode = WorkspaceMode::Research;
        return true;
    }

    return false;
}

QString toString(const TaskStatus status)
{
    QString text;
    switch (status) {
    case TaskStatus::Draft:
        text = QStringLiteral("draft");
        break;
    case TaskStatus::Ready:
        text = QStringLiteral("ready");
        break;
    case TaskStatus::Running:
        text = QStringLiteral("running");
        break;
    case TaskStatus::Completed:
        text = QStringLiteral("completed");
        break;
    case TaskStatus::Failed:
        text = QStringLiteral("failed");
        break;
    case TaskStatus::Aborted:
        text = QStringLiteral("aborted");
        break;
    case TaskStatus::Stale:
        text = QStringLiteral("stale");
        break;
    default:
        text = QStringLiteral("draft");
        break;
    }
    return text;
}

bool parseTaskStatus(const QString& text, TaskStatus* const status)
{
    if (status == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("ready")) {
        *status = TaskStatus::Ready;
    } else if (normalized == QStringLiteral("running")) {
        *status = TaskStatus::Running;
    } else if (normalized == QStringLiteral("completed")) {
        *status = TaskStatus::Completed;
    } else if (normalized == QStringLiteral("failed")) {
        *status = TaskStatus::Failed;
    } else if (normalized == QStringLiteral("aborted")) {
        *status = TaskStatus::Aborted;
    } else if (normalized == QStringLiteral("stale")) {
        *status = TaskStatus::Stale;
    } else if (normalized == QStringLiteral("draft")) {
        *status = TaskStatus::Draft;
    } else {
        return false;
    }

    return true;
}

QString toString(const ArtifactKind kind)
{
    QString text;
    switch (kind) {
    case ArtifactKind::Log:
        text = QStringLiteral("log");
        break;
    case ArtifactKind::Evidence:
        text = QStringLiteral("evidence");
        break;
    case ArtifactKind::Waveform:
        text = QStringLiteral("waveform");
        break;
    case ArtifactKind::Report:
        text = QStringLiteral("report");
        break;
    case ArtifactKind::Other:
    default:
        text = QStringLiteral("other");
        break;
    }
    return text;
}

bool parseArtifactKind(const QString& text, ArtifactKind* const kind)
{
    if (kind == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("log")) {
        *kind = ArtifactKind::Log;
    } else if (normalized == QStringLiteral("evidence")) {
        *kind = ArtifactKind::Evidence;
    } else if (normalized == QStringLiteral("waveform")) {
        *kind = ArtifactKind::Waveform;
    } else if (normalized == QStringLiteral("report")) {
        *kind = ArtifactKind::Report;
    } else if (normalized == QStringLiteral("other")) {
        *kind = ArtifactKind::Other;
    } else {
        return false;
    }

    return true;
}

bool WorkspaceManager::createWorkspace(const QString& rootPath, QString* const errorMessage)
{
    const QString cleanedRoot = QDir::cleanPath(rootPath);
    if (cleanedRoot.isEmpty()) {
        setError(errorMessage, QStringLiteral("工作区路径为空"));
        return false;
    }

    if (!ensureDir(cleanedRoot, errorMessage)) {
        return false;
    }

    rootPath_ = cleanedRoot;
    hasWorkspace_ = true;

    const QList<WorkspaceMode> modes = {WorkspaceMode::Research, WorkspaceMode::Test};
    for (const WorkspaceMode mode : modes) {
        const QString subPath = subWorkspacePath(rootPath_, mode);
        if (!ensureDir(subPath, errorMessage) ||
            !ensureDir(QDir(subPath).filePath(QString::fromLatin1(kSystemLogsName)), errorMessage) ||
            !ensureDir(QDir(subPath).filePath(QString::fromLatin1(kTasksName)), errorMessage)) {
            return false;
        }

        QJsonObject index;
        if (!loadIndex(rootPath_, mode, &index, errorMessage) ||
            !saveIndex(rootPath_, mode, index, errorMessage)) {
            return false;
        }

        const QString modeConfigPath = QDir(subPath).filePath(QString::fromLatin1(kModeConfigName));
        if (!QFileInfo::exists(modeConfigPath)) {
            QJsonObject modeConfig;
            modeConfig.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
            modeConfig.insert(QStringLiteral("mode"), toString(mode));
            modeConfig.insert(QStringLiteral("resource_snapshot"), QJsonObject());
            if (!writeJsonObject(modeConfigPath, modeConfig, errorMessage)) {
                return false;
            }
        }
    }

    return true;
}

bool WorkspaceManager::openWorkspace(const QString& rootPath, QString* const errorMessage)
{
    if (!createWorkspace(rootPath, errorMessage)) {
        return false;
    }

    // 打开时复用初始化流程，保证缺失的基础目录会被补齐。
    return true;
}

bool WorkspaceManager::switchMode(const WorkspaceMode mode, QString* const errorMessage)
{
    if (!hasWorkspace_) {
        setError(errorMessage, QStringLiteral("尚未打开工作区"));
        return false;
    }

    currentMode_ = mode;
    return true;
}

WorkspaceMode WorkspaceManager::currentMode() const
{
    return currentMode_;
}

QString WorkspaceManager::workspaceRootPath() const
{
    return rootPath_;
}

QString WorkspaceManager::workspaceDisplayName() const
{
    return QFileInfo(rootPath_).fileName();
}

bool WorkspaceManager::listTasks(
    const WorkspaceMode mode,
    QList<TaskSummary>* const tasks,
    QString* const errorMessage) const
{
    if (tasks == nullptr) {
        setError(errorMessage, QStringLiteral("任务列表输出为空"));
        return false;
    }

    QJsonObject index;
    if (!loadIndex(rootPath_, mode, &index, errorMessage)) {
        return false;
    }

    QList<TaskSummary> parsedTasks;
    const QJsonArray array = index.value(QStringLiteral("tasks")).toArray();
    for (const QJsonValue& value : array) {
        TaskSummary summary;
        if (taskSummaryFromJson(value.toObject(), &summary)) {
            parsedTasks.append(summary);
        }
    }

    *tasks = parsedTasks;
    return true;
}

bool WorkspaceManager::createTask(
    const WorkspaceMode mode,
    const TaskCreateOptions& options,
    TaskContext* const context,
    QString* const errorMessage)
{
    QJsonObject index;
    if (!loadIndex(rootPath_, mode, &index, errorMessage)) {
        return false;
    }

    QJsonArray tasks = index.value(QStringLiteral("tasks")).toArray();
    const QString requestedName = options.taskName.trimmed().isEmpty()
        ? QStringLiteral("未命名任务")
        : options.taskName.trimmed();
    if (taskNameExists(tasks, requestedName, QString())) {
        setError(errorMessage, QStringLiteral("任务名称已存在: %1").arg(requestedName));
        return false;
    }

    TaskSummary summary;
    summary.taskId = generateTaskId(tasks, mode);
    summary.taskName = requestedName;
    summary.description = options.description;
    summary.mode = mode;
    summary.status = TaskStatus::Draft;
    summary.relativePath = QStringLiteral("%1/%2").arg(QString::fromLatin1(kTasksName), summary.taskId);
    summary.createdAt = currentTimeText();
    summary.updatedAt = summary.createdAt;

    TaskPaths paths;
    if (!getTaskPaths(mode, summary.taskId, &paths, errorMessage) ||
        !createTaskFolders(paths, errorMessage)) {
        return false;
    }

    QJsonObject taskObject = taskSummaryToJson(summary);
    taskObject.insert(QStringLiteral("inputs"), taskInputsToJson(options.inputs));
    taskObject.insert(QStringLiteral("stage_status"), options.stageStatus);
    taskObject.insert(QStringLiteral("last_error_id"), options.lastErrorId);

    QJsonObject artifactIndex;
    artifactIndex.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    artifactIndex.insert(QStringLiteral("task_id"), summary.taskId);
    artifactIndex.insert(QStringLiteral("artifacts"), QJsonArray());

    if (!writeJsonObject(paths.taskJsonPath, taskObject, errorMessage) ||
        !writeJsonObject(paths.artifactIndexPath, artifactIndex, errorMessage)) {
        return false;
    }

    tasks.append(taskSummaryToJson(summary));
    index.insert(QStringLiteral("tasks"), tasks);
    if (!saveIndex(rootPath_, mode, index, errorMessage)) {
        return false;
    }

    if (context != nullptr) {
        context->summary = summary;
        context->inputs = options.inputs;
        context->paths = paths;
        context->stageStatus = options.stageStatus;
        context->lastErrorId = options.lastErrorId;
    }

    return true;
}

bool WorkspaceManager::loadTask(
    const WorkspaceMode mode,
    const QString& taskId,
    TaskContext* const context,
    QString* const errorMessage) const
{
    if (context == nullptr) {
        setError(errorMessage, QStringLiteral("任务上下文输出为空"));
        return false;
    }

    TaskPaths paths;
    if (!getTaskPaths(mode, taskId, &paths, errorMessage)) {
        return false;
    }

    QJsonObject object;
    if (!readJsonObject(paths.taskJsonPath, &object, errorMessage)) {
        return false;
    }

    TaskSummary summary;
    if (!taskSummaryFromJson(object, &summary)) {
        setError(errorMessage, QStringLiteral("任务文件字段不完整: %1").arg(taskId));
        return false;
    }

    context->summary = summary;
    context->inputs = taskInputsFromJson(object.value(QStringLiteral("inputs")).toObject());
    context->paths = paths;
    context->stageStatus = object.value(QStringLiteral("stage_status")).toString();
    context->lastErrorId = object.value(QStringLiteral("last_error_id")).toString();
    return true;
}

bool WorkspaceManager::renameTask(
    const WorkspaceMode mode,
    const QString& taskId,
    const QString& newTaskName,
    QString* const errorMessage)
{
    TaskContext context;
    if (!loadTask(mode, taskId, &context, errorMessage)) {
        return false;
    }

    return updateTaskMetadata(mode, taskId, newTaskName, context.summary.description, errorMessage);
}

bool WorkspaceManager::updateTaskMetadata(
    const WorkspaceMode mode,
    const QString& taskId,
    const QString& newTaskName,
    const QString& description,
    QString* const errorMessage)
{
    QJsonObject index;
    if (!loadIndex(rootPath_, mode, &index, errorMessage)) {
        return false;
    }

    QJsonArray tasks = index.value(QStringLiteral("tasks")).toArray();
    const QString trimmed = newTaskName.trimmed();
    if (trimmed.isEmpty()) {
        setError(errorMessage, QStringLiteral("任务名称为空"));
        return false;
    }
    if (taskNameExists(tasks, trimmed, taskId)) {
        setError(errorMessage, QStringLiteral("任务名称已存在: %1").arg(trimmed));
        return false;
    }

    bool found = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonObject item = tasks.at(i).toObject();
        if (item.value(QStringLiteral("task_id")).toString() == taskId) {
            item.insert(QStringLiteral("task_name"), trimmed);
            item.insert(QStringLiteral("description"), description);
            item.insert(QStringLiteral("updated_at"), currentTimeText());
            tasks.replace(i, item);
            found = true;
            break;
        }
    }

    if (!found) {
        setError(errorMessage, QStringLiteral("任务不存在: %1").arg(taskId));
        return false;
    }

    TaskContext context;
    if (!loadTask(mode, taskId, &context, errorMessage)) {
        return false;
    }

    context.summary.taskName = trimmed;
    context.summary.description = description;
    context.summary.updatedAt = currentTimeText();
    QJsonObject taskObject = taskSummaryToJson(context.summary);
    taskObject.insert(QStringLiteral("inputs"), taskInputsToJson(context.inputs));
    taskObject.insert(QStringLiteral("stage_status"), context.stageStatus);
    taskObject.insert(QStringLiteral("last_error_id"), context.lastErrorId);

    index.insert(QStringLiteral("tasks"), tasks);
    return writeJsonObject(context.paths.taskJsonPath, taskObject, errorMessage) &&
        saveIndex(rootPath_, mode, index, errorMessage);
}

bool WorkspaceManager::deleteTask(
    const WorkspaceMode mode,
    const QString& taskId,
    QString* const errorMessage)
{
    const QString normalizedTaskId = taskId.trimmed();
    if (normalizedTaskId.isEmpty()) {
        setError(errorMessage, QStringLiteral("任务 ID 为空"));
        return false;
    }
    if (normalizedTaskId.contains(QLatin1Char('/')) ||
        normalizedTaskId.contains(QLatin1Char('\\')) ||
        normalizedTaskId.contains(QStringLiteral(".."))) {
        setError(errorMessage, QStringLiteral("任务 ID 非法: %1").arg(normalizedTaskId));
        return false;
    }

    QJsonObject index;
    if (!loadIndex(rootPath_, mode, &index, errorMessage)) {
        return false;
    }

    const QJsonArray tasks = index.value(QStringLiteral("tasks")).toArray();
    QJsonArray remainingTasks;
    bool found = false;
    for (const QJsonValue& value : tasks) {
        const QJsonObject taskObject = value.toObject();
        if (taskObject.value(QStringLiteral("task_id")).toString() == normalizedTaskId) {
            found = true;
        } else {
            remainingTasks.append(value);
        }
    }

    if (!found) {
        setError(errorMessage, QStringLiteral("任务不存在: %1").arg(normalizedTaskId));
        return false;
    }

    TaskPaths paths;
    if (!getTaskPaths(mode, normalizedTaskId, &paths, errorMessage)) {
        return false;
    }

    if (!isTaskRootPath(tasksRootPath(rootPath_, mode), paths.taskRootPath)) {
        setError(errorMessage, QStringLiteral("任务目录非法，拒绝删除: %1").arg(paths.taskRootPath));
        return false;
    }

    QDir taskDir(paths.taskRootPath);
    if (taskDir.exists() && !taskDir.removeRecursively()) {
        setError(errorMessage, QStringLiteral("无法删除任务目录: %1").arg(paths.taskRootPath));
        return false;
    }

    index.insert(QStringLiteral("tasks"), remainingTasks);
    return saveIndex(rootPath_, mode, index, errorMessage);
}

bool WorkspaceManager::saveTaskInputs(
    const WorkspaceMode mode,
    const QString& taskId,
    const TaskInputSet& inputs,
    TaskContext* const context,
    QString* const errorMessage)
{
    TaskContext loaded;
    if (!loadTask(mode, taskId, &loaded, errorMessage)) {
        return false;
    }

    loaded.inputs = inputs;
    loaded.summary.status = TaskStatus::Ready;
    loaded.summary.updatedAt = currentTimeText();

    QJsonObject object = taskSummaryToJson(loaded.summary);
    object.insert(QStringLiteral("inputs"), taskInputsToJson(loaded.inputs));
    object.insert(QStringLiteral("stage_status"), loaded.stageStatus);
    object.insert(QStringLiteral("last_error_id"), loaded.lastErrorId);
    if (!writeJsonObject(loaded.paths.taskJsonPath, object, errorMessage)) {
        return false;
    }

    QJsonObject index;
    if (!loadIndex(rootPath_, mode, &index, errorMessage)) {
        return false;
    }

    QJsonArray tasks = index.value(QStringLiteral("tasks")).toArray();
    bool found = false;
    for (int i = 0; i < tasks.size(); ++i) {
        QJsonObject item = tasks.at(i).toObject();
        if (item.value(QStringLiteral("task_id")).toString() == taskId) {
            tasks.replace(i, taskSummaryToJson(loaded.summary));
            found = true;
            break;
        }
    }
    if (!found) {
        setError(errorMessage, QStringLiteral("任务不存在: %1").arg(taskId));
        return false;
    }

    index.insert(QStringLiteral("tasks"), tasks);
    if (!saveIndex(rootPath_, mode, index, errorMessage)) {
        return false;
    }

    if (context != nullptr) {
        *context = loaded;
    }

    return true;
}

bool WorkspaceManager::importTaskInputFile(
    const TaskInputImportRequest& request,
    TaskInputItem* const importedItem,
    QString* const errorMessage)
{
    if (importedItem == nullptr) {
        setError(errorMessage, QStringLiteral("导入结果输出为空"));
        return false;
    }

    TaskPaths paths;
    if (!getTaskPaths(request.mode, request.taskId, &paths, errorMessage)) {
        return false;
    }

    const QFileInfo sourceInfo(request.sourceFilePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        setError(errorMessage, QStringLiteral("输入文件不存在: %1").arg(request.sourceFilePath));
        return false;
    }

    QString targetPath;
    switch (request.kind) {
    case TaskInputFileKind::ProgramFile:
        targetPath = QDir(paths.programPath).filePath(
            request.targetFileName.isEmpty() ? sourceInfo.fileName() : request.targetFileName);
        break;
    case TaskInputFileKind::ProgramManifest:
        targetPath = paths.programManifestPath;
        break;
    case TaskInputFileKind::SamplingConfig:
        targetPath = paths.samplingConfigPath;
        break;
    case TaskInputFileKind::FaultInjectionConfig:
        targetPath = paths.faultInjectionConfigPath;
        break;
    default:
        setError(errorMessage, QStringLiteral("未知输入文件类型"));
        return false;
    }

    if (!ensureDir(QFileInfo(targetPath).absolutePath(), errorMessage)) {
        return false;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        setError(errorMessage, QStringLiteral("无法覆盖任务输入文件: %1").arg(targetPath));
        return false;
    }
    if (!QFile::copy(request.sourceFilePath, targetPath)) {
        setError(errorMessage, QStringLiteral("无法复制任务输入文件: %1").arg(targetPath));
        return false;
    }

    const QByteArray digest = sha256File(targetPath, errorMessage);
    if (digest.isEmpty() && sourceInfo.size() > 0) {
        return false;
    }

    const QDir taskDir(paths.taskRootPath);
    importedItem->id = QStringLiteral("input_%1").arg(currentTimeText());
    importedItem->relativePath = taskDir.relativeFilePath(targetPath);
    importedItem->originalFileName = sourceInfo.fileName();
    importedItem->sha256 = QString::fromLatin1(digest);
    importedItem->sizeBytes = QFileInfo(targetPath).size();
    importedItem->importedAt = currentTimeText();
    return true;
}

bool WorkspaceManager::handleInputChange(
    const InputChangeRequest& request,
    InputChangeResult* const result,
    QString* const errorMessage)
{
    if (result == nullptr) {
        setError(errorMessage, QStringLiteral("输入变更结果输出为空"));
        return false;
    }

    InputChangeResult nextResult;

    if (request.action == InputChangeAction::Cancel) {
        nextResult.success = true;
        nextResult.canceled = true;
        *result = nextResult;
        return true;
    }

    if (request.action == InputChangeAction::CreateNewTask) {
        TaskCreateOptions options;
        options.taskName = request.newTaskName;
        options.inputs = request.changedInputs;
        TaskContext created;
        if (!createTask(request.mode, options, &created, errorMessage)) {
            return false;
        }
        nextResult.success = true;
        nextResult.createdNewTask = true;
        nextResult.task = created;
        *result = nextResult;
        return true;
    }

    TaskContext context;
    if (!saveTaskInputs(request.mode, request.taskId, request.changedInputs, &context, errorMessage)) {
        return false;
    }

    if (!clearFolder(context.paths.logsPath, &nextResult.removedPaths, errorMessage) ||
        !clearFolder(context.paths.evidencePath, &nextResult.removedPaths, errorMessage) ||
        !clearFolder(context.paths.waveformPath, &nextResult.removedPaths, errorMessage) ||
        !clearFolder(context.paths.reportsPath, &nextResult.removedPaths, errorMessage)) {
        return false;
    }

    QJsonObject artifactIndex;
    artifactIndex.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    artifactIndex.insert(QStringLiteral("task_id"), request.taskId);
    artifactIndex.insert(QStringLiteral("artifacts"), QJsonArray());
    if (!writeJsonObject(context.paths.artifactIndexPath, artifactIndex, errorMessage)) {
        return false;
    }

    nextResult.success = true;
    nextResult.task = context;
    *result = nextResult;
    return true;
}

bool WorkspaceManager::attachArtifact(
    const WorkspaceMode mode,
    const QString& taskId,
    const ArtifactRecord& artifact,
    QString* const errorMessage)
{
    TaskPaths paths;
    if (!getTaskPaths(mode, taskId, &paths, errorMessage)) {
        return false;
    }

    if (artifact.relativePath.contains(QStringLiteral(".."))) {
        setError(errorMessage, QStringLiteral("产物路径不得越出任务目录"));
        return false;
    }

    QJsonObject index;
    if (QFileInfo::exists(paths.artifactIndexPath)) {
        if (!readJsonObject(paths.artifactIndexPath, &index, errorMessage)) {
            return false;
        }
    } else {
        index.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
        index.insert(QStringLiteral("task_id"), taskId);
    }

    QJsonArray artifacts = index.value(QStringLiteral("artifacts")).toArray();
    artifacts.append(artifactToJson(artifact));
    index.insert(QStringLiteral("artifacts"), artifacts);
    return writeJsonObject(paths.artifactIndexPath, index, errorMessage);
}

bool WorkspaceManager::getTaskPaths(
    const WorkspaceMode mode,
    const QString& taskId,
    TaskPaths* const paths,
    QString* const errorMessage) const
{
    if (paths == nullptr) {
        setError(errorMessage, QStringLiteral("任务路径输出为空"));
        return false;
    }
    if (rootPath_.isEmpty()) {
        setError(errorMessage, QStringLiteral("尚未打开工作区"));
        return false;
    }
    if (taskId.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("任务 ID 为空"));
        return false;
    }

    const QString taskRootRelative = QStringLiteral("%1/%2").arg(QString::fromLatin1(kTasksName), taskId);
    const QString taskRoot = QDir(tasksRootPath(rootPath_, mode)).filePath(taskId);

    TaskPaths built;
    built.taskId = taskId;
    built.taskRootRelativePath = taskRootRelative;
    built.taskRootPath = taskRoot;
    built.taskJsonPath = QDir(taskRoot).filePath(QString::fromLatin1(kTaskJsonName));
    built.inputsPath = QDir(taskRoot).filePath(QString::fromLatin1(kInputsName));
    built.programPath = QDir(built.inputsPath).filePath(QString::fromLatin1(kProgramName));
    built.programManifestPath = QDir(built.inputsPath).filePath(QString::fromLatin1(kProgramManifestName));
    built.samplingConfigPath = QDir(built.inputsPath).filePath(QString::fromLatin1(kSamplingConfigName));
    built.faultInjectionConfigPath = QDir(built.inputsPath).filePath(QString::fromLatin1(kFaultInjectionConfigName));
    built.logsPath = QDir(taskRoot).filePath(QString::fromLatin1(kLogsName));
    built.evidencePath = QDir(taskRoot).filePath(QString::fromLatin1(kEvidenceName));
    built.artifactIndexPath = QDir(built.evidencePath).filePath(QString::fromLatin1(kArtifactsName));
    built.waveformPath = QDir(taskRoot).filePath(QString::fromLatin1(kWaveformName));
    built.reportsPath = QDir(taskRoot).filePath(QString::fromLatin1(kReportsName));
    *paths = built;
    return true;
}

}  // namespace lockstep::workspace
