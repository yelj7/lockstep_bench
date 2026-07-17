/**********************************************************
* 文件名: protocol_analysis.cpp
* 日期: 2026-07-14
* 版本: v2.1
* 更新记录: 增加原始 AHB/SPI/I2C/JTAG 解码及统一协议事件索引
* 描述: 实现 1024 路 VCD 校验、协议字段聚合、事件解析和分析输出。
**********************************************************/

#include "protocol_analysis.h"

#include <algorithm>
#include <limits>

#include <QByteArray>
#include <QBitArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace lockstep::protocol_analyzer {
namespace {

constexpr int kTraceWidth = 1024;
constexpr int kLegacyTraceWidth = 512;
constexpr int kMaxTraceSamples = 65536;
constexpr int kMismatchLowBit = 502;
constexpr int kMismatchHighBit = 506;
constexpr char kWaveformDirName[] = "waveform";
constexpr char kTraceVcdName[] = "capture.vcd";
constexpr char kTraceSchemaName[] = "capture_schema.json";

struct VcdSample final {
    qint64 time = 0;
    quint8 mismatchMask = 0U;
    QByteArray packedHex;
    QBitArray unknownBits;
    bool hasUnknownValues = false;
};

struct VcdParseResult final {
    bool success = false;
    QString errorMessage;
    QString timescale = QStringLiteral("1 ns");
    QString timescaleUnit = QStringLiteral("ns");
    qint64 timescaleMultiplier = 1;
    bool hasUnknownValues = false;
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

struct ProtocolEvent final {
    QString groupId;
    QString type;
    QString summary;
    QString severity;
    qint64 startTime = 0;
    qint64 endTime = 0;
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
        normalized == QStringLiteral("lockstep_trace_sample[511:0]") ||
        normalized == QStringLiteral("lockstep_trace_sample[1023:0]");
}

int scalarChannelIndex(const QString& reference)
{
    const QString normalized = normalizeSignalReference(reference);
    QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("^CH(\\d+)$"), QRegularExpression::CaseInsensitiveOption)
            .match(normalized);
    if (!match.hasMatch()) {
        match = QRegularExpression(QStringLiteral("^(?:signal|CH)\\[(\\d+)\\]$"),
                                   QRegularExpression::CaseInsensitiveOption)
                    .match(normalized);
    }
    bool ok = false;
    const int channel = match.hasMatch() ? match.captured(1).toInt(&ok) : -1;
    return ok && channel >= 0 && channel < kTraceWidth ? channel : -1;
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

QString paddedBinaryValue(const QString& rawValue, bool* const hasUnknown, const int width = kTraceWidth)
{
    QString normalized;
    normalized.reserve(kTraceWidth);
    for (const QChar character : rawValue.trimmed()) {
        if (character == QLatin1Char('x') || character == QLatin1Char('X') ||
            character == QLatin1Char('z') || character == QLatin1Char('Z')) {
            if (hasUnknown != nullptr) {
                *hasUnknown = true;
            }
        }
        normalized.append(character == QLatin1Char('1') ? QLatin1Char('1') : QLatin1Char('0'));
    }

    if (normalized.size() >= width) {
        return normalized.right(width);
    }
    return QString(width - normalized.size(), QLatin1Char('0')) + normalized;
}

QBitArray unknownBitsFromPackedValue(const QString& rawValue, const int width)
{
    QBitArray unknown(width, false);
    const QString normalized = rawValue.trimmed();
    for (int index = normalized.size() - 1, bit = 0; index >= 0 && bit < width; --index, ++bit) {
        const QChar value = normalized.at(index).toLower();
        if (value == QLatin1Char('x') || value == QLatin1Char('z')) unknown.setBit(bit);
    }
    return unknown;
}

QString packedBitsToHex(const QString& packedBits)
{
    QString hex;
    hex.reserve((packedBits.size() + 3) / 4);
    for (int offset = 0; offset < packedBits.size(); offset += 4) {
        int nibble = 0;
        for (int bit = 0; bit < 4; ++bit) {
            nibble <<= 1;
            if (offset + bit < packedBits.size() && packedBits.at(offset + bit) == QLatin1Char('1')) {
                nibble |= 1;
            }
        }
        hex.append(QString::number(nibble, 16).toUpper());
    }
    return hex;
}

bool bitFromPackedValue(const QString& paddedValue, const int bit)
{
    const int index = paddedValue.size() - 1 - bit;
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

quint64 sampleValue(const VcdSample& sample, const int lsb, const int width)
{
    if (lsb < 0 || width <= 0 || width > 64) {
        return 0U;
    }
    quint64 value = 0U;
    for (int bit = 0; bit < width; ++bit) {
        const int packedBit = lsb + bit;
        const int nibbleIndex = sample.packedHex.size() - 1 - packedBit / 4;
        if (nibbleIndex < 0 || nibbleIndex >= sample.packedHex.size()) {
            continue;
        }
        const char digit = sample.packedHex.at(nibbleIndex);
        const int nibble = digit >= '0' && digit <= '9'
            ? digit - '0' : digit - 'A' + 10;
        if ((nibble & (1 << (packedBit % 4))) != 0) {
            value |= quint64(1) << bit;
        }
    }
    return value;
}

bool sampleBit(const VcdSample& sample, const int bit)
{
    return sampleValue(sample, bit, 1) != 0U;
}

bool sampleRangeUnknown(const VcdSample& sample, const int lsb, const int width)
{
    if (lsb < 0 || width <= 0) return true;
    for (int bit = lsb; bit < lsb + width && bit < sample.unknownBits.size(); ++bit) {
        if (sample.unknownBits.testBit(bit)) return true;
    }
    return false;
}

QString hexValue(const quint64 value, const int width)
{
    return QStringLiteral("0x%1").arg(
        QStringLiteral("%1")
            .arg(value, qMax(1, (width + 3) / 4), 16, QLatin1Char('0'))
            .toUpper());
}

QString ahbResponseText(const quint64 response)
{
    switch (response & 0x3U) {
    case 0U: return QStringLiteral("OKAY");
    case 1U: return QStringLiteral("ERROR");
    case 2U: return QStringLiteral("RETRY");
    case 3U: return QStringLiteral("SPLIT");
    default: return QStringLiteral("UNKNOWN");
    }
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
    QHash<QString, int> scalarChannels;
    QVector<QString> scalarIdentifiers;
    QVector<QString> scalarReferences;
    QByteArray scalarState(kTraceWidth, '0');
    bool scalarDirty = false;
    bool trustedDeclarationOrder = false;
    const auto finalizeScalarChannels = [&]() {
        const QList<QPair<int, QString>> anchors = {
            {0, QStringLiteral("sample_abs_index[0]")},
            {32, QStringLiteral("ahb_haddr[0]")},
            {417, QStringLiteral("ahb_htrans[0]")},
            {512, QStringLiteral("UART_TX")},
            {544, QStringLiteral("SPI_SCLK")},
            {608, QStringLiteral("I2C_SCL")},
            {736, QStringLiteral("JTAG_TCK")},
            {1023, QStringLiteral("NOELV_1024_RESERVED[239]")}
        };
        if (scalarChannels.size() == kTraceWidth || !trustedDeclarationOrder ||
            scalarIdentifiers.size() != kTraceWidth || scalarReferences.size() != kTraceWidth ||
            std::any_of(anchors.cbegin(), anchors.cend(), [&scalarReferences](const auto& anchor) {
                return scalarReferences.at(anchor.first) != anchor.second;
            })) return;
        scalarChannels.clear();
        for (int channel = 0; channel < scalarIdentifiers.size(); ++channel) {
            scalarChannels.insert(scalarIdentifiers.at(channel), channel);
        }
    };
    const auto appendScalarSample = [&]() {
        if (!scalarDirty) return;
        QString binary(kTraceWidth, QLatin1Char('0'));
        bool currentHasUnknown = false;
        for (int channel = 0; channel < kTraceWidth; ++channel) {
            const char value = scalarState.at(channel);
            currentHasUnknown = currentHasUnknown || value == 'x' || value == 'z';
            binary[kTraceWidth - 1 - channel] = value == '1' ? QLatin1Char('1') : QLatin1Char('0');
        }
        VcdSample sample;
        sample.time = currentTime;
        sample.mismatchMask = mismatchMaskFromPackedValue(binary);
        sample.packedHex = packedBitsToHex(binary).toLatin1();
        sample.unknownBits = QBitArray(kTraceWidth, false);
        for (int channel = 0; channel < kTraceWidth; ++channel) {
            const char value = scalarState.at(channel);
            if (value == 'x' || value == 'z') sample.unknownBits.setBit(channel);
        }
        sample.hasUnknownValues = currentHasUnknown;
        result.samples.append(sample);
        result.hasUnknownValues = result.hasUnknownValues || currentHasUnknown;
        scalarDirty = false;
    };

    bool readingTimescale = false;
    QString timescaleText;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.contains(QStringLiteral("host export"), Qt::CaseInsensitive)) {
            trustedDeclarationOrder = true;
        }

        if (line.startsWith(QStringLiteral("$timescale"))) {
            readingTimescale = true;
            timescaleText = line.mid(QStringLiteral("$timescale").size()).trimmed();
            timescaleText.remove(QStringLiteral("$end"));
            timescaleText = timescaleText.trimmed();
            if (line.contains(QStringLiteral("$end"))) {
                readingTimescale = false;
            }
            const QRegularExpressionMatch match = QRegularExpression(
                QStringLiteral("(\\d+)\\s*([a-zA-Z]+)")).match(timescaleText);
            if (match.hasMatch()) {
                result.timescale = QStringLiteral("%1 %2").arg(match.captured(1), match.captured(2));
                result.timescaleMultiplier = match.captured(1).toLongLong();
                result.timescaleUnit = match.captured(2).toLower();
            }
            continue;
        }

        if (readingTimescale) {
            timescaleText += QLatin1Char(' ') + line;
            if (line.contains(QStringLiteral("$end"))) {
                readingTimescale = false;
                timescaleText.remove(QStringLiteral("$end"));
                const QRegularExpressionMatch match = QRegularExpression(
                    QStringLiteral("(\\d+)\\s*([a-zA-Z]+)")).match(timescaleText.trimmed());
                if (match.hasMatch()) {
                    result.timescale = QStringLiteral("%1 %2").arg(match.captured(1), match.captured(2));
                    result.timescaleMultiplier = match.captured(1).toLongLong();
                    result.timescaleUnit = match.captured(2).toLower();
                }
            }
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
                } else if (widthOk && width == 1) {
                    scalarIdentifiers.append(identifier);
                    scalarReferences.append(normalizeSignalReference(reference));
                    const int channel = scalarChannelIndex(reference);
                    if (channel >= 0) scalarChannels.insert(identifier, channel);
                }
            }
            continue;
        }

