/**********************************************************
* 文件名: protocol_analysis.cpp
* 日期: 2026-07-08
* 版本: v1.0
* 更新记录: 初版实现固定 trace VCD 协议解析与 mismatch 事件提取
* 描述: 实现 M12 协议解析模块的 VCD/schema 读取、9 组结果生成、诊断聚合和 analysis 输出
**********************************************************/

#include "protocol_analysis.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace lockstep::protocol_analyzer {
namespace {

constexpr int kTraceWidth = 512;
constexpr int kMismatchLowBit = 502;
constexpr int kMismatchHighBit = 506;
constexpr char kWaveformDirName[] = "waveform";
constexpr char kTraceVcdName[] = "lockstep_trace.vcd";
constexpr char kTraceSchemaName[] = "lockstep_trace_schema.json";
constexpr char kTraceAnalysisName[] = "lockstep_trace_analysis.json";

struct VcdSample final {
    qint64 time = 0;
    quint8 mismatchMask = 0U;
};

struct VcdParseResult final {
    bool success = false;
    QString errorMessage;
    QString timescale = QStringLiteral("1 ns");
    QString sampleIdentifier;
    int sampleWidth = 0;
    qint64 startTime = 0;
    qint64 endTime = 0;
    QList<VcdSample> samples;
};

struct MismatchItem final {
    int bit = 0;
    QString name;
    qint64 startTime = 0;
    qint64 endTime = 0;
};

struct MismatchEvent final {
    qint64 startTime = 0;
    qint64 endTime = 0;
    QList<MismatchItem> items;
};

struct MismatchState final {
    int bit = 0;
    QString name;
    bool active = false;
    qint64 startTime = 0;
};

QString currentTimeText()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString waveformFilePath(const QString& taskRootPath, const QString& fileName)
{
    return QDir(QDir(taskRootPath).filePath(QString::fromLatin1(kWaveformDirName))).filePath(fileName);
}

void addDiagnostic(
    QList<ProtocolDiagnostic>* const diagnostics,
    const QString& code,
    const QString& severity,
    const QString& message,
    const QString& detail,
    const QJsonObject& context = QJsonObject())
{
    if (diagnostics == nullptr) {
        return;
    }

    ProtocolDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = message;
    diagnostic.detail = detail;
    diagnostic.context = context;
    diagnostics->append(diagnostic);
}

QString sha256File(const QString& path, QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取文件摘要: %1").arg(path);
        }
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool readJsonObject(const QString& path, QJsonObject* const object, QString* const errorMessage)
{
    if (object == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("JSON 输出为空");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取 JSON 文件: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("JSON 格式错误: %1").arg(path);
        }
        return false;
    }

    *object = document.object();
    return true;
}

bool writeJsonObject(const QString& path, const QJsonObject& object, QString* const errorMessage)
{
    const QFileInfo info(path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法创建目录: %1").arg(info.absolutePath());
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入 JSON 文件: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument document(object);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("JSON 写入不完整: %1").arg(path);
        }
        return false;
    }

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("JSON 提交失败: %1").arg(path);
        }
        return false;
    }

    return true;
}

QString normalizeSignalReference(const QString& reference)
{
    QString value = reference.trimmed();
    value.remove(QChar::fromLatin1(' '));
    return value;
}

bool isTraceSampleReference(const QString& reference)
{
    const QString normalized = normalizeSignalReference(reference);
    return normalized == QStringLiteral("lockstep_trace_sample") ||
        normalized == QStringLiteral("lockstep_trace_sample[511:0]");
}

QString extractVarReference(const QStringList& tokens)
{
    QStringList referenceParts;
    for (int index = 4; index < tokens.size(); ++index) {
        if (tokens.at(index) == QStringLiteral("$end")) {
            break;
        }
        referenceParts.append(tokens.at(index));
    }
    return referenceParts.join(QLatin1Char(' '));
}

