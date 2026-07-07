/*****************************************************************************
*  @file      target_control.cpp
*  @brief     目标连接烧写回读运行控制模块实现
*  Details.   实现目标连接烧写回读运行控制模块的业务逻辑、状态转换和文件访问流程。
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

#include "target_control.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <limits>

namespace lockstep::target_control {
namespace {

QString requestId(const QString& prefix)
{
    return QStringLiteral("%1_%2").arg(
        prefix,
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
}

DebugResult makeFailure(const QString& idPrefix, const QString& message)
{
    DebugResult result;
    result.requestId = requestId(idPrefix);
    result.success = false;
    result.errorMessage = message;
    result.rawReturn = message;
    return result;
}

DebugResult makeSuccess(const QString& idPrefix, const QString& rawReturn)
{
    DebugResult result;
    result.requestId = requestId(idPrefix);
    result.success = true;
    result.rawReturn = rawReturn;
    return result;
}

bool addWillOverflow(const quint64 base, const quint64 size)
{
    return (std::numeric_limits<quint64>::max() - base) < size;
}

bool isRangeAllowed(const DebugProfile& profile, const quint64 address, const quint64 length)
{
    if (length == 0U || addWillOverflow(address, length)) {
        return false;
    }
    if (address < profile.ramBaseAddress) {
        return false;
    }
    if (profile.maxWritableAddress == 0U) {
        return true;
    }
    return (address + length) <= profile.maxWritableAddress;
}

QByteArray sha256Bytes(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool readWholeFile(const QString& path, QByteArray* const data, QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法读取镜像: %1").arg(path);
        }
        return false;
    }

    *data = file.readAll();
    return true;
}

int hexValue(const QChar value)
{
    if (value >= QChar::fromLatin1('0') && value <= QChar::fromLatin1('9')) {
        return value.unicode() - QChar::fromLatin1('0').unicode();
    }
    if (value >= QChar::fromLatin1('A') && value <= QChar::fromLatin1('F')) {
        return 10 + value.unicode() - QChar::fromLatin1('A').unicode();
    }
    if (value >= QChar::fromLatin1('a') && value <= QChar::fromLatin1('f')) {
        return 10 + value.unicode() - QChar::fromLatin1('a').unicode();
    }
    return -1;
}

bool parseHexByte(const QString& text, const int offset, quint8* const byte)
{
    if ((byte == nullptr) || ((offset + 1) >= text.size())) {
        return false;
    }
    const int high = hexValue(text.at(offset));
    const int low = hexValue(text.at(offset + 1));
    if (high < 0 || low < 0) {
        return false;
    }

    *byte = static_cast<quint8>((high * 16) + low);
    return true;
}

bool parseSrecLine(const QString& line, ImageSegment* const segment)
{
    if (segment == nullptr || line.size() < 4 || line.at(0) != QChar::fromLatin1('S')) {
        return false;
    }

    const QChar type = line.at(1);
    int addressBytes = 0;
    if (type == QChar::fromLatin1('1')) {
        addressBytes = 2;
    } else if (type == QChar::fromLatin1('2')) {
        addressBytes = 3;
    } else if (type == QChar::fromLatin1('3')) {
        addressBytes = 4;
    } else {
        return false;
    }

    quint8 count = 0U;
    if (!parseHexByte(line, 2, &count)) {
        return false;
    }

    const int dataBytes = static_cast<int>(count) - addressBytes - 1;
    if (dataBytes < 0) {
        return false;
    }

    const int expectedChars = 4 + (static_cast<int>(count) * 2);
    if (line.size() < expectedChars) {
        return false;
    }

    quint64 address = 0U;
    int cursor = 4;
    for (int i = 0; i < addressBytes; ++i) {
        quint8 byte = 0U;
        if (!parseHexByte(line, cursor, &byte)) {
            return false;
        }
        address = (address << 8U) | static_cast<quint64>(byte);
        cursor += 2;
    }

    QByteArray data;
    data.reserve(dataBytes);
    for (int i = 0; i < dataBytes; ++i) {
        quint8 byte = 0U;
        if (!parseHexByte(line, cursor, &byte)) {
            return false;
        }
        data.append(static_cast<char>(byte));
        cursor += 2;
    }

    segment->address = address;
    segment->data = data;
    return true;
}

bool parseSrec(const QByteArray& raw, QList<ImageSegment>* const segments)
{
    if (segments == nullptr) {
        return false;
    }

    QList<ImageSegment> parsed;
    const QString text = QString::fromLatin1(raw);
    const QStringList lines = text.split(QChar::fromLatin1('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        ImageSegment segment;
        if (parseSrecLine(line, &segment)) {
            parsed.append(segment);
        }
    }

    if (parsed.isEmpty()) {
        return false;
    }

    *segments = parsed;
    return true;
}

bool parseIntelHexRecord(
    const QString& line,
    quint64* const baseAddress,
    quint64* const entryAddress,
    QList<ImageSegment>* const segments)
{
    if (baseAddress == nullptr || entryAddress == nullptr || segments == nullptr) {
        return false;
    }
    if (line.size() < 11 || line.at(0) != QChar::fromLatin1(':')) {
        return false;
    }

    quint8 byteCount = 0U;
    quint8 addressHigh = 0U;
    quint8 addressLow = 0U;
    quint8 recordType = 0U;
    if (!parseHexByte(line, 1, &byteCount) ||
        !parseHexByte(line, 3, &addressHigh) ||
        !parseHexByte(line, 5, &addressLow) ||
        !parseHexByte(line, 7, &recordType)) {
        return false;
    }

    const int expectedChars = 11 + (static_cast<int>(byteCount) * 2);
    if (line.size() < expectedChars) {
        return false;
    }

    quint32 checksumSum = byteCount;
    checksumSum += addressHigh;
    checksumSum += addressLow;
    checksumSum += recordType;

    QByteArray data;
    data.reserve(static_cast<int>(byteCount));
    int cursor = 9;
    for (int i = 0; i < static_cast<int>(byteCount); ++i) {
        quint8 byte = 0U;
        if (!parseHexByte(line, cursor, &byte)) {
            return false;
        }
        checksumSum += byte;
        data.append(static_cast<char>(byte));
        cursor += 2;
    }

    quint8 checksum = 0U;
    if (!parseHexByte(line, cursor, &checksum)) {
        return false;
    }
    checksumSum += checksum;
    if ((checksumSum & 0xFFU) != 0U) {
        return false;
    }

    const quint16 offset = static_cast<quint16>(
        (static_cast<quint16>(addressHigh) << 8U) | static_cast<quint16>(addressLow));
    if (recordType == 0U) {
        ImageSegment segment;
        segment.address = *baseAddress + static_cast<quint64>(offset);
        segment.data = data;
        segments->append(segment);
    } else if (recordType == 1U) {
        return true;
    } else if (recordType == 2U && data.size() == 2) {
        const quint64 value = (static_cast<quint64>(static_cast<quint8>(data.at(0))) << 8U) |
            static_cast<quint64>(static_cast<quint8>(data.at(1)));
        *baseAddress = value << 4U;
    } else if (recordType == 4U && data.size() == 2) {
        const quint64 value = (static_cast<quint64>(static_cast<quint8>(data.at(0))) << 8U) |
            static_cast<quint64>(static_cast<quint8>(data.at(1)));
        *baseAddress = value << 16U;
    } else if (recordType == 5U && data.size() == 4) {
        *entryAddress =
            (static_cast<quint64>(static_cast<quint8>(data.at(0))) << 24U) |
            (static_cast<quint64>(static_cast<quint8>(data.at(1))) << 16U) |
            (static_cast<quint64>(static_cast<quint8>(data.at(2))) << 8U) |
            static_cast<quint64>(static_cast<quint8>(data.at(3)));
    } else if (recordType != 3U) {
        return false;
    }

    return true;
}

bool parseIntelHex(
    const QByteArray& raw,
    QList<ImageSegment>* const segments,
    quint64* const entryAddress)
{
    if (segments == nullptr || entryAddress == nullptr) {
        return false;
    }

    QList<ImageSegment> parsed;
    quint64 baseAddress = 0U;
    quint64 parsedEntry = *entryAddress;
    const QString text = QString::fromLatin1(raw);
    const QStringList lines = text.split(QChar::fromLatin1('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!parseIntelHexRecord(line, &baseAddress, &parsedEntry, &parsed)) {
            return false;
        }
    }

    if (parsed.isEmpty()) {
        return false;
    }

    *segments = parsed;
    *entryAddress = parsedEntry;
    return true;
}

bool hasSrecSuffix(const QString& suffix)
{
    const QString normalized = suffix.toLower();
    return normalized == QStringLiteral("srec") ||
        normalized == QStringLiteral("s19") ||
        normalized == QStringLiteral("s28") ||
        normalized == QStringLiteral("s37") ||
        normalized == QStringLiteral("mot");
}

bool hasIntelHexSuffix(const QString& suffix)
{
    const QString normalized = suffix.toLower();
    return normalized == QStringLiteral("hex") ||
        normalized == QStringLiteral("ihex") ||
        normalized == QStringLiteral("ihx");
}

quint64 totalLength(const QList<ImageSegment>& segments)
{
    quint64 length = 0U;
    for (const ImageSegment& segment : segments) {
        length += static_cast<quint64>(segment.data.size());
    }
    return length;
}

}  // namespace

QString toString(const ImageType type)
{
    QString text;
    switch (type) {
    case ImageType::Elf:
        text = QStringLiteral("elf");
        break;
    case ImageType::Srec:
        text = QStringLiteral("srec");
        break;
    case ImageType::Bin:
        text = QStringLiteral("bin");
        break;
    case ImageType::IntelHex:
        text = QStringLiteral("intel_hex");
        break;
    case ImageType::Unknown:
    default:
        text = QStringLiteral("unknown");
        break;
    }
    return text;
}

QString toString(const VerifyState state)
{
    QString text;
    switch (state) {
    case VerifyState::Passed:
        text = QStringLiteral("passed");
        break;
    case VerifyState::Mismatch:
        text = QStringLiteral("mismatch");
        break;
    case VerifyState::Failed:
        text = QStringLiteral("failed");
        break;
    case VerifyState::NotRun:
    default:
        text = QStringLiteral("not_run");
        break;
    }
    return text;
}

QString toString(const RunState state)
{
    QString text;
    switch (state) {
    case RunState::Ready:
        text = QStringLiteral("ready");
        break;
    case RunState::Running:
        text = QStringLiteral("running");
        break;
    case RunState::Halted:
        text = QStringLiteral("halted");
        break;
    case RunState::Failed:
        text = QStringLiteral("failed");
        break;
    case RunState::NotAllowed:
    default:
        text = QStringLiteral("not_allowed");
        break;
    }
    return text;
}

InMemoryDebugAccess::InMemoryDebugAccess(const quint64 memorySizeBytes)
    : memory_(static_cast<int>(memorySizeBytes), '\0')
{
}

DebugResult InMemoryDebugAccess::connectTarget(const DebugProfile& profile)
{
    profile_ = profile;
    connected_ = true;
    running_ = false;
    return makeSuccess(QStringLiteral("connect"), QStringLiteral("connected:%1").arg(profile.profileId));
}

DebugResult InMemoryDebugAccess::disconnectTarget()
{
    connected_ = false;
    running_ = false;
    return makeSuccess(QStringLiteral("disconnect"), QStringLiteral("disconnected"));
}

DebugResult InMemoryDebugAccess::status()
{
    if (!connected_) {
        return makeFailure(QStringLiteral("status"), QStringLiteral("target not connected"));
    }

    return makeSuccess(
        QStringLiteral("status"),
        running_ ? QStringLiteral("state=running") : QStringLiteral("state=halted"));
}

DebugResult InMemoryDebugAccess::read(const quint64 address, const quint64 length)
{
    if (!connected_) {
        return makeFailure(QStringLiteral("read"), QStringLiteral("target not connected"));
    }
    if (!isRangeAllowed(profile_, address, length) || length > static_cast<quint64>(memory_.size())) {
        return makeFailure(QStringLiteral("read"), QStringLiteral("read range invalid"));
    }

    const quint64 offset = address - profile_.ramBaseAddress;
    if ((offset + length) > static_cast<quint64>(memory_.size())) {
        return makeFailure(QStringLiteral("read"), QStringLiteral("read range exceeds memory"));
    }

    DebugResult result = makeSuccess(QStringLiteral("read"), QStringLiteral("read:%1").arg(length));
    result.data = memory_.mid(static_cast<int>(offset), static_cast<int>(length));
    return result;
}

DebugResult InMemoryDebugAccess::write(const quint64 address, const QByteArray& data)
{
    if (!connected_) {
        return makeFailure(QStringLiteral("write"), QStringLiteral("target not connected"));
    }
    const quint64 length = static_cast<quint64>(data.size());
    if (!isRangeAllowed(profile_, address, length)) {
        return makeFailure(QStringLiteral("write"), QStringLiteral("write range invalid"));
    }

    const quint64 offset = address - profile_.ramBaseAddress;
    if ((offset + length) > static_cast<quint64>(memory_.size())) {
        return makeFailure(QStringLiteral("write"), QStringLiteral("write range exceeds memory"));
    }

    for (int i = 0; i < data.size(); ++i) {
        memory_[static_cast<int>(offset) + i] = data.at(i);
    }

    return makeSuccess(QStringLiteral("write"), QStringLiteral("write:%1").arg(data.size()));
}

DebugResult InMemoryDebugAccess::reset(const QString& strategy)
{
    if (!connected_) {
        return makeFailure(QStringLiteral("reset"), QStringLiteral("target not connected"));
    }
    running_ = false;
    return makeSuccess(QStringLiteral("reset"), QStringLiteral("reset:%1").arg(strategy));
}

DebugResult InMemoryDebugAccess::run(const quint64 entryAddress)
{
    if (!connected_) {
        return makeFailure(QStringLiteral("run"), QStringLiteral("target not connected"));
    }
    running_ = true;
    return makeSuccess(QStringLiteral("run"), QStringLiteral("running:%1").arg(entryAddress));
}

DebugResult InMemoryDebugAccess::halt()
{
    if (!connected_) {
        return makeFailure(QStringLiteral("halt"), QStringLiteral("target not connected"));
    }
    running_ = false;
    return makeSuccess(QStringLiteral("halt"), QStringLiteral("halted"));
}

ConnectionRecord TargetConnectionService::connectTarget(
    DebugAccess& access,
    const DebugProfile& profile) const
{
    const DebugResult result = access.connectTarget(profile);
    ConnectionRecord record;
    record.profileId = profile.profileId;
    record.rawReturn = result.rawReturn;
    record.errorMessage = result.errorMessage;
    record.state = result.success ? ConnectionState::Connected : ConnectionState::Failed;
    return record;
}

ConnectionRecord TargetConnectionService::disconnectTarget(DebugAccess& access) const
{
    const DebugResult result = access.disconnectTarget();
    ConnectionRecord record;
    record.rawReturn = result.rawReturn;
    record.errorMessage = result.errorMessage;
    record.state = result.success ? ConnectionState::NotConnected : ConnectionState::Failed;
    return record;
}

PrecheckRecord TargetConnectionService::runPrecheck(
    DebugAccess& access,
    const DebugProfile& profile) const
{
    PrecheckRecord record;
    const DebugResult statusResult = access.status();
    if (!statusResult.success) {
        record.state = PrecheckState::Failed;
        record.errorMessage = statusResult.errorMessage;
        record.rawReturn = statusResult.rawReturn;
        return record;
    }

    const DebugResult resetResult = access.reset(profile.resetStrategy);
    const QByteArray probe(4, '\0');
    const DebugResult writeResult = access.write(profile.ramBaseAddress, probe);
    const DebugResult readResult = access.read(profile.ramBaseAddress, static_cast<quint64>(probe.size()));

    record.resetSupported = resetResult.success;
    record.writeSupported = writeResult.success;
    record.readSupported = readResult.success;
    record.runSupported = true;
    record.rawReturn = QStringLiteral("%1;%2;%3")
        .arg(statusResult.rawReturn, writeResult.rawReturn, readResult.rawReturn);
    record.state = (record.resetSupported && record.writeSupported && record.readSupported)
        ? PrecheckState::Passed
        : PrecheckState::Failed;
    return record;
}

ProgramImageInfo ProgramController::detectImage(
    const QString& imagePath,
    const DebugProfile& profile) const
{
    ProgramImageInfo info;
    const QFileInfo fileInfo(imagePath);
    info.fileName = fileInfo.fileName();

    QByteArray raw;
    QString error;
    if (!readWholeFile(imagePath, &raw, &error)) {
        info.errorMessage = error;
        return info;
    }

    info.sha256 = QString::fromLatin1(sha256Bytes(raw));
    info.sizeBytes = raw.size();
    info.entryAddress = profile.defaultRunAddress;

    const QString suffix = fileInfo.suffix();
    if (raw.size() >= 4 &&
        raw.at(0) == '\x7f' &&
        raw.at(1) == 'E' &&
        raw.at(2) == 'L' &&
        raw.at(3) == 'F') {
        info.type = ImageType::Elf;
        ImageSegment segment;
        segment.address = profile.ramBaseAddress;
        segment.data = raw;
        info.segments.append(segment);
    } else if (parseSrec(raw, &info.segments) || hasSrecSuffix(suffix)) {
        if (info.segments.isEmpty() && !parseSrec(raw, &info.segments)) {
            info.errorMessage = QStringLiteral("SREC 内容不可识别");
            return info;
        }
        info.type = ImageType::Srec;
    } else if (parseIntelHex(raw, &info.segments, &info.entryAddress) || hasIntelHexSuffix(suffix)) {
        if (info.segments.isEmpty() && !parseIntelHex(raw, &info.segments, &info.entryAddress)) {
            info.errorMessage = QStringLiteral("Intel HEX 内容不可识别");
            return info;
        }
        info.type = ImageType::IntelHex;
    } else if (suffix.compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
        info.type = ImageType::Bin;
        ImageSegment segment;
        segment.address = profile.ramBaseAddress;
        segment.data = raw;
        info.segments.append(segment);
    } else {
        info.errorMessage = QStringLiteral("不支持或无法识别的镜像格式");
    }

    for (const ImageSegment& segment : info.segments) {
        if (!isRangeAllowed(profile, segment.address, static_cast<quint64>(segment.data.size()))) {
            info.type = ImageType::Unknown;
            info.segments.clear();
            info.errorMessage = QStringLiteral("镜像写入范围越界");
            break;
        }
    }

    return info;
}

WriteRecord ProgramController::programTarget(
    DebugAccess& access,
    const QString& taskId,
    const ProgramImageInfo& image) const
{
    WriteRecord record;
    record.taskId = taskId;
    if (image.type == ImageType::Unknown || image.segments.isEmpty()) {
        record.errorMessage = image.errorMessage.isEmpty() ? QStringLiteral("镜像无效") : image.errorMessage;
        return record;
    }

    QStringList returns;
    for (const ImageSegment& segment : image.segments) {
        const DebugResult result = access.write(segment.address, segment.data);
        returns.append(result.rawReturn);
        if (!result.success) {
            record.errorMessage = result.errorMessage;
            record.rawReturn = returns.join(QChar::fromLatin1(';'));
            return record;
        }
    }

    record.success = true;
    record.segments = image.segments;
    record.rawReturn = returns.join(QChar::fromLatin1(';'));
    return record;
}

ReadbackVerifyRecord ProgramController::verifyReadback(
    DebugAccess& access,
    const QString& taskId,
    const ProgramImageInfo& image) const
{
    ReadbackVerifyRecord record;
    record.taskId = taskId;
    if (image.type == ImageType::Unknown || image.segments.isEmpty()) {
        record.state = VerifyState::Failed;
        record.errorMessage = QStringLiteral("镜像无效，无法回读");
        return record;
    }

    QStringList returns;
    for (const ImageSegment& segment : image.segments) {
        const quint64 expectedSize = static_cast<quint64>(segment.data.size());
        const DebugResult result = access.read(segment.address, expectedSize);
        returns.append(result.rawReturn);
        record.expectedLength += expectedSize;
        record.actualLength += static_cast<quint64>(result.data.size());
        if (!result.success) {
            record.state = VerifyState::Failed;
            record.errorMessage = result.errorMessage;
            record.rawReturn = returns.join(QChar::fromLatin1(';'));
            return record;
        }
        const int compareLength = qMin(segment.data.size(), result.data.size());
        for (int i = 0; i < compareLength; ++i) {
            if (segment.data.at(i) != result.data.at(i)) {
                ++record.diffCount;
            }
        }
        if (segment.data.size() != result.data.size()) {
            const int delta = qAbs(segment.data.size() - result.data.size());
            record.diffCount += static_cast<quint64>(delta);
        }
    }

    record.rawReturn = returns.join(QChar::fromLatin1(';'));
    record.state = (record.diffCount == 0U && record.actualLength == totalLength(image.segments))
        ? VerifyState::Passed
        : VerifyState::Mismatch;
    return record;
}

RunControlRecord ProgramController::runTarget(
    DebugAccess& access,
    const QString& taskId,
    const ProgramImageInfo& image,
    const ReadbackVerifyRecord& verifyRecord) const
{
    RunControlRecord record;
    record.taskId = taskId;
    record.entryAddress = image.entryAddress;
    if (verifyRecord.state != VerifyState::Passed) {
        record.state = RunState::NotAllowed;
        record.errorMessage = QStringLiteral("回读未通过，禁止运行");
        return record;
    }

    const DebugResult result = access.run(image.entryAddress);
    record.rawReturn = result.rawReturn;
    record.snapshot = result.success ? QStringLiteral("target_running") : QString();
    record.errorMessage = result.errorMessage;
    record.state = result.success ? RunState::Running : RunState::Failed;
    return record;
}

ProgramGate ProgramController::getProgramGate(
    const ConnectionRecord& connection,
    const PrecheckRecord& precheck,
    const WriteRecord& writeRecord,
    const ReadbackVerifyRecord& verifyRecord) const
{
    ProgramGate gate;
    gate.connected = (connection.state == ConnectionState::Connected);
    gate.precheckPassed = (precheck.state == PrecheckState::Passed);
    gate.programWritten = writeRecord.success;
    gate.readbackPassed = (verifyRecord.state == VerifyState::Passed);
    gate.runAllowed = gate.connected && gate.precheckPassed && gate.programWritten && gate.readbackPassed;

    if (!gate.connected) {
        gate.reason = QStringLiteral("目标未连接");
    } else if (!gate.precheckPassed) {
        gate.reason = QStringLiteral("预检未通过");
    } else if (!gate.programWritten) {
        gate.reason = QStringLiteral("烧写未成功");
    } else if (!gate.readbackPassed) {
        gate.reason = QStringLiteral("回读校验未通过");
    }

    return gate;
}

}  // namespace lockstep::target_control
