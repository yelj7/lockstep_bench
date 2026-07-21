/**********************************************************
* 文件名: report_generator.cpp
* 日期: 2026-07-14
* 版本: v2.2
* 更新记录: 增加程序镜像摘要导入和采集编号一致性门禁
* 描述: 实现测试报告结论计算、序列化、归档发布和读取
**********************************************************/

#include "report_generator.h"

#include <cmath>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace lockstep::reporting {
namespace {

constexpr char kReportsName[] = "reports";
constexpr char kVersionsName[] = "versions";
constexpr char kReportJsonName[] = "report.json";
constexpr char kReportHtmlName[] = "report.html";
constexpr char kManifestName[] = "manifest.json";
constexpr char kLatestName[] = "latest.json";
constexpr char kProgramWritePath[] = "evidence/program_write_record.json";
constexpr char kReadbackVerifyPath[] = "evidence/readback_verify_record.json";
constexpr char kRunControlPath[] = "evidence/run_control_record.json";
constexpr char kArtifactIndexPath[] = "evidence/artifacts.json";
constexpr char kWaveformPath[] = "waveform/capture.vcd";
constexpr char kAnalysisPath[] = "evidence/protocol_analysis.json";
constexpr char kCaptureSidecarPath[] = "evidence/capture_sidecar.json";
constexpr char kProtocolEventsPath[] = "evidence/protocol_events.json";
constexpr char kCaptureStatusPath[] = "evidence/capture_status.json";
constexpr char kTemporaryReportRoot[] = "D:/tmp/lockstep/report-staging";

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QByteArray jsonBytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QString sha256(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool readObject(const QString& path, QJsonObject* const object)
{
    if (object == nullptr) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    *object = document.object();
    return true;
}

bool captureIdFromJson(const QJsonValue& value, quint32* const captureId)
{
    if (captureId == nullptr) {
        return false;
    }
    qulonglong parsed = 0U;
    if (value.isDouble()) {
        const double numeric = value.toDouble(-1.0);
        if (!std::isfinite(numeric) || numeric < 0.0 || numeric > 4294967295.0 ||
            std::floor(numeric) != numeric) {
            return false;
        }
        parsed = static_cast<qulonglong>(numeric);
    } else if (value.isString()) {
        bool ok = false;
        parsed = value.toString().toULongLong(&ok);
        if (!ok || parsed > 0xffffffffULL) {
            return false;
        }
    } else {
        return false;
    }
    *captureId = static_cast<quint32>(parsed);
    return true;
}

bool isSha256(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return expression.match(value).hasMatch();
}

bool isSafeRelativePath(const QString& path)
{
    if (path.isEmpty()) {
        return true;
    }
    const QString normalized = QDir::fromNativeSeparators(path);
    const QString clean = QDir::cleanPath(normalized);
    return !QDir::isAbsolutePath(clean) && clean != QStringLiteral("..") &&
        !clean.startsWith(QStringLiteral("../")) && !clean.contains(QStringLiteral(":"));
}

QString safePath(const QString& path)
{
    return isSafeRelativePath(path) ? QDir::fromNativeSeparators(QDir::cleanPath(path)) : QString();
}

QJsonObject sanitizedResourceSnapshot(const QJsonObject& source)
{
    static const QStringList allowedKeys = {
        QStringLiteral("resource_pack_id"), QStringLiteral("resource_pack_version"),
        QStringLiteral("manifest_sha256"), QStringLiteral("profile_id"),
        QStringLiteral("profile_sha256"), QStringLiteral("report_template_id"),
        QStringLiteral("report_template_sha256"), QStringLiteral("debug_adapter_id"),
        QStringLiteral("debug_adapter_status"), QStringLiteral("protocol_rule_id"),
        QStringLiteral("protocol_rule_status"), QStringLiteral("trace_profile_id"),
        QStringLiteral("trace_profile_status")
    };
    QJsonObject result;
    for (const QString& key : allowedKeys) {
        if (source.contains(key)) {
            result.insert(key, source.value(key));
        }
    }
    return result;
}

QJsonObject sanitizedEnvironment(const QJsonObject& source)
{
    static const QStringList allowedKeys = {
        QStringLiteral("application_version"), QStringLiteral("qt_version"),
        QStringLiteral("os"), QStringLiteral("cpu_architecture")
    };
    QJsonObject result;
    for (const QString& key : allowedKeys) {
        if (source.contains(key)) {
            result.insert(key, source.value(key));
        }
    }
    return result;
}

QJsonArray sanitizedArtifacts(const QJsonArray& source)
{
    QJsonArray result;
    for (const QJsonValue& value : source) {
        const QJsonObject artifact = value.toObject();
        const QString relativePath = safePath(artifact.value(QStringLiteral("relative_path")).toString());
        if (relativePath.isEmpty()) {
            continue;
        }
        QJsonObject clean;
        clean.insert(QStringLiteral("artifact_id"), artifact.value(QStringLiteral("artifact_id")).toString());
        clean.insert(QStringLiteral("kind"), artifact.value(QStringLiteral("kind")).toString());
        clean.insert(QStringLiteral("relative_path"), relativePath);
        clean.insert(QStringLiteral("name"), QFileInfo(relativePath).fileName());
        clean.insert(QStringLiteral("sha256"), artifact.value(QStringLiteral("sha256")).toString());
        clean.insert(QStringLiteral("size_bytes"), artifact.value(QStringLiteral("size_bytes")).toVariant().toLongLong());
        clean.insert(QStringLiteral("created_at"), artifact.value(QStringLiteral("created_at")).toString());
        result.append(clean);
    }
    return result;
}

QString safeReportId(const QString& value)
{
    QString id = value;
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (id.isEmpty() || id == QStringLiteral(".") || id == QStringLiteral("..")) {
        return QStringLiteral("report");
    }
    return id.left(120);
}

bool writeFile(const QString& path, const QByteArray& data, QString* const errorMessage)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建报告目录: %1").arg(info.absolutePath());
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入报告文件: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool publishLatestFiles(
    const QString& reportsPath,
    const QList<QPair<QString, QByteArray>>& files,
    QString* const errorMessage)
{
    QStringList preparedPaths;
    QStringList backupPaths;
    for (const auto& item : files) {
        const QString prepared = QDir(reportsPath).filePath(QStringLiteral(".next_%1").arg(item.first));
        QFile::remove(prepared);
        if (!writeFile(prepared, item.second, errorMessage)) {
            for (const QString& path : preparedPaths) {
                QFile::remove(path);
            }
            return false;
        }
        preparedPaths.append(prepared);
    }

    for (int index = 0; index < files.size(); ++index) {
        const QString current = QDir(reportsPath).filePath(files.at(index).first);
        const QString backup = QDir(reportsPath).filePath(QStringLiteral(".previous_%1").arg(files.at(index).first));
        QFile::remove(backup);
        backupPaths.append(backup);
        if (QFileInfo::exists(current) && !QFile::rename(current, backup)) {
            for (int rollback = 0; rollback < index; ++rollback) {
                const QString priorCurrent = QDir(reportsPath).filePath(files.at(rollback).first);
                QFile::rename(backupPaths.at(rollback), priorCurrent);
            }
            for (const QString& path : preparedPaths) {
                QFile::remove(path);
            }
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法暂存上一版报告: %1").arg(current);
            }
            return false;
        }
    }

    for (int index = 0; index < files.size(); ++index) {
        const QString current = QDir(reportsPath).filePath(files.at(index).first);
        if (!QFile::rename(preparedPaths.at(index), current)) {
            for (int published = 0; published < index; ++published) {
                QFile::remove(QDir(reportsPath).filePath(files.at(published).first));
            }
            for (int rollback = 0; rollback < files.size(); ++rollback) {
                const QString rollbackCurrent = QDir(reportsPath).filePath(files.at(rollback).first);
                if (QFileInfo::exists(backupPaths.at(rollback))) {
                    QFile::rename(backupPaths.at(rollback), rollbackCurrent);
                }
            }
            for (const QString& path : preparedPaths) {
                QFile::remove(path);
            }
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("无法发布最新版报告: %1").arg(current);
            }
            return false;
        }
    }
    for (const QString& backup : backupPaths) {
        QFile::remove(backup);
    }
    return true;
}