QString paddedBinaryValue(const QString& rawValue)
{
    QString normalized;
    normalized.reserve(kTraceWidth);
    for (const QChar character : rawValue.trimmed()) {
        normalized.append(character == QLatin1Char('1') ? QLatin1Char('1') : QLatin1Char('0'));
    }

    if (normalized.size() >= kTraceWidth) {
        return normalized.right(kTraceWidth);
    }
    return QString(kTraceWidth - normalized.size(), QLatin1Char('0')) + normalized;
}

bool bitFromPackedValue(const QString& paddedValue, const int bit)
{
    const int index = kTraceWidth - 1 - bit;
    if (index < 0 || index >= paddedValue.size()) {
        return false;
    }
    return paddedValue.at(index) == QLatin1Char('1');
}

quint8 mismatchMaskFromPackedValue(const QString& paddedValue)
{
    quint8 mask = 0U;
    for (int bit = kMismatchLowBit; bit <= kMismatchHighBit; ++bit) {
        if (bitFromPackedValue(paddedValue, bit)) {
            mask |= static_cast<quint8>(1U << (bit - kMismatchLowBit));
        }
    }
    return mask;
}

VcdParseResult parseVcd(const QString& path)
{
    VcdParseResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("无法读取 VCD: %1").arg(path);
        return result;
    }

    qint64 currentTime = 0;
    bool foundTrace = false;
    QString sampleIdentifier;
    int sampleWidth = 0;

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith(QStringLiteral("$timescale"))) {
            QString timescale = line;
            timescale.remove(QStringLiteral("$timescale"));
            timescale.remove(QStringLiteral("$end"));
            result.timescale = timescale.trimmed();
            continue;
        }

        if (line.startsWith(QStringLiteral("$var"))) {
            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (tokens.size() >= 6) {
                bool widthOk = false;
                const int width = tokens.at(2).toInt(&widthOk);
                const QString identifier = tokens.at(3);
                const QString reference = extractVarReference(tokens);
                if (widthOk && isTraceSampleReference(reference)) {
                    foundTrace = true;
                    sampleIdentifier = identifier;
                    sampleWidth = width;
                }
            }
            continue;
        }

        if (line.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const qint64 parsedTime = line.mid(1).toLongLong(&ok);
            if (!ok) {
                result.errorMessage = QStringLiteral("VCD 时间戳无法解析: %1").arg(line);
                return result;
            }
            currentTime = parsedTime;
            continue;
        }

        if (!foundTrace || sampleIdentifier.isEmpty()) {
            continue;
        }

        if (line.startsWith(QLatin1Char('b'))) {
            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (tokens.size() != 2 || tokens.at(1) != sampleIdentifier) {
                continue;
            }
            const QString paddedValue = paddedBinaryValue(tokens.at(0).mid(1));
            VcdSample sample;
            sample.time = currentTime;
            sample.mismatchMask = mismatchMaskFromPackedValue(paddedValue);
            result.samples.append(sample);
        }
    }

    if (!foundTrace) {
        result.errorMessage = QStringLiteral("VCD 缺少 lockstep_trace_sample[511:0]");
        return result;
    }
    if (sampleWidth != kTraceWidth) {
        result.errorMessage = QStringLiteral("lockstep_trace_sample 位宽为 %1，不是 512").arg(sampleWidth);
        result.sampleWidth = sampleWidth;
        result.sampleIdentifier = sampleIdentifier;
        return result;
    }
    if (result.samples.isEmpty()) {
        result.errorMessage = QStringLiteral("VCD 中没有 lockstep_trace_sample 变化数据");
        result.sampleWidth = sampleWidth;
        result.sampleIdentifier = sampleIdentifier;
        return result;
    }

    result.success = true;
    result.sampleIdentifier = sampleIdentifier;
    result.sampleWidth = sampleWidth;
    result.startTime = result.samples.first().time;
    result.endTime = result.samples.last().time;
    return result;
}

QString mismatchNameForBit(const int bit)
{
    switch (bit) {
    case 4:
        return QStringLiteral("ahb_master_output_mismatch");
    case 3:
        return QStringLiteral("irq_output_mismatch");
    case 2:
        return QStringLiteral("debug_output_mismatch");
    case 1:
        return QStringLiteral("trace_output_mismatch");
    case 0:
    default:
        return QStringLiteral("counter_output_mismatch");
    }
}

