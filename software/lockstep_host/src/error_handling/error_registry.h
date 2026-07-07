/*****************************************************************************
*  @file      error_registry.h
*  @brief     错误处理与日志登记模块接口
*  Details.   声明错误处理与日志登记模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_ERROR_HANDLING_ERROR_REGISTRY_H_
#define LOCKSTEP_HOST_SRC_ERROR_HANDLING_ERROR_REGISTRY_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::error_handling {

enum class ErrorSeverity : unsigned char {
    Info = 0U,
    Warning = 1U,
    Error = 2U,
    Blocking = 3U,
    Critical = 4U
};

enum class ErrorStatus : unsigned char {
    Active = 0U,
    Resolved = 1U,
    Acknowledged = 2U
};

enum class LogScope : unsigned char {
    Task = 0U,
    System = 1U
};

enum class MaintenanceState : unsigned char {
    Passed = 0U,
    Warning = 1U,
    Failed = 2U
};

struct ErrorEvent final {
    QString code;
    QString source;
    QString module;
    QString taskId;
    ErrorSeverity severity = ErrorSeverity::Error;
    QString message;
    QString detail;
    QJsonObject context;
};

struct ErrorRecord final {
    QString errorId;
    QString code;
    QString source;
    QString module;
    QString taskId;
    ErrorSeverity severity = ErrorSeverity::Error;
    ErrorStatus status = ErrorStatus::Active;
    QString message;
    QString detail;
    QString createdAt;
    QString updatedAt;
    QString resolvedAt;
    QString resolution;
    QJsonObject context;
};

struct LogEntry final {
    LogScope scope = LogScope::Task;
    QString taskId;
    QString source;
    QString level;
    QString message;
    QJsonObject context;
};

struct MaintenanceCheckItem final {
    QString id;
    MaintenanceState state = MaintenanceState::Passed;
    QString message;
    QJsonObject detail;
};

struct MaintenanceReport final {
    MaintenanceState state = MaintenanceState::Passed;
    QList<MaintenanceCheckItem> items;
    QStringList blockingErrorIds;
};

QString toString(ErrorSeverity severity);
QString toString(ErrorStatus status);
QString toString(LogScope scope);
QString toString(MaintenanceState state);

bool parseErrorSeverity(const QString& text, ErrorSeverity* severity);
bool parseErrorStatus(const QString& text, ErrorStatus* status);

QJsonObject toJson(const ErrorRecord& record);
QJsonObject toJson(const LogEntry& entry);
QJsonObject toJson(const MaintenanceCheckItem& item);
QJsonObject toJson(const MaintenanceReport& report);

class ErrorRegistry final {
public:
    ErrorRegistry() = default;

    ErrorRecord createRecord(const ErrorEvent& event) const;

    bool appendTaskError(
        const QString& taskRootPath,
        const ErrorEvent& event,
        ErrorRecord* record,
        QString* errorMessage = nullptr) const;

    bool appendSystemError(
        const QString& systemLogRootPath,
        const ErrorEvent& event,
        ErrorRecord* record,
        QString* errorMessage = nullptr) const;

    bool appendLog(
        const QString& logRootPath,
        const LogEntry& entry,
        QString* errorMessage = nullptr) const;

    bool loadTaskErrors(
        const QString& taskRootPath,
        QList<ErrorRecord>* records,
        QString* errorMessage = nullptr) const;

    bool writeTaskErrors(
        const QString& taskRootPath,
        const QList<ErrorRecord>& records,
        QString* errorMessage = nullptr) const;

    bool resolveTaskError(
        const QString& taskRootPath,
        const QString& errorId,
        const QString& resolution,
        QString* errorMessage = nullptr) const;

    QStringList unresolvedBlockingErrors(const QList<ErrorRecord>& records) const;
    bool canContinueWith(const QList<ErrorRecord>& records, QString* reason = nullptr) const;
    bool canAcknowledgeWithoutResolution(const ErrorRecord& record) const;

    MaintenanceReport runMaintenanceCheck(
        const QString& taskRootPath,
        const QString& systemLogRootPath,
        const QList<ErrorRecord>& records) const;
};

}  // namespace lockstep::error_handling

#endif  // LOCKSTEP_HOST_SRC_ERROR_HANDLING_ERROR_REGISTRY_H_