QJsonArray stringsToJson(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QJsonObject evidenceToJson(const ReportEvidence& evidence)
{
    QJsonObject object;
    object.insert(QStringLiteral("status"), toString(evidence.state));
    object.insert(QStringLiteral("summary"), evidence.summary);
    object.insert(QStringLiteral("recorded_at"), evidence.recordedAt);
    object.insert(QStringLiteral("record_path"), safePath(evidence.recordPath));
    object.insert(QStringLiteral("sha256"), evidence.sha256);
    object.insert(QStringLiteral("metrics"), evidence.metrics);
    object.insert(QStringLiteral("error_ids"), stringsToJson(evidence.errorIds));
    return object;
}

QJsonObject optionalToJson(const ReportOptionalRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("status"), toString(record.state));
    object.insert(QStringLiteral("summary"), record.summary);
    object.insert(QStringLiteral("recorded_at"), record.recordedAt);
    object.insert(QStringLiteral("path"), safePath(record.path));
    object.insert(QStringLiteral("sha256"), record.sha256);
    object.insert(QStringLiteral("diagnostic_count"), record.diagnosticCount);
    return object;
}

QJsonObject diagnosticToJson(const ReportDiagnostic& diagnostic)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), diagnostic.id);
    object.insert(QStringLiteral("code"), diagnostic.code);
    object.insert(QStringLiteral("severity"), diagnostic.severity);
    object.insert(QStringLiteral("source"), diagnostic.source);
    object.insert(QStringLiteral("message"), diagnostic.message);
    object.insert(QStringLiteral("suggestion"), diagnostic.suggestion);
    object.insert(QStringLiteral("occurred_at"), diagnostic.occurredAt);
    return object;
}

QJsonArray diagnosticsToJson(const QList<ReportDiagnostic>& diagnostics)
{
    QJsonArray array;
    for (const ReportDiagnostic& diagnostic : diagnostics) {
        array.append(diagnosticToJson(diagnostic));
    }
    return array;
}

QJsonObject requiredToJson(const ReportRequiredEvidence& evidence)
{
    QJsonObject object;
    object.insert(QStringLiteral("program_write"), evidenceToJson(evidence.programWrite));
    object.insert(QStringLiteral("readback_verify"), evidenceToJson(evidence.readbackVerify));
    object.insert(QStringLiteral("run_control"), evidenceToJson(evidence.runControl));
    return object;
}

QJsonObject optionalRecordsToJson(const ReportOptionalRecords& records)
{
    QJsonObject object;
    object.insert(QStringLiteral("vcd_waveform"), optionalToJson(records.vcdWaveform));
    object.insert(QStringLiteral("protocol_analysis"), optionalToJson(records.protocolAnalysis));
    object.insert(QStringLiteral("acquisition"), optionalToJson(records.acquisition));
    object.insert(QStringLiteral("fault_injection"), optionalToJson(records.faultInjection));
    return object;
}

QJsonObject normalizedInput(const ReportDocumentModel& model)
{
    QJsonObject object;
    object.insert(QStringLiteral("task_id"), model.taskId);
    object.insert(QStringLiteral("task_name"), model.taskName);
    object.insert(QStringLiteral("task_description"), model.taskDescription);
    object.insert(QStringLiteral("mode"), model.mode);
    object.insert(QStringLiteral("program_file_name"), model.programFileName);
    object.insert(QStringLiteral("program_sha256"), model.programSha256);
    object.insert(QStringLiteral("entry_address"), model.entryAddress);
    object.insert(QStringLiteral("capture_id"), model.hasCaptureId
        ? QJsonValue(static_cast<double>(model.captureId)) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("target_summary"), model.targetSummary);
    object.insert(QStringLiteral("required_evidence"), requiredToJson(model.requiredEvidence));
    object.insert(QStringLiteral("optional_records"), optionalRecordsToJson(model.optionalRecords));
    object.insert(QStringLiteral("warnings"), diagnosticsToJson(model.warnings));
    object.insert(QStringLiteral("blocking_errors"), diagnosticsToJson(model.blockingErrors));
    object.insert(QStringLiteral("environment"), sanitizedEnvironment(model.environment));
    object.insert(QStringLiteral("resource_snapshot"), sanitizedResourceSnapshot(model.resourceSnapshot));
    object.insert(QStringLiteral("artifacts"), sanitizedArtifacts(model.artifacts));
    return object;
}

QString conclusionDisplay(const ReportConclusion conclusion)
{
    switch (conclusion) {
    case ReportConclusion::Pass:
        return QStringLiteral("通过");
    case ReportConclusion::Fail:
        return QStringLiteral("失败");
    case ReportConclusion::Blocked:
        return QStringLiteral("已阻断");
    case ReportConclusion::Incomplete:
    default:
        return QStringLiteral("未完成");
    }
}

QString evidenceDisplay(const EvidenceState state)
{
    switch (state) {
    case EvidenceState::Passed:
        return QStringLiteral("通过");
    case EvidenceState::Failed:
        return QStringLiteral("失败");
    case EvidenceState::Missing:
        return QStringLiteral("缺失");
    case EvidenceState::NotRun:
    default:
        return QStringLiteral("未执行");
    }
}

QString optionalDisplay(const OptionalRecordState state)
{
    switch (state) {
    case OptionalRecordState::Available:
        return QStringLiteral("可用");
    case OptionalRecordState::Failed:
        return QStringLiteral("失败");
    case OptionalRecordState::Skipped:
        return QStringLiteral("已跳过");
    case OptionalRecordState::NotAvailable:
    default:
        return QStringLiteral("不可用");
    }
}

QString evidenceRow(const QString& name, const ReportEvidence& evidence)
{
    return QStringLiteral("<tr><td>%1</td><td><span class=\"state\">%2</span></td><td>%3</td><td>%4</td><td>%5</td></tr>")
        .arg(name.toHtmlEscaped(),
             evidenceDisplay(evidence.state).toHtmlEscaped(),
             evidence.summary.toHtmlEscaped(),
             evidence.recordedAt.toHtmlEscaped(),
             safePath(evidence.recordPath).toHtmlEscaped());
}

QString optionalRow(const QString& name, const ReportOptionalRecord& record)
{
    return QStringLiteral("<tr><td>%1</td><td><span class=\"state\">%2</span></td><td>%3</td><td>%4</td><td>%5</td></tr>")
        .arg(name.toHtmlEscaped(),
             optionalDisplay(record.state).toHtmlEscaped(),
             record.summary.toHtmlEscaped(),
             record.recordedAt.toHtmlEscaped(),
             safePath(record.path).toHtmlEscaped());
}