QJsonObject mismatchItemToJson(const MismatchItem& item)
{
    QJsonObject object;
    object.insert(QStringLiteral("bit"), item.bit);
    object.insert(QStringLiteral("name"), item.name);
    object.insert(QStringLiteral("start_time"), item.startTime);
    object.insert(QStringLiteral("end_time"), item.endTime);
    object.insert(QStringLiteral("duration"), std::max<qint64>(0, item.endTime - item.startTime));
    return object;
}

QJsonObject mismatchEventToJson(const MismatchEvent& event)
{
    QJsonArray items;
    QStringList names;
    for (const MismatchItem& item : event.items) {
        items.append(mismatchItemToJson(item));
        names.append(item.name);
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("mismatch_event"));
    object.insert(QStringLiteral("group_id"), QStringLiteral("mismatch"));
    object.insert(QStringLiteral("start_time"), event.startTime);
    object.insert(QStringLiteral("end_time"), event.endTime);
    object.insert(QStringLiteral("duration"), std::max<qint64>(0, event.endTime - event.startTime));
    object.insert(QStringLiteral("summary"), names.join(QStringLiteral(", ")));
    object.insert(QStringLiteral("items"), items);
    return object;
}

QList<MismatchEvent> extractMismatchEvents(const QList<VcdSample>& samples)
{
    QList<MismatchEvent> events;
    if (samples.isEmpty()) {
        return events;
    }

    QList<MismatchState> states;
    for (int bit = 0; bit <= 4; ++bit) {
        MismatchState state;
        state.bit = bit;
        state.name = mismatchNameForBit(bit);
        states.append(state);
    }

    bool eventActive = false;
    MismatchEvent currentEvent;
    quint8 previousMask = 0U;

    for (const VcdSample& sample : samples) {
        const quint8 mask = sample.mismatchMask;
        const bool hasRisingEdge = ((mask & static_cast<quint8>(~previousMask)) != 0U);
        if (!eventActive && hasRisingEdge) {
            eventActive = true;
            currentEvent = MismatchEvent();
            currentEvent.startTime = sample.time;
        }

        for (int bit = 0; bit <= 4; ++bit) {
            MismatchState& state = states[bit];
            const quint8 bitMask = static_cast<quint8>(1U << bit);
            const bool nowActive = (mask & bitMask) != 0U;
            const bool wasActive = (previousMask & bitMask) != 0U;
            if (nowActive && !wasActive) {
                state.active = true;
                state.startTime = sample.time;
            } else if (!nowActive && wasActive && state.active) {
                MismatchItem item;
                item.bit = state.bit;
                item.name = state.name;
                item.startTime = state.startTime;
                item.endTime = sample.time;
                currentEvent.items.append(item);
                state.active = false;
            }
        }

        if (eventActive && mask == 0U && previousMask != 0U) {
            currentEvent.endTime = sample.time;
            if (!currentEvent.items.isEmpty()) {
                events.append(currentEvent);
            }
            eventActive = false;
        }
        previousMask = mask;
    }

    if (eventActive) {
        const qint64 endTime = samples.last().time;
        for (MismatchState& state : states) {
            if (state.active) {
                MismatchItem item;
                item.bit = state.bit;
                item.name = state.name;
                item.startTime = state.startTime;
                item.endTime = endTime;
                currentEvent.items.append(item);
                state.active = false;
            }
        }
        currentEvent.endTime = endTime;
        if (!currentEvent.items.isEmpty()) {
            events.append(currentEvent);
        }
    }

    return events;
}

QJsonObject makeProtocolGroup(const QString& id, const QString& displayName, const QString& status, const QString& reason)
{
    QJsonObject group;
    group.insert(QStringLiteral("id"), id);
    group.insert(QStringLiteral("display_name"), displayName);
    group.insert(QStringLiteral("status"), status);
    group.insert(QStringLiteral("reason"), reason);
    group.insert(QStringLiteral("transactions"), QJsonArray());
    group.insert(QStringLiteral("fields"), QJsonArray());
    return group;
}

