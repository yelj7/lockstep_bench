/**********************************************************
* 文件名: report_generator.h
* 日期: 2026-07-14
* 版本: v2.0
* 更新记录: 增加统一报告模型、证据状态和版本化归档接口
* 描述: 声明测试报告结论计算、预览、读取和归档生成接口
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_
#define LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_

#include <QJsonArray>
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

enum class EvidenceState : unsigned char {
    NotRun = 0U,
    Missing = 1U,
    Passed = 2U,
    Failed = 3U
};

struct ReportEvidence final {
    EvidenceState state = EvidenceState::NotRun;
    QString summary;
    QString recordedAt;
    QString recordPath;
    QString sha256;
    QJsonObject metrics;
    QStringList errorIds;
};

struct ReportRequiredEvidence final {
    ReportEvidence programWrite;
    ReportEvidence readbackVerify;
    ReportEvidence runControl;
};

struct ReportDiagnostic final {
    QString id;
    QString code;
    QString severity;
    QString source;
    QString message;
    QString suggestion;
    QString occurredAt;
};

struct ReportOptionalRecord final {
    OptionalRecordState state = OptionalRecordState::NotAvailable;
    QString summary;
    QString recordedAt;
    QString path;
    QString sha256;
    int diagnosticCount = 0;
};

struct ReportOptionalRecords final {
    ReportOptionalRecord vcdWaveform;
    ReportOptionalRecord protocolAnalysis;
    ReportOptionalRecord acquisition;
    ReportOptionalRecord faultInjection;
};

struct ReportDocumentModel final {
    QString schemaVersion = QStringLiteral("2.0");
    QString generatorVersion = QStringLiteral("0.1.0");
    QString reportId;
    int revision = 1;
    QString taskId;
    QString taskName;
    QString taskDescription;
    QString mode;
    QString generatedAt;
    QString inputDigest;
    QString programFileName;
    QString programRelativePath;
    QString programSha256;
    QString entryAddress;
    QString targetSummary;
    ReportRequiredEvidence requiredEvidence;
    ReportOptionalRecords optionalRecords;
    QList<ReportDiagnostic> warnings;
    QList<ReportDiagnostic> blockingErrors;
    QJsonObject environment;
    QJsonObject resourceSnapshot;
    QJsonArray artifacts;
};

struct ReportResult final {
    bool success = false;
    QString reportPath;
    QString htmlPath;
    QString versionPath;
    QString reportSha256;
    ReportConclusion conclusion = ReportConclusion::Incomplete;
    QStringList reasons;
    QString errorMessage;
};

QString toString(ReportConclusion conclusion);
QString toString(OptionalRecordState state);
QString toString(EvidenceState state);
QJsonObject toJson(const ReportDocumentModel& model, ReportConclusion conclusion, const QStringList& reasons);

class ReportGenerator final {
public:
    ReportDocumentModel buildModelFromTask(
        const QString& taskRootPath,
        const ReportDocumentModel& context,
        QString* errorMessage = nullptr) const;
    ReportConclusion calculateConclusion(const ReportDocumentModel& model, QStringList* reasons) const;
    ReportResult generateReport(const QString& taskRootPath, const ReportDocumentModel& model) const;
    bool loadLatestReport(
        const QString& taskRootPath,
        ReportDocumentModel* model,
        ReportConclusion* conclusion,
        QStringList* reasons,
        QString* errorMessage = nullptr) const;
    QString calculateInputDigest(const ReportDocumentModel& model) const;
};

}  // namespace lockstep::reporting

#endif  // LOCKSTEP_HOST_SRC_REPORTING_REPORT_GENERATOR_H_
