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

quint16 readU16(const QByteArray& raw, const int offset, const bool littleEndian)
{
    const quint8 b0 = static_cast<quint8>(raw.at(offset));
    const quint8 b1 = static_cast<quint8>(raw.at(offset + 1));
    if (littleEndian) {
        return static_cast<quint16>(static_cast<quint16>(b1) << 8U) | static_cast<quint16>(b0);
    }
    return static_cast<quint16>(static_cast<quint16>(b0) << 8U) | static_cast<quint16>(b1);
}

quint32 readU32(const QByteArray& raw, const int offset, const bool littleEndian)
{
    quint32 value = 0U;
    if (littleEndian) {
        for (int i = 3; i >= 0; --i) {
            value = (value << 8U) | static_cast<quint32>(static_cast<quint8>(raw.at(offset + i)));
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            value = (value << 8U) | static_cast<quint32>(static_cast<quint8>(raw.at(offset + i)));
        }
    }
    return value;
}

quint64 readU64(const QByteArray& raw, const int offset, const bool littleEndian)
{
    quint64 value = 0U;
    if (littleEndian) {
        for (int i = 7; i >= 0; --i) {
            value = (value << 8U) | static_cast<quint64>(static_cast<quint8>(raw.at(offset + i)));
        }
    } else {
        for (int i = 0; i < 8; ++i) {
            value = (value << 8U) | static_cast<quint64>(static_cast<quint8>(raw.at(offset + i)));
        }
    }
    return value;
}

bool sliceInRange(const QByteArray& raw, const quint64 offset, const quint64 size)
{
    const quint64 rawSize = static_cast<quint64>(raw.size());
    if (offset > rawSize) {
        return false;
    }
    return size <= (rawSize - offset);
}

bool parseElf32(
    const QByteArray& raw,
    const bool littleEndian,
    QList<ImageSegment>* const segments,
    quint64* const entryAddress)
{
    if (raw.size() < 52 || segments == nullptr || entryAddress == nullptr) {
        return false;
    }

    *entryAddress = static_cast<quint64>(readU32(raw, 24, littleEndian));
    const quint64 programHeaderOffset = static_cast<quint64>(readU32(raw, 28, littleEndian));
    const quint16 programHeaderEntrySize = readU16(raw, 42, littleEndian);
    const quint16 programHeaderCount = readU16(raw, 44, littleEndian);
    if (programHeaderEntrySize < 32U || programHeaderCount == 0U) {
        return false;
    }

    QList<ImageSegment> parsed;
    for (quint16 i = 0U; i < programHeaderCount; ++i) {
        const quint64 entryOffset = programHeaderOffset + (static_cast<quint64>(i) * programHeaderEntrySize);
        if (!sliceInRange(raw, entryOffset, programHeaderEntrySize)) {
            return false;
        }
        const int base = static_cast<int>(entryOffset);
        const quint32 type = readU32(raw, base, littleEndian);
        constexpr quint32 kLoadableSegment = 1U;
        if (type != kLoadableSegment) {
            continue;
        }

        const quint64 fileOffset = static_cast<quint64>(readU32(raw, base + 4, littleEndian));
        const quint64 virtualAddress = static_cast<quint64>(readU32(raw, base + 8, littleEndian));
        const quint64 physicalAddress = static_cast<quint64>(readU32(raw, base + 12, littleEndian));
        const quint64 fileSize = static_cast<quint64>(readU32(raw, base + 16, littleEndian));
        if (fileSize == 0U) {
            continue;
        }
        if (!sliceInRange(raw, fileOffset, fileSize)) {
            return false;
        }

        ImageSegment segment;
        segment.address = (physicalAddress != 0U) ? physicalAddress : virtualAddress;
        segment.data = raw.mid(static_cast<int>(fileOffset), static_cast<int>(fileSize));
        parsed.append(segment);
    }

    if (parsed.isEmpty()) {
        return false;
    }

    *segments = parsed;
    return true;
}

bool parseElf64(
    const QByteArray& raw,
    const bool littleEndian,
    QList<ImageSegment>* const segments,
    quint64* const entryAddress)
{
    if (raw.size() < 64 || segments == nullptr || entryAddress == nullptr) {
        return false;
    }

    *entryAddress = readU64(raw, 24, littleEndian);
    const quint64 programHeaderOffset = readU64(raw, 32, littleEndian);
    const quint16 programHeaderEntrySize = readU16(raw, 54, littleEndian);
    const quint16 programHeaderCount = readU16(raw, 56, littleEndian);
    if (programHeaderEntrySize < 56U || programHeaderCount == 0U) {
        return false;
    }

    QList<ImageSegment> parsed;
    for (quint16 i = 0U; i < programHeaderCount; ++i) {
        const quint64 entryOffset = programHeaderOffset + (static_cast<quint64>(i) * programHeaderEntrySize);
        if (!sliceInRange(raw, entryOffset, programHeaderEntrySize)) {
            return false;
        }
        const int base = static_cast<int>(entryOffset);
        const quint32 type = readU32(raw, base, littleEndian);
        constexpr quint32 kLoadableSegment = 1U;
        if (type != kLoadableSegment) {
            continue;
        }

        const quint64 fileOffset = readU64(raw, base + 8, littleEndian);
        const quint64 virtualAddress = readU64(raw, base + 16, littleEndian);
        const quint64 physicalAddress = readU64(raw, base + 24, littleEndian);
        const quint64 fileSize = readU64(raw, base + 32, littleEndian);
        if (fileSize == 0U) {
            continue;
        }
        if (!sliceInRange(raw, fileOffset, fileSize)) {
            return false;
        }

        ImageSegment segment;
        segment.address = (physicalAddress != 0U) ? physicalAddress : virtualAddress;
        segment.data = raw.mid(static_cast<int>(fileOffset), static_cast<int>(fileSize));
        parsed.append(segment);
    }

    if (parsed.isEmpty()) {
        return false;
    }

    *segments = parsed;
    return true;
}

