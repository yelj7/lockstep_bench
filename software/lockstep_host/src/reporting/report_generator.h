/*****************************************************************************
*  @file      report_generator.h
*  @brief     数据归档与报告生成模块接口
*  Details.   声明数据归档与报告生成模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_
#define LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::reporting {

enum class ReportConclusion : unsigned char {
    Pass = 0U,
    Fail = 1U,
    Incomplete = 2U,
    Blocked = 3U
};

enum class OptionalRecordState : unsigned char {
    Available = 0U,
    NotAvailable = 1U,
    Skipped = 2U,
    Failed = 3U
};

struct RequiredEvidence final {
    bool programWritePassed = false;
    bool readbackVerifyPassed = false;
    bool runControlReturned = false;
    QString programWriteRecordPath;
    QString readbackVerifyRecordPath;
    QString runControlRecordPath;
};

struct OptionalRecords final {
    OptionalRecordState vcdWaveform = OptionalRecordState::NotAvailable;
    OptionalRecordState protocolAnalysis = OptionalRecordState::NotAvailable;
    OptionalRecordState acquisition = OptionalRecordState::NotAvailable;
    OptionalRecordState faultInjection = OptionalRecordState::Skipped;
};

struct WarningRecord final {
    QString code;
    QString source;
    QString message;
};

struct ReportInput final {
    QString reportId;
    QString taskId;
    QString mode;
    RequiredEvidence requiredEvidence;
    OptionalRecords optionalRecords;
    QList<WarningRecord> warnings;
    QStringList unresolvedBlockingErrors;
    QJsonObject resourceSnapshot;
};

struct ReportResult final {
    bool success = false;
    QString reportPath;
    ReportConclusion conclusion = ReportConclusion::Incomplete;
    QStringList reasons;
    QString errorMessage;
};

QString toString(ReportConclusion conclusion);
QString toString(OptionalRecordState state);
QJsonObject toJson(const ReportInput& input, ReportConclusion conclusion, const QStringList& reasons);

class ReportGenerator final {
public:
    ReportConclusion calculateConclusion(const ReportInput& input, QStringList* reasons) const;
    ReportResult generateReport(
        const QString& taskRootPath,
        const ReportInput& input) const;
};

}  // namespace lockstep::reporting

#endif  // LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_
