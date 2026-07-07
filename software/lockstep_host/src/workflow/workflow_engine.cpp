/*****************************************************************************
*  @file      workflow_engine.cpp
*  @brief     研发测试流程编排模块实现
*  Details.   实现研发测试流程编排模块的业务逻辑、状态转换和文件访问流程。
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

#include "workflow_engine.h"

#include <QJsonArray>

namespace lockstep::workflow {
namespace {

QList<Stage> stageOrder(const FlowMode mode)
{
    QList<Stage> stages = {
        Stage::Task,
        Stage::Connection,
        Stage::ProgramWrite,
        Stage::ReadbackVerify
    };

    if (mode == FlowMode::Test) {
        stages.append(Stage::FaultInjection);
    }

    stages.append(Stage::AcquisitionConfig);
    stages.append(Stage::RunControl);
    stages.append(Stage::Waveform);
    stages.append(Stage::ProtocolAnalysis);
    stages.append(Stage::Report);
    return stages;
}

ActionGate allowedGate()
{
    ActionGate gate;
    gate.allowed = true;
    return gate;
}

ActionGate blockedGate(const QString& reason)
{
    ActionGate gate;
    gate.allowed = false;
    gate.reason = reason;
    return gate;
}

}  // namespace

QString toString(const FlowMode mode)
{
    return (mode == FlowMode::Test) ? QStringLiteral("test") : QStringLiteral("research");
}

QString toString(const Stage stage)
{
    QString text;
    switch (stage) {
    case Stage::Task:
        text = QStringLiteral("task");
        break;
    case Stage::Connection:
        text = QStringLiteral("connection");
        break;
    case Stage::ProgramWrite:
        text = QStringLiteral("program_write");
        break;
    case Stage::ReadbackVerify:
        text = QStringLiteral("readback_verify");
        break;
    case Stage::FaultInjection:
        text = QStringLiteral("fault_injection");
        break;
    case Stage::AcquisitionConfig:
        text = QStringLiteral("acquisition_config");
        break;
    case Stage::RunControl:
        text = QStringLiteral("run_control");
        break;
    case Stage::Waveform:
        text = QStringLiteral("waveform");
        break;
    case Stage::ProtocolAnalysis:
        text = QStringLiteral("protocol_analysis");
        break;
    case Stage::Report:
        text = QStringLiteral("report");
        break;
    default:
        text = QStringLiteral("task");
        break;
    }
    return text;
}

QString toString(const StageStatus status)
{
    QString text;
    switch (status) {
    case StageStatus::Ready:
        text = QStringLiteral("ready");
        break;
    case StageStatus::Running:
        text = QStringLiteral("running");
        break;
    case StageStatus::Passed:
        text = QStringLiteral("passed");
        break;
    case StageStatus::Failed:
        text = QStringLiteral("failed");
        break;
    case StageStatus::Blocked:
        text = QStringLiteral("blocked");
        break;
    case StageStatus::Skipped:
        text = QStringLiteral("skipped");
        break;
    case StageStatus::Warning:
        text = QStringLiteral("warning");
        break;
    case StageStatus::NotStarted:
    default:
        text = QStringLiteral("not_started");
        break;
    }
    return text;
}

QJsonObject toJson(const StageRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("stage"), toString(record.stage));
    object.insert(QStringLiteral("status"), toString(record.status));
    object.insert(QStringLiteral("reason"), record.reason);
    object.insert(QStringLiteral("detail"), record.detail);
    return object;
}

QJsonObject toJson(const FlowState& state)
{
    QJsonArray stages;
    for (const StageRecord& stage : state.stages) {
        stages.append(toJson(stage));
    }

    QJsonObject object;
    object.insert(QStringLiteral("task_id"), state.taskId);
    object.insert(QStringLiteral("mode"), toString(state.mode));
    object.insert(QStringLiteral("stages"), stages);
    return object;
}

FlowState WorkflowEngine::startFlow(const QString& taskId, const FlowMode mode) const
{
    FlowState state;
    state.taskId = taskId;
    state.mode = mode;

    const QList<Stage> stages = stageOrder(mode);
    for (const Stage stage : stages) {
        StageRecord record;
        record.stage = stage;
        record.status = (stage == Stage::Task) ? StageStatus::Ready : StageStatus::NotStarted;
        state.stages.append(record);
    }

    return state;
}

FlowState WorkflowEngine::recordStageResult(
    const FlowState& state,
    const Stage stage,
    const StageStatus status,
    const QString& reason,
    const QJsonObject& detail) const
{
    FlowState next = state;
    bool found = false;
    for (StageRecord& record : next.stages) {
        if (record.stage == stage) {
            record.status = status;
            record.reason = reason;
            record.detail = detail;
            found = true;
            break;
        }
    }

    if (!found) {
        StageRecord record;
        record.stage = stage;
        record.status = status;
        record.reason = reason;
        record.detail = detail;
        next.stages.append(record);
    }

    return next;
}

ActionGate WorkflowEngine::canExecute(const Stage action, const FlowFacts& facts) const
{
    if (facts.hasBlockingError) {
        return blockedGate(QStringLiteral("存在未修复阻断错误"));
    }

    switch (action) {
    case Stage::Task:
        return allowedGate();
    case Stage::Connection:
        return facts.hasTask ? allowedGate() : blockedGate(QStringLiteral("尚未创建任务"));
    case Stage::ProgramWrite:
    case Stage::ReadbackVerify:
        return (facts.hasTask && facts.connectionReady)
            ? allowedGate()
            : blockedGate(QStringLiteral("连接或预检未通过"));
    case Stage::FaultInjection:
        if (!facts.faultInjectionConfigured) {
            return blockedGate(QStringLiteral("未配置故障注入，允许跳过"));
        }
        return facts.readbackPassed ? allowedGate() : blockedGate(QStringLiteral("回读未通过"));
    case Stage::AcquisitionConfig:
    case Stage::RunControl:
        return facts.readbackPassed ? allowedGate() : blockedGate(QStringLiteral("回读未通过，禁止运行"));
    case Stage::Waveform:
    case Stage::ProtocolAnalysis:
    case Stage::Report:
        return facts.hasTask ? allowedGate() : blockedGate(QStringLiteral("尚未创建任务"));
    default:
        return blockedGate(QStringLiteral("未知流程动作"));
    }
}

StageStatus WorkflowEngine::faultInjectionStatus(const FlowFacts& facts) const
{
    if (!facts.faultInjectionConfigured) {
        return StageStatus::Skipped;
    }
    return facts.faultInjectionPassed ? StageStatus::Passed : StageStatus::Failed;
}

}  // namespace lockstep::workflow