bool parseElf(
    const QByteArray& raw,
    QList<ImageSegment>* const segments,
    quint64* const entryAddress,
    QString* const errorMessage)
{
    if (segments == nullptr || entryAddress == nullptr) {
        return false;
    }
    if (raw.size() < 16 ||
        raw.at(0) != '\x7f' ||
        raw.at(1) != 'E' ||
        raw.at(2) != 'L' ||
        raw.at(3) != 'F') {
        return false;
    }

    const quint8 elfClass = static_cast<quint8>(raw.at(4));
    const quint8 dataEncoding = static_cast<quint8>(raw.at(5));
    const bool littleEndian = (dataEncoding == 1U);
    if (dataEncoding != 1U && dataEncoding != 2U) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("ELF 字节序不可识别");
        }
        return false;
    }

    bool parsed = false;
    if (elfClass == 1U) {
        parsed = parseElf32(raw, littleEndian, segments, entryAddress);
    } else if (elfClass == 2U) {
        parsed = parseElf64(raw, littleEndian, segments, entryAddress);
    } else if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("ELF 类型不可识别");
    }

    if (!parsed && errorMessage != nullptr && errorMessage->isEmpty()) {
        *errorMessage = QStringLiteral("ELF 可加载段解析失败");
    }
    return parsed;
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

int stagePercent(const OperationStage stage)
{
    int percent = 0;
    switch (stage) {
    case OperationStage::CheckDebugAccess:
    case OperationStage::CheckReadbackAccess:
    case OperationStage::CheckRunGate:
    case OperationStage::CheckHaltAccess:
        percent = 15;
        break;
    case OperationStage::DetectImage:
    case OperationStage::PrepareReadRanges:
        percent = 30;
        break;
    case OperationStage::ParseWritePlan:
        percent = 45;
        break;
    case OperationStage::WriteSegments:
    case OperationStage::ReadSegments:
    case OperationStage::DispatchRun:
    case OperationStage::DispatchHalt:
        percent = 65;
        break;
    case OperationStage::ConfirmWriteResult:
    case OperationStage::CompareData:
    case OperationStage::CaptureRunStatus:
    case OperationStage::CaptureHaltStatus:
        percent = 85;
        break;
    case OperationStage::PersistWriteRecord:
    case OperationStage::PersistVerifyRecord:
    case OperationStage::PersistRunRecord:
    case OperationStage::PersistHaltRecord:
    case OperationStage::Completed:
        percent = 100;
        break;
    case OperationStage::Failed:
        percent = 100;
        break;
    case OperationStage::NotStarted:
    default:
        percent = 0;
        break;
    }
    return percent;
}