QJsonObject makeMismatchGroup(const QList<MismatchEvent>& events)
{
    QJsonObject group = makeProtocolGroup(
        QStringLiteral("mismatch"),
        QStringLiteral("mismatch"),
        events.isEmpty() ? QStringLiteral("complete") : QStringLiteral("event_detected"),
        events.isEmpty() ? QStringLiteral("未检测到 mismatch") : QStringLiteral("检测到 mismatch 事件"));

    QJsonArray transactions;
    for (const MismatchEvent& event : events) {
        transactions.append(mismatchEventToJson(event));
    }
    group.insert(QStringLiteral("transactions"), transactions);

    QJsonArray fields;
    for (int bit = 4; bit >= 0; --bit) {
        QJsonObject field;
        field.insert(QStringLiteral("name"), QStringLiteral("mismatch[%1]").arg(bit));
        field.insert(QStringLiteral("display_name"), mismatchNameForBit(bit));
        field.insert(QStringLiteral("sample_bit"), kMismatchLowBit + bit);
        field.insert(QStringLiteral("format"), QStringLiteral("level"));
        fields.append(field);
    }
    group.insert(QStringLiteral("fields"), fields);
    return group;
}

QJsonArray defaultGroups(const QList<MismatchEvent>& mismatchEvents)
{
    QJsonArray groups;
    const QList<QPair<QString, QString>> protocolGroups = {
        {QStringLiteral("ahb"), QStringLiteral("AHB")},
        {QStringLiteral("uart"), QStringLiteral("UART")},
        {QStringLiteral("spi"), QStringLiteral("SPI")},
        {QStringLiteral("can"), QStringLiteral("CAN")},
        {QStringLiteral("i2c"), QStringLiteral("I2C")},
        {QStringLiteral("eth"), QStringLiteral("ETH")},
        {QStringLiteral("usb"), QStringLiteral("USB")},
        {QStringLiteral("jtag"), QStringLiteral("JTAG")}
    };

    for (const QPair<QString, QString>& protocol : protocolGroups) {
        groups.append(makeProtocolGroup(
            protocol.first,
            protocol.second,
            QStringLiteral("not_captured"),
            QStringLiteral("当前 trace profile 尚未提供该协议字段映射")));
    }
    groups.append(makeMismatchGroup(mismatchEvents));
    return groups;
}

QJsonArray eventsToJson(const QList<MismatchEvent>& events)
{
    QJsonArray array;
    for (const MismatchEvent& event : events) {
        array.append(mismatchEventToJson(event));
    }
    return array;
}

QJsonArray diagnosticsToJson(const QList<ProtocolDiagnostic>& diagnostics)
{
    QJsonArray array;
    for (const ProtocolDiagnostic& diagnostic : diagnostics) {
        QJsonObject object;
        object.insert(QStringLiteral("code"), diagnostic.code);
        object.insert(QStringLiteral("severity"), diagnostic.severity);
        object.insert(QStringLiteral("message"), diagnostic.message);
        object.insert(QStringLiteral("detail"), diagnostic.detail);
        object.insert(QStringLiteral("error_id"), diagnostic.errorId);
        object.insert(QStringLiteral("context"), diagnostic.context);
        array.append(object);
    }
    return array;
}

QList<error_handling::ErrorEvent> diagnosticsToErrorEvents(
    const QList<ProtocolDiagnostic>& diagnostics,
    const QString& taskId)
{
    QList<error_handling::ErrorEvent> events;
    for (const ProtocolDiagnostic& diagnostic : diagnostics) {
        error_handling::ErrorEvent event;
        event.code = diagnostic.code;
        event.source = QStringLiteral("M12_PROTOCOL_ANALYZER");
        event.module = QStringLiteral("protocol_analyzer");
        event.taskId = taskId;
        event.message = diagnostic.message;
        event.detail = diagnostic.detail;
        event.context = diagnostic.context;
        if (diagnostic.severity == QStringLiteral("warning")) {
            event.severity = error_handling::ErrorSeverity::Warning;
        } else if (diagnostic.severity == QStringLiteral("info")) {
            event.severity = error_handling::ErrorSeverity::Info;
        } else {
            event.severity = error_handling::ErrorSeverity::Error;
        }
        events.append(event);
    }
    return events;
}