QString diagnosticRows(const QList<ReportDiagnostic>& diagnostics, const QString& emptyText)
{
    if (diagnostics.isEmpty()) {
        return QStringLiteral("<tr><td colspan=\"7\">%1</td></tr>").arg(emptyText.toHtmlEscaped());
    }
    QString rows;
    for (const ReportDiagnostic& item : diagnostics) {
        rows.append(QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
                        .arg(item.severity.toHtmlEscaped(),
                             item.id.toHtmlEscaped(),
                             item.code.toHtmlEscaped(),
                             item.source.toHtmlEscaped(),
                             item.message.toHtmlEscaped(),
                             item.suggestion.toHtmlEscaped(),
                             item.occurredAt.toHtmlEscaped()));
    }
    return rows;
}

QString artifactRows(const QJsonArray& artifacts)
{
    if (artifacts.isEmpty()) {
        return QStringLiteral("<tr><td colspan=\"4\">无已登记产物</td></tr>");
    }
    QString rows;
    for (const QJsonValue& value : artifacts) {
        const QJsonObject artifact = value.toObject();
        rows.append(QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td><code>%4</code></td></tr>")
                        .arg(artifact.value(QStringLiteral("kind")).toString().toHtmlEscaped(),
                             safePath(artifact.value(QStringLiteral("relative_path")).toString()).toHtmlEscaped(),
                             QString::number(artifact.value(QStringLiteral("size_bytes")).toVariant().toLongLong()),
                             artifact.value(QStringLiteral("sha256")).toString().toHtmlEscaped()));
    }
    return rows;
}

QByteArray renderHtml(
    const ReportDocumentModel& model,
    const ReportConclusion conclusion,
    const QStringList& reasons,
    const QString& reportJsonSha)
{
    QString reasonItems;
    for (const QString& reason : reasons) {
        reasonItems.append(QStringLiteral("<li>%1</li>").arg(reason.toHtmlEscaped()));
    }
    const QString html = QStringLiteral(
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<title>测试报告 %1</title><style>"
        "@page{size:A4;margin:15mm}*{box-sizing:border-box}body{font-family:'Noto Sans CJK SC','Microsoft YaHei',sans-serif;color:#17202a;margin:0;font-size:13px}"
        "header{border-bottom:2px solid #1f4b5f;padding-bottom:12px;margin-bottom:16px}h1{font-size:24px;margin:0 0 8px}h2{font-size:16px;margin:20px 0 8px;color:#1f4b5f}"
        ".meta{display:grid;grid-template-columns:1fr 1fr;gap:4px 24px}.conclusion{border-left:8px solid #1f4b5f;background:#eef5f7;padding:14px;margin:14px 0}"
        ".conclusion strong{font-size:22px}.state{font-weight:700}table{width:100%;border-collapse:collapse;table-layout:fixed}th,td{border:1px solid #c9d3d8;padding:7px;text-align:left;vertical-align:top;word-break:break-word}th{background:#edf2f4}"
        "code{word-break:break-all}pre{white-space:pre-wrap;word-break:break-all;background:#f5f7f8;border:1px solid #d8e0e4;padding:8px}footer{margin-top:22px;border-top:1px solid #c9d3d8;padding-top:8px;color:#52616b;font-size:11px}@media print{body{font-size:10pt}h2{break-after:avoid}table{break-inside:auto}tr{break-inside:avoid}}"
        "</style></head><body><header><h1>锁步研发测试系统测试报告</h1>"
        "<div class=\"meta\"><div>任务：%2</div><div>任务 ID：%3</div><div>模式：%4</div><div>报告 ID：%1</div><div>版本：%5</div><div>生成时间：%6</div></div></header>"
        "<section class=\"conclusion\"><strong>结论：%7</strong><ul>%8</ul></section>"
        "<h2>任务与测试对象</h2><table><tr><th>程序镜像</th><td>%9</td><th>入口地址</th><td>%10</td></tr><tr><th>程序 SHA-256</th><td colspan=\"3\"><code>%11</code></td></tr><tr><th>采集编号</th><td colspan=\"3\">%27</td></tr><tr><th>目标摘要</th><td colspan=\"3\">%12</td></tr></table>"
        "<h2>强制证据</h2><table><tr><th>步骤</th><th>状态</th><th>摘要</th><th>时间</th><th>记录路径</th></tr>%13%14%15</table>"
        "<h2>可选记录（不影响通过判定）</h2><table><tr><th>记录</th><th>状态</th><th>摘要</th><th>时间</th><th>路径</th></tr>%16%17%18%19</table>"
        "<h2>阻断错误</h2><table><tr><th>级别</th><th>ID</th><th>代码</th><th>来源</th><th>消息</th><th>建议动作</th><th>时间</th></tr>%20</table>"
        "<h2>警告</h2><table><tr><th>级别</th><th>ID</th><th>代码</th><th>来源</th><th>消息</th><th>建议动作</th><th>时间</th></tr>%21</table>"
        "<h2>资源与可追溯信息</h2><table><tr><th>输入摘要</th><td><code>%22</code></td></tr><tr><th>report.json SHA-256</th><td><code>%23</code></td></tr></table>"
        "<h2>执行环境</h2><pre>%24</pre><h2>资源快照</h2><pre>%25</pre><h2>产物索引</h2><table><tr><th>类型</th><th>相对路径</th><th>字节数</th><th>SHA-256</th></tr>%26</table>"
        "<footer>本文件由 report.json 同批生成，仅用于阅读与打印；机器判定以 report.json 为准。</footer></body></html>")
        .arg(model.reportId.toHtmlEscaped(),
             model.taskName.toHtmlEscaped(),
             model.taskId.toHtmlEscaped(),
             model.mode.toHtmlEscaped(),
             QString::number(model.revision),
             model.generatedAt.toHtmlEscaped(),
             conclusionDisplay(conclusion).toHtmlEscaped(),
             reasonItems,
             model.programFileName.toHtmlEscaped(),
             model.entryAddress.toHtmlEscaped(),
             model.programSha256.toHtmlEscaped(),
             model.targetSummary.toHtmlEscaped(),
             evidenceRow(QStringLiteral("程序烧写"), model.requiredEvidence.programWrite),
             evidenceRow(QStringLiteral("回读校验"), model.requiredEvidence.readbackVerify),
             evidenceRow(QStringLiteral("程序运行"), model.requiredEvidence.runControl),
             optionalRow(QStringLiteral("VCD 波形"), model.optionalRecords.vcdWaveform),
             optionalRow(QStringLiteral("协议分析"), model.optionalRecords.protocolAnalysis),
             optionalRow(QStringLiteral("采集记录"), model.optionalRecords.acquisition),
             optionalRow(QStringLiteral("故障注入"), model.optionalRecords.faultInjection),
             diagnosticRows(model.blockingErrors, QStringLiteral("无未解决阻断错误")),
             diagnosticRows(model.warnings, QStringLiteral("无警告")),
             model.inputDigest.toHtmlEscaped(),
             reportJsonSha.toHtmlEscaped(),
             QString::fromUtf8(QJsonDocument(model.environment).toJson(QJsonDocument::Compact)).toHtmlEscaped(),
             QString::fromUtf8(QJsonDocument(model.resourceSnapshot).toJson(QJsonDocument::Compact)).toHtmlEscaped(),
             artifactRows(model.artifacts),
             model.hasCaptureId ? QString::number(model.captureId) : QStringLiteral("不适用"));
    return html.toUtf8();
}

