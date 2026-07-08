/*****************************************************************************
*  @file      error_registry.cpp
*  @brief     错误处理与日志登记模块实现
*  Details.   实现错误处理与日志登记模块的业务逻辑、状态转换和文件访问流程。
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

#include "error_registry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace lockstep::error_handling {
namespace {

constexpr char kLogsName[] = "logs";
constexpr char kTaskErrorsName[] = "error_events.json";
constexpr char kTaskLogName[] = "task_log.jsonl";
constexpr char kSystemErrorsName[] = "system_error_events.json";
constexpr char kSystemLogName[] = "system_log.jsonl";

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

QString makeErrorId(const ErrorEvent& event)
{
    const QString seed = event.code.trimmed().isEmpty() ? QStringLiteral("ERR") : event.code.trimmed();
    return QStringLiteral("%1_%2").arg(seed, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
}

bool ensureDir(const QString& path, QString* const errorMessage)
{
    QDir dir;
    if (dir.mkpath(path)) {
        return true;
    }

    setError(errorMessage, QStringLiteral("无法创建日志目录: %1").arg(path));
    return false;
}

QString taskLogRootPath(const QString& taskRootPath)
{
    return QDir(taskRootPath).filePath(QString::fromLatin1(kLogsName));
}

QString taskErrorsPath(const QString& taskRootPath)
{
    return QDir(taskLogRootPath(taskRootPath)).filePath(QString::fromLatin1(kTaskErrorsName));
}

QString systemErrorsPath(const QString& systemLogRootPath)
{
    return QDir(systemLogRootPath).filePath(QString::fromLatin1(kSystemErrorsName));
}

QJsonArray stringListToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

ErrorRecord recordFromJson(const QJsonObject& object)
{
    ErrorRecord record;
    record.errorId = object.value(QStringLiteral("error_id")).toString();
    record.code = object.value(QStringLiteral("code")).toString();
    record.source = object.value(QStringLiteral("source")).toString();
    record.module = object.value(QStringLiteral("module")).toString();
    record.taskId = object.value(QStringLiteral("task_id")).toString();
    record.message = object.value(QStringLiteral("message")).toString();
    record.detail = object.value(QStringLiteral("detail")).toString();
    record.createdAt = object.value(QStringLiteral("created_at")).toString();
    record.updatedAt = object.value(QStringLiteral("updated_at")).toString();
    record.resolvedAt = object.value(QStringLiteral("resolved_at")).toString();
    record.resolution = object.value(QStringLiteral("resolution")).toString();
    record.context = object.value(QStringLiteral("context")).toObject();

    ErrorSeverity severity = ErrorSeverity::Error;
    if (parseErrorSeverity(object.value(QStringLiteral("severity")).toString(), &severity)) {
        record.severity = severity;
    }

    ErrorStatus status = ErrorStatus::Active;
    if (parseErrorStatus(object.value(QStringLiteral("status")).toString(), &status)) {
        record.status = status;
    }

    return record;
}

bool readRecordFile(
    const QString& path,
    QList<ErrorRecord>* const records,
    QString* const errorMessage)
{
    if (records == nullptr) {
        setError(errorMessage, QStringLiteral("错误记录输出为空"));
        return false;
    }

    if (!QFileInfo::exists(path)) {
        records->clear();
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取错误记录: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("错误记录 JSON 格式无效: %1").arg(path));
        return false;
    }

    QList<ErrorRecord> parsed;
    const QJsonArray array = document.object().value(QStringLiteral("errors")).toArray();
    for (const QJsonValue& value : array) {
        parsed.append(recordFromJson(value.toObject()));
    }

    *records = parsed;
    return true;
}

bool writeRecordFile(
    const QString& path,
    const QList<ErrorRecord>& records,
    QString* const errorMessage)
{
    const QFileInfo info(path);
    if (!ensureDir(info.absolutePath(), errorMessage)) {
        return false;
    }

    QJsonArray array;
    for (const ErrorRecord& record : records) {
        array.append(toJson(record));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("updated_at"), currentTimeText());
    root.insert(QStringLiteral("errors"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("无法写入错误记录: %1").arg(path));
        return false;
    }

    const QJsonDocument document(root);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        setError(errorMessage, QStringLiteral("错误记录写入不完整: %1").arg(path));
        return false;
    }

    if (!file.commit()) {
        setError(errorMessage, QStringLiteral("错误记录提交失败: %1").arg(path));
        return false;
    }

    return true;
}

bool appendJsonLine(
    const QString& path,
    const QJsonObject& object,
    QString* const errorMessage)
{
    const QFileInfo info(path);
    if (!ensureDir(info.absolutePath(), errorMessage)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        setError(errorMessage, QStringLiteral("无法追加日志: %1").arg(path));
        return false;
    }

    const QJsonDocument document(object);
    const QByteArray line = document.toJson(QJsonDocument::Compact) + QByteArray("\n");
    if (file.write(line) != line.size()) {
        setError(errorMessage, QStringLiteral("日志追加不完整: %1").arg(path));
        return false;
    }

    return true;
}

bool isBlockingSeverity(const ErrorSeverity severity)
{
    return severity == ErrorSeverity::Error ||
        severity == ErrorSeverity::Blocking ||
        severity == ErrorSeverity::Critical;
}

MaintenanceState combineState(const MaintenanceState left, const MaintenanceState right)
{
    if (left == MaintenanceState::Failed || right == MaintenanceState::Failed) {
        return MaintenanceState::Failed;
    }
    if (left == MaintenanceState::Warning || right == MaintenanceState::Warning) {
        return MaintenanceState::Warning;
    }
    return MaintenanceState::Passed;
}

MaintenanceCheckItem makeCheckItem(
    const QString& id,
    const MaintenanceState state,
    const QString& message)
{
    MaintenanceCheckItem item;
    item.id = id;
    item.state = state;
    item.message = message;
    return item;
}

}  // namespace

QString toString(const ErrorSeverity severity)
{
    QString text;
    switch (severity) {
    case ErrorSeverity::Info:
        text = QStringLiteral("info");
        break;
    case ErrorSeverity::Warning:
        text = QStringLiteral("warning");
        break;
    case ErrorSeverity::Blocking:
        text = QStringLiteral("blocking");
        break;
    case ErrorSeverity::Critical:
        text = QStringLiteral("critical");
        break;
    case ErrorSeverity::Error:
    default:
        text = QStringLiteral("error");
        break;
    }
    return text;
}

QString toString(const ErrorStatus status)
{
    QString text;
    switch (status) {
    case ErrorStatus::Resolved:
        text = QStringLiteral("resolved");
        break;
    case ErrorStatus::Acknowledged:
        text = QStringLiteral("acknowledged");
        break;
    case ErrorStatus::Active:
    default:
        text = QStringLiteral("active");
        break;
    }
    return text;
}

QString toString(const LogScope scope)
{
    return (scope == LogScope::System) ? QStringLiteral("system") : QStringLiteral("task");
}

QString toString(const MaintenanceState state)
{
    QString text;
    switch (state) {
    case MaintenanceState::Warning:
        text = QStringLiteral("warning");
        break;
    case MaintenanceState::Failed:
        text = QStringLiteral("failed");
        break;
    case MaintenanceState::Passed:
    default:
        text = QStringLiteral("passed");
        break;
    }
    return text;
}

bool parseErrorSeverity(const QString& text, ErrorSeverity* const severity)
{
    if (severity == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("info")) {
        *severity = ErrorSeverity::Info;
    } else if (normalized == QStringLiteral("warning")) {
        *severity = ErrorSeverity::Warning;
    } else if (normalized == QStringLiteral("error")) {
        *severity = ErrorSeverity::Error;
    } else if (normalized == QStringLiteral("blocking")) {
        *severity = ErrorSeverity::Blocking;
    } else if (normalized == QStringLiteral("critical")) {
        *severity = ErrorSeverity::Critical;
    } else {
        return false;
    }

    return true;
}

bool parseErrorStatus(const QString& text, ErrorStatus* const status)
{
    if (status == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("active")) {
        *status = ErrorStatus::Active;
    } else if (normalized == QStringLiteral("resolved")) {
        *status = ErrorStatus::Resolved;
    } else if (normalized == QStringLiteral("acknowledged")) {
        *status = ErrorStatus::Acknowledged;
    } else {
        return false;
    }

    return true;
}

QJsonObject toJson(const ErrorRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("error_id"), record.errorId);
    object.insert(QStringLiteral("code"), record.code);
    object.insert(QStringLiteral("source"), record.source);
    object.insert(QStringLiteral("module"), record.module);
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("severity"), toString(record.severity));
    object.insert(QStringLiteral("status"), toString(record.status));
    object.insert(QStringLiteral("message"), record.message);
    object.insert(QStringLiteral("detail"), record.detail);
    object.insert(QStringLiteral("created_at"), record.createdAt);
    object.insert(QStringLiteral("updated_at"), record.updatedAt);
    object.insert(QStringLiteral("resolved_at"), record.resolvedAt);
    object.insert(QStringLiteral("resolution"), record.resolution);
    object.insert(QStringLiteral("context"), record.context);
    return object;
}

QJsonObject toJson(const LogEntry& entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("scope"), toString(entry.scope));
    object.insert(QStringLiteral("task_id"), entry.taskId);
    object.insert(QStringLiteral("source"), entry.source);
    object.insert(QStringLiteral("level"), entry.level);
    object.insert(QStringLiteral("message"), entry.message);
    object.insert(QStringLiteral("created_at"), currentTimeText());
    object.insert(QStringLiteral("context"), entry.context);
    return object;
}

QJsonObject toJson(const MaintenanceCheckItem& item)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("state"), toString(item.state));
    object.insert(QStringLiteral("message"), item.message);
    object.insert(QStringLiteral("detail"), item.detail);
    return object;
}

QJsonObject toJson(const MaintenanceReport& report)
{
    QJsonArray items;
    for (const MaintenanceCheckItem& item : report.items) {
        items.append(toJson(item));
    }

    QJsonObject object;
    object.insert(QStringLiteral("state"), toString(report.state));
    object.insert(QStringLiteral("items"), items);
    object.insert(QStringLiteral("blocking_error_ids"), stringListToJson(report.blockingErrorIds));
    return object;
}

ErrorRecord ErrorRegistry::createRecord(const ErrorEvent& event) const
{
    ErrorRecord record;
    record.errorId = makeErrorId(event);
    record.code = event.code;
    record.source = event.source;
    record.module = event.module;
    record.taskId = event.taskId;
    record.severity = event.severity;
    record.status = ErrorStatus::Active;
    record.message = event.message;
    record.detail = event.detail;
    record.createdAt = currentTimeText();
    record.updatedAt = record.createdAt;
    record.context = event.context;
    return record;
}

bool ErrorRegistry::appendTaskError(
    const QString& taskRootPath,
    const ErrorEvent& event,
    ErrorRecord* const record,
    QString* const errorMessage) const
{
    QList<ErrorRecord> records;
    if (!loadTaskErrors(taskRootPath, &records, errorMessage)) {
        return false;
    }

    const ErrorRecord created = createRecord(event);
    records.append(created);
    if (!writeTaskErrors(taskRootPath, records, errorMessage)) {
        return false;
    }

    LogEntry entry;
    entry.scope = LogScope::Task;
    entry.taskId = created.taskId;
    entry.source = created.source;
    entry.level = toString(created.severity);
    entry.message = created.message;
    entry.context = toJson(created);
    if (!appendLog(taskLogRootPath(taskRootPath), entry, errorMessage)) {
        return false;
    }

    if (record != nullptr) {
        *record = created;
    }
    return true;
}

bool ErrorRegistry::appendTaskErrorsBulk(
    const QString& taskRootPath,
    const QList<ErrorEvent>& events,
    QList<ErrorRecord>* const records,
    QString* const errorMessage) const
{
    if (records != nullptr) {
        records->clear();
    }
    if (events.isEmpty()) {
        return true;
    }

    QList<ErrorRecord> existingRecords;
    if (!loadTaskErrors(taskRootPath, &existingRecords, errorMessage)) {
        return false;
    }

    QList<ErrorRecord> createdRecords;
    for (const ErrorEvent& event : events) {
        const ErrorRecord created = createRecord(event);
        existingRecords.append(created);
        createdRecords.append(created);
    }

    if (!writeTaskErrors(taskRootPath, existingRecords, errorMessage)) {
        return false;
    }

    for (const ErrorRecord& created : createdRecords) {
        LogEntry entry;
        entry.scope = LogScope::Task;
        entry.taskId = created.taskId;
        entry.source = created.source;
        entry.level = toString(created.severity);
        entry.message = created.message;
        entry.context = toJson(created);
        if (!appendLog(taskLogRootPath(taskRootPath), entry, errorMessage)) {
            return false;
        }
    }

    if (records != nullptr) {
        *records = createdRecords;
    }
    return true;
}

bool ErrorRegistry::appendSystemError(
    const QString& systemLogRootPath,
    const ErrorEvent& event,
    ErrorRecord* const record,
    QString* const errorMessage) const
{
    QList<ErrorRecord> records;
    if (!readRecordFile(systemErrorsPath(systemLogRootPath), &records, errorMessage)) {
        return false;
    }

    const ErrorRecord created = createRecord(event);
    records.append(created);
    if (!writeRecordFile(systemErrorsPath(systemLogRootPath), records, errorMessage)) {
        return false;
    }

    LogEntry entry;
    entry.scope = LogScope::System;
    entry.taskId = created.taskId;
    entry.source = created.source;
    entry.level = toString(created.severity);
    entry.message = created.message;
    entry.context = toJson(created);
    if (!appendLog(systemLogRootPath, entry, errorMessage)) {
        return false;
    }

    if (record != nullptr) {
        *record = created;
    }
    return true;
}

bool ErrorRegistry::appendLog(
    const QString& logRootPath,
    const LogEntry& entry,
    QString* const errorMessage) const
{
    const char* name = (entry.scope == LogScope::System) ? kSystemLogName : kTaskLogName;
    return appendJsonLine(QDir(logRootPath).filePath(QString::fromLatin1(name)), toJson(entry), errorMessage);
}

bool ErrorRegistry::loadTaskErrors(
    const QString& taskRootPath,
    QList<ErrorRecord>* const records,
    QString* const errorMessage) const
{
    return readRecordFile(taskErrorsPath(taskRootPath), records, errorMessage);
}

bool ErrorRegistry::writeTaskErrors(
    const QString& taskRootPath,
    const QList<ErrorRecord>& records,
    QString* const errorMessage) const
{
    return writeRecordFile(taskErrorsPath(taskRootPath), records, errorMessage);
}

bool ErrorRegistry::resolveTaskError(
    const QString& taskRootPath,
    const QString& errorId,
    const QString& resolution,
    QString* const errorMessage) const
{
    QList<ErrorRecord> records;
    if (!loadTaskErrors(taskRootPath, &records, errorMessage)) {
        return false;
    }

    bool found = false;
    const QString now = currentTimeText();
    for (ErrorRecord& record : records) {
        if (record.errorId == errorId) {
            record.status = ErrorStatus::Resolved;
            record.updatedAt = now;
            record.resolvedAt = now;
            record.resolution = resolution;
            found = true;
            break;
        }
    }

    if (!found) {
        setError(errorMessage, QStringLiteral("错误记录不存在: %1").arg(errorId));
        return false;
    }

    return writeTaskErrors(taskRootPath, records, errorMessage);
}

QStringList ErrorRegistry::unresolvedBlockingErrors(const QList<ErrorRecord>& records) const
{
    QStringList ids;
    for (const ErrorRecord& record : records) {
        if (record.status != ErrorStatus::Resolved && isBlockingSeverity(record.severity)) {
            ids.append(record.errorId);
        }
    }
    return ids;
}

bool ErrorRegistry::canContinueWith(
    const QList<ErrorRecord>& records,
    QString* const reason) const
{
    const QStringList ids = unresolvedBlockingErrors(records);
    if (!ids.isEmpty()) {
        setError(reason, QStringLiteral("存在未修复错误，不能忽略: %1").arg(ids.join(QChar::fromLatin1(','))));
        return false;
    }

    return true;
}

bool ErrorRegistry::canAcknowledgeWithoutResolution(const ErrorRecord& record) const
{
    // warning/info 可确认后继续；error 及以上必须修复为 resolved。
    return !isBlockingSeverity(record.severity);
}

MaintenanceReport ErrorRegistry::runMaintenanceCheck(
    const QString& taskRootPath,
    const QString& systemLogRootPath,
    const QList<ErrorRecord>& records) const
{
    MaintenanceReport report;
    report.state = MaintenanceState::Passed;

    const QFileInfo taskInfo(taskRootPath);
    const QFileInfo systemInfo(systemLogRootPath);

    const MaintenanceCheckItem taskItem = taskInfo.exists() && taskInfo.isDir()
        ? makeCheckItem(QStringLiteral("task_log_root"), MaintenanceState::Passed, QStringLiteral("任务日志根目录可用"))
        : makeCheckItem(QStringLiteral("task_log_root"), MaintenanceState::Warning, QStringLiteral("任务日志根目录尚未创建"));
    report.items.append(taskItem);
    report.state = combineState(report.state, taskItem.state);

    const MaintenanceCheckItem systemItem = systemInfo.exists() && systemInfo.isDir()
        ? makeCheckItem(QStringLiteral("system_log_root"), MaintenanceState::Passed, QStringLiteral("系统日志根目录可用"))
        : makeCheckItem(QStringLiteral("system_log_root"), MaintenanceState::Warning, QStringLiteral("系统日志根目录尚未创建"));
    report.items.append(systemItem);
    report.state = combineState(report.state, systemItem.state);

    report.blockingErrorIds = unresolvedBlockingErrors(records);
    const MaintenanceCheckItem errorsItem = report.blockingErrorIds.isEmpty()
        ? makeCheckItem(QStringLiteral("unresolved_errors"), MaintenanceState::Passed, QStringLiteral("无未修复阻断错误"))
        : makeCheckItem(QStringLiteral("unresolved_errors"), MaintenanceState::Failed, QStringLiteral("存在未修复错误，不能忽略"));
    report.items.append(errorsItem);
    report.state = combineState(report.state, errorsItem.state);

    return report;
}

}  // namespace lockstep::error_handling