void attachErrorIds(
    const QList<error_handling::ErrorRecord>& records,
    QList<ProtocolDiagnostic>* const diagnostics)
{
    if (diagnostics == nullptr) {
        return;
    }
    const int count = std::min(records.size(), diagnostics->size());
    for (int index = 0; index < count; ++index) {
        (*diagnostics)[index].errorId = records.at(index).errorId;
    }
}

void reportDiagnosticsToErrorRegistry(
    const ProtocolAnalysisRequest& request,
    const QString& taskRootPath,
    QList<ProtocolDiagnostic>* const diagnostics)
{
    if (!request.reportDiagnosticsToErrorRegistry ||
        request.errorRegistry == nullptr ||
        diagnostics == nullptr ||
        diagnostics->isEmpty()) {
        return;
    }

    QList<error_handling::ErrorRecord> records;
    QString error;
    const QList<error_handling::ErrorEvent> events = diagnosticsToErrorEvents(*diagnostics, request.taskId);
    if (request.errorRegistry->appendTaskErrorsBulk(taskRootPath, events, &records, &error)) {
        attachErrorIds(records, diagnostics);
    } else {
        addDiagnostic(
            diagnostics,
            QStringLiteral("TRACE_DIAGNOSTIC_REPORT_FAILED"),
            QStringLiteral("warning"),
            QStringLiteral("协议诊断无法写入 M14"),
            error);
    }
}