ReportEvidence evidenceFromJson(const QJsonObject& object)
{
    ReportEvidence evidence;
    const QString status = object.value(QStringLiteral("status")).toString();
    evidence.state = status == QStringLiteral("passed") ? EvidenceState::Passed
        : status == QStringLiteral("failed") ? EvidenceState::Failed
        : status == QStringLiteral("missing") ? EvidenceState::Missing
        : EvidenceState::NotRun;
    evidence.summary = object.value(QStringLiteral("summary")).toString();
    evidence.recordedAt = object.value(QStringLiteral("recorded_at")).toString();
    evidence.recordPath = object.value(QStringLiteral("record_path")).toString();
    evidence.sha256 = object.value(QStringLiteral("sha256")).toString();
    evidence.metrics = object.value(QStringLiteral("metrics")).toObject();
    return evidence;
}

ReportOptionalRecord optionalFromJson(const QJsonObject& object)
{
    ReportOptionalRecord record;
    const QString status = object.value(QStringLiteral("status")).toString();
    record.state = status == QStringLiteral("available") ? OptionalRecordState::Available
        : status == QStringLiteral("failed") ? OptionalRecordState::Failed
        : status == QStringLiteral("skipped") ? OptionalRecordState::Skipped
        : OptionalRecordState::NotAvailable;
    record.summary = object.value(QStringLiteral("summary")).toString();
    record.recordedAt = object.value(QStringLiteral("recorded_at")).toString();
    record.path = object.value(QStringLiteral("path")).toString();
    record.sha256 = object.value(QStringLiteral("sha256")).toString();
    record.diagnosticCount = object.value(QStringLiteral("diagnostic_count")).toInt();
    return record;
}

QList<ReportDiagnostic> diagnosticsFromJson(const QJsonArray& array)
{
    QList<ReportDiagnostic> diagnostics;
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        ReportDiagnostic item;
        item.id = object.value(QStringLiteral("id")).toString();
        item.code = object.value(QStringLiteral("code")).toString();
        item.severity = object.value(QStringLiteral("severity")).toString();
        item.source = object.value(QStringLiteral("source")).toString();
        item.message = object.value(QStringLiteral("message")).toString();
        item.suggestion = object.value(QStringLiteral("suggestion")).toString();
        item.occurredAt = object.value(QStringLiteral("occurred_at")).toString();
        diagnostics.append(item);
    }
    return diagnostics;
}

}  // namespace

QString toString(const ReportConclusion conclusion)
{
    switch (conclusion) {
    case ReportConclusion::Pass:
        return QStringLiteral("pass");
    case ReportConclusion::Fail:
        return QStringLiteral("fail");
    case ReportConclusion::Blocked:
        return QStringLiteral("blocked");
    case ReportConclusion::Incomplete:
    default:
        return QStringLiteral("incomplete");
    }
}

QString toString(const OptionalRecordState state)
{
    switch (state) {
    case OptionalRecordState::Available:
        return QStringLiteral("available");
    case OptionalRecordState::Skipped:
        return QStringLiteral("skipped");
    case OptionalRecordState::Failed:
        return QStringLiteral("failed");
    case OptionalRecordState::NotAvailable:
    default:
        return QStringLiteral("not_available");
    }
}

QString toString(const EvidenceState state)
{
    switch (state) {
    case EvidenceState::Passed:
        return QStringLiteral("passed");
    case EvidenceState::Failed:
        return QStringLiteral("failed");
    case EvidenceState::Missing:
        return QStringLiteral("missing");
    case EvidenceState::NotRun:
    default:
        return QStringLiteral("not_run");
    }
}