        if (line.startsWith(QLatin1Char('#'))) {
            finalizeScalarChannels();
            if (scalarChannels.size() == kTraceWidth) appendScalarSample();
            bool ok = false;
            const qint64 parsedTime = line.mid(1).toLongLong(&ok);
            if (!ok) {
                result.errorMessage = QStringLiteral("VCD 时间戳无法解析: %1").arg(line);
                return result;
            }
            currentTime = parsedTime;
            continue;
        }

        if (scalarChannels.size() == kTraceWidth && line.size() >= 2 &&
            (line.at(0) == QLatin1Char('0') || line.at(0) == QLatin1Char('1') ||
             line.at(0) == QLatin1Char('x') || line.at(0) == QLatin1Char('z'))) {
            const QString identifier = line.mid(1).trimmed();
            const auto channelIt = scalarChannels.constFind(identifier);
            if (channelIt != scalarChannels.constEnd()) {
                const char value = line.at(0).toLower().toLatin1();
                scalarState[*channelIt] = value;
                scalarDirty = true;
            }
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
            bool hasUnknown = false;
            const QString paddedValue = paddedBinaryValue(tokens.at(0).mid(1), &hasUnknown,
                                                         sampleWidth == kLegacyTraceWidth ? kLegacyTraceWidth : kTraceWidth);
            result.hasUnknownValues = result.hasUnknownValues || hasUnknown;
            if (result.samples.size() >= kMaxTraceSamples) {
                result.errorMessage = QStringLiteral("VCD 采样点超过上位机上限 %1").arg(kMaxTraceSamples);
                return result;
            }
            VcdSample sample;
            sample.time = currentTime;
            sample.mismatchMask = mismatchMaskFromPackedValue(paddedValue);
            sample.packedHex = packedBitsToHex(paddedValue).toLatin1();
            sample.unknownBits = unknownBitsFromPackedValue(tokens.at(0).mid(1), sampleWidth);
            sample.hasUnknownValues = hasUnknown;
            result.samples.append(sample);
        }
    }

    finalizeScalarChannels();
    if (scalarChannels.size() == kTraceWidth) {
        appendScalarSample();
        foundTrace = true;
        sampleIdentifier = QStringLiteral("CH0..CH1023");
        sampleWidth = kTraceWidth;
    }

    if (!foundTrace) {
        result.errorMessage = QStringLiteral("VCD 缺少 CH0..CH1023 标量通道");
        return result;
    }
    if (sampleWidth != kTraceWidth && sampleWidth != kLegacyTraceWidth) {
        result.errorMessage = QStringLiteral("VCD 样本位宽为 %1，不是 1024").arg(sampleWidth);
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
    if (result.timescaleMultiplier > 1) {
        for (VcdSample& sample : result.samples) {
            if (sample.time > std::numeric_limits<qint64>::max() / result.timescaleMultiplier) {
                result.errorMessage = QStringLiteral("VCD 时间戳乘 timescale 后溢出");
                result.success = false;
                return result;
            }
            sample.time *= result.timescaleMultiplier;
        }
    }
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
        if (sampleRangeUnknown(sample, kMismatchLowBit, kMismatchHighBit - kMismatchLowBit + 1)) {
            continue;
        }
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

QList<ProtocolEvent> decodeProtocolEvents(const QList<VcdSample>& samples)
{
    QList<ProtocolEvent> events;
    if (samples.isEmpty()) {
        return events;
    }

    const auto append = [&events](const QString& groupId, const QString& type,
                                  const qint64 start, const qint64 end,
                                  const QString& summary, const QString& severity = QString()) {
        ProtocolEvent event;
        event.groupId = groupId;
        event.type = type;
        event.startTime = start;
        event.endTime = qMax(start + 1, end);
        event.summary = summary;
        event.severity = severity;
        events.append(event);
    };

    bool ahbOpen = false;
    VcdSample ahbStart;
    bool spiOpen = false;
    VcdSample spiStart;
    int spiBitCount = 0;
    quint64 spiTx = 0U;
    quint64 spiRx = 0U;
    bool spiUsingHint = false;
    bool canOpen = false;
    VcdSample canStart;
    bool i2cOpen = false;
    VcdSample i2cStart;
    int i2cBitCount = 0;
    quint64 i2cByte = 0U;
    QList<quint64> i2cBytes;
    bool ethOpen = false;
    VcdSample ethStart;
    bool usbOpen = false;
    VcdSample usbStart;
    VcdSample previous = samples.first();

    const bool wide = samples.first().packedHex.size() > (kLegacyTraceWidth / 4);
    const auto p = [wide](const int legacyBit, const int wideBit) { return wide ? wideBit : legacyBit; };
    const bool uartReal = !wide || sampleBit(samples.first(), 768);
    const bool spiReal = !wide || sampleBit(samples.first(), 781);
    const bool canReal = !wide || sampleBit(samples.first(), 770);
    const bool i2cReal = !wide || sampleBit(samples.first(), 782);
    const bool ethReal = !wide || sampleBit(samples.first(), 773);
    const bool usbReal = false;  // 当前两个 profile 都没有可验收的真实 USB PHY source。
    const bool jtagReal = !wide || sampleBit(samples.first(), 776);

    for (const VcdSample& sample : samples) {
        const bool ahbKnown =
            !sampleRangeUnknown(sample, p(0, 32), 32) &&
            !sampleRangeUnknown(sample, p(32, 417), 2) &&
            !sampleRangeUnknown(sample, p(34, 416), 1) &&
            !sampleRangeUnknown(sample, p(41, 429), 3) &&
            !sampleRangeUnknown(sample, p(44, 64), 32) &&
            !sampleRangeUnknown(sample, p(76, 192), 32);
        const bool ahbActive = ahbKnown &&
            (sampleValue(sample, p(32, 417), 2) & 0x2U) != 0U;
        if (ahbOpen && ahbKnown && sampleBit(sample, p(41, 429))) {
            const bool write = sampleBit(ahbStart, p(34, 416));
            const quint64 address = sampleValue(ahbStart, p(0, 32), 32);
            const quint64 data = sampleValue(sample, write ? p(44, 64) : p(76, 192), 32);
            const quint64 response = sampleValue(sample, p(42, 430), 2);
            append(QStringLiteral("ahb"), QStringLiteral("ahb_transfer"),
                   ahbStart.time, sample.time,
                   QStringLiteral("AHB %1 %2 DATA=%3 RESP=%4")
                       .arg(write ? QStringLiteral("WRITE") : QStringLiteral("READ"),
                            hexValue(address, 32), hexValue(data, 32), ahbResponseText(response)),
                   response == 0U ? QString() : QStringLiteral("error"));
            ahbOpen = false;
        }
        if (ahbActive && !ahbOpen) {
            ahbOpen = true;
            ahbStart = sample;
        }

        const bool uartKnown = !sampleRangeUnknown(sample, p(112, 512), 32) &&
            !sampleRangeUnknown(previous, p(112, 512), 32);
        if (uartReal && uartKnown && sampleBit(sample, p(142, 518)) &&
            !sampleBit(previous, p(142, 518))) {
            const bool tx = sampleBit(sample, p(114, 516)) || !sampleBit(sample, p(115, 517));
            const quint64 data = sampleValue(sample, tx ? p(118, 520) : p(126, 528), 8);
            append(QStringLiteral("uart"), QStringLiteral("uart_frame"),
                   previous.time, sample.time,
                   QStringLiteral("UART %1 %2").arg(tx ? QStringLiteral("TX") : QStringLiteral("RX"),
                                                     hexValue(data, 8)));
        }

        const bool spiCs = sampleBit(sample, p(147, 547));
        const bool previousSpiCs = sampleBit(previous, p(147, 547));
        const bool spiFrameHint = sampleBit(sample, p(148, 548)) &&
            !sampleBit(previous, p(148, 548));
        const bool spiKnown = !sampleRangeUnknown(sample, p(144, 544), 32) &&
            !sampleRangeUnknown(previous, p(144, 544), 32);
        if (spiReal && spiKnown && ((!spiCs && previousSpiCs) || spiFrameHint)) {
            spiOpen = true;
            spiStart = sample;
            spiBitCount = 0;
            spiTx = 0U;
            spiRx = 0U;
            spiUsingHint = spiFrameHint;
        }
        const bool spiClockRising = sampleBit(sample, p(144, 544)) &&
            !sampleBit(previous, p(144, 544));
        if (spiOpen && spiKnown && !spiCs && spiClockRising) {
            spiTx = (spiTx << 1U) | (sampleBit(sample, p(145, 545)) ? 1U : 0U);
            spiRx = (spiRx << 1U) | (sampleBit(sample, p(146, 546)) ? 1U : 0U);
            ++spiBitCount;
        }
        if (spiOpen && spiKnown && ((spiCs && !previousSpiCs) ||
                        (spiUsingHint && !sampleBit(sample, p(175, 575)) &&
                         sample.time > spiStart.time))) {
            const quint64 tx = spiBitCount > 0 ? spiTx : sampleValue(sample, p(152, 552), 8);
            const quint64 rx = spiBitCount > 0 ? spiRx : sampleValue(sample, p(160, 560), 8);
            append(QStringLiteral("spi"), QStringLiteral("spi_transfer"), spiStart.time, sample.time,
                   QStringLiteral("SPI %1 bits TX=%2 RX=%3")
                       .arg(spiBitCount > 0 ? spiBitCount : 8)
                       .arg(hexValue(tx, qMax(8, spiBitCount)), hexValue(rx, qMax(8, spiBitCount))));
            spiOpen = false;
        }

        const bool canKnown = !sampleRangeUnknown(sample, p(176, 576), 32) &&
            !sampleRangeUnknown(previous, p(176, 576), 32);
        if (canReal && canKnown && sampleBit(sample, p(178, 578)) &&
            !sampleBit(previous, p(178, 578))) {
            canOpen = true;
            canStart = sample;
        }
        if (canOpen && canKnown && sampleBit(sample, p(179, 579))) {
            append(QStringLiteral("can"), QStringLiteral("can_frame"), canStart.time, sample.time,
                   QStringLiteral("CAN ID=%1 DATA=%2")
                       .arg(hexValue(sampleValue(canStart, p(184, 584), 11), 11),
                            hexValue(sampleValue(sample, p(195, 595), 4), 4)),
                   sampleBit(sample, p(183, 583)) ? QStringLiteral("error") : QString());
            canOpen = false;
        }

        const bool i2cScl = sampleBit(sample, p(208, 608));
        const bool i2cSda = sampleBit(sample, p(209, 609));
        const bool previousI2cScl = sampleBit(previous, p(208, 608));
        const bool previousI2cSda = sampleBit(previous, p(209, 609));
        const bool i2cStartCondition = i2cScl && previousI2cSda && !i2cSda;
        const bool i2cStopCondition = i2cScl && !previousI2cSda && i2cSda;
        const bool i2cKnown = !sampleRangeUnknown(sample, p(208, 608), 32) &&
            !sampleRangeUnknown(previous, p(208, 608), 32);
        if (i2cReal && i2cKnown && (i2cStartCondition ||
                        (sampleBit(sample, p(210, 610)) && !sampleBit(previous, p(210, 610))))) {
            if (i2cOpen && !i2cBytes.isEmpty()) {
                const quint64 addressByte = i2cBytes.first();
                append(QStringLiteral("i2c"), QStringLiteral("i2c_segment"),
                       i2cStart.time, sample.time,
                       QStringLiteral("I2C REPEATED_START %1 ADDR=%2 DATA=%3")
                           .arg((addressByte & 1U) != 0U ? QStringLiteral("READ") : QStringLiteral("WRITE"),
                                hexValue(addressByte >> 1U, 7),
                                i2cBytes.size() > 1 ? hexValue(i2cBytes.at(1), 8) : QStringLiteral("n/a")));
            }
            i2cOpen = true;
            i2cStart = sample;
            i2cBitCount = 0;
            i2cByte = 0U;
            i2cBytes.clear();
        }
        if (i2cOpen && i2cKnown && i2cScl && !previousI2cScl && !i2cStartCondition) {
            if ((i2cBitCount % 9) < 8) {
                i2cByte = (i2cByte << 1U) | (i2cSda ? 1U : 0U);
            } else {
                i2cBytes.append(i2cByte);
                i2cByte = 0U;
            }
            ++i2cBitCount;
        }
        if (i2cOpen && i2cKnown &&
            (i2cStopCondition || sampleBit(sample, p(211, 611)))) {
            const quint64 addressByte = i2cBytes.isEmpty()
                ? sampleValue(i2cStart, p(216, 616), 7) << 1U : i2cBytes.first();
            const bool read = (addressByte & 1U) != 0U;
            const quint64 data = i2cBytes.size() > 1
                ? i2cBytes.at(1) : sampleValue(sample, p(223, 623), 8);
            append(QStringLiteral("i2c"), QStringLiteral("i2c_transfer"), i2cStart.time, sample.time,
                   QStringLiteral("I2C %1 ADDR=%2 DATA=%3")
                       .arg(read ? QStringLiteral("READ") : QStringLiteral("WRITE"),
                            hexValue(addressByte >> 1U, 7), hexValue(data, 8)),
                   sampleBit(sample, p(239, 639)) ? QStringLiteral("error") : QString());
            i2cOpen = false;
        }

        const bool ethKnown = !sampleRangeUnknown(sample, p(240, 640), 64) &&
            !sampleRangeUnknown(previous, p(240, 640), 64);
        if (ethReal && ethKnown && sampleBit(sample, p(246, 646)) &&
            !sampleBit(previous, p(246, 646))) {
            ethOpen = true;
            ethStart = sample;
        }
        if (ethOpen && ethKnown && sampleBit(sample, p(247, 647))) {
            append(QStringLiteral("eth"), QStringLiteral("eth_frame"), ethStart.time, sample.time,
                   QStringLiteral("ETH FRAME TYPE=%1")
                       .arg(hexValue(sampleValue(sample, p(288, 688), 16), 16)),
                   (sampleBit(sample, p(241, 641)) || sampleBit(sample, p(243, 643)))
                       ? QStringLiteral("error") : QString());
            ethOpen = false;
        }

        const bool usbKnown = !sampleRangeUnknown(sample, p(304, 704), 32) &&
            !sampleRangeUnknown(previous, p(304, 704), 32);
        if (usbReal && usbKnown && sampleBit(sample, p(309, 709)) &&
            !sampleBit(previous, p(309, 709))) {
            usbOpen = true;
            usbStart = sample;
        }
        if (usbOpen && usbKnown && sampleBit(sample, p(310, 710))) {
            append(QStringLiteral("usb"), QStringLiteral("usb_packet"), usbStart.time, sample.time,
                   QStringLiteral("USB PID=%1 EP=%2 DATA=%3")
                       .arg(hexValue(sampleValue(usbStart, p(312, 712), 4), 4),
                            QString::number(sampleValue(sample, p(316, 716), 4)),
                            hexValue(sampleValue(sample, p(320, 720), 8), 8)));
            usbOpen = false;
        }

        const bool tckRising = sampleBit(sample, p(336, 736)) && !sampleBit(previous, p(336, 736));
        const bool jtagKnown = !sampleRangeUnknown(sample, p(336, 736), 32) &&
            !sampleRangeUnknown(previous, p(336, 736), 32);
        if (jtagReal && jtagKnown && tckRising) {
            append(QStringLiteral("jtag"), QStringLiteral("jtag_cycle"), previous.time, sample.time,
                   QStringLiteral("JTAG STATE=%1 TMS=%2 TDI=%3 TDO=%4")
                       .arg(hexValue(sampleValue(sample, p(363, 760), 5), 5),
                            sampleBit(sample, p(337, 737)) ? QStringLiteral("1") : QStringLiteral("0"),
                            sampleBit(sample, p(338, 738)) ? QStringLiteral("1") : QStringLiteral("0"),
                            sampleBit(sample, p(339, 739)) ? QStringLiteral("1") : QStringLiteral("0")));
        }
        previous = sample;
    }
    return events;
}

QJsonObject protocolEventToJson(const ProtocolEvent& event)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), event.type);
    object.insert(QStringLiteral("group_id"), event.groupId);
    object.insert(QStringLiteral("start_time"), event.startTime);
    object.insert(QStringLiteral("end_time"), event.endTime);
    object.insert(QStringLiteral("duration"), qMax<qint64>(0, event.endTime - event.startTime));
    object.insert(QStringLiteral("summary"), event.summary);
    if (!event.severity.isEmpty()) {
        object.insert(QStringLiteral("severity"), event.severity);
    }
    return object;
}

