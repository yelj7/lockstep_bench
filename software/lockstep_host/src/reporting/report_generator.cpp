/*****************************************************************************
*  @file      report_generator.cpp
*  @brief     数据归档与报告生成模块实现
*  Details.   实现数据归档与报告生成模块的业务逻辑、状态转换和文件访问流程。
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

#include "report_generator.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace lockstep::reporting {
namespace {

constexpr char kReportsName[] = "reports";
constexpr char kReportJsonName[] = "report.json";

QJsonObject evidenceToJson(const RequiredEvidence& evidence)
{
    QJsonObject object;
    object.insert(QStringLiteral("program_write"), evidence.programWritePassed ? QStringLiteral("passed") : QStringLiteral("missing_or_failed"));
    object.insert(QStringLiteral("readback_verify"), evidence.readbackVerifyPassed ? QStringLiteral("passed") : QStringLiteral("missing_or_failed"));
    object.insert(QStringLiteral("run_control"), evidence.runControlReturned ? QStringLiteral("passed") : QStringLiteral("missing_or_failed"));
    object.insert(QStringLiteral("program_write_record_path"), evidence.programWriteRecordPath);
    object.insert(QStringLiteral("readback_verify_record_path"), evidence.readbackVerifyRecordPath);
    object.insert(QStringLiteral("run_control_record_path"), evidence.runControlRecordPath);
    return object;
}

QJsonObject optionalToJson(const OptionalRecords& records)
{
    QJsonObject object;
    object.insert(QStringLiteral("vcd_waveform"), toString(records.vcdWaveform));
    object.insert(QStringLiteral("protocol_analysis"), toString(records.protocolAnalysis));
    object.insert(QStringLiteral("acquisition"), toString(records.acquisition));
    object.insert(QStringLiteral("fault_injection"), toString(records.faultInjection));
    return object;
}

QJsonArray warningsToJson(const QList<WarningRecord>& warnings)
{
    QJsonArray array;
    for (const WarningRecord& warning : warnings) {
        QJsonObject object;
        object.insert(QStringLiteral("code"), warning.code);
        object.insert(QStringLiteral("source"), warning.source);
        object.insert(QStringLiteral("message"), warning.message);
        array.append(object);
    }
    return array;
}

QJsonArray stringListToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

bool writeJson(const QString& path, const QJsonObject& object, QString* const errorMessage)
{
    const QFileInfo info(path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建报告目录: %1").arg(info.absolutePath());
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入报告: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument document(object);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告写入不完整: %1").arg(path);
        }
        return false;
    }

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告提交失败: %1").arg(path);
        }
        return false;
    }

    return true;
}

}  // namespace

QString toString(const ReportConclusion conclusion)
{
    QString text;
    switch (conclusion) {
    case ReportConclusion::Pass:
        text = QStringLiteral("pass");
        break;
    case ReportConclusion::Fail:
        text = QStringLiteral("fail");
        break;
    case ReportConclusion::Blocked:
        text = QStringLiteral("blocked");
        break;
    case ReportConclusion::Incomplete:
    default:
        text = QStringLiteral("incomplete");
        break;
    }
    return text;
}

QString toString(const OptionalRecordState state)
{
    QString text;
    switch (state) {
    case OptionalRecordState::Available:
        text = QStringLiteral("available");
        break;
    case OptionalRecordState::Skipped:
        text = QStringLiteral("skipped");
        break;
    case OptionalRecordState::Failed:
        text = QStringLiteral("failed");
        break;
    case OptionalRecordState::NotAvailable:
    default:
        text = QStringLiteral("not_available");
        break;
    }
    return text;
}

QJsonObject toJson(
    const ReportInput& input,
    const ReportConclusion conclusion,
    const QStringList& reasons)
{
    QJsonObject object;
    object.insert(QStringLiteral("report_id"), input.reportId);
    object.insert(QStringLiteral("task_id"), input.taskId);
    object.insert(QStringLiteral("mode"), input.mode);
    object.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("conclusion"), toString(conclusion));
    object.insert(QStringLiteral("required_evidence"), evidenceToJson(input.requiredEvidence));
    object.insert(QStringLiteral("optional_records"), optionalToJson(input.optionalRecords));
    object.insert(QStringLiteral("warnings"), warningsToJson(input.warnings));
    object.insert(QStringLiteral("unresolved_blocking_errors"), stringListToJson(input.unresolvedBlockingErrors));
    object.insert(QStringLiteral("reasons"), stringListToJson(reasons));
    object.insert(QStringLiteral("resource_snapshot"), input.resourceSnapshot);
    return object;
}

ReportConclusion ReportGenerator::calculateConclusion(
    const ReportInput& input,
    QStringList* const reasons) const
{
    QStringList localReasons;

    if (!input.unresolvedBlockingErrors.isEmpty()) {
        localReasons.append(QStringLiteral("存在未修复阻断错误"));
        if (reasons != nullptr) {
            *reasons = localReasons;
        }
        return ReportConclusion::Blocked;
    }

    if (!input.requiredEvidence.programWritePassed) {
        localReasons.append(QStringLiteral("缺少烧写成功记录"));
    }
    if (!input.requiredEvidence.readbackVerifyPassed) {
        localReasons.append(QStringLiteral("缺少回读校验通过记录"));
    }
    if (!input.requiredEvidence.runControlReturned) {
        localReasons.append(QStringLiteral("缺少运行控制真实返回或快照"));
    }

    if (!localReasons.isEmpty()) {
        if (reasons != nullptr) {
            *reasons = localReasons;
        }
        return ReportConclusion::Incomplete;
    }

    localReasons.append(QStringLiteral("程序跑通强制证据齐全"));
    if (reasons != nullptr) {
        *reasons = localReasons;
    }
    return ReportConclusion::Pass;
}

ReportResult ReportGenerator::generateReport(
    const QString& taskRootPath,
    const ReportInput& input) const
{
    ReportResult result;
    QStringList reasons;
    result.conclusion = calculateConclusion(input, &reasons);
    result.reasons = reasons;

    const QString reportPath = QDir(QDir(taskRootPath).filePath(QString::fromLatin1(kReportsName)))
        .filePath(QString::fromLatin1(kReportJsonName));
    const QJsonObject object = toJson(input, result.conclusion, reasons);
    QString error;
    if (!writeJson(reportPath, object, &error)) {
        result.success = false;
        result.errorMessage = error;
        return result;
    }

    result.success = true;
    result.reportPath = reportPath;
    return result;
}

}  // namespace lockstep::reporting