ReportDocumentModel ReportGenerator::buildModelFromTask(
    const QString& taskRootPath,
    const ReportDocumentModel& context,
    QString* const errorMessage) const
{
    ReportDocumentModel model = context;
    for (qsizetype index = model.blockingErrors.size() - 1; index >= 0; --index) {
        if (model.blockingErrors.at(index).id.startsWith(QStringLiteral("report_capture_"))) {
            model.blockingErrors.removeAt(index);
        }
    }
    model.captureId = 0U;
    model.hasCaptureId = false;
    model.environment = sanitizedEnvironment(context.environment);
    const QString programRelativePath = safePath(context.programRelativePath);
    const QString programPath = programRelativePath.isEmpty()
        ? QString() : QDir(taskRootPath).filePath(programRelativePath);
    if (QFileInfo::exists(programPath)) {
        model.programFileName = QFileInfo(programPath).fileName();
        model.programSha256 = fileSha256(programPath);
    } else {
        model.programFileName = QFileInfo(context.programFileName).fileName();
        model.programSha256.clear();
    }
    model.programRelativePath.clear();
    model.resourceSnapshot = sanitizedResourceSnapshot(context.resourceSnapshot);
    model.artifacts = QJsonArray();
    const auto absolutePath = [&taskRootPath](const QString& relativePath) {
        return QDir(taskRootPath).filePath(relativePath);
    };
    const auto numberValue = [](const QJsonValue& value) -> qint64 {
        if (value.isDouble()) {
            return static_cast<qint64>(value.toDouble());
        }
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok);
        return ok ? parsed : 0;
    };

    QSet<QString> registeredPaths;
    QJsonObject artifactIndex;
    const QString artifactIndexFile = absolutePath(QString::fromLatin1(kArtifactIndexPath));
    if (QFileInfo::exists(artifactIndexFile) && !readObject(artifactIndexFile, &artifactIndex)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("产物索引无法解析: %1").arg(artifactIndexFile);
        }
    } else {
        for (const QJsonValue& value : artifactIndex.value(QStringLiteral("artifacts")).toArray()) {
            const QJsonObject source = value.toObject();
            const QString relativePath = safePath(source.value(QStringLiteral("relative_path")).toString());
            if (relativePath.isEmpty() || source.value(QStringLiteral("kind")).toString() == QStringLiteral("report")) {
                continue;
            }
            registeredPaths.insert(relativePath);
            const QString path = absolutePath(relativePath);
            if (!QFileInfo::exists(path)) {
                continue;
            }
            QJsonObject artifact;
            artifact.insert(QStringLiteral("artifact_id"), source.value(QStringLiteral("artifact_id")).toString());
            artifact.insert(QStringLiteral("kind"), source.value(QStringLiteral("kind")).toString());
            artifact.insert(QStringLiteral("relative_path"), relativePath);
            artifact.insert(QStringLiteral("name"), QFileInfo(relativePath).fileName());
            artifact.insert(QStringLiteral("sha256"), fileSha256(path));
            artifact.insert(QStringLiteral("size_bytes"), QFileInfo(path).size());
            artifact.insert(QStringLiteral("created_at"), source.value(QStringLiteral("created_at")).toString());
            model.artifacts.append(artifact);
        }
    }

    const auto readEvidence = [&](
                                  const QString& relativePath,
                                  ReportEvidence* const evidence,
                                  QJsonObject* const object) {
        evidence->recordPath = relativePath;
        const QString path = absolutePath(relativePath);
        if (!QFileInfo::exists(path)) {
            evidence->state = registeredPaths.contains(relativePath)
                ? EvidenceState::Missing : EvidenceState::NotRun;
            evidence->summary = evidence->state == EvidenceState::Missing
                ? QStringLiteral("已登记证据文件缺失") : QStringLiteral("尚未执行");
            return false;
        }
        evidence->sha256 = fileSha256(path);
        evidence->metrics.insert(QStringLiteral("record_size_bytes"), QFileInfo(path).size());
        if (!readObject(path, object)) {
            evidence->state = EvidenceState::Missing;
            evidence->summary = QStringLiteral("证据文件无法解析");
            return false;
        }
        evidence->recordedAt = object->value(QStringLiteral("created_at")).toString();
        return true;
    };

    QJsonObject write;
    if (readEvidence(QString::fromLatin1(kProgramWritePath), &model.requiredEvidence.programWrite, &write)) {
        model.requiredEvidence.programWrite.state = write.value(QStringLiteral("success")).toBool()
            ? EvidenceState::Passed : EvidenceState::Failed;
        qint64 totalBytes = 0;
        const QJsonArray segments = write.value(QStringLiteral("segments")).toArray();
        for (const QJsonValue& value : segments) {
            totalBytes += numberValue(value.toObject().value(QStringLiteral("length")));
        }
        model.requiredEvidence.programWrite.metrics.insert(QStringLiteral("segment_count"), segments.size());
        model.requiredEvidence.programWrite.metrics.insert(QStringLiteral("size_bytes"), totalBytes);
        model.requiredEvidence.programWrite.summary =
            model.requiredEvidence.programWrite.state == EvidenceState::Passed
            ? QStringLiteral("烧写 %1 个段，共 %2 字节").arg(segments.size()).arg(totalBytes)
            : QStringLiteral("烧写失败: %1").arg(write.value(QStringLiteral("error_message")).toString());
        const QString evidenceProgramSha256 = write.value(QStringLiteral("image_sha256")).toString().toLower();
        if (model.programSha256.isEmpty() && isSha256(evidenceProgramSha256)) {
            model.programSha256 = evidenceProgramSha256;
        }
    }

    QJsonObject verify;
    if (readEvidence(QString::fromLatin1(kReadbackVerifyPath), &model.requiredEvidence.readbackVerify, &verify)) {
        const QString state = verify.value(QStringLiteral("state")).toString();
        model.requiredEvidence.readbackVerify.state = state == QStringLiteral("passed")
            ? EvidenceState::Passed
            : (state == QStringLiteral("not_run") ? EvidenceState::NotRun : EvidenceState::Failed);
        const qint64 expected = numberValue(verify.value(QStringLiteral("expected_length")));
        const qint64 actual = numberValue(verify.value(QStringLiteral("actual_length")));
        const qint64 diff = numberValue(verify.value(QStringLiteral("diff_count")));
        model.requiredEvidence.readbackVerify.metrics.insert(QStringLiteral("expected_length"), expected);
        model.requiredEvidence.readbackVerify.metrics.insert(QStringLiteral("actual_length"), actual);
        model.requiredEvidence.readbackVerify.metrics.insert(QStringLiteral("diff_count"), diff);
        model.requiredEvidence.readbackVerify.summary = state == QStringLiteral("passed")
            ? QStringLiteral("回读一致，校验 %1 字节").arg(actual)
            : (state == QStringLiteral("not_run")
                ? QStringLiteral("回读校验尚未执行")
                : QStringLiteral("回读不一致或失败，差异 %1 处").arg(diff));
    }

    QJsonObject run;
    if (readEvidence(QString::fromLatin1(kRunControlPath), &model.requiredEvidence.runControl, &run)) {
        const QString state = run.value(QStringLiteral("state")).toString();
        const bool hasRawReturn = !run.value(QStringLiteral("raw_return")).toString().isEmpty();
        const bool hasSnapshot = !run.value(QStringLiteral("snapshot")).toString().isEmpty();
        const QString evidenceKind = hasSnapshot ? QStringLiteral("snapshot")
            : (hasRawReturn ? QStringLiteral("raw_return") : QStringLiteral("none"));
        model.requiredEvidence.runControl.state = state == QStringLiteral("not_run")
            ? EvidenceState::NotRun
            : (state != QStringLiteral("running")
                ? EvidenceState::Failed
                : ((hasRawReturn || hasSnapshot) ? EvidenceState::Passed : EvidenceState::Missing));
        model.requiredEvidence.runControl.metrics.insert(
            QStringLiteral("entry_address"), run.value(QStringLiteral("entry_address")).toString());
        if (!run.value(QStringLiteral("entry_address")).toString().isEmpty()) {
            model.entryAddress = run.value(QStringLiteral("entry_address")).toString();
        }
        model.requiredEvidence.runControl.metrics.insert(QStringLiteral("state"), state);
        model.requiredEvidence.runControl.metrics.insert(QStringLiteral("evidence_kind"), evidenceKind);
        model.requiredEvidence.runControl.summary = state == QStringLiteral("running")
            ? QStringLiteral("运行状态已返回，证据类型: %1").arg(evidenceKind)
            : (state == QStringLiteral("not_run")
                ? QStringLiteral("程序运行尚未执行")
                : QStringLiteral("运行失败: %1").arg(run.value(QStringLiteral("error_message")).toString()));
    }

    const auto appendCaptureDiagnostic = [&model](
                                             const QString& code,
                                             const QString& source,
                                             const QString& message) {
        ReportDiagnostic diagnostic;
        diagnostic.id = QStringLiteral("report_capture_%1").arg(model.blockingErrors.size() + 1);
        diagnostic.code = code;
        diagnostic.severity = QStringLiteral("blocking");
        diagnostic.source = source;
        diagnostic.message = message;
        diagnostic.suggestion = QStringLiteral("重新生成同一 capture_id 的完整采集证据后再归档");
        model.blockingErrors.append(diagnostic);
    };
    QJsonObject captureSidecar;
    const QString captureSidecarFile = absolutePath(QString::fromLatin1(kCaptureSidecarPath));
    if (QFileInfo::exists(captureSidecarFile)) {
        quint32 sidecarCaptureId = 0U;
        if (!readObject(captureSidecarFile, &captureSidecar) ||
            !captureIdFromJson(captureSidecar.value(QStringLiteral("capture_id")), &sidecarCaptureId)) {
            appendCaptureDiagnostic(
                QStringLiteral("CAPTURE_ID_INVALID"), QString::fromLatin1(kCaptureSidecarPath),
                QStringLiteral("采集 sidecar 缺少有效的 capture_id"));
            model.hasCaptureId = false;
            model.captureId = 0U;
        } else {
            model.hasCaptureId = true;
            model.captureId = sidecarCaptureId;
        }
    }
    const auto crossCheckCaptureId = [&](const QString& relativePath, const QJsonValue& value) {
        if (!model.hasCaptureId) {
            return;
        }
        quint32 evidenceCaptureId = 0U;
        if (!captureIdFromJson(value, &evidenceCaptureId)) {
            appendCaptureDiagnostic(
                QStringLiteral("CAPTURE_ID_INVALID"), relativePath,
                QStringLiteral("证据 %1 缺少有效的 capture_id").arg(relativePath));
        } else if (evidenceCaptureId != model.captureId) {
            appendCaptureDiagnostic(
                QStringLiteral("CAPTURE_ID_MISMATCH"), relativePath,
                QStringLiteral("证据 %1 的 capture_id=%2，与 sidecar 的 capture_id=%3 不一致")
                    .arg(relativePath).arg(evidenceCaptureId).arg(model.captureId));
        }
    };
    QJsonObject protocolEvents;
    const QString protocolEventsFile = absolutePath(QString::fromLatin1(kProtocolEventsPath));
    if (QFileInfo::exists(protocolEventsFile)) {
        if (readObject(protocolEventsFile, &protocolEvents)) {
            crossCheckCaptureId(QString::fromLatin1(kProtocolEventsPath),
                                protocolEvents.value(QStringLiteral("capture_id")));
        } else {
            appendCaptureDiagnostic(
                QStringLiteral("CAPTURE_ID_INVALID"), QString::fromLatin1(kProtocolEventsPath),
                QStringLiteral("协议事件证据无法解析，不能核验 capture_id"));
        }
    }
    QJsonObject captureStatus;
    const QString captureStatusFile = absolutePath(QString::fromLatin1(kCaptureStatusPath));
    if (QFileInfo::exists(captureStatusFile)) {
        if (readObject(captureStatusFile, &captureStatus)) {
            crossCheckCaptureId(
                QString::fromLatin1(kCaptureStatusPath),
                captureStatus.value(QStringLiteral("status")).toObject().value(QStringLiteral("capture_id")));
        } else {
            appendCaptureDiagnostic(
                QStringLiteral("CAPTURE_ID_INVALID"), QString::fromLatin1(kCaptureStatusPath),
                QStringLiteral("采集状态证据无法解析，不能核验 capture_id"));
        }
    }

    const auto fillOptional = [&](
                                  const QString& relativePath,
                                  const QString& displayName,
                                  ReportOptionalRecord* const record) {
        record->path = relativePath;
        const QFileInfo info(absolutePath(relativePath));
        if (!info.exists()) {
            record->state = OptionalRecordState::NotAvailable;
            record->summary = QStringLiteral("未生成");
            return;
        }
        record->state = OptionalRecordState::Available;
        record->summary = QStringLiteral("%1 %2 字节").arg(displayName).arg(info.size());
        record->recordedAt = info.lastModified().toUTC().toString(Qt::ISODateWithMs);
        record->sha256 = fileSha256(info.absoluteFilePath());
    };
    fillOptional(QString::fromLatin1(kWaveformPath), QStringLiteral("波形文件"),
                 &model.optionalRecords.vcdWaveform);
    fillOptional(QString::fromLatin1(kAnalysisPath), QStringLiteral("协议分析文件"),
                 &model.optionalRecords.protocolAnalysis);
    QJsonObject analysis;
    if (model.optionalRecords.protocolAnalysis.state == OptionalRecordState::Available &&
        readObject(absolutePath(QString::fromLatin1(kAnalysisPath)), &analysis)) {
        model.optionalRecords.protocolAnalysis.diagnosticCount =
            analysis.value(QStringLiteral("diagnostic_summary")).toArray().size();
        model.optionalRecords.protocolAnalysis.summary = QStringLiteral("%1 组协议，%2 条诊断")
            .arg(analysis.value(QStringLiteral("groups")).toArray().size())
            .arg(model.optionalRecords.protocolAnalysis.diagnosticCount);
        model.optionalRecords.protocolAnalysis.recordedAt =
            analysis.value(QStringLiteral("generated_at")).toString();
    }
    model.optionalRecords.acquisition.state = OptionalRecordState::NotAvailable;
    model.optionalRecords.acquisition.summary = QStringLiteral("未生成采集记录");
    const QString rawCapturePath = QStringLiteral("evidence/raw_capture.dat");
    if (QFileInfo::exists(absolutePath(rawCapturePath))) {
        model.optionalRecords.acquisition.state = OptionalRecordState::Available;
        model.optionalRecords.acquisition.path = rawCapturePath;
        model.optionalRecords.acquisition.sha256 = fileSha256(absolutePath(rawCapturePath));
        model.optionalRecords.acquisition.summary = QStringLiteral("原始采集数据已生成");
    }
    for (const QJsonValue& value : model.artifacts) {
        const QJsonObject artifact = value.toObject();
        if (artifact.value(QStringLiteral("name")).toString().contains(QStringLiteral("acquisition"), Qt::CaseInsensitive)) {
            model.optionalRecords.acquisition.state = OptionalRecordState::Available;
            model.optionalRecords.acquisition.path = artifact.value(QStringLiteral("relative_path")).toString();
            model.optionalRecords.acquisition.sha256 = artifact.value(QStringLiteral("sha256")).toString();
            model.optionalRecords.acquisition.recordedAt = artifact.value(QStringLiteral("created_at")).toString();
            model.optionalRecords.acquisition.summary = QStringLiteral("采集记录已登记");
            break;
        }
    }
    const QString faultPath = QStringLiteral("evidence/fault_injection.json");
    if (QFileInfo::exists(absolutePath(faultPath))) {
        model.optionalRecords.faultInjection.state = OptionalRecordState::Available;
        model.optionalRecords.faultInjection.path = faultPath;
        model.optionalRecords.faultInjection.sha256 = fileSha256(absolutePath(faultPath));
        model.optionalRecords.faultInjection.summary = QStringLiteral("错误注入记录已生成");
    } else {
        model.optionalRecords.faultInjection.state = OptionalRecordState::Skipped;
        model.optionalRecords.faultInjection.summary = QStringLiteral("本轮未执行故障注入");
    }
    return model;
}