QJsonArray protocolEventsToJson(const QList<ProtocolEvent>& events)
{
    QJsonArray array;
    for (const ProtocolEvent& event : events) array.append(protocolEventToJson(event));
    return array;
}

QJsonObject makeProtocolGroup(
    const ProtocolGroupDefinition& definition,
    const QString& status,
    const QString& reason)
{
    QJsonObject group;
    group.insert(QStringLiteral("id"), definition.id);
    group.insert(QStringLiteral("display_name"), definition.displayName);
    group.insert(QStringLiteral("status"), status);
    group.insert(QStringLiteral("reason"), reason);
    group.insert(QStringLiteral("transactions"), QJsonArray());
    QJsonArray fields;
    for (const ProtocolFieldDefinition& definitionField : definition.fields) {
        QJsonObject field;
        field.insert(QStringLiteral("name"), definitionField.name);
        field.insert(QStringLiteral("display_name"), definitionField.displayName);
        field.insert(QStringLiteral("sample_bit"), definitionField.lsb);
        field.insert(QStringLiteral("width"), definitionField.width);
        field.insert(QStringLiteral("format"), definitionField.width > 1
            ? QStringLiteral("hex") : QStringLiteral("level"));
        field.insert(QStringLiteral("error_signal"), definitionField.errorSignal);
        fields.append(field);
    }
    group.insert(QStringLiteral("fields"), fields);
    return group;
}

