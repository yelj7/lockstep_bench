/**********************************************************
* 文件名: protocol_analysis.cpp
* 日期: 2026-07-19
* 版本: v3.2
* 更新记录: 基于全局时间戳重建稀疏 UART 115200 8N1 字节流。
* 描述: 实现 1024 路 VCD 与稀疏事件的统一时间轴、协议解析和分析输出。
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
    QJsonObject fields;
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

enum class TapState {
    TestLogicReset, RunTestIdle, SelectDrScan, CaptureDr, ShiftDr, Exit1Dr,
    PauseDr, Exit2Dr, UpdateDr, SelectIrScan, CaptureIr, ShiftIr, Exit1Ir,
    PauseIr, Exit2Ir, UpdateIr
};

TapState nextTapState(const TapState state, const bool tms)
{
    switch (state) {
    case TapState::TestLogicReset: return tms ? TapState::TestLogicReset : TapState::RunTestIdle;
    case TapState::RunTestIdle: return tms ? TapState::SelectDrScan : TapState::RunTestIdle;
    case TapState::SelectDrScan: return tms ? TapState::SelectIrScan : TapState::CaptureDr;
    case TapState::CaptureDr: return tms ? TapState::Exit1Dr : TapState::ShiftDr;
    case TapState::ShiftDr: return tms ? TapState::Exit1Dr : TapState::ShiftDr;
    case TapState::Exit1Dr: return tms ? TapState::UpdateDr : TapState::PauseDr;
    case TapState::PauseDr: return tms ? TapState::Exit2Dr : TapState::PauseDr;
    case TapState::Exit2Dr: return tms ? TapState::UpdateDr : TapState::ShiftDr;
    case TapState::UpdateDr: return tms ? TapState::SelectDrScan : TapState::RunTestIdle;
    case TapState::SelectIrScan: return tms ? TapState::TestLogicReset : TapState::CaptureIr;
    case TapState::CaptureIr: return tms ? TapState::Exit1Ir : TapState::ShiftIr;
    case TapState::ShiftIr: return tms ? TapState::Exit1Ir : TapState::ShiftIr;
    case TapState::Exit1Ir: return tms ? TapState::UpdateIr : TapState::PauseIr;
    case TapState::PauseIr: return tms ? TapState::Exit2Ir : TapState::PauseIr;
    case TapState::Exit2Ir: return tms ? TapState::UpdateIr : TapState::ShiftIr;
    case TapState::UpdateIr: return tms ? TapState::SelectDrScan : TapState::RunTestIdle;
    }
    return TapState::TestLogicReset;
}

QString tapStateName(const TapState state)
{
    static const QStringList names = {
        QStringLiteral("TEST-LOGIC-RESET"), QStringLiteral("RUN-TEST/IDLE"),
        QStringLiteral("SELECT-DR-SCAN"), QStringLiteral("CAPTURE-DR"),
        QStringLiteral("SHIFT-DR"), QStringLiteral("EXIT1-DR"), QStringLiteral("PAUSE-DR"),
        QStringLiteral("EXIT2-DR"), QStringLiteral("UPDATE-DR"),
        QStringLiteral("SELECT-IR-SCAN"), QStringLiteral("CAPTURE-IR"),
        QStringLiteral("SHIFT-IR"), QStringLiteral("EXIT1-IR"), QStringLiteral("PAUSE-IR"),
        QStringLiteral("EXIT2-IR"), QStringLiteral("UPDATE-IR")
    };
    return names.at(static_cast<int>(state));
}

qint64 timeUnitToPs(const QString& unit)
{
    const QString normalized = unit.toLower();
    if (normalized == QStringLiteral("s")) return 1'000'000'000'000LL;
    if (normalized == QStringLiteral("ms")) return 1'000'000'000LL;
    if (normalized == QStringLiteral("us")) return 1'000'000LL;
    if (normalized == QStringLiteral("ns")) return 1'000LL;
    if (normalized == QStringLiteral("ps")) return 1;
    if (normalized == QStringLiteral("fs")) return 0;
    return 1;
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
    bool scalarSeenTime = false;
    const auto appendScalarSample = [&]() {
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
            if (scalarChannels.size() == kTraceWidth && scalarSeenTime) appendScalarSample();
            bool ok = false;
            const qint64 parsedTime = line.mid(1).toLongLong(&ok);
            if (!ok) {
                result.errorMessage = QStringLiteral("VCD 时间戳无法解析: %1").arg(line);
                return result;
            }
            currentTime = parsedTime;
            scalarSeenTime = true;
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
    if (scalarChannels.size() == kTraceWidth && scalarSeenTime) {
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

QList<ProtocolEvent> decodeProtocolEvents(const QList<VcdSample>& samples,
                                          const quint32 sampleRateHz,
                                          const qint64 timeUnitPs)
{
    QList<ProtocolEvent> events;
    if (samples.isEmpty()) {
        return events;
    }

    const auto append = [&events](const QString& groupId, const QString& type,
                                  const qint64 start, const qint64 end,
                                  const QString& summary, const QString& severity = QString(),
                                  const QJsonObject& fields = QJsonObject()) {
        ProtocolEvent event;
        event.groupId = groupId;
        event.type = type;
        event.startTime = start;
        event.endTime = qMax(start + 1, end);
        event.summary = summary;
        event.severity = severity;
        event.fields = fields;
        events.append(event);
    };

    bool ahbOpen = false;
    VcdSample ahbStart;
    int ahbStallSamples = 0;
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
    QList<bool> i2cAcks;
    bool ethOpen = false;
    VcdSample ethStart;
    bool usbOpen = false;
    VcdSample usbStart;
    TapState tapState = TapState::TestLogicReset;
    bool jtagScanOpen = false;
    bool jtagScanIsIr = false;
    qint64 jtagScanStart = 0;
    QString jtagTdiBits;
    QString jtagTdoBits;
    VcdSample previous = samples.first();

    const bool wide = samples.first().packedHex.size() > (kLegacyTraceWidth / 4);
    const auto p = [wide](const int legacyBit, const int wideBit) { return wide ? wideBit : legacyBit; };
    const bool uartReal = !wide || sampleBit(samples.first(), 768);
    const bool spiReal = !wide || sampleBit(samples.first(), 781);
    const bool canReal = !wide || sampleBit(samples.first(), 770);
    const bool i2cReal = !wide || sampleBit(samples.first(), 782);
    const bool ethReal = !wide || sampleBit(samples.first(), 773);
    const bool usbReal = !wide || sampleBit(samples.first(), 775);
    const bool jtagReal = !wide || sampleBit(samples.first(), 776);
    const bool uartHintDataAvailable = std::any_of(samples.cbegin(), samples.cend(), [p](const VcdSample& value) {
        return sampleValue(value, p(118, 520), 8) != 0U || sampleValue(value, p(126, 528), 8) != 0U;
    });
    const bool i2cHintAvailable = wide && std::any_of(
        samples.cbegin(), samples.cend(), [p](const VcdSample& value) {
            return sampleBit(value, p(210, 610)) || sampleBit(value, p(211, 611)) ||
                sampleBit(value, p(214, 614));
        });
    const qint64 samplePeriodPs = samples.size() > 1
        ? (samples.at(1).time - samples.at(0).time) * timeUnitPs : 0;
    const qint64 uartBitSamples = sampleRateHz > 0U && samplePeriodPs > 0
        ? qMax<qint64>(1, qRound64(static_cast<double>(sampleRateHz) / 115200.0)) : 0;
    qint64 uartNextTx = 0;
    qint64 uartNextRx = 0;
    const bool uartRawMode = uartReal && !uartHintDataAvailable && uartBitSamples > 1 &&
        samplePeriodPs > 0 && samplePeriodPs * uartBitSamples > 0;

    for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
        const VcdSample& sample = samples.at(sampleIndex);
        const bool ahbKnown =
            !sampleRangeUnknown(sample, p(0, 32), 32) &&
            !sampleRangeUnknown(sample, p(32, 417), 2) &&
            !sampleRangeUnknown(sample, p(34, 416), 1) &&
            !sampleRangeUnknown(sample, p(41, 429), 3) &&
            !sampleRangeUnknown(sample, p(44, 64), 32) &&
            !sampleRangeUnknown(sample, p(76, 192), 32);
        const bool ahbActive = ahbKnown &&
            (sampleValue(sample, p(32, 417), 2) & 0x2U) != 0U;
        if (ahbOpen && ahbKnown && !sampleBit(sample, p(41, 429))) {
            ++ahbStallSamples;
        }
        if (ahbOpen && ahbKnown && sampleBit(sample, p(41, 429))) {
            const bool write = sampleBit(ahbStart, p(34, 416));
            const quint64 address = sampleValue(ahbStart, p(0, 32), 32);
            const quint64 data = sampleValue(sample, write ? p(44, 64) : p(76, 192), 32);
            const quint64 response = sampleValue(sample, p(42, 430), 2);
            const quint64 size = sampleValue(ahbStart, p(35, 419), 3);
            const quint64 burst = sampleValue(ahbStart, p(38, 422), 3);
            QJsonObject fields;
            fields.insert(QStringLiteral("operation"), write ? QStringLiteral("write") : QStringLiteral("read"));
            fields.insert(QStringLiteral("address"), hexValue(address, 32));
            fields.insert(QStringLiteral("data"), hexValue(data, 32));
            fields.insert(QStringLiteral("size_code"), static_cast<qint64>(size));
            fields.insert(QStringLiteral("burst_code"), static_cast<qint64>(burst));
            fields.insert(QStringLiteral("stall_samples"), ahbStallSamples);
            fields.insert(QStringLiteral("response"), ahbResponseText(response));
            append(QStringLiteral("ahb"), QStringLiteral("ahb_transfer"),
                   ahbStart.time, sample.time,
                   QStringLiteral("AHB %1 %2 DATA=%3 SIZE=%4 BURST=%5 STALL=%6 RESP=%7")
                       .arg(write ? QStringLiteral("WRITE") : QStringLiteral("READ"),
                            hexValue(address, 32), hexValue(data, 32))
                       .arg(size).arg(burst).arg(ahbStallSamples).arg(ahbResponseText(response)),
                   response == 0U ? QString() : QStringLiteral("error"), fields);
            ahbOpen = false;
        }
        if (ahbActive && !ahbOpen) {
            ahbOpen = true;
            ahbStart = sample;
            ahbStallSamples = sampleBit(sample, p(41, 429)) ? 0 : 1;
        }

        const bool uartKnown = !sampleRangeUnknown(sample, p(112, 512), 32) &&
            !sampleRangeUnknown(previous, p(112, 512), 32);
        if (uartReal && uartKnown && sampleBit(sample, p(142, 518)) &&
            !sampleBit(previous, p(142, 518))) {
            const bool tx = sampleBit(sample, p(114, 516)) || !sampleBit(sample, p(115, 517));
            const quint64 data = sampleValue(sample, tx ? p(118, 520) : p(126, 528), 8);
            QJsonObject fields;
            fields.insert(QStringLiteral("direction"), tx ? QStringLiteral("TX") : QStringLiteral("RX"));
            fields.insert(QStringLiteral("data"), hexValue(data, 8));
            fields.insert(QStringLiteral("source"), QStringLiteral("rtl_frame_hint"));
            fields.insert(QStringLiteral("frame_error_available"), false);
            append(QStringLiteral("uart"), QStringLiteral("uart_frame"),
                   previous.time, sample.time,
                   QStringLiteral("UART %1 %2").arg(tx ? QStringLiteral("TX") : QStringLiteral("RX"),
                                                     hexValue(data, 8)), QString(), fields);
        }
        if (uartRawMode && sample.time >= qMin(uartNextTx, uartNextRx)) {
            const auto decodeUartLine = [&](const int lineBit, const QString& direction, qint64* const nextAllowed) {
                if (!sampleBit(previous, lineBit) || sampleBit(sample, lineBit) ||
                    nextAllowed == nullptr || sample.time < *nextAllowed) return;
                const qint64 stopIndex = sampleIndex + uartBitSamples * 10;
                if (stopIndex >= samples.size()) return;
                quint64 data = 0U;
                for (int bit = 0; bit < 8; ++bit) {
                    const qint64 index = sampleIndex + uartBitSamples + uartBitSamples / 2 + bit * uartBitSamples;
                    if (index < samples.size() && sampleBit(samples.at(static_cast<int>(index)), lineBit)) {
                        data |= quint64(1) << bit;
                    }
                }
                const qint64 stopSample = sampleIndex + uartBitSamples * 9 + uartBitSamples / 2;
                const bool stopHigh = stopSample < samples.size() &&
                    sampleBit(samples.at(static_cast<int>(stopSample)), lineBit);
                QJsonObject rawFields;
                rawFields.insert(QStringLiteral("direction"), direction);
                rawFields.insert(QStringLiteral("data"), hexValue(data, 8));
                rawFields.insert(QStringLiteral("baud"), 115200);
                rawFields.insert(QStringLiteral("format"), QStringLiteral("8N1"));
                rawFields.insert(QStringLiteral("frame_error"), !stopHigh);
                append(QStringLiteral("uart"), QStringLiteral("uart_frame"), sample.time,
                       samples.at(static_cast<int>(stopIndex)).time,
                       QStringLiteral("UART %1 %2").arg(direction, hexValue(data, 8)),
                       stopHigh ? QString() : QStringLiteral("error"), rawFields);
                *nextAllowed = samples.at(static_cast<int>(stopIndex)).time;
            };
            decodeUartLine(p(112, 512), QStringLiteral("TX"), &uartNextTx);
            decodeUartLine(p(113, 513), QStringLiteral("RX"), &uartNextRx);
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
            const quint64 mode = sampleValue(spiStart, p(150, 550), 2);
            QJsonObject fields;
            fields.insert(QStringLiteral("tx"), hexValue(tx, qMax(8, spiBitCount)));
            fields.insert(QStringLiteral("rx"), hexValue(rx, qMax(8, spiBitCount)));
            fields.insert(QStringLiteral("bit_count"), spiBitCount > 0 ? spiBitCount : 8);
            fields.insert(QStringLiteral("mode"), static_cast<qint64>(mode));
            fields.insert(QStringLiteral("cs_frame"), true);
            append(QStringLiteral("spi"), QStringLiteral("spi_transfer"), spiStart.time, sample.time,
                   QStringLiteral("SPI %1 bits TX=%2 RX=%3")
                       .arg(spiBitCount > 0 ? spiBitCount : 8)
                       .arg(hexValue(tx, qMax(8, spiBitCount)), hexValue(rx, qMax(8, spiBitCount))),
                   QString(), fields);
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
            const quint64 id = sampleValue(canStart, p(184, 584), 11);
            const quint64 dataNibble = sampleValue(sample, p(195, 595), 4);
            const bool ack = sampleBit(sample, p(206, 606));
            QJsonObject fields;
            fields.insert(QStringLiteral("id"), hexValue(id, 11));
            fields.insert(QStringLiteral("data_nibble"), hexValue(dataNibble, 4));
            fields.insert(QStringLiteral("ide_available"), false);
            fields.insert(QStringLiteral("rtr_available"), false);
            fields.insert(QStringLiteral("ack"), ack);
            fields.insert(QStringLiteral("crc_ok_available"), false);
            append(QStringLiteral("can"), QStringLiteral("can_frame"), canStart.time, sample.time,
                   QStringLiteral("CAN ID=%1 DATA=%2")
                       .arg(hexValue(id, 11), hexValue(dataNibble, 4)),
                   sampleBit(sample, p(183, 583)) ? QStringLiteral("error") : QString(), fields);
            canOpen = false;
        }
        const bool canActivity = sampleBit(sample, p(207, 607));
        const bool previousCanActivity = sampleBit(previous, p(207, 607));
        if (canReal && canKnown && canActivity && !previousCanActivity && !canOpen) {
            QJsonObject fields;
            fields.insert(QStringLiteral("source"), QStringLiteral("raw_can_line_activity"));
            fields.insert(QStringLiteral("frame_decode_available"), false);
            fields.insert(QStringLiteral("reason"), QStringLiteral("bitrate_and_bit_timing_not_in_trace"));
            append(QStringLiteral("can"), QStringLiteral("can_activity"), previous.time, sample.time,
                   QStringLiteral("CAN ACTIVITY (limited: bitrate metadata required)"),
                   QStringLiteral("warning"), fields);
        }

        const bool i2cScl = sampleBit(sample, p(208, 608));
        const bool i2cSda = sampleBit(sample, p(209, 609));
        const bool previousI2cScl = sampleBit(previous, p(208, 608));
        const bool previousI2cSda = sampleBit(previous, p(209, 609));
        const bool i2cStartCondition = i2cHintAvailable
            ? sampleBit(sample, p(210, 610))
            : i2cScl && previousI2cSda && !i2cSda;
        const bool i2cStopCondition = i2cHintAvailable
            ? sampleBit(sample, p(211, 611))
            : i2cScl && !previousI2cSda && i2cSda;
        const bool i2cSclRise = i2cHintAvailable
            ? sampleBit(sample, p(214, 614))
            : i2cScl && !previousI2cScl;
        const bool i2cKnown = !sampleRangeUnknown(sample, p(208, 608), 32) &&
            !sampleRangeUnknown(previous, p(208, 608), 32);
        if (i2cReal && i2cKnown && i2cStartCondition) {
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
            i2cAcks.clear();
        }
        if (i2cOpen && i2cKnown && i2cSclRise && !i2cStartCondition) {
            if ((i2cBitCount % 9) < 8) {
                i2cByte = (i2cByte << 1U) | (i2cSda ? 1U : 0U);
            } else {
                i2cBytes.append(i2cByte);
                const bool ack = sampleBit(sample, p(212, 612));
                i2cAcks.append(ack);
                QJsonObject ackFields;
                ackFields.insert(QStringLiteral("ack"), ack);
                ackFields.insert(QStringLiteral("byte_index"), i2cAcks.size() - 1);
                append(QStringLiteral("i2c"), QStringLiteral("i2c_ack"), previous.time, sample.time,
                       ack ? QStringLiteral("I2C ACK") : QStringLiteral("I2C NACK"),
                       ack ? QString() : QStringLiteral("error"), ackFields);
                i2cByte = 0U;
            }
            ++i2cBitCount;
        }
        if (i2cOpen && i2cKnown && i2cStopCondition) {
            const quint64 addressByte = i2cBytes.isEmpty()
                ? sampleValue(i2cStart, p(216, 616), 7) << 1U : i2cBytes.first();
            const bool read = (addressByte & 1U) != 0U;
            const quint64 data = i2cBytes.size() > 1
                ? i2cBytes.at(1) : sampleValue(sample, p(223, 623), 8);
            QStringList dataBytes;
            for (int index = 1; index < i2cBytes.size(); ++index) {
                dataBytes.append(hexValue(i2cBytes.at(index), 8));
            }
            if (dataBytes.isEmpty()) dataBytes.append(hexValue(data, 8));
            QJsonObject fields;
            fields.insert(QStringLiteral("address"), hexValue(addressByte >> 1U, 7));
            fields.insert(QStringLiteral("operation"), read ? QStringLiteral("read") : QStringLiteral("write"));
            fields.insert(QStringLiteral("data"), dataBytes.join(QStringLiteral(",")));
            fields.insert(QStringLiteral("ack_count"), i2cAcks.size());
            append(QStringLiteral("i2c"), QStringLiteral("i2c_transfer"), i2cStart.time, sample.time,
                   QStringLiteral("I2C %1 ADDR=%2 DATA=%3 ACKS=%4")
                       .arg(read ? QStringLiteral("READ") : QStringLiteral("WRITE"),
                            hexValue(addressByte >> 1U, 7), dataBytes.join(QStringLiteral(",")))
                       .arg(i2cAcks.size()),
                   sampleBit(sample, p(239, 639)) ? QStringLiteral("error") : QString(), fields);
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
            const bool tx = sampleBit(sample, p(240, 640));
            const bool rx = sampleBit(sample, p(242, 642));
            const quint64 ethertype = sampleValue(sample, p(288, 688), 16);
            const quint64 byteIndex = sampleValue(sample, p(264, 664), 8);
            QJsonObject fields;
            fields.insert(QStringLiteral("direction"), tx ? QStringLiteral("TX") : (rx ? QStringLiteral("RX") : QStringLiteral("unknown")));
            fields.insert(QStringLiteral("ethertype"), hexValue(ethertype, 16));
            fields.insert(QStringLiteral("byte_index"), static_cast<qint64>(byteIndex));
            fields.insert(QStringLiteral("payload_summary_available"), byteIndex != 0U);
            append(QStringLiteral("eth"), QStringLiteral("eth_frame"), ethStart.time, sample.time,
                   QStringLiteral("ETH FRAME TYPE=%1")
                       .arg(hexValue(ethertype, 16)),
                   (sampleBit(sample, p(241, 641)) || sampleBit(sample, p(243, 643)))
                       ? QStringLiteral("error") : QString(), fields);
            ethOpen = false;
        }

        const bool usbKnown = !sampleRangeUnknown(sample, p(304, 704), 32) &&
            !sampleRangeUnknown(previous, p(304, 704), 32);
        if (usbReal && usbKnown && sampleBit(sample, p(335, 735)) &&
            !sampleBit(previous, p(335, 735))) {
            QJsonObject resetFields;
            resetFields.insert(QStringLiteral("reset"), true);
            append(QStringLiteral("usb"), QStringLiteral("usb_reset"), previous.time, sample.time,
                   QStringLiteral("USB RESET"), QStringLiteral("warning"), resetFields);
        }
        if (usbReal && usbKnown && sampleBit(sample, p(309, 709)) &&
            !sampleBit(previous, p(309, 709))) {
            usbOpen = true;
            usbStart = sample;
        }
        if (usbOpen && usbKnown && sampleBit(sample, p(310, 710))) {
            const quint64 pid = sampleValue(usbStart, p(312, 712), 4);
            const quint64 endpoint = sampleValue(sample, p(316, 716), 4);
            const quint64 data = sampleValue(sample, p(320, 720), 8);
            QJsonObject fields;
            fields.insert(QStringLiteral("pid"), hexValue(pid, 4));
            fields.insert(QStringLiteral("endpoint"), static_cast<qint64>(endpoint));
            fields.insert(QStringLiteral("data"), hexValue(data, 8));
            append(QStringLiteral("usb"), QStringLiteral("usb_packet"), usbStart.time, sample.time,
                   QStringLiteral("USB PID=%1 EP=%2 DATA=%3")
                       .arg(hexValue(pid, 4), QString::number(endpoint), hexValue(data, 8)),
                   QString(), fields);
            usbOpen = false;
        }

        const bool primaryTckRising = sampleBit(sample, p(336, 736)) && !sampleBit(previous, p(336, 736));
        const bool rvTckRising = sampleBit(sample, p(340, 740)) && !sampleBit(previous, p(340, 740));
        const bool useRvJtag = !primaryTckRising && rvTckRising;
        const bool tckRising = primaryTckRising || rvTckRising;
        const bool jtagKnown = !sampleRangeUnknown(sample, p(336, 736), 32) &&
            !sampleRangeUnknown(previous, p(336, 736), 32);
        if (jtagReal && jtagKnown && tckRising) {
            const int tmsBit = useRvJtag ? p(341, 741) : p(337, 737);
            const int tdiBit = useRvJtag ? p(342, 742) : p(338, 738);
            const int tdoBit = useRvJtag ? p(343, 743) : p(339, 739);
            const TapState nextState = nextTapState(tapState, sampleBit(sample, tmsBit));
            const bool nextIsShift = nextState == TapState::ShiftDr || nextState == TapState::ShiftIr;
            if (jtagScanOpen && !nextIsShift) {
                QJsonObject scanFields;
                scanFields.insert(QStringLiteral("register"), jtagScanIsIr ? QStringLiteral("ir") : QStringLiteral("dr"));
                scanFields.insert(QStringLiteral("tdi_bits"), jtagTdiBits);
                scanFields.insert(QStringLiteral("tdo_bits"), jtagTdoBits);
                scanFields.insert(QStringLiteral("bit_count"), jtagTdiBits.size());
                append(QStringLiteral("jtag"), QStringLiteral("jtag_scan"), jtagScanStart, sample.time,
                       QStringLiteral("JTAG %1 SCAN bits=%2 TDI=%3 TDO=%4")
                           .arg(jtagScanIsIr ? QStringLiteral("IR") : QStringLiteral("DR"))
                           .arg(jtagTdiBits.size()).arg(jtagTdiBits, jtagTdoBits),
                       QString(), scanFields);
                jtagScanOpen = false;
                jtagTdiBits.clear();
                jtagTdoBits.clear();
            }
            if (nextIsShift) {
                if (!jtagScanOpen) {
                    jtagScanOpen = true;
                    jtagScanIsIr = nextState == TapState::ShiftIr;
                    jtagScanStart = previous.time;
                }
                jtagTdiBits.append(sampleBit(sample, tdiBit) ? QLatin1Char('1') : QLatin1Char('0'));
                jtagTdoBits.append(sampleBit(sample, tdoBit) ? QLatin1Char('1') : QLatin1Char('0'));
            }
            tapState = nextState;
            QJsonObject cycleFields;
            cycleFields.insert(QStringLiteral("state"), tapStateName(tapState));
            cycleFields.insert(QStringLiteral("tms"), sampleBit(sample, tmsBit));
            cycleFields.insert(QStringLiteral("tdi"), sampleBit(sample, tdiBit));
            cycleFields.insert(QStringLiteral("tdo"), sampleBit(sample, tdoBit));
            append(QStringLiteral("jtag"), QStringLiteral("jtag_cycle"), previous.time, sample.time,
                   QStringLiteral("JTAG STATE=%1 TMS=%2 TDI=%3 TDO=%4")
                       .arg(tapStateName(tapState),
                            sampleBit(sample, tmsBit) ? QStringLiteral("1") : QStringLiteral("0"),
                            sampleBit(sample, tdiBit) ? QStringLiteral("1") : QStringLiteral("0"),
                            sampleBit(sample, tdoBit) ? QStringLiteral("1") : QStringLiteral("0")),
                   QString(), cycleFields);
        }
        previous = sample;
    }
    if (jtagScanOpen && !jtagTdiBits.isEmpty()) {
        QJsonObject fields;
        fields.insert(QStringLiteral("register"), jtagScanIsIr ? QStringLiteral("ir") : QStringLiteral("dr"));
        fields.insert(QStringLiteral("tdi_bits"), jtagTdiBits);
        fields.insert(QStringLiteral("tdo_bits"), jtagTdoBits);
        fields.insert(QStringLiteral("bit_count"), jtagTdiBits.size());
        append(QStringLiteral("jtag"), QStringLiteral("jtag_scan"), jtagScanStart,
               samples.last().time,
               QStringLiteral("JTAG %1 SCAN bits=%2 TDI=%3 TDO=%4")
                   .arg(jtagScanIsIr ? QStringLiteral("IR") : QStringLiteral("DR"))
                   .arg(jtagTdiBits.size()).arg(jtagTdiBits, jtagTdoBits),
                   QString(), fields);
    }
    std::sort(events.begin(), events.end(), [](const ProtocolEvent& left, const ProtocolEvent& right) {
        if (left.startTime != right.startTime) return left.startTime < right.startTime;
        if (left.endTime != right.endTime) return left.endTime < right.endTime;
        return left.groupId < right.groupId;
    });
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
    object.insert(QStringLiteral("fields"), event.fields);
    return object;
}

QJsonArray protocolEventsToJson(const QList<ProtocolEvent>& events)
{
    QJsonArray array;
    for (const ProtocolEvent& event : events) array.append(protocolEventToJson(event));
    return array;
}

QList<ProtocolEvent> loadSparseProtocolEvents(
    const QString& taskRootPath,
    const QJsonObject& captureSidecar,
    const qint64 vcdTimeUnitPs,
    quint32* const designGapMask,
    bool* const archiveValid,
    QList<ProtocolDiagnostic>* const diagnostics)
{
    struct SparseLineEvent final {
        quint64 timestamp = 0U;
        quint16 state = 0U;
        QJsonObject record;
    };

    QList<ProtocolEvent> result;
    QList<SparseLineEvent> uartEdges;
    QList<SparseLineEvent> spiEdges;
    QList<SparseLineEvent> i2cEdges;
    QList<SparseLineEvent> jtagEdges;
    if (designGapMask != nullptr) *designGapMask = 0U;
    if (archiveValid != nullptr) *archiveValid = true;
    const QString relativePath = captureSidecar.value(QStringLiteral("protocol_events")).toString();
    if (relativePath.isEmpty()) return result;

    QJsonObject archive;
    QString error;
    const QString archivePath = QDir(taskRootPath).filePath(relativePath);
    if (!readJsonObject(archivePath, &archive, &error) ||
        archive.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("lockstep-protocol-events-v3")) {
        addDiagnostic(
            diagnostics, QStringLiteral("TRACE_EVENT_ARCHIVE_ERROR"), QStringLiteral("error"),
            QStringLiteral("稀疏协议事件归档无效"), error.isEmpty() ? archivePath : error);
        if (archiveValid != nullptr) *archiveValid = false;
        return result;
    }

    const quint32 mask = static_cast<quint32>(
        archive.value(QStringLiteral("design_gap_mask")).toInteger());
    if (designGapMask != nullptr) *designGapMask = mask & 0x1ffU;
    const quint64 timebaseHz = static_cast<quint64>(
        archive.value(QStringLiteral("timebase_hz")).toInteger());
    const quint32 windowStart = static_cast<quint32>(
        captureSidecar.value(QStringLiteral("window_start_index")).toInteger());
    if (timebaseHz == 0U || vcdTimeUnitPs <= 0) {
        addDiagnostic(
            diagnostics, QStringLiteral("TRACE_EVENT_TIMEBASE_ERROR"), QStringLiteral("error"),
            QStringLiteral("稀疏协议事件缺少有效时间基准"), archivePath);
        if (archiveValid != nullptr) *archiveValid = false;
        return result;
    }

    const QList<ProtocolGroupDefinition> definitions = fixedProtocolGroups();
    const QJsonArray events = archive.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& value : events) {
        const QJsonObject item = value.toObject();
        bool timestampOk = false;
        const quint64 timestamp = item.value(QStringLiteral("timestamp_ticks")).toString()
                                      .toULongLong(&timestampOk);
        const int protocolId = item.value(QStringLiteral("protocol_id")).toInt(-1);
        if (!timestampOk || protocolId < 0 || protocolId >= definitions.size()) {
            addDiagnostic(
                diagnostics, QStringLiteral("TRACE_EVENT_RECORD_ERROR"), QStringLiteral("error"),
                QStringLiteral("稀疏协议事件字段无效"), QString::fromUtf8(
                    QJsonDocument(item).toJson(QJsonDocument::Compact)));
            if (archiveValid != nullptr) *archiveValid = false;
            continue;
        }

        const quint32 relativeTicks = static_cast<quint32>(timestamp) - windowStart;
        const qint64 eventTime = static_cast<qint64>(
            (static_cast<long double>(relativeTicks) * 1.0e12L) /
            (static_cast<long double>(timebaseHz) * static_cast<long double>(vcdTimeUnitPs)));
        const int sourceKind = item.value(QStringLiteral("source_kind")).toInt();
        ProtocolEvent event;
        event.groupId = definitions.at(protocolId).id;
        event.type = sourceKind == 2 ? QStringLiteral("bus_transfer") :
                     sourceKind == 3 ? QStringLiteral("mismatch_event") :
                     sourceKind == 1 ? QStringLiteral("decoded_hint") :
                                       QStringLiteral("raw_line_event");
        event.startTime = eventTime;
        event.endTime = eventTime + 1;
        event.summary = QStringLiteral("%1 稀疏事件 seq=%2 source_kind=%3")
                            .arg(definitions.at(protocolId).displayName)
                            .arg(item.value(QStringLiteral("local_sequence")).toInteger())
                            .arg(sourceKind);
        event.fields = item;
        event.fields.insert(QStringLiteral("evidence"), relativePath);
        result.append(event);

        if (protocolId == 1 && sourceKind == 0) {
            const QByteArray payload = QByteArray::fromHex(
                item.value(QStringLiteral("payload_hex")).toString().toLatin1());
            if (!payload.isEmpty()) {
                SparseLineEvent edge;
                edge.timestamp = timestamp;
                edge.state = static_cast<quint8>(payload.at(0));
                edge.record = item;
                uartEdges.append(edge);
            }
        }
        if (protocolId == 2 && sourceKind == 0) {
            const QByteArray payload = QByteArray::fromHex(
                item.value(QStringLiteral("payload_hex")).toString().toLatin1());
            if (!payload.isEmpty()) {
                SparseLineEvent edge;
                edge.timestamp = timestamp;
                edge.state = static_cast<quint8>(payload.at(0));
                edge.record = item;
                spiEdges.append(edge);
            }
        }
        if (protocolId == 4 && sourceKind == 0) {
            const QByteArray payload = QByteArray::fromHex(
                item.value(QStringLiteral("payload_hex")).toString().toLatin1());
            if (!payload.isEmpty()) {
                SparseLineEvent edge;
                edge.timestamp = timestamp;
                edge.state = static_cast<quint8>(payload.at(0));
                edge.record = item;
                i2cEdges.append(edge);
            }
        }
        if (protocolId == 7 && sourceKind == 0) {
            const QByteArray payload = QByteArray::fromHex(
                item.value(QStringLiteral("payload_hex")).toString().toLatin1());
            if (!payload.isEmpty()) {
                SparseLineEvent edge;
                edge.timestamp = timestamp;
                edge.state = static_cast<quint8>(payload.at(0));
                if (payload.size() > 1) {
                    edge.state |= quint16(static_cast<quint8>(payload.at(1))) << 8U;
                }
                edge.record = item;
                jtagEdges.append(edge);
            }
        }
    }

    if (!uartEdges.isEmpty()) {
        std::sort(uartEdges.begin(), uartEdges.end(), [](const SparseLineEvent& left,
                                                        const SparseLineEvent& right) {
            return left.timestamp < right.timestamp;
        });
        constexpr quint64 uartBaud = 115'200ULL;
        const quint64 nominalBitTicks = qMax<quint64>(1U, (timebaseHz + uartBaud / 2U) / uartBaud);
        quint64 bitTicks = nominalBitTicks;
        quint64 bestDistance = std::numeric_limits<quint64>::max();
        for (int index = 1; index < uartEdges.size(); ++index) {
            const quint64 delta = uartEdges.at(index).timestamp - uartEdges.at(index - 1).timestamp;
            if (delta == 0U || delta > nominalBitTicks * 12U) continue;
            const quint64 multiple = qMax<quint64>(1U, (delta + nominalBitTicks / 2U) / nominalBitTicks);
            const quint64 candidate = qMax<quint64>(1U, delta / multiple);
            const quint64 distance = candidate > nominalBitTicks
                ? candidate - nominalBitTicks : nominalBitTicks - candidate;
            if (distance * 20U <= nominalBitTicks && distance < bestDistance) {
                bitTicks = candidate;
                bestDistance = distance;
            }
        }
        const auto lineAt = [&uartEdges](const quint64 timestamp, const quint8 lineMask) {
            bool level = true;
            for (const SparseLineEvent& edge : uartEdges) {
                if (edge.timestamp > timestamp) break;
                level = (edge.state & lineMask) != 0U;
            }
            return level;
        };
        const auto ticksToVcdTime = [timebaseHz, windowStart, vcdTimeUnitPs](const quint64 timestamp) {
            const quint32 relativeTicks = static_cast<quint32>(timestamp) - windowStart;
            return static_cast<qint64>(
                (static_cast<long double>(relativeTicks) * 1.0e12L) /
                (static_cast<long double>(timebaseHz) * static_cast<long double>(vcdTimeUnitPs)));
        };
        const auto decodeLine = [&](const quint8 lineMask, const quint8 fallingMask,
                                    const QString& direction) {
            quint64 nextAllowedTimestamp = 0U;
            for (const SparseLineEvent& edge : uartEdges) {
                if ((edge.state & fallingMask) == 0U || edge.timestamp < nextAllowedTimestamp) continue;
                const quint64 startSample = edge.timestamp + bitTicks / 2U;
                if (lineAt(startSample, lineMask)) continue;
                quint8 data = 0U;
                for (int bit = 0; bit < 8; ++bit) {
                    const quint64 sampleTimestamp = edge.timestamp +
                        ((3U + quint64(bit) * 2U) * bitTicks) / 2U;
                    if (lineAt(sampleTimestamp, lineMask)) data |= quint8(1U << bit);
                }
                const quint64 stopTimestamp = edge.timestamp + (19U * bitTicks) / 2U;
                const bool stopHigh = lineAt(stopTimestamp, lineMask);
                ProtocolEvent frame;
                frame.groupId = QStringLiteral("uart");
                frame.type = QStringLiteral("uart_frame");
                frame.startTime = ticksToVcdTime(edge.timestamp);
                frame.endTime = ticksToVcdTime(edge.timestamp + 10U * bitTicks);
                frame.summary = QStringLiteral("UART %1 %2").arg(direction, hexValue(data, 8));
                frame.severity = stopHigh ? QString() : QStringLiteral("error");
                frame.fields.insert(QStringLiteral("direction"), direction);
                frame.fields.insert(QStringLiteral("data"), hexValue(data, 8));
                frame.fields.insert(QStringLiteral("baud"), static_cast<qint64>(uartBaud));
                frame.fields.insert(QStringLiteral("format"), QStringLiteral("8N1"));
                frame.fields.insert(QStringLiteral("frame_error"), !stopHigh);
                frame.fields.insert(QStringLiteral("source"), QStringLiteral("sparse_timestamp_edges"));
                frame.fields.insert(QStringLiteral("capture_id"), edge.record.value(QStringLiteral("capture_id")));
                frame.fields.insert(QStringLiteral("evidence"), relativePath);
                result.append(frame);
                nextAllowedTimestamp = edge.timestamp + 10U * bitTicks;
            }
        };
        decodeLine(0x01U, 0x10U, QStringLiteral("TX"));
        decodeLine(0x02U, 0x20U, QStringLiteral("RX"));
    }
    if (!spiEdges.isEmpty()) {
        std::sort(spiEdges.begin(), spiEdges.end(), [](const SparseLineEvent& left,
                                                      const SparseLineEvent& right) {
            return left.timestamp < right.timestamp;
        });
        quint64 edgeInterval = std::numeric_limits<quint64>::max();
        for (int index = 1; index < spiEdges.size(); ++index) {
            const quint64 delta = spiEdges.at(index).timestamp - spiEdges.at(index - 1).timestamp;
            if (delta > 0U) edgeInterval = qMin(edgeInterval, delta);
        }
        if (edgeInterval == std::numeric_limits<quint64>::max()) edgeInterval = 1U;
        const auto ticksToVcdTime = [timebaseHz, windowStart, vcdTimeUnitPs](const quint64 timestamp) {
            const quint32 relativeTicks = static_cast<quint32>(timestamp) - windowStart;
            return static_cast<qint64>(
                (static_cast<long double>(relativeTicks) * 1.0e12L) /
                (static_cast<long double>(timebaseHz) * static_cast<long double>(vcdTimeUnitPs)));
        };
        const auto appendTransfer = [&](const QList<SparseLineEvent>& transfer) {
            if (transfer.isEmpty() || transfer.size() > 64) return;
            quint64 txData = 0U;
            quint64 rxData = 0U;
            for (const SparseLineEvent& edge : transfer) {
                txData = (txData << 1U) | ((edge.state & 0x02U) != 0U ? 1U : 0U);
                rxData = (rxData << 1U) | ((edge.state & 0x04U) != 0U ? 1U : 0U);
            }
            ProtocolEvent event;
            event.groupId = QStringLiteral("spi");
            event.type = QStringLiteral("spi_transfer");
            event.startTime = ticksToVcdTime(transfer.first().timestamp);
            event.endTime = ticksToVcdTime(transfer.last().timestamp);
            event.summary = QStringLiteral("SPI %1 bits TX=%2 RX=%3")
                .arg(transfer.size()).arg(hexValue(txData, transfer.size()),
                                          hexValue(rxData, transfer.size()));
            event.fields.insert(QStringLiteral("tx_data"), hexValue(txData, transfer.size()));
            event.fields.insert(QStringLiteral("rx_data"), hexValue(rxData, transfer.size()));
            event.fields.insert(QStringLiteral("bit_count"), transfer.size());
            event.fields.insert(QStringLiteral("mode_available"), false);
            event.fields.insert(QStringLiteral("source"), QStringLiteral("sparse_rising_edges"));
            event.fields.insert(QStringLiteral("evidence"), relativePath);
            result.append(event);
        };
        QList<SparseLineEvent> transfer;
        for (const SparseLineEvent& edge : spiEdges) {
            const bool newTransfer = !transfer.isEmpty() &&
                edge.timestamp - transfer.last().timestamp > edgeInterval * 8U;
            if (newTransfer) {
                appendTransfer(transfer);
                transfer.clear();
            }
            if ((edge.state & 0x20U) != 0U && (edge.state & 0x08U) == 0U) transfer.append(edge);
        }
        appendTransfer(transfer);
    }
    if (!i2cEdges.isEmpty()) {
        std::sort(i2cEdges.begin(), i2cEdges.end(), [](const SparseLineEvent& left,
                                                      const SparseLineEvent& right) {
            return left.timestamp < right.timestamp;
        });
        const auto ticksToVcdTime = [timebaseHz, windowStart, vcdTimeUnitPs](const quint64 timestamp) {
            const quint32 relativeTicks = static_cast<quint32>(timestamp) - windowStart;
            return static_cast<qint64>(
                (static_cast<long double>(relativeTicks) * 1.0e12L) /
                (static_cast<long double>(timebaseHz) * static_cast<long double>(vcdTimeUnitPs)));
        };
        bool active = false;
        quint64 startTimestamp = 0U;
        int bitCount = 0;
        quint8 currentByte = 0U;
        QList<quint8> bytes;
        QList<bool> acks;
        const auto appendTransfer = [&](const quint64 endTimestamp, const bool complete,
                                        const QString& endReason) {
            if (!active || (bytes.isEmpty() && bitCount == 0)) return;
            QJsonArray byteArray;
            QStringList byteTexts;
            for (const quint8 byte : bytes) {
                const QString text = hexValue(byte, 8);
                byteArray.append(text);
                byteTexts.append(text);
            }
            QJsonArray ackArray;
            for (const bool ack : acks) ackArray.append(ack ? QStringLiteral("ACK") : QStringLiteral("NACK"));
            ProtocolEvent event;
            event.groupId = QStringLiteral("i2c");
            event.type = complete ? QStringLiteral("i2c_transfer") : QStringLiteral("i2c_segment");
            event.startTime = ticksToVcdTime(startTimestamp);
            event.endTime = ticksToVcdTime(endTimestamp);
            event.summary = QStringLiteral("I2C %1 bytes=[%2]")
                .arg(endReason, byteTexts.join(QStringLiteral(", ")));
            event.fields.insert(QStringLiteral("bytes"), byteArray);
            event.fields.insert(QStringLiteral("acks"), ackArray);
            event.fields.insert(QStringLiteral("trailing_partial_bits"), bitCount);
            event.fields.insert(QStringLiteral("end_reason"), endReason);
            event.fields.insert(QStringLiteral("complete"), complete);
            event.fields.insert(QStringLiteral("source"), QStringLiteral("sparse_scl_rising_edges"));
            event.fields.insert(QStringLiteral("evidence"), relativePath);
            if (!complete) event.severity = QStringLiteral("warning");
            result.append(event);
        };
        for (const SparseLineEvent& edge : i2cEdges) {
            const bool start = (edge.state & 0x04U) != 0U;
            const bool stop = (edge.state & 0x08U) != 0U;
            const bool sclRise = (edge.state & 0x40U) != 0U;
            if (start) {
                if (active) appendTransfer(edge.timestamp, false, QStringLiteral("REPEATED_START"));
                active = true;
                startTimestamp = edge.timestamp;
                bitCount = 0;
                currentByte = 0U;
                bytes.clear();
                acks.clear();
            }
            if (active && sclRise) {
                const bool sda = (edge.state & 0x02U) != 0U;
                if (bitCount < 8) {
                    currentByte = quint8((currentByte << 1U) | (sda ? 1U : 0U));
                    ++bitCount;
                } else {
                    bytes.append(currentByte);
                    acks.append(!sda);
                    bitCount = 0;
                    currentByte = 0U;
                }
            }
            if (active && stop) {
                const bool complete = !bytes.isEmpty() && bytes.size() == acks.size() && bitCount <= 1;
                appendTransfer(edge.timestamp, complete, QStringLiteral("STOP"));
                active = false;
                bitCount = 0;
                currentByte = 0U;
                bytes.clear();
                acks.clear();
            }
        }
        if (active) appendTransfer(i2cEdges.last().timestamp, false, QStringLiteral("INCOMPLETE"));
    }
    if (!jtagEdges.isEmpty()) {
        std::sort(jtagEdges.begin(), jtagEdges.end(), [](const SparseLineEvent& left,
                                                        const SparseLineEvent& right) {
            return left.timestamp < right.timestamp;
        });
        const auto ticksToVcdTime = [timebaseHz, windowStart, vcdTimeUnitPs](const quint64 timestamp) {
            const quint32 relativeTicks = static_cast<quint32>(timestamp) - windowStart;
            return static_cast<qint64>(
                (static_cast<long double>(relativeTicks) * 1.0e12L) /
                (static_cast<long double>(timebaseHz) * static_cast<long double>(vcdTimeUnitPs)));
        };
        TapState tapState = TapState::TestLogicReset;
        bool scanOpen = false;
        bool scanIsIr = false;
        quint64 scanStartTimestamp = 0U;
        QString tdiBits;
        QString tdoBits;
        for (const SparseLineEvent& edge : jtagEdges) {
            const bool rvJtag = (edge.record.value(QStringLiteral("flags")).toInt() & 1) != 0 ||
                (edge.state & 0x0200U) != 0U;
            const quint16 risingMask = rvJtag ? 0x0200U : 0x0100U;
            if ((edge.state & risingMask) == 0U) continue;
            const quint16 tmsMask = rvJtag ? 0x0020U : 0x0002U;
            const quint16 tdiMask = rvJtag ? 0x0040U : 0x0004U;
            const quint16 tdoMask = rvJtag ? 0x0080U : 0x0008U;
            const bool tms = (edge.state & tmsMask) != 0U;
            const bool tdi = (edge.state & tdiMask) != 0U;
            const bool tdo = (edge.state & tdoMask) != 0U;
            const bool currentIsShift = tapState == TapState::ShiftDr || tapState == TapState::ShiftIr;
            const TapState nextState = nextTapState(tapState, tms);
            const bool nextIsShift = nextState == TapState::ShiftDr || nextState == TapState::ShiftIr;
            if (!currentIsShift && nextIsShift) {
                scanOpen = true;
                scanIsIr = nextState == TapState::ShiftIr;
                scanStartTimestamp = edge.timestamp;
                tdiBits.clear();
                tdoBits.clear();
            } else if (currentIsShift && scanOpen) {
                tdiBits.append(tdi ? QLatin1Char('1') : QLatin1Char('0'));
                tdoBits.append(tdo ? QLatin1Char('1') : QLatin1Char('0'));
                if (!nextIsShift) {
                    ProtocolEvent scan;
                    scan.groupId = QStringLiteral("jtag");
                    scan.type = QStringLiteral("jtag_scan");
                    scan.startTime = ticksToVcdTime(scanStartTimestamp);
                    scan.endTime = ticksToVcdTime(edge.timestamp);
                    scan.summary = QStringLiteral("JTAG %1 SCAN bits=%2 TDI=%3 TDO=%4")
                        .arg(scanIsIr ? QStringLiteral("IR") : QStringLiteral("DR"))
                        .arg(tdiBits.size()).arg(tdiBits, tdoBits);
                    scan.fields.insert(QStringLiteral("register"),
                                       scanIsIr ? QStringLiteral("ir") : QStringLiteral("dr"));
                    scan.fields.insert(QStringLiteral("tdi_bits"), tdiBits);
                    scan.fields.insert(QStringLiteral("tdo_bits"), tdoBits);
                    scan.fields.insert(QStringLiteral("bit_count"), tdiBits.size());
                    scan.fields.insert(QStringLiteral("source"), QStringLiteral("sparse_tap_cycles"));
                    scan.fields.insert(QStringLiteral("jtag_path"),
                                       rvJtag ? QStringLiteral("rv_jtag") : QStringLiteral("primary_jtag"));
                    scan.fields.insert(QStringLiteral("evidence"), relativePath);
                    result.append(scan);
                    scanOpen = false;
                    tdiBits.clear();
                    tdoBits.clear();
                }
            }
            ProtocolEvent cycle;
            cycle.groupId = QStringLiteral("jtag");
            cycle.type = QStringLiteral("jtag_cycle");
            cycle.startTime = ticksToVcdTime(edge.timestamp);
            cycle.endTime = cycle.startTime + 1;
            cycle.summary = QStringLiteral("JTAG STATE=%1 TMS=%2 TDI=%3 TDO=%4")
                .arg(tapStateName(nextState), tms ? QStringLiteral("1") : QStringLiteral("0"),
                     tdi ? QStringLiteral("1") : QStringLiteral("0"),
                     tdo ? QStringLiteral("1") : QStringLiteral("0"));
            cycle.fields.insert(QStringLiteral("state"), tapStateName(nextState));
            cycle.fields.insert(QStringLiteral("tms"), tms);
            cycle.fields.insert(QStringLiteral("tdi"), tdi);
            cycle.fields.insert(QStringLiteral("tdo"), tdo);
            cycle.fields.insert(QStringLiteral("source"), QStringLiteral("sparse_tap_cycles"));
            cycle.fields.insert(QStringLiteral("evidence"), relativePath);
            result.append(cycle);
            tapState = nextState;
        }
    }
    std::sort(result.begin(), result.end(), [](const ProtocolEvent& left, const ProtocolEvent& right) {
        if (left.startTime != right.startTime) return left.startTime < right.startTime;
        if (left.endTime != right.endTime) return left.endTime < right.endTime;
        return left.type < right.type;
    });
    return result;
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

QJsonObject makeMismatchGroup(const QList<MismatchEvent>& events,
                              const QList<ProtocolEvent>& protocolEvents)
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
    for (const ProtocolEvent& event : protocolEvents) {
        if (event.groupId == QStringLiteral("mismatch")) {
            transactions.append(protocolEventToJson(event));
        }
    }
    if (!transactions.isEmpty()) {
        group.insert(QStringLiteral("status"), QStringLiteral("event_detected"));
        group.insert(QStringLiteral("reason"), QStringLiteral("检测到 mismatch 事件"));
    }
    group.insert(QStringLiteral("transactions"), transactions);

    return group;
}

QJsonArray defaultGroups(
    const QList<ProtocolEvent>& protocolEvents,
    const QList<MismatchEvent>& mismatchEvents,
    const QList<VcdSample>& samples,
    const quint32 designGapMask)
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
        if (id == QStringLiteral("usb")) realSource = !wide || sampleBit(samples.first(), 775);
        if (wide && id == QStringLiteral("jtag")) realSource = sampleBit(samples.first(), 776);
        if ((designGapMask & (1U << index)) != 0U) realSource = false;
        if (!realSource) {
            group.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
            group.insert(QStringLiteral("reason"), QStringLiteral("unavailable/design_gap: 无真实协议源"));
            groups.append(group);
            continue;
        }
        QJsonArray transactions;
        bool hasDecodedTransaction = false;
        for (const ProtocolEvent& event : protocolEvents) {
            if (event.groupId == definitions.at(index).id) {
                transactions.append(protocolEventToJson(event));
                hasDecodedTransaction = hasDecodedTransaction ||
                    event.type != QStringLiteral("raw_line_event");
            }
        }
        if (!transactions.isEmpty()) {
            group.insert(QStringLiteral("status"), hasDecodedTransaction
                ? QStringLiteral("event_detected") : QStringLiteral("limited"));
            group.insert(QStringLiteral("reason"), hasDecodedTransaction
                ? QStringLiteral("检测到真实协议事务")
                : QStringLiteral("检测到真实线级活动，但现有字段不足以重建完整协议事务"));
            group.insert(QStringLiteral("transactions"), transactions);
        }
        groups.append(group);
    }
    groups.append(makeMismatchGroup(mismatchEvents, protocolEvents));
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

QJsonArray keyBehaviorsToJson(const QList<ProtocolEvent>& protocolEvents,
                              const QList<MismatchEvent>& mismatchEvents)
{
    QList<QJsonObject> objects;
    for (const ProtocolEvent& event : protocolEvents) {
        objects.append(protocolEventToJson(event));
    }
    for (const MismatchEvent& event : mismatchEvents) {
        objects.append(mismatchEventToJson(event));
    }
    std::sort(objects.begin(), objects.end(), [](const QJsonObject& left, const QJsonObject& right) {
        const qint64 leftStart = left.value(QStringLiteral("start_time")).toInteger();
        const qint64 rightStart = right.value(QStringLiteral("start_time")).toInteger();
        if (leftStart != rightStart) return leftStart < rightStart;
        return left.value(QStringLiteral("end_time")).toInteger() <
            right.value(QStringLiteral("end_time")).toInteger();
    });
    QJsonArray array;
    for (const QJsonObject& object : objects) array.append(object);
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
    const QList<ProtocolDiagnostic>& diagnostics,
    const quint32 designGapMask = 0U)
{
    QByteArray uartTxBytes;
    for (const ProtocolEvent& event : protocolEvents) {
        if (event.groupId != QStringLiteral("uart") || event.type != QStringLiteral("uart_frame") ||
            event.fields.value(QStringLiteral("direction")).toString() != QStringLiteral("TX") ||
            event.fields.value(QStringLiteral("frame_error")).toBool()) {
            continue;
        }
        const QString dataText = event.fields.value(QStringLiteral("data")).toString();
        bool ok = false;
        const uint data = dataText.startsWith(QStringLiteral("0x"))
            ? dataText.mid(2).toUInt(&ok, 16) : dataText.toUInt(&ok, 16);
        if (ok && data <= 0xffU) uartTxBytes.append(static_cast<char>(data));
    }
    const QString uartTxText = QString::fromLatin1(uartTxBytes);
    QString programDoneMarker;
    if (uartTxText.contains(QStringLiteral("PROGRAM_RUN_DONE"), Qt::CaseInsensitive)) {
        programDoneMarker = QStringLiteral("PROGRAM_RUN_DONE");
    } else if (uartTxText.contains(QStringLiteral("LOCKSTEP_RUN_DONE"), Qt::CaseInsensitive)) {
        programDoneMarker = QStringLiteral("LOCKSTEP_RUN_DONE");
    }

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
    analysis.insert(QStringLiteral("groups"),
                    defaultGroups(protocolEvents, mismatchEvents, parseResult.samples, designGapMask));
    analysis.insert(QStringLiteral("protocol_events"), protocolEventsToJson(protocolEvents));
    analysis.insert(QStringLiteral("uart_tx_text"), uartTxText);
    analysis.insert(QStringLiteral("program_done_marker"), programDoneMarker);
    analysis.insert(QStringLiteral("program_done_marker_detected"), !programDoneMarker.isEmpty());
    analysis.insert(QStringLiteral("samples"), samplesToJson(parseResult.samples));
    analysis.insert(QStringLiteral("key_behaviors"), keyBehaviorsToJson(protocolEvents, mismatchEvents));
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
            field(QStringLiteral("sclk"), 544), field(QStringLiteral("mosi"), 545),
            field(QStringLiteral("miso"), 546), field(QStringLiteral("cs_n"), 547)
        }),
        group(QStringLiteral("can"), QStringLiteral("CAN"), {
            field(QStringLiteral("rx"), 576), field(QStringLiteral("tx"), 577)
        }),
        group(QStringLiteral("i2c"), QStringLiteral("I2C"), {
            field(QStringLiteral("scl"), 608), field(QStringLiteral("sda"), 609)
        }),
        group(QStringLiteral("eth"), QStringLiteral("ETH"), {
            field(QStringLiteral("tx_en"), 640), field(QStringLiteral("tx_er"), 641, 1, true),
            field(QStringLiteral("rx_dv"), 642), field(QStringLiteral("rx_er"), 643, 1, true),
            field(QStringLiteral("txd"), 648, 8), field(QStringLiteral("rxd"), 656, 8)
        }),
        group(QStringLiteral("usb"), QStringLiteral("USB"), {
            field(QStringLiteral("dp"), 704), field(QStringLiteral("dm"), 705)
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

    quint32 sampleRateHz = 0U;
    QJsonObject captureSidecar;
    const QString captureSidecarPath = QDir(taskRootPath).filePath(
        QStringLiteral("evidence/capture_sidecar.json"));
    if (QFileInfo::exists(captureSidecarPath) &&
        readJsonObject(captureSidecarPath, &captureSidecar, nullptr)) {
        const qint64 rate = captureSidecar.value(QStringLiteral("sample_rate_hz")).toInteger();
        if (rate > 0 && rate <= std::numeric_limits<quint32>::max()) {
            sampleRateHz = static_cast<quint32>(rate);
        }
    }
    QList<ProtocolEvent> protocolEvents = decodeProtocolEvents(
        parseResult.samples, sampleRateHz,
        parseResult.timescaleMultiplier * timeUnitToPs(parseResult.timescaleUnit));
    quint32 designGapMask = 0U;
    bool sparseArchiveValid = true;
    protocolEvents.append(loadSparseProtocolEvents(
        taskRootPath,
        captureSidecar,
        parseResult.timescaleMultiplier * timeUnitToPs(parseResult.timescaleUnit),
        &designGapMask,
        &sparseArchiveValid,
        &result.diagnostics));
    std::sort(protocolEvents.begin(), protocolEvents.end(), [](const ProtocolEvent& left,
                                                               const ProtocolEvent& right) {
        if (left.startTime != right.startTime) return left.startTime < right.startTime;
        return left.groupId < right.groupId;
    });
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
        result.diagnostics,
        designGapMask);
    if (!sparseArchiveValid) {
        result.analysis.insert(QStringLiteral("status"), QStringLiteral("failed"));
    }
    QString writeError;
    if (!writeJsonObject(analysisPath, result.analysis, &writeError)) {
        result.status = QStringLiteral("failed");
        result.errorMessage = writeError;
        return result;
    }

    if (!sparseArchiveValid) {
        result.status = QStringLiteral("failed");
        result.errorMessage = QStringLiteral("稀疏协议事件证据无效");
        result.wroteAnalysis = true;
        return result;
    }

    result.success = true;
    result.wroteAnalysis = true;
    result.status = QStringLiteral("complete");
    return result;
}

}  // namespace lockstep::protocol_analyzer
