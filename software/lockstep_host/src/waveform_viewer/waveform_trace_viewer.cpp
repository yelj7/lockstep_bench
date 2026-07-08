/**********************************************************
* 文件名: waveform_trace_viewer.cpp
* 日期: 2026-07-08
* 版本: v1.0
* 更新记录: 初版实现固定 trace analysis 读取与 UI 展示模型生成
* 描述: 实现 M11 波形显示模块的任务级固定产物检测、analysis 读取和摘要整理
**********************************************************/

#include "waveform_trace_viewer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace lockstep::waveform_viewer {
namespace {

constexpr char kWaveformDirName[] = "waveform";
constexpr char kTraceVcdName[] = "lockstep_trace.vcd";
constexpr char kTraceSchemaName[] = "lockstep_trace_schema.json";
constexpr char kTraceAnalysisName[] = "lockstep_trace_analysis.json";

QString waveformFilePath(const QString& taskRootPath, const QString& fileName)
{
    return QDir(QDir(taskRootPath).filePath(QString::fromLatin1(kWaveformDirName))).filePath(fileName);
}

QString sha256File(const QString& path)
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

bool readJsonObject(const QString& path, QJsonObject* const object)
{
    if (object == nullptr) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    *object = document.object();
    return true;
}

qint64 jsonInt64(const QJsonValue& value)
{
    if (value.isDouble()) {
        return static_cast<qint64>(value.toDouble());
    }
    bool ok = false;
    const qint64 parsed = value.toString().toLongLong(&ok);
    return ok ? parsed : 0;
}

QString eventSummary(const QJsonObject& object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    const QString summary = object.value(QStringLiteral("summary")).toString();
    const qint64 startTime = jsonInt64(object.value(QStringLiteral("start_time")));
    const qint64 endTime = jsonInt64(object.value(QStringLiteral("end_time")));
    if (!summary.isEmpty()) {
        return QStringLiteral("[%1..%2] %3: %4").arg(startTime).arg(endTime).arg(type, summary);
    }
    return QStringLiteral("[%1..%2] %3").arg(startTime).arg(endTime).arg(type);
}

QString diagnosticSummary(const QJsonObject& object)
{
    const QString code = object.value(QStringLiteral("code")).toString();
    const QString severity = object.value(QStringLiteral("severity")).toString();
    const QString message = object.value(QStringLiteral("message")).toString();
    const QString errorId = object.value(QStringLiteral("error_id")).toString();
    return errorId.isEmpty()
        ? QStringLiteral("%1 %2: %3").arg(severity, code, message)
        : QStringLiteral("%1 %2: %3 (%4)").arg(severity, code, message, errorId);
}

QString fieldSummary(const QJsonObject& object)
{
    const QString displayName = object.value(QStringLiteral("display_name")).toString();
    const QString name = object.value(QStringLiteral("name")).toString();
    const QString format = object.value(QStringLiteral("format")).toString();
    const int sampleBit = object.value(QStringLiteral("sample_bit")).toInt(-1);
    const QString label = displayName.isEmpty() ? name : displayName;
    if (sampleBit >= 0) {
        return QStringLiteral("%1 bit=%2 %3").arg(label).arg(sampleBit).arg(format);
    }
    return label;
}

QList<WaveformGroupView> groupsFromAnalysis(const QJsonArray& array)
{
    QList<WaveformGroupView> groups;
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        WaveformGroupView group;
        group.id = object.value(QStringLiteral("id")).toString();
        group.displayName = object.value(QStringLiteral("display_name")).toString(group.id);
        group.status = object.value(QStringLiteral("status")).toString();
        group.reason = object.value(QStringLiteral("reason")).toString();

        const QJsonArray fields = object.value(QStringLiteral("fields")).toArray();
        for (const QJsonValue& field : fields) {
            group.fields.append(fieldSummary(field.toObject()));
        }

        const QJsonArray transactions = object.value(QStringLiteral("transactions")).toArray();
        for (const QJsonValue& transaction : transactions) {
            group.transactions.append(eventSummary(transaction.toObject()));
        }

        groups.append(group);
    }
    return groups;
}

}  // namespace

QString fixedWaveformRelativePath()
{
    return QStringLiteral("waveform/lockstep_trace.vcd");
}

QString fixedTraceSchemaRelativePath()
{
    return QStringLiteral("waveform/lockstep_trace_schema.json");
}

QString fixedTraceAnalysisRelativePath()
{
    return QStringLiteral("waveform/lockstep_trace_analysis.json");
}

WaveformViewModel WaveformTraceViewer::loadTask(const QString& taskRootPath) const
{
    WaveformViewModel model;
    const QString taskRoot = QDir::cleanPath(taskRootPath);
    model.vcdPath = waveformFilePath(taskRoot, QString::fromLatin1(kTraceVcdName));
    model.schemaPath = waveformFilePath(taskRoot, QString::fromLatin1(kTraceSchemaName));
    model.analysisPath = waveformFilePath(taskRoot, QString::fromLatin1(kTraceAnalysisName));
    model.hasVcd = QFileInfo::exists(model.vcdPath);
    model.hasSchema = QFileInfo::exists(model.schemaPath);
    model.hasAnalysis = QFileInfo::exists(model.analysisPath);

    if (!model.hasVcd) {
        model.status = QStringLiteral("not_available");
        model.diagnostics.append(QStringLiteral("当前任务未生成 waveform/lockstep_trace.vcd"));
        return model;
    }
    if (!model.hasAnalysis) {
        model.status = QStringLiteral("analysis_missing");
        model.diagnostics.append(QStringLiteral("缺少 waveform/lockstep_trace_analysis.json"));
        return model;
    }

    QJsonObject analysis;
    if (!readJsonObject(model.analysisPath, &analysis)) {
        model.status = QStringLiteral("analysis_invalid");
        model.diagnostics.append(QStringLiteral("协议解析结果 JSON 无法读取"));
        return model;
    }

    model.analysis = analysis;
    model.status = analysis.value(QStringLiteral("status")).toString(QStringLiteral("unknown"));
    const QJsonObject input = analysis.value(QStringLiteral("input")).toObject();
    const QString expectedVcdSha = input.value(QStringLiteral("vcd_sha256")).toString();
    if (!expectedVcdSha.isEmpty()) {
        model.analysisStale = sha256File(model.vcdPath).compare(expectedVcdSha, Qt::CaseInsensitive) != 0;
        if (model.analysisStale) {
            model.diagnostics.append(QStringLiteral("analysis 与当前 VCD 摘要不一致，需要重新解析"));
        }
    }

    const QJsonObject timeBase = analysis.value(QStringLiteral("time_base")).toObject();
    model.timeRangeText = QStringLiteral("%1 .. %2 %3")
        .arg(jsonInt64(timeBase.value(QStringLiteral("start_time"))))
        .arg(jsonInt64(timeBase.value(QStringLiteral("end_time"))))
        .arg(timeBase.value(QStringLiteral("unit")).toString());

    model.groups = groupsFromAnalysis(analysis.value(QStringLiteral("groups")).toArray());

    const QJsonArray keyBehaviors = analysis.value(QStringLiteral("key_behaviors")).toArray();
    for (const QJsonValue& value : keyBehaviors) {
        model.keyBehaviors.append(eventSummary(value.toObject()));
    }

    const QJsonArray diagnostics = analysis.value(QStringLiteral("diagnostic_summary")).toArray();
    for (const QJsonValue& value : diagnostics) {
        model.diagnostics.append(diagnosticSummary(value.toObject()));
    }
    return model;
}

}  // namespace lockstep::waveform_viewer
