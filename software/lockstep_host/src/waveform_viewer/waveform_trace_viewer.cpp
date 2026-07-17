/**********************************************************
* 文件名: waveform_trace_viewer.cpp
* 日期: 2026-07-14
* 版本: v1.2
* 更新记录: 将各协议组事务汇总到统一关键行为列表
* 描述: 实现任务波形产物检测、协议束视图和采样模型生成。
**********************************************************/

#include "waveform_trace_viewer.h"

#include "protocol_analysis.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace lockstep::waveform_viewer {
namespace {

constexpr char kWaveformDirName[] = "waveform";
constexpr char kTraceVcdName[] = "capture.vcd";
constexpr char kTraceSchemaName[] = "capture_schema.json";

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

WaveformFieldView fieldFromJson(const QJsonObject& object)
{
    WaveformFieldView field;
    field.name = object.value(QStringLiteral("name")).toString();
    field.displayName = object.value(QStringLiteral("display_name")).toString(field.name);
    field.lsb = object.value(QStringLiteral("sample_bit")).toInt(-1);
    field.width = qMax(1, object.value(QStringLiteral("width")).toInt(1));
    field.errorSignal = object.value(QStringLiteral("error_signal")).toBool(false);
    return field;
}

QList<WaveformGroupView> defaultGroups()
{
    QList<WaveformGroupView> groups;
    for (const protocol_analyzer::ProtocolGroupDefinition& definition :
         protocol_analyzer::fixedProtocolGroups()) {
        WaveformGroupView group;
        group.id = definition.id;
        group.displayName = definition.displayName;
        group.status = QStringLiteral("not_available");
        group.reason = QStringLiteral("尚未导入 VCD");
        for (const protocol_analyzer::ProtocolFieldDefinition& definitionField : definition.fields) {
            WaveformFieldView field;
            field.name = definitionField.name;
            field.displayName = definitionField.displayName;
            field.lsb = definitionField.lsb;
            field.width = definitionField.width;
            field.errorSignal = definitionField.errorSignal;
            group.fields.append(field);
        }
        groups.append(group);
    }
    return groups;
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
            group.fields.append(fieldFromJson(field.toObject()));
        }

        const QJsonArray transactions = object.value(QStringLiteral("transactions")).toArray();
        for (const QJsonValue& transaction : transactions) {
            group.transactions.append(eventSummary(transaction.toObject()));
        }

        groups.append(group);
    }
    return groups;
}

QList<WaveformSampleView> samplesFromAnalysis(const QJsonArray& array)
{
    QList<WaveformSampleView> samples;
    samples.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QJsonObject object = value.toObject();
        WaveformSampleView sample;
        sample.time = jsonInt64(object.value(QStringLiteral("time")));
        sample.valueHex = object.value(QStringLiteral("value_hex")).toString();
        sample.unknown = object.value(QStringLiteral("unknown")).toBool(false);
        if (!sample.valueHex.isEmpty()) {
            samples.append(sample);
        }
    }
    return samples;
}

}  // namespace

QString fixedWaveformRelativePath()
{
    return QStringLiteral("waveform/capture.vcd");
}

QString fixedTraceSchemaRelativePath()
{
    return QStringLiteral("waveform/capture_schema.json");
}

QString fixedTraceAnalysisRelativePath()
{
    return QStringLiteral("evidence/protocol_analysis.json");
}

WaveformViewModel WaveformTraceViewer::loadTask(const QString& taskRootPath) const
{
    WaveformViewModel model;
    const QString taskRoot = QDir::cleanPath(taskRootPath);
    model.vcdPath = waveformFilePath(taskRoot, QString::fromLatin1(kTraceVcdName));
    model.schemaPath = waveformFilePath(taskRoot, QString::fromLatin1(kTraceSchemaName));
    model.analysisPath = QDir(taskRoot).filePath(fixedTraceAnalysisRelativePath());
    if (!QFileInfo::exists(model.vcdPath)) {
        const QString legacy = waveformFilePath(taskRoot, QStringLiteral("lockstep_trace.vcd"));
        if (QFileInfo::exists(legacy)) model.vcdPath = legacy;
    }
    if (!QFileInfo::exists(model.schemaPath)) {
        const QString legacy = waveformFilePath(taskRoot, QStringLiteral("lockstep_trace_schema.json"));
        if (QFileInfo::exists(legacy)) model.schemaPath = legacy;
    }
    if (!QFileInfo::exists(model.analysisPath)) {
        const QString legacy = waveformFilePath(taskRoot, QStringLiteral("lockstep_trace_analysis.json"));
        if (QFileInfo::exists(legacy)) model.analysisPath = legacy;
    }
    model.hasVcd = QFileInfo::exists(model.vcdPath);
    model.hasSchema = QFileInfo::exists(model.schemaPath);
    model.hasAnalysis = QFileInfo::exists(model.analysisPath);
    model.groups = defaultGroups();

    if (!model.hasVcd) {
        model.status = QStringLiteral("not_available");
        model.diagnostics.append(QStringLiteral("当前任务未生成 waveform/capture.vcd"));
        return model;
    }
    if (!model.hasAnalysis) {
        model.status = QStringLiteral("analysis_missing");
        model.diagnostics.append(QStringLiteral("缺少 evidence/protocol_analysis.json"));
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

    const QList<WaveformGroupView> analysisGroups =
        groupsFromAnalysis(analysis.value(QStringLiteral("groups")).toArray());
    if (!analysisGroups.isEmpty()) {
        model.groups = analysisGroups;
    }
    if (model.status != QStringLiteral("failed")) {
        model.samples = samplesFromAnalysis(analysis.value(QStringLiteral("samples")).toArray());
    } else {
        model.diagnostics.append(QStringLiteral("协议解析失败，拒绝显示截断或无效采样"));
    }

    for (const WaveformGroupView& group : model.groups) {
        for (const QString& transaction : group.transactions) {
            if (!model.keyBehaviors.contains(transaction)) model.keyBehaviors.append(transaction);
        }
    }
    const QJsonArray keyBehaviors = analysis.value(QStringLiteral("key_behaviors")).toArray();
    for (const QJsonValue& value : keyBehaviors) {
        const QString summary = eventSummary(value.toObject());
        if (!model.keyBehaviors.contains(summary)) model.keyBehaviors.append(summary);
    }

    const QJsonArray diagnostics = analysis.value(QStringLiteral("diagnostic_summary")).toArray();
    for (const QJsonValue& value : diagnostics) {
        model.diagnostics.append(diagnosticSummary(value.toObject()));
    }
    return model;
}

}  // namespace lockstep::waveform_viewer