ReportConclusion ReportGenerator::calculateConclusion(
    const ReportDocumentModel& model,
    QStringList* const reasons) const
{
    QStringList localReasons;
    const auto addReason = [&localReasons](const QString& name, const ReportEvidence& evidence) {
        if (evidence.state == EvidenceState::Failed) {
            localReasons.append(QStringLiteral("%1失败").arg(name));
        } else if (evidence.state == EvidenceState::Missing) {
            localReasons.append(QStringLiteral("%1证据缺失").arg(name));
        } else if (evidence.state == EvidenceState::NotRun) {
            localReasons.append(QStringLiteral("%1尚未执行").arg(name));
        }
    };
    addReason(QStringLiteral("程序烧写"), model.requiredEvidence.programWrite);
    addReason(QStringLiteral("回读校验"), model.requiredEvidence.readbackVerify);
    addReason(QStringLiteral("程序运行"), model.requiredEvidence.runControl);

    ReportConclusion conclusion = ReportConclusion::Pass;
    if (!model.blockingErrors.isEmpty()) {
        localReasons.prepend(QStringLiteral("存在未解决阻断错误"));
        conclusion = ReportConclusion::Blocked;
    } else if (model.requiredEvidence.programWrite.state == EvidenceState::Failed ||
               model.requiredEvidence.readbackVerify.state == EvidenceState::Failed ||
               model.requiredEvidence.runControl.state == EvidenceState::Failed) {
        conclusion = ReportConclusion::Fail;
    } else if (model.requiredEvidence.programWrite.state != EvidenceState::Passed ||
               model.requiredEvidence.readbackVerify.state != EvidenceState::Passed ||
               model.requiredEvidence.runControl.state != EvidenceState::Passed) {
        conclusion = ReportConclusion::Incomplete;
    } else {
        localReasons.append(QStringLiteral("程序跑通所需强制证据齐全"));
    }
    if (reasons != nullptr) {
        *reasons = localReasons;
    }
    return conclusion;
}

QString ReportGenerator::calculateInputDigest(const ReportDocumentModel& model) const
{
    return sha256(QJsonDocument(normalizedInput(model)).toJson(QJsonDocument::Compact));
}