QJsonObject makeMismatchGroup(const QList<MismatchEvent>& events)
{
    const ProtocolGroupDefinition definition = fixedProtocolGroups().last();
    QJsonObject group = makeProtocolGroup(
        definition,
        events.isEmpty() ? QStringLiteral("complete") : QStringLiteral("event_detected"),
        events.isEmpty() ? QStringLiteral("未检测到 mismatch") : QStringLiteral("检测到 mismatch 事件"));

    QJsonArray transactions;
    for (const MismatchEvent& event : events) {
        transactions.append(mismatchEventToJson(event));
    }
    group.insert(QStringLiteral("transactions"), transactions);

    return group;
}

QJsonArray defaultGroups(
    const QList<ProtocolEvent>& protocolEvents,
    const QList<MismatchEvent>& mismatchEvents,
    const QList<VcdSample>& samples)
{
    QJsonArray groups;
    const QList<ProtocolGroupDefinition> definitions = fixedProtocolGroups();
    for (int index = 0; index < definitions.size() - 1; ++index) {
        QJsonObject group = makeProtocolGroup(
            definitions.at(index),
            QStringLiteral("idle"),
            QStringLiteral("真实协议信号已解析，采样窗口内未检测到完整事务"));
        const bool wide = !samples.isEmpty() && samples.first().packedHex.size() > (kLegacyTraceWidth / 4);
        const QString id = definitions.at(index).id;
        bool realSource = true;
        if (wide && id == QStringLiteral("uart")) realSource = sampleBit(samples.first(), 768);
        if (wide && id == QStringLiteral("spi")) realSource = sampleBit(samples.first(), 781);
        if (wide && id == QStringLiteral("can")) realSource = sampleBit(samples.first(), 770);
        if (wide && id == QStringLiteral("i2c")) realSource = sampleBit(samples.first(), 782);
        if (wide && id == QStringLiteral("eth")) realSource = sampleBit(samples.first(), 773);
        if (id == QStringLiteral("usb")) realSource = false;
        if (wide && id == QStringLiteral("jtag")) realSource = sampleBit(samples.first(), 776);
        if (!realSource) {
            group.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
            group.insert(QStringLiteral("reason"), QStringLiteral("design_gap: 当前位图仅提供 synthetic/loopback source"));
            groups.append(group);
            continue;
        }
        QJsonArray transactions;
        for (const ProtocolEvent& event : protocolEvents) {
            if (event.groupId == definitions.at(index).id) {
                transactions.append(protocolEventToJson(event));
            }
        }
        if (!transactions.isEmpty()) {
            group.insert(QStringLiteral("status"), QStringLiteral("event_detected"));
            group.insert(QStringLiteral("transactions"), transactions);
        }
        groups.append(group);
    }
    groups.append(makeMismatchGroup(mismatchEvents));
    return groups;
}