QString stageMessage(const OperationStage stage)
{
    QString message;
    switch (stage) {
    case OperationStage::CheckDebugAccess:
        message = QStringLiteral("确认片上调试器和写入通道可用");
        break;
    case OperationStage::DetectImage:
        message = QStringLiteral("识别程序镜像格式");
        break;
    case OperationStage::ParseWritePlan:
        message = QStringLiteral("解析写入地址和段信息");
        break;
    case OperationStage::WriteSegments:
        message = QStringLiteral("发送写入命令并等待返回");
        break;
    case OperationStage::ConfirmWriteResult:
        message = QStringLiteral("确认烧写结果");
        break;
    case OperationStage::PersistWriteRecord:
        message = QStringLiteral("保存烧写记录");
        break;
    case OperationStage::CheckReadbackAccess:
        message = QStringLiteral("确认回读通道可用");
        break;
    case OperationStage::PrepareReadRanges:
        message = QStringLiteral("准备回读地址范围");
        break;
    case OperationStage::ReadSegments:
        message = QStringLiteral("发送回读命令并等待返回");
        break;
    case OperationStage::CompareData:
        message = QStringLiteral("比较回读数据");
        break;
    case OperationStage::PersistVerifyRecord:
        message = QStringLiteral("保存回读校验记录");
        break;
    case OperationStage::CheckRunGate:
        message = QStringLiteral("确认运行前置条件");
        break;
    case OperationStage::DispatchRun:
        message = QStringLiteral("发送程序运行命令");
        break;
    case OperationStage::CaptureRunStatus:
        message = QStringLiteral("获取运行返回或状态快照");
        break;
    case OperationStage::PersistRunRecord:
        message = QStringLiteral("保存运行控制记录");
        break;
    case OperationStage::CheckHaltAccess:
        message = QStringLiteral("确认中止控制通道可用");
        break;
    case OperationStage::DispatchHalt:
        message = QStringLiteral("发送程序中止命令");
        break;
    case OperationStage::CaptureHaltStatus:
        message = QStringLiteral("获取中止返回或状态快照");
        break;
    case OperationStage::PersistHaltRecord:
        message = QStringLiteral("保存中止控制记录");
        break;
    case OperationStage::Completed:
        message = QStringLiteral("操作完成");
        break;
    case OperationStage::Failed:
        message = QStringLiteral("操作失败");
        break;
    case OperationStage::NotStarted:
    default:
        message = QStringLiteral("尚未开始");
        break;
    }
    return message;
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

QString toString(const ProgramOperation operation)
{
    QString text;
    switch (operation) {
    case ProgramOperation::Readback:
        text = QStringLiteral("readback");
        break;
    case ProgramOperation::Run:
        text = QStringLiteral("run");
        break;
    case ProgramOperation::Halt:
        text = QStringLiteral("halt");
        break;
    case ProgramOperation::Write:
    default:
        text = QStringLiteral("write");
        break;
    }
    return text;
}

QString toString(const OperationStage stage)
{
    QString text;
    switch (stage) {
    case OperationStage::CheckDebugAccess:
        text = QStringLiteral("check_debug_access");
        break;
    case OperationStage::DetectImage:
        text = QStringLiteral("detect_image");
        break;
    case OperationStage::ParseWritePlan:
        text = QStringLiteral("parse_write_plan");
        break;
    case OperationStage::WriteSegments:
        text = QStringLiteral("write_segments");
        break;
    case OperationStage::ConfirmWriteResult:
        text = QStringLiteral("confirm_write_result");
        break;
    case OperationStage::PersistWriteRecord:
        text = QStringLiteral("persist_write_record");
        break;
    case OperationStage::CheckReadbackAccess:
        text = QStringLiteral("check_readback_access");
        break;
    case OperationStage::PrepareReadRanges:
        text = QStringLiteral("prepare_read_ranges");
        break;
    case OperationStage::ReadSegments:
        text = QStringLiteral("read_segments");
        break;
    case OperationStage::CompareData:
        text = QStringLiteral("compare_data");
        break;
    case OperationStage::PersistVerifyRecord:
        text = QStringLiteral("persist_verify_record");
        break;
    case OperationStage::CheckRunGate:
        text = QStringLiteral("check_run_gate");
        break;
    case OperationStage::DispatchRun:
        text = QStringLiteral("dispatch_run");
        break;
    case OperationStage::CaptureRunStatus:
        text = QStringLiteral("capture_run_status");
        break;
    case OperationStage::PersistRunRecord:
        text = QStringLiteral("persist_run_record");
        break;
    case OperationStage::CheckHaltAccess:
        text = QStringLiteral("check_halt_access");
        break;
    case OperationStage::DispatchHalt:
        text = QStringLiteral("dispatch_halt");
        break;
    case OperationStage::CaptureHaltStatus:
        text = QStringLiteral("capture_halt_status");
        break;
    case OperationStage::PersistHaltRecord:
        text = QStringLiteral("persist_halt_record");
        break;
    case OperationStage::Completed:
        text = QStringLiteral("completed");
        break;
    case OperationStage::Failed:
        text = QStringLiteral("failed");
        break;
    case OperationStage::NotStarted:
    default:
        text = QStringLiteral("not_started");
        break;
    }
    return text;
}

OperationProgress makeOperationProgress(
    const ProgramOperation operation,
    const OperationStage stage)
{
    OperationProgress progress;
    progress.operation = operation;
    progress.stage = stage;
    progress.percent = stagePercent(stage);
    progress.message = stageMessage(stage);
    progress.canCancel = (operation == ProgramOperation::Run || operation == ProgramOperation::Halt) &&
        (stage != OperationStage::Completed) &&
        (stage != OperationStage::Failed);
    return progress;
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
        QString elfError;
        if (!parseElf(raw, &info.segments, &info.entryAddress, &elfError)) {
            info.type = ImageType::Unknown;
            info.errorMessage = elfError.isEmpty()
                ? QStringLiteral("ELF 可加载段解析失败")
                : elfError;
            return info;
        }
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
    record.operation = ProgramOperation::Run;
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

RunControlRecord ProgramController::haltTarget(
    DebugAccess& access,
    const QString& taskId) const
{
    RunControlRecord record;
    record.operation = ProgramOperation::Halt;
    record.taskId = taskId;

    const DebugResult result = access.halt();
    record.rawReturn = result.rawReturn;
    record.snapshot = result.success ? QStringLiteral("target_halted") : QString();
    record.errorMessage = result.errorMessage;
    record.state = result.success ? RunState::Halted : RunState::Failed;
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