QJsonObject toJson(
    const ReportDocumentModel& model,
    const ReportConclusion conclusion,
    const QStringList& reasons)
{
    QJsonObject task;
    task.insert(QStringLiteral("task_id"), model.taskId);
    task.insert(QStringLiteral("task_name"), model.taskName);
    task.insert(QStringLiteral("description"), model.taskDescription);
    task.insert(QStringLiteral("mode"), model.mode);
    QJsonObject program;
    program.insert(QStringLiteral("file_name"), model.programFileName);
    program.insert(QStringLiteral("sha256"), model.programSha256);
    program.insert(QStringLiteral("entry_address"), model.entryAddress);
    QJsonArray reasonArray;
    for (const QString& reason : reasons) {
        QJsonObject item;
        item.insert(QStringLiteral("code"), QStringLiteral("conclusion_reason"));
        item.insert(QStringLiteral("message"), reason);
        reasonArray.append(item);
    }
    QJsonObject object;
    object.insert(QStringLiteral("schema_version"), model.schemaVersion);
    object.insert(QStringLiteral("generator_version"), model.generatorVersion);
    object.insert(QStringLiteral("report_id"), model.reportId);
    object.insert(QStringLiteral("revision"), model.revision);
    object.insert(QStringLiteral("generated_at"), model.generatedAt);
    object.insert(QStringLiteral("input_digest"), model.inputDigest);
    object.insert(QStringLiteral("task_id"), model.taskId);
    object.insert(QStringLiteral("mode"), model.mode);
    object.insert(QStringLiteral("task"), task);
    object.insert(QStringLiteral("program"), program);
    object.insert(QStringLiteral("capture_id"), model.hasCaptureId
        ? QJsonValue(static_cast<double>(model.captureId)) : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("target_summary"), model.targetSummary);
    object.insert(QStringLiteral("conclusion"), toString(conclusion));
    object.insert(QStringLiteral("reasons"), reasonArray);
    object.insert(QStringLiteral("required_evidence"), requiredToJson(model.requiredEvidence));
    object.insert(QStringLiteral("optional_records"), optionalRecordsToJson(model.optionalRecords));
    object.insert(QStringLiteral("warnings"), diagnosticsToJson(model.warnings));
    object.insert(QStringLiteral("unresolved_blocking_errors"), diagnosticsToJson(model.blockingErrors));
    object.insert(QStringLiteral("environment"), sanitizedEnvironment(model.environment));
    object.insert(QStringLiteral("resource_snapshot"), sanitizedResourceSnapshot(model.resourceSnapshot));
    object.insert(QStringLiteral("artifacts"), sanitizedArtifacts(model.artifacts));
    return object;
}

ReportResult ReportGenerator::generateReport(
    const QString& taskRootPath,
    const ReportDocumentModel& inputModel) const
{
    ReportResult result;
    ReportDocumentModel model = inputModel;
    model.environment = sanitizedEnvironment(inputModel.environment);
    model.programFileName = QFileInfo(inputModel.programFileName).fileName();
    model.programRelativePath.clear();
    model.resourceSnapshot = sanitizedResourceSnapshot(model.resourceSnapshot);
    model.artifacts = sanitizedArtifacts(model.artifacts);
    model.reportId = safeReportId(model.reportId);
    model.generatedAt = model.generatedAt.isEmpty() ? utcNow() : model.generatedAt;
    model.inputDigest = calculateInputDigest(model);
    result.conclusion = calculateConclusion(model, &result.reasons);

    const QString reportsPath = QDir(taskRootPath).filePath(QString::fromLatin1(kReportsName));
    const QString versionsPath = QDir(reportsPath).filePath(QString::fromLatin1(kVersionsName));
    const QString versionPath = QDir(versionsPath).filePath(model.reportId);
    const QString taskKey = sha256(QDir::cleanPath(taskRootPath).toUtf8()).left(16);
    const QString tempPath = QDir(QString::fromLatin1(kTemporaryReportRoot)).filePath(
        QStringLiteral("%1_%2").arg(taskKey, model.reportId));
    if (!QDir().mkpath(versionsPath)) {
        result.errorMessage = QStringLiteral("无法创建报告版本目录: %1").arg(versionsPath);
        return result;
    }
    if (!QDir().mkpath(QString::fromLatin1(kTemporaryReportRoot))) {
        result.errorMessage = QStringLiteral("无法创建固定报告临时目录: %1")
            .arg(QString::fromLatin1(kTemporaryReportRoot));
        return result;
    }
    if (QFileInfo::exists(versionPath)) {
        result.errorMessage = QStringLiteral("报告版本已存在: %1").arg(model.reportId);
        return result;
    }
    if (QFileInfo::exists(tempPath) && !QDir(tempPath).removeRecursively()) {
        result.errorMessage = QStringLiteral("无法清理报告临时目录: %1").arg(tempPath);
        return result;
    }
    if (!QDir().mkpath(tempPath)) {
        result.errorMessage = QStringLiteral("无法创建报告临时目录: %1").arg(tempPath);
        return result;
    }

    const QByteArray reportJson = jsonBytes(toJson(model, result.conclusion, result.reasons));
    result.reportSha256 = sha256(reportJson);
    const QByteArray reportHtml = renderHtml(model, result.conclusion, result.reasons, result.reportSha256);
    QJsonArray files;
    const auto addManifestFile = [&files](const QString& path, const QByteArray& data) {
        QJsonObject item;
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("size_bytes"), data.size());
        item.insert(QStringLiteral("sha256"), sha256(data));
        files.append(item);
    };
    addManifestFile(QString::fromLatin1(kReportJsonName), reportJson);
    addManifestFile(QString::fromLatin1(kReportHtmlName), reportHtml);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    manifest.insert(QStringLiteral("report_id"), model.reportId);
    manifest.insert(QStringLiteral("generated_at"), model.generatedAt);
    manifest.insert(QStringLiteral("files"), files);
    const auto evidenceReference = [&taskRootPath](
                                       const QString& id,
                                       const ReportEvidence& evidence) {
        QJsonObject reference;
        const QString relativePath = safePath(evidence.recordPath);
        const QString path = relativePath.isEmpty() ? QString() : QDir(taskRootPath).filePath(relativePath);
        reference.insert(QStringLiteral("id"), id);
        reference.insert(QStringLiteral("status"), toString(evidence.state));
        reference.insert(QStringLiteral("relative_path"), relativePath);
        reference.insert(QStringLiteral("size_bytes"), QFileInfo::exists(path) ? QFileInfo(path).size() : 0);
        reference.insert(QStringLiteral("sha256"), QFileInfo::exists(path) ? fileSha256(path) : QString());
        return reference;
    };
    QJsonArray referencedEvidence;
    referencedEvidence.append(evidenceReference(QStringLiteral("program_write"), model.requiredEvidence.programWrite));
    referencedEvidence.append(evidenceReference(QStringLiteral("readback_verify"), model.requiredEvidence.readbackVerify));
    referencedEvidence.append(evidenceReference(QStringLiteral("run_control"), model.requiredEvidence.runControl));
    const auto optionalReference = [&taskRootPath](
                                       const QString& id,
                                       const ReportOptionalRecord& record) {
        QJsonObject reference;
        const QString relativePath = safePath(record.path);
        const QString path = relativePath.isEmpty() ? QString() : QDir(taskRootPath).filePath(relativePath);
        reference.insert(QStringLiteral("id"), id);
        reference.insert(QStringLiteral("status"), toString(record.state));
        reference.insert(QStringLiteral("relative_path"), relativePath);
        reference.insert(QStringLiteral("size_bytes"), QFileInfo::exists(path) ? QFileInfo(path).size() : 0);
        reference.insert(QStringLiteral("sha256"), QFileInfo::exists(path) ? fileSha256(path) : QString());
        return reference;
    };
    QJsonArray referencedOptional;
    referencedOptional.append(optionalReference(QStringLiteral("vcd_waveform"), model.optionalRecords.vcdWaveform));
    referencedOptional.append(optionalReference(QStringLiteral("protocol_analysis"), model.optionalRecords.protocolAnalysis));
    referencedOptional.append(optionalReference(QStringLiteral("acquisition"), model.optionalRecords.acquisition));
    referencedOptional.append(optionalReference(QStringLiteral("fault_injection"), model.optionalRecords.faultInjection));
    manifest.insert(QStringLiteral("referenced_evidence"), referencedEvidence);
    manifest.insert(QStringLiteral("referenced_optional_records"), referencedOptional);
    manifest.insert(QStringLiteral("referenced_artifacts"), sanitizedArtifacts(model.artifacts));
    const QByteArray manifestJson = jsonBytes(manifest);

    QString error;
    if (!writeFile(QDir(tempPath).filePath(QString::fromLatin1(kReportJsonName)), reportJson, &error) ||
        !writeFile(QDir(tempPath).filePath(QString::fromLatin1(kReportHtmlName)), reportHtml, &error) ||
        !writeFile(QDir(tempPath).filePath(QString::fromLatin1(kManifestName)), manifestJson, &error)) {
        QDir(tempPath).removeRecursively();
        result.errorMessage = error;
        return result;
    }
    if (!QDir().rename(tempPath, versionPath)) {
        QDir(tempPath).removeRecursively();
        result.errorMessage = QStringLiteral("无法发布报告版本: %1").arg(versionPath);
        return result;
    }

    QJsonObject latest;
    latest.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    latest.insert(QStringLiteral("report_id"), model.reportId);
    latest.insert(QStringLiteral("revision"), model.revision);
    latest.insert(QStringLiteral("generated_at"), model.generatedAt);
    latest.insert(QStringLiteral("conclusion"), toString(result.conclusion));
    latest.insert(QStringLiteral("input_digest"), model.inputDigest);
    latest.insert(QStringLiteral("report_json"),
                  QStringLiteral("versions/%1/report.json").arg(model.reportId));
    latest.insert(QStringLiteral("report_html"),
                  QStringLiteral("versions/%1/report.html").arg(model.reportId));
    latest.insert(QStringLiteral("report_sha256"), result.reportSha256);
    const QList<QPair<QString, QByteArray>> latestFiles = {
        {QString::fromLatin1(kReportJsonName), reportJson},
        {QString::fromLatin1(kReportHtmlName), reportHtml},
        {QString::fromLatin1(kLatestName), jsonBytes(latest)},
    };
    if (!publishLatestFiles(reportsPath, latestFiles, &error)) {
        result.errorMessage = error;
        return result;
    }

    result.success = true;
    result.reportPath = QDir(reportsPath).filePath(QString::fromLatin1(kReportJsonName));
    result.htmlPath = QDir(reportsPath).filePath(QString::fromLatin1(kReportHtmlName));
    result.versionPath = versionPath;
    return result;
}