QJsonArray samplesToJson(const QList<VcdSample>& samples)
{
    QJsonArray array;
    for (const VcdSample& sample : samples) {
        QJsonObject object;
        object.insert(QStringLiteral("time"), sample.time);
        object.insert(QStringLiteral("value_hex"), QString::fromLatin1(sample.packedHex));
        object.insert(QStringLiteral("unknown"), sample.hasUnknownValues);
        array.append(object);
    }
    return array;
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
    const QList<ProtocolEvent>& protocolEvents,
    const QList<MismatchEvent>& mismatchEvents,
    const QList<ProtocolDiagnostic>& diagnostics)
{
    QJsonObject input;
    input.insert(QStringLiteral("vcd_file"), fixedWaveformRelativePath());
    input.insert(QStringLiteral("schema_file"), fixedTraceSchemaRelativePath());
    input.insert(QStringLiteral("vcd_sha256"), vcdSha256);
    input.insert(QStringLiteral("schema_sha256"), schemaSha256);

    QJsonObject timeBase;
    timeBase.insert(QStringLiteral("unit"), parseResult.timescaleUnit);
    timeBase.insert(QStringLiteral("timescale"), parseResult.timescale);
    timeBase.insert(QStringLiteral("start_time"), parseResult.startTime);
    timeBase.insert(QStringLiteral("end_time"), parseResult.endTime);

    QJsonObject analysis;
    analysis.insert(QStringLiteral("schema_version"), QStringLiteral("1.1"));
    analysis.insert(QStringLiteral("task_id"), taskId);
    analysis.insert(QStringLiteral("generated_at"), currentTimeText());
    analysis.insert(QStringLiteral("input"), input);
    analysis.insert(QStringLiteral("status"), QStringLiteral("complete"));
    analysis.insert(QStringLiteral("time_base"), timeBase);
    analysis.insert(QStringLiteral("groups"), defaultGroups(protocolEvents, mismatchEvents, parseResult.samples));
    analysis.insert(QStringLiteral("protocol_events"), protocolEventsToJson(protocolEvents));
    analysis.insert(QStringLiteral("samples"), samplesToJson(parseResult.samples));
    analysis.insert(QStringLiteral("key_behaviors"), eventsToJson(mismatchEvents));
    analysis.insert(QStringLiteral("diagnostic_summary"), diagnosticsToJson(diagnostics));
    analysis.insert(QStringLiteral("diagnostic_details"), diagnosticsToJson(diagnostics));
    return analysis;
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

QList<ProtocolGroupDefinition> fixedProtocolGroups()
{
    const auto field = [](const QString& name, const int lsb, const int width = 1,
                          const bool errorSignal = false, const QString& displayName = QString()) {
        ProtocolFieldDefinition definition;
        definition.name = name;
        definition.displayName = displayName.isEmpty()
            ? (width > 1 ? QStringLiteral("%1[%2:0]").arg(name).arg(width - 1) : name)
            : displayName;
        definition.lsb = lsb;
        definition.width = width;
        definition.errorSignal = errorSignal;
        return definition;
    };
    const auto group = [](const QString& id, const QString& displayName,
                          const QList<ProtocolFieldDefinition>& fields) {
        ProtocolGroupDefinition definition;
        definition.id = id;
        definition.displayName = displayName;
        definition.fields = fields;
        return definition;
    };

    QList<ProtocolGroupDefinition> definitions = {
        group(QStringLiteral("ahb"), QStringLiteral("AHB"), {
            field(QStringLiteral("haddr"), 32, 32), field(QStringLiteral("htrans"), 417, 2),
            field(QStringLiteral("hwrite"), 416), field(QStringLiteral("hsize"), 419, 3),
            field(QStringLiteral("hburst"), 422, 3), field(QStringLiteral("hready"), 429),
            field(QStringLiteral("hresp"), 430, 2), field(QStringLiteral("hwdata"), 64, 32),
            field(QStringLiteral("hrdata"), 192, 32), field(QStringLiteral("hsel"), 320, 16)
        }),
        group(QStringLiteral("uart"), QStringLiteral("UART"), {
            field(QStringLiteral("tx"), 512), field(QStringLiteral("rx"), 513),
            field(QStringLiteral("cts_n"), 514), field(QStringLiteral("rts_n"), 515)
        }),
        group(QStringLiteral("spi"), QStringLiteral("SPI"), {
            field(QStringLiteral("sclk"), 144), field(QStringLiteral("mosi"), 145),
            field(QStringLiteral("miso"), 146), field(QStringLiteral("cs_n"), 147)
        }),
        group(QStringLiteral("can"), QStringLiteral("CAN"), {
            field(QStringLiteral("rx"), 176), field(QStringLiteral("tx"), 177)
        }),
        group(QStringLiteral("i2c"), QStringLiteral("I2C"), {
            field(QStringLiteral("scl"), 208), field(QStringLiteral("sda"), 209)
        }),
        group(QStringLiteral("eth"), QStringLiteral("ETH"), {
            field(QStringLiteral("tx_en"), 240), field(QStringLiteral("tx_er"), 241, 1, true),
            field(QStringLiteral("rx_dv"), 242), field(QStringLiteral("rx_er"), 243, 1, true),
            field(QStringLiteral("txd"), 248, 8), field(QStringLiteral("rxd"), 256, 8)
        }),
        group(QStringLiteral("usb"), QStringLiteral("USB"), {
            field(QStringLiteral("dp"), 304), field(QStringLiteral("dm"), 305)
        }),
        group(QStringLiteral("jtag"), QStringLiteral("JTAG"), {
            field(QStringLiteral("tck"), 736), field(QStringLiteral("tms"), 737),
            field(QStringLiteral("tdi"), 738), field(QStringLiteral("tdo"), 739),
            field(QStringLiteral("rv_tck"), 740), field(QStringLiteral("rv_tms"), 741),
            field(QStringLiteral("rv_tdi"), 742), field(QStringLiteral("rv_tdo"), 743)
        }),
        group(QStringLiteral("mismatch"), QStringLiteral("Mismatch"), {
            field(QStringLiteral("mismatch[4]"), 506, 1, true,
                  QStringLiteral("mismatch[4] ahb_master_output")),
            field(QStringLiteral("mismatch[3]"), 505, 1, true,
                  QStringLiteral("mismatch[3] irq_output")),
            field(QStringLiteral("mismatch[2]"), 504, 1, true,
                  QStringLiteral("mismatch[2] debug_output")),
            field(QStringLiteral("mismatch[1]"), 503, 1, true,
                  QStringLiteral("mismatch[1] trace_output")),
            field(QStringLiteral("mismatch[0]"), 502, 1, true,
                  QStringLiteral("mismatch[0] counter_output"))
        })
    };
    // 旧 512-bit profile 的协议探针从 bit 112 开始，1024-bit 硬件从 bit 512 开始。
    for (ProtocolGroupDefinition& definition : definitions) {
        if (definition.id == QStringLiteral("ahb") || definition.id == QStringLiteral("uart") ||
            definition.id == QStringLiteral("jtag") || definition.id == QStringLiteral("mismatch")) {
            continue;
        }
        for (ProtocolFieldDefinition& protocolField : definition.fields) {
            protocolField.lsb += 400;
        }
    }
    return definitions;
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

    QString vcdPath = waveformFilePath(taskRootPath, QString::fromLatin1(kTraceVcdName));
    QString schemaPath = waveformFilePath(taskRootPath, QString::fromLatin1(kTraceSchemaName));
    if (!QFileInfo::exists(vcdPath)) {
        const QString legacy = waveformFilePath(taskRootPath, QStringLiteral("lockstep_trace.vcd"));
        if (QFileInfo::exists(legacy)) vcdPath = legacy;
    }
    if (!QFileInfo::exists(schemaPath)) {
        const QString legacy = waveformFilePath(taskRootPath, QStringLiteral("lockstep_trace_schema.json"));
        if (QFileInfo::exists(legacy)) schemaPath = legacy;
    }
    const QString analysisPath = QDir(taskRootPath).filePath(fixedTraceAnalysisRelativePath());
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
            QStringLiteral("VCD 不符合 1024 路标量 trace 合同"),
            parseResult.errorMessage);
        reportDiagnosticsToErrorRegistry(request, taskRootPath, &result.diagnostics);
        result.analysis = buildAnalysisObject(
            request.taskId,
            vcdSha256,
            schemaSha256,
            parseResult,
            QList<ProtocolEvent>(),
            QList<MismatchEvent>(),
            result.diagnostics);
        result.analysis.insert(QStringLiteral("status"), QStringLiteral("failed"));
        QJsonArray invalidGroups = result.analysis.value(QStringLiteral("groups")).toArray();
        for (int index = 0; index < invalidGroups.size(); ++index) {
            QJsonObject group = invalidGroups.at(index).toObject();
            group.insert(QStringLiteral("status"), QStringLiteral("invalid"));
            group.insert(QStringLiteral("reason"), parseResult.errorMessage);
            invalidGroups.replace(index, group);
        }
        result.analysis.insert(QStringLiteral("groups"), invalidGroups);
        QString writeError;
        result.wroteAnalysis = writeJsonObject(analysisPath, result.analysis, &writeError);
        return result;
    }

    const QList<ProtocolEvent> protocolEvents = decodeProtocolEvents(parseResult.samples);
    const QList<MismatchEvent> mismatchEvents = extractMismatchEvents(parseResult.samples);
    if (parseResult.hasUnknownValues) {
        addDiagnostic(
            &result.diagnostics,
            QStringLiteral("TRACE_UNKNOWN_VALUES"),
            QStringLiteral("warning"),
            QStringLiteral("VCD 包含 x/z 未知值"),
            QStringLiteral("含未知值的采样点不参与协议事件判定，波形值按 0 占位显示"));
    }
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
        protocolEvents,
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
    result.status = QStringLiteral("complete");
    return result;
}

}  // namespace lockstep::protocol_analyzer
