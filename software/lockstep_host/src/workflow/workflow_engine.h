/*****************************************************************************
*  @file      workflow_engine.h
*  @brief     研发测试流程编排模块接口
*  Details.   声明研发测试流程编排模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_WORKFLOW_WORKFLOW_ENGINE_H_
#define LOCKSTEP_HOST_SRC_WORKFLOW_WORKFLOW_ENGINE_H_

#include <QJsonObject>
#include <QList>
#include <QString>

namespace lockstep::workflow {

enum class FlowMode : unsigned char {
    Research = 0U,
    Test = 1U
};

enum class Stage : unsigned char {
    Task = 0U,
    Connection = 1U,
    ProgramWrite = 2U,
    ReadbackVerify = 3U,
    FaultInjection = 4U,
    AcquisitionConfig = 5U,
    RunControl = 6U,
    Waveform = 7U,
    ProtocolAnalysis = 8U,
    Report = 9U
};

enum class StageStatus : unsigned char {
    NotStarted = 0U,
    Ready = 1U,
    Running = 2U,
    Passed = 3U,
    Failed = 4U,
    Blocked = 5U,
    Skipped = 6U,
    Warning = 7U
};

struct StageRecord final {
    Stage stage = Stage::Task;
    StageStatus status = StageStatus::NotStarted;
    QString reason;
    QJsonObject detail;
};

struct FlowFacts final {
    bool hasTask = false;
    bool connectionReady = false;
    bool programWritten = false;
    bool readbackPassed = false;
    bool faultInjectionConfigured = false;
    bool faultInjectionPassed = false;
    bool hasBlockingError = false;
};

struct ActionGate final {
    bool allowed = false;
    QString reason;
};

struct FlowState final {
    QString taskId;
    FlowMode mode = FlowMode::Research;
    QList<StageRecord> stages;
};

QString toString(FlowMode mode);
QString toString(Stage stage);
QString toString(StageStatus status);

QJsonObject toJson(const StageRecord& record);
QJsonObject toJson(const FlowState& state);

class WorkflowEngine final {
public:
    FlowState startFlow(const QString& taskId, FlowMode mode) const;
    FlowState recordStageResult(
        const FlowState& state,
        Stage stage,
        StageStatus status,
        const QString& reason,
        const QJsonObject& detail) const;

    ActionGate canExecute(Stage action, const FlowFacts& facts) const;
    StageStatus faultInjectionStatus(const FlowFacts& facts) const;
};

}  // namespace lockstep::workflow

#endif  // LOCKSTEP_HOST_SRC_WORKFLOW_WORKFLOW_ENGINE_H_