bool ReportGenerator::loadLatestReport(
    const QString& taskRootPath,
    ReportDocumentModel* const model,
    ReportConclusion* const conclusion,
    QStringList* const reasons,
    QString* const errorMessage) const
{
    QFile file(QDir(QDir(taskRootPath).filePath(QString::fromLatin1(kReportsName)))
                   .filePath(QString::fromLatin1(kReportJsonName)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告尚未生成");
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("报告 JSON 无效: %1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    ReportDocumentModel loaded;
    loaded.schemaVersion = object.value(QStringLiteral("schema_version")).toString();
    loaded.generatorVersion = object.value(QStringLiteral("generator_version")).toString();
    loaded.reportId = object.value(QStringLiteral("report_id")).toString();
    loaded.revision = object.value(QStringLiteral("revision")).toInt(1);
    loaded.generatedAt = object.value(QStringLiteral("generated_at")).toString();
    loaded.inputDigest = object.value(QStringLiteral("input_digest")).toString();
    const QJsonObject task = object.value(QStringLiteral("task")).toObject();
    loaded.taskId = task.value(QStringLiteral("task_id")).toString();
    loaded.taskName = task.value(QStringLiteral("task_name")).toString();
    loaded.taskDescription = task.value(QStringLiteral("description")).toString();
    loaded.mode = task.value(QStringLiteral("mode")).toString();
    const QJsonObject program = object.value(QStringLiteral("program")).toObject();
    loaded.programFileName = program.value(QStringLiteral("file_name")).toString();
    loaded.programSha256 = program.value(QStringLiteral("sha256")).toString();
    loaded.entryAddress = program.value(QStringLiteral("entry_address")).toString();
    loaded.hasCaptureId = captureIdFromJson(object.value(QStringLiteral("capture_id")), &loaded.captureId);
    loaded.targetSummary = object.value(QStringLiteral("target_summary")).toString();
    const QJsonObject required = object.value(QStringLiteral("required_evidence")).toObject();
    loaded.requiredEvidence.programWrite = evidenceFromJson(required.value(QStringLiteral("program_write")).toObject());
    loaded.requiredEvidence.readbackVerify = evidenceFromJson(required.value(QStringLiteral("readback_verify")).toObject());
    loaded.requiredEvidence.runControl = evidenceFromJson(required.value(QStringLiteral("run_control")).toObject());
    const QJsonObject optional = object.value(QStringLiteral("optional_records")).toObject();
    loaded.optionalRecords.vcdWaveform = optionalFromJson(optional.value(QStringLiteral("vcd_waveform")).toObject());
    loaded.optionalRecords.protocolAnalysis = optionalFromJson(optional.value(QStringLiteral("protocol_analysis")).toObject());
    loaded.optionalRecords.acquisition = optionalFromJson(optional.value(QStringLiteral("acquisition")).toObject());
    loaded.optionalRecords.faultInjection = optionalFromJson(optional.value(QStringLiteral("fault_injection")).toObject());
    loaded.warnings = diagnosticsFromJson(object.value(QStringLiteral("warnings")).toArray());
    loaded.blockingErrors = diagnosticsFromJson(object.value(QStringLiteral("unresolved_blocking_errors")).toArray());
    loaded.environment = object.value(QStringLiteral("environment")).toObject();
    loaded.resourceSnapshot = object.value(QStringLiteral("resource_snapshot")).toObject();
    loaded.artifacts = object.value(QStringLiteral("artifacts")).toArray();
    if (model != nullptr) {
        *model = loaded;
    }
    const QString conclusionText = object.value(QStringLiteral("conclusion")).toString();
    if (conclusion != nullptr) {
        *conclusion = conclusionText == QStringLiteral("pass") ? ReportConclusion::Pass
            : conclusionText == QStringLiteral("fail") ? ReportConclusion::Fail
            : conclusionText == QStringLiteral("blocked") ? ReportConclusion::Blocked
            : ReportConclusion::Incomplete;
    }
    if (reasons != nullptr) {
        reasons->clear();
        for (const QJsonValue& value : object.value(QStringLiteral("reasons")).toArray()) {
            reasons->append(value.toObject().value(QStringLiteral("message")).toString());
        }
    }
    return true;
}

}  // namespace lockstep::reporting