QJsonObject buildAnalysisObject(
    const QString& taskId,
    const QString& vcdSha256,
    const QString& schemaSha256,
    const VcdParseResult& parseResult,
    const QList<MismatchEvent>& mismatchEvents,
    const QList<ProtocolDiagnostic>& diagnostics)
{
    QJsonObject input;
    input.insert(QStringLiteral("vcd_file"), fixedWaveformRelativePath());
    input.insert(QStringLiteral("schema_file"), fixedTraceSchemaRelativePath());
    input.insert(QStringLiteral("vcd_sha256"), vcdSha256);
    input.insert(QStringLiteral("schema_sha256"), schemaSha256);

    QJsonObject timeBase;
    timeBase.insert(QStringLiteral("unit"), parseResult.timescale);
    timeBase.insert(QStringLiteral("start_time"), parseResult.startTime);
    timeBase.insert(QStringLiteral("end_time"), parseResult.endTime);

    QJsonObject analysis;
    analysis.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    analysis.insert(QStringLiteral("task_id"), taskId);
    analysis.insert(QStringLiteral("generated_at"), currentTimeText());
    analysis.insert(QStringLiteral("input"), input);
    analysis.insert(QStringLiteral("status"), QStringLiteral("partial"));
    analysis.insert(QStringLiteral("time_base"), timeBase);
    analysis.insert(QStringLiteral("groups"), defaultGroups(mismatchEvents));
    analysis.insert(QStringLiteral("key_behaviors"), eventsToJson(mismatchEvents));
    analysis.insert(QStringLiteral("diagnostic_summary"), diagnosticsToJson(diagnostics));
    analysis.insert(QStringLiteral("diagnostic_details"), diagnosticsToJson(diagnostics));
    return analysis;
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

ProtocolAnalysisResult ProtocolAnalyzer::analyzeTask(const ProtocolAnalysisRequest& request) const
{
    ProtocolAnalysisResult result;
    const QString taskRootPath = QDir::cleanPath(request.taskRootPath);
    if (taskRootPath.trimmed().isEmpty()) {
        result.status = QStringLiteral("failed");
        result.errorMessage = QStringLiteral("任务目录为空");
        return result;
    }

    const QString vcdPath = waveformFilePath(taskRootPath, QString::fromLatin1(kTraceVcdName));
    const QString schemaPath = waveformFilePath(taskRootPath, QString::fromLatin1(kTraceSchemaName));
    const QString analysisPath = waveformFilePath(taskRootPath, QString::fromLatin1(kTraceAnalysisName));
    result.analysisPath = analysisPath;

    if (!QFileInfo::exists(vcdPath)) {
        result.status = QStringLiteral("not_available");
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_VCD_MISSING"),
            QStringLiteral("warning"),
            QStringLiteral("当前任务未生成 VCD 波形"),
            fixedWaveformRelativePath());
        reportDiagnosticsToErrorRegistry(request, taskRootPath, &result.diagnostics);
        result.errorMessage = QStringLiteral("VCD 不存在: %1").arg(vcdPath);
        return result;
    }

    QJsonObject schema;
    if (!QFileInfo::exists(schemaPath)) {
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_SCHEMA_MISSING"),
            QStringLiteral("warning"),
            QStringLiteral("当前任务缺少 trace schema"),
            fixedTraceSchemaRelativePath());
    } else {
        QString error;
        if (!readJsonObject(schemaPath, &schema, &error)) {
            addDiagnostic(
                &result.diagnostics,
                QStringLiteral("TRACE_SCHEMA_INVALID"),
                QStringLiteral("warning"),
                QStringLiteral("trace schema 无法读取"),
                error);
        }
    }

    QString hashError;
    const QString vcdSha256 = sha256File(vcdPath, &hashError);
    const QString schemaSha256 = QFileInfo::exists(schemaPath) ? sha256File(schemaPath, nullptr) : QString();
    if (!hashError.isEmpty()) {
        result.status = QStringLiteral("failed");
        result.errorMessage = hashError;
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_VCD_UNREADABLE"),
            QStringLiteral("error"),
            QStringLiteral("VCD 无法读取"),
            hashError);
        reportDiagnosticsToErrorRegistry(request, taskRootPath, &result.diagnostics);
        return result;
    }

    const VcdParseResult parseResult = parseVcd(vcdPath);
    if (!parseResult.success) {
        result.status = QStringLiteral("failed");
        result.errorMessage = parseResult.errorMessage;
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_VCD_CONTRACT_ERROR"),
            QStringLiteral("error"),
            QStringLiteral("VCD 不符合 512-bit trace 合同"),
            parseResult.errorMessage);
        reportDiagnosticsToErrorRegistry(request, taskRootPath, &result.diagnostics);
        result.analysis = buildAnalysisObject(
            request.taskId,
            vcdSha256,
            schemaSha256,
            parseResult,
            QList<MismatchEvent>(),
            result.diagnostics);
        QString writeError;
        result.wroteAnalysis = writeJsonObject(analysisPath, result.analysis, &writeError);
        return result;
    }

    const QList<MismatchEvent> mismatchEvents = extractMismatchEvents(parseResult.samples);
    addDiagnostic(
        &result.diagnostics,
        QStringLiteral("TRACE_PROTOCOL_FIELDS_NOT_MAPPED"),
        QStringLiteral("warning"),
        QStringLiteral("8 组协议字段映射尚未固化到当前 trace profile"),
        QStringLiteral("AHB/UART/SPI/CAN/I2C/ETH/USB/JTAG 标记为 not_captured"));
    if (!mismatchEvents.isEmpty()) {
        QJsonObject context;
        context.insert(QStringLiteral("event_count"), mismatchEvents.size());
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_MISMATCH_DETECTED"),
            QStringLiteral("warning"),
            QStringLiteral("检测到 lockstep mismatch"),
            QStringLiteral("详见 key_behaviors 与 mismatch 组 transactions"),
            context);
    }

    reportDiagnosticsToErrorRegistry(request, taskRootPath, &result.diagnostics);

    result.analysis = buildAnalysisObject(
        request.taskId,
        vcdSha256,
        schemaSha256,
        parseResult,
        mismatchEvents,
        result.diagnostics);
    QString writeError;
    if (!writeJsonObject(analysisPath, result.analysis, &writeError)) {
        result.status = QStringLiteral("failed");
        result.errorMessage = writeError;
        return result;
    }

    result.success = true;
    result.wroteAnalysis = true;
    result.status = QStringLiteral("partial");
    return result;
}

}  // namespace lockstep::protocol_analyzer
