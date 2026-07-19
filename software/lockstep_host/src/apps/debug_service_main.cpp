/**********************************************************
* 文件名: debug_service_main.cpp
* 日期: 2026-07-09
* 版本: 1.0.0.1
* 更新记录: 合并为 lockstep_ui_preview 的调试服务模式
* 描述: 解析统一产品程序的调试请求并执行自研板卡传输层。
**********************************************************/

#include "debug_service_entry.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <hidapi/hidapi.h>

#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr char kRequestSchema[] = "lockstep-debug-service-request-v1";
constexpr char kResponseSchema[] = "lockstep-debug-service-response-v1";
constexpr char kLocalServerName[] = "lockstep_ui_preview_local_v1";

constexpr unsigned short kDefaultCmsisDapVendorId = 0x0D28U;
constexpr unsigned short kDefaultCmsisDapProductId = 0x0204U;
constexpr int kCmsisDapPayloadBytes = 64;
constexpr int kCmsisDapReportBytes = kCmsisDapPayloadBytes + 1;
constexpr int kCmsisDapTimeoutMs = 3000;
constexpr int kCmsisDapCommandRetryCount = 3;

constexpr unsigned char kDapInfo = 0x00U;
constexpr unsigned char kDapConnect = 0x02U;
constexpr unsigned char kDapDisconnect = 0x03U;
constexpr unsigned char kDapResetTarget = 0x0AU;
constexpr unsigned char kDapSwjClock = 0x11U;
constexpr unsigned char kDapSwjSequence = 0x12U;
constexpr unsigned char kDapJtagSequence = 0x14U;
constexpr unsigned char kDapJtagConfigure = 0x15U;
constexpr unsigned char kDapJtagIdCode = 0x16U;

constexpr unsigned char kDapOk = 0x00U;
constexpr unsigned char kDapPortJtag = 0x02U;
constexpr int kDefaultTapIrLength = 6;

constexpr quint32 kDefaultIdcodeIr = 0x01U;
constexpr quint32 kDefaultDtmcsIr = 0x10U;
constexpr quint32 kDefaultDmiIr = 0x11U;
constexpr int kDefaultDmiRetryCount = 64;
constexpr int kDefaultDmPollCount = 200;
constexpr int kDefaultSystemBusAccessBytes = 4;
constexpr int kDefaultRiscvXlen = 64;

constexpr unsigned int kDtmcsVersionOffset = 0U;
constexpr unsigned int kDtmcsAbitsOffset = 4U;
constexpr unsigned int kDtmcsDmistatOffset = 10U;
constexpr unsigned int kDtmcsIdleOffset = 12U;
constexpr quint32 kDtmcsDmiReset = 0x00010000U;

constexpr unsigned int kDmiOpOffset = 0U;
constexpr unsigned int kDmiDataOffset = 2U;
constexpr unsigned int kDmiAddressOffset = 34U;
constexpr quint32 kDmiOpNop = 0U;
constexpr quint32 kDmiOpRead = 1U;
constexpr quint32 kDmiOpWrite = 2U;
constexpr quint32 kDmiStatusSuccess = 0U;
constexpr quint32 kDmiStatusFailed = 2U;
constexpr quint32 kDmiStatusBusy = 3U;
constexpr quint32 kDmiStatusMask = 0x3U;

constexpr quint32 kDmData0 = 0x04U;
constexpr quint32 kDmData1 = 0x05U;
constexpr quint32 kDmControl = 0x10U;
constexpr quint32 kDmStatus = 0x11U;
constexpr quint32 kDmAbstractCs = 0x16U;
constexpr quint32 kDmCommand = 0x17U;
constexpr quint32 kDmSbCs = 0x38U;
constexpr quint32 kDmSbAddress0 = 0x39U;
constexpr quint32 kDmSbAddress1 = 0x3AU;
constexpr quint32 kDmSbData0 = 0x3CU;
constexpr quint32 kDmSbData1 = 0x3DU;

constexpr quint32 kDmControlDmActive = 0x00000001U;
constexpr quint32 kDmControlSetResetHaltReq = 0x00000008U;
constexpr quint32 kDmControlHaltReq = 0x80000000U;
constexpr quint32 kDmControlResumeReq = 0x40000000U;
constexpr quint32 kDmControlAckHaveReset = 0x10000000U;
constexpr quint32 kDmControlHartSelMask = 0x03FFFFC0U;

constexpr quint32 kDmStatusAuthenticated = 0x00000080U;
constexpr quint32 kDmStatusAnyHalted = 0x00000100U;
constexpr quint32 kDmStatusAllHalted = 0x00000200U;
constexpr quint32 kDmStatusAnyRunning = 0x00000400U;
constexpr quint32 kDmStatusAllRunning = 0x00000800U;
constexpr quint32 kDmStatusAllUnavailable = 0x00002000U;
constexpr quint32 kDmStatusAllNonexistent = 0x00008000U;
constexpr quint32 kDmStatusAnyResumeAck = 0x00010000U;
constexpr quint32 kDmStatusAllResumeAck = 0x00020000U;
constexpr quint32 kDmStatusAnyHaveReset = 0x00040000U;

constexpr quint32 kAbstractCsBusy = 0x00001000U;
constexpr quint32 kAbstractCsCommandErrorMask = 0x00000700U;
constexpr quint32 kAbstractCsCommandErrorClear = 0x00000700U;
constexpr quint32 kAbstractCommandAccessRegister = 0x00000000U;
constexpr quint32 kAbstractCommandTransfer = 0x00020000U;
constexpr quint32 kAbstractCommandWrite = 0x00010000U;
constexpr quint32 kAbstractCommandAarSize32 = 0x00200000U;
constexpr quint32 kAbstractCommandAarSize64 = 0x00300000U;
constexpr quint32 kRiscvCsrMepc = 0x0341U;
constexpr quint32 kRiscvCsrMcause = 0x0342U;
constexpr quint32 kRiscvCsrMtval = 0x0343U;
constexpr quint32 kRiscvCsrDpc = 0x07B1U;

constexpr quint32 kSbCsBusyError = 0x00400000U;
constexpr quint32 kSbCsBusy = 0x00200000U;
constexpr quint32 kSbCsReadOnAddress = 0x00100000U;
constexpr quint32 kSbCsAccessOffset = 17U;
constexpr quint32 kSbCsAutoIncrement = 0x00010000U;
constexpr quint32 kSbCsReadOnData = 0x00008000U;
constexpr quint32 kSbCsErrorMask = 0x00007000U;
constexpr quint32 kSbCsAddressSizeMask = 0x00000FE0U;
constexpr quint32 kSbCsAccess64 = 0x00000008U;
constexpr quint32 kSbCsAccess32 = 0x00000004U;
constexpr quint32 kSbCsAccess16 = 0x00000002U;
constexpr quint32 kSbCsAccess8 = 0x00000001U;

enum class DebugOperation : unsigned char {
    Connect = 0U,
    Disconnect = 1U,
    Status = 2U,
    Read = 3U,
    Write = 4U,
    Reset = 5U,
    Run = 6U,
    Halt = 7U,
    Unknown = 8U
};

struct MemorySegmentRequest final {
    quint64 address = 0U;
    quint64 length = 0U;
    QString dataPath;
};

struct DebugServiceRequest final {
    QString requestId;
    DebugOperation operation = DebugOperation::Unknown;
    QString operationText;
    QString responsePath;
    QJsonObject profile;
    QString interfaceConfigPath;
    QString targetConfigPath;
    QString progressPath;
    int adapterSpeedKhz = 0;
    quint64 address = 0U;
    quint64 entryAddress = 0U;
    quint64 length = 0U;
    QString dataPath;
    QList<MemorySegmentRequest> segments;
    QString strategy;
};

struct TransportResult final {
    bool success = false;
    QString errorCode;
    QString errorMessage;
    QByteArray data;
    QJsonObject snapshot;
};

QString nowText()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

void setError(QString* const errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QString operationToText(const DebugOperation operation)
{
    QString text;
    switch (operation) {
    case DebugOperation::Connect:
        text = QStringLiteral("connect");
        break;
    case DebugOperation::Disconnect:
        text = QStringLiteral("disconnect");
        break;
    case DebugOperation::Status:
        text = QStringLiteral("status");
        break;
    case DebugOperation::Read:
        text = QStringLiteral("read");
        break;
    case DebugOperation::Write:
        text = QStringLiteral("write");
        break;
    case DebugOperation::Reset:
        text = QStringLiteral("reset");
        break;
    case DebugOperation::Run:
        text = QStringLiteral("run");
        break;
    case DebugOperation::Halt:
        text = QStringLiteral("halt");
        break;
    case DebugOperation::Unknown:
    default:
        text = QStringLiteral("unknown");
        break;
    }
    return text;
}

DebugOperation parseOperation(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("connect")) {
        return DebugOperation::Connect;
    }
    if (normalized == QStringLiteral("disconnect")) {
        return DebugOperation::Disconnect;
    }
    if (normalized == QStringLiteral("status")) {
        return DebugOperation::Status;
    }
    if (normalized == QStringLiteral("read")) {
        return DebugOperation::Read;
    }
    if (normalized == QStringLiteral("write")) {
        return DebugOperation::Write;
    }
    if (normalized == QStringLiteral("reset")) {
        return DebugOperation::Reset;
    }
    if (normalized == QStringLiteral("run")) {
        return DebugOperation::Run;
    }
    if (normalized == QStringLiteral("halt")) {
        return DebugOperation::Halt;
    }
    return DebugOperation::Unknown;
}

bool parseU64(const QString& text, quint64* const value)
{
    if (value == nullptr) {
        return false;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    bool ok = false;
    quint64 parsed = 0U;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        parsed = trimmed.mid(2).toULongLong(&ok, 16);
    } else {
        parsed = trimmed.toULongLong(&ok, 10);
    }
    if (!ok) {
        return false;
    }

    *value = parsed;
    return true;
}

bool parseJsonU64(const QJsonValue& jsonValue, quint64* const value)
{
    if (value == nullptr) {
        return false;
    }

    if (jsonValue.isString()) {
        return parseU64(jsonValue.toString(), value);
    }

    if (!jsonValue.isDouble()) {
        return false;
    }

    const double numeric = jsonValue.toDouble(-1.0);
    if (numeric < 0.0 || numeric > static_cast<double>(std::numeric_limits<quint64>::max())) {
        return false;
    }

    const quint64 parsed = static_cast<quint64>(numeric);
    if (static_cast<double>(parsed) != numeric) {
        return false;
    }

    *value = parsed;
    return true;
}

QString hexAddress(const quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

bool addWillOverflow(const quint64 lhs, const quint64 rhs)
{
    return (std::numeric_limits<quint64>::max() - lhs) < rhs;
}

bool fileExistsIfSet(const QString& path)
{
    const QString trimmed = path.trimmed();
    return !trimmed.isEmpty() && QFileInfo::exists(trimmed);
}

bool readJsonObject(
    const QString& path,
    QJsonObject* const object,
    QString* const errorMessage)
{
    if (object == nullptr) {
        setError(errorMessage, QStringLiteral("JSON 输出对象为空。"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取请求文件: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("请求文件不是有效 JSON 对象: %1").arg(path));
        return false;
    }

    *object = document.object();
    return true;
}

bool validateJsonConfigFile(
    const QString& path,
    const QString& expectedSchema,
    const QString& label,
    QString* const errorMessage)
{
    if (!fileExistsIfSet(path)) {
        setError(errorMessage, QStringLiteral("%1不存在: %2").arg(label, path));
        return false;
    }

    QJsonObject object;
    QString error;
    if (!readJsonObject(path, &object, &error)) {
        setError(errorMessage, QStringLiteral("%1不是有效 JSON: %2").arg(label, error));
        return false;
    }

    if (object.value(QStringLiteral("schema")).toString() != expectedSchema) {
        setError(errorMessage, QStringLiteral("%1 schema 不匹配: %2").arg(label, path));
        return false;
    }
    return true;
}

bool writeJsonObject(
    const QString& path,
    const QJsonObject& object,
    QString* const errorMessage)
{
    const QFileInfo fileInfo(path);
    const QString directoryPath = fileInfo.absoluteDir().absolutePath();
    if (!QDir().mkpath(directoryPath)) {
        setError(errorMessage, QStringLiteral("无法创建响应目录: %1").arg(directoryPath));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("无法写入响应文件: %1").arg(path));
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        setError(errorMessage, QStringLiteral("响应文件写入不完整: %1").arg(path));
        return false;
    }

    if (!file.commit()) {
        setError(errorMessage, QStringLiteral("响应文件提交失败: %1").arg(path));
        return false;
    }
    return true;
}

bool writeProgressObject(
    const QString& path,
    const quint64 completedBytes,
    const quint64 totalBytes,
    const QString& message)
{
    if (path.trimmed().isEmpty()) {
        return true;
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), QStringLiteral("lockstep-debug-service-progress-v1"));
    object.insert(QStringLiteral("completed_bytes"), QString::number(completedBytes));
    object.insert(QStringLiteral("total_bytes"), QString::number(totalBytes));
    object.insert(QStringLiteral("message"), message);
    QString ignoredError;
    return writeJsonObject(path, object, &ignoredError);
}

QJsonObject requestProfileSnapshot(const DebugServiceRequest& request)
{
    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("profile_id"), request.profile.value(QStringLiteral("profile_id")).toString());
    snapshot.insert(QStringLiteral("profile_name"), request.profile.value(QStringLiteral("profile_name")).toString());
    snapshot.insert(QStringLiteral("ram_base_address"), request.profile.value(QStringLiteral("ram_base_address")).toString());
    snapshot.insert(QStringLiteral("default_run_address"), request.profile.value(QStringLiteral("default_run_address")).toString());
    snapshot.insert(QStringLiteral("max_writable_address"), request.profile.value(QStringLiteral("max_writable_address")).toString());
    snapshot.insert(QStringLiteral("reset_strategy"), request.profile.value(QStringLiteral("reset_strategy")).toString());
    return snapshot;
}

QJsonObject requestConfigSnapshot(const DebugServiceRequest& request)
{
    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("interface_config_present"), fileExistsIfSet(request.interfaceConfigPath));
    snapshot.insert(QStringLiteral("target_config_present"), fileExistsIfSet(request.targetConfigPath));
    snapshot.insert(QStringLiteral("adapter_speed_khz"), request.adapterSpeedKhz);
    return snapshot;
}

QJsonObject makeResponse(
    const DebugServiceRequest& request,
    const TransportResult& result)
{
    QJsonObject response;
    response.insert(QStringLiteral("schema"), QString::fromLatin1(kResponseSchema));
    response.insert(QStringLiteral("request_id"), request.requestId);
    response.insert(QStringLiteral("operation"), operationToText(request.operation));
    response.insert(QStringLiteral("generated_at"), nowText());
    response.insert(QStringLiteral("success"), result.success);
    response.insert(QStringLiteral("error_code"), result.errorCode);
    response.insert(QStringLiteral("error_message"), result.errorMessage);
    response.insert(QStringLiteral("data_hex"), QString::fromLatin1(result.data.toHex()));
    response.insert(QStringLiteral("snapshot"), result.snapshot);
    response.insert(QStringLiteral("profile"), requestProfileSnapshot(request));
    response.insert(QStringLiteral("config"), requestConfigSnapshot(request));
    return response;
}

TransportResult makeFailureResult(
    const QString& code,
    const QString& message,
    const DebugServiceRequest& request)
{
    TransportResult result;
    result.success = false;
    result.errorCode = code;
    result.errorMessage = message;
    result.snapshot.insert(QStringLiteral("target_state"), QStringLiteral("unknown"));
    result.snapshot.insert(QStringLiteral("operation"), operationToText(request.operation));
    return result;
}

bool validateConfigFiles(
    const DebugServiceRequest& request,
    QString* const errorMessage)
{
    if (request.operation == DebugOperation::Disconnect) {
        return true;
    }

    if (!validateJsonConfigFile(
            request.interfaceConfigPath,
            QStringLiteral("lockstep-debug-interface-v1"),
            QStringLiteral("调试接口配置"),
            errorMessage)) {
        return false;
    }
    if (!validateJsonConfigFile(
            request.targetConfigPath,
            QStringLiteral("lockstep-debug-target-v1"),
            QStringLiteral("目标配置"),
            errorMessage)) {
        return false;
    }
    if (request.adapterSpeedKhz <= 0) {
        setError(errorMessage, QStringLiteral("调试接口速度无效。"));
        return false;
    }
    return true;
}

unsigned char byteAt(const QByteArray& data, const int index)
{
    return static_cast<unsigned char>(data.at(index));
}

void appendLe32(QByteArray* const data, const quint32 value)
{
    if (data == nullptr) {
        return;
    }

    data->append(static_cast<char>(value & 0xFFU));
    data->append(static_cast<char>((value >> 8U) & 0xFFU));
    data->append(static_cast<char>((value >> 16U) & 0xFFU));
    data->append(static_cast<char>((value >> 24U) & 0xFFU));
}

void appendBitsToU32(
    const QByteArray& bytes,
    const int bitCount,
    int* const bitOffset,
    quint32* const value)
{
    if (bitOffset == nullptr || value == nullptr || bitCount <= 0) {
        return;
    }

    for (int bit = 0; bit < bitCount; ++bit) {
        const int byteIndex = bit / 8;
        const int bitIndex = bit % 8;
        if (byteIndex >= bytes.size() || *bitOffset >= 32) {
            break;
        }

        const quint32 bitValue = (static_cast<quint32>(byteAt(bytes, byteIndex)) >> bitIndex) & 0x1U;
        *value |= (bitValue << static_cast<unsigned int>(*bitOffset));
        ++(*bitOffset);
    }
}

QString hexU32(const quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QChar::fromLatin1('0'));
}

bool isValidJtagIdCode(const quint32 value)
{
    return ((value & 0x1U) == 0x1U) && value != 0xFFFFFFFFU;
}

QString wideText(const wchar_t* const text)
{
    return (text == nullptr) ? QString() : QString::fromWCharArray(text);
}

bool parseU16JsonValue(const QJsonValue& value, unsigned short* const parsed)
{
    if (parsed == nullptr) {
        return false;
    }

    quint64 number = 0U;
    bool ok = false;
    if (value.isString()) {
        ok = parseU64(value.toString(), &number);
    } else if (value.isDouble()) {
        const int intValue = value.toInt(-1);
        if (intValue >= 0) {
            number = static_cast<quint64>(intValue);
            ok = true;
        }
    }

    if (!ok || number > 0xFFFFU) {
        return false;
    }

    *parsed = static_cast<unsigned short>(number);
    return true;
}

bool parseU32JsonValue(const QJsonValue& value, quint32* const parsed)
{
    if (parsed == nullptr) {
        return false;
    }

    quint64 number = 0U;
    bool ok = false;
    if (value.isString()) {
        ok = parseU64(value.toString(), &number);
    } else if (value.isDouble()) {
        const qint64 intValue = static_cast<qint64>(value.toDouble(-1.0));
        if (intValue >= 0) {
            number = static_cast<quint64>(intValue);
            ok = true;
        }
    }

    if (!ok || number > std::numeric_limits<quint32>::max()) {
        return false;
    }

    *parsed = static_cast<quint32>(number);
    return true;
}

bool parseIntJsonValue(const QJsonValue& value, int* const parsed)
{
    if (parsed == nullptr) {
        return false;
    }

    quint64 number = 0U;
    bool ok = false;
    if (value.isString()) {
        ok = parseU64(value.toString(), &number);
    } else if (value.isDouble()) {
        const int intValue = value.toInt(-1);
        if (intValue >= 0) {
            number = static_cast<quint64>(intValue);
            ok = true;
        }
    }

    if (!ok || number > static_cast<quint64>(std::numeric_limits<int>::max())) {
        return false;
    }

    *parsed = static_cast<int>(number);
    return true;
}

quint64 lowBitMask(const int bitCount)
{
    if (bitCount <= 0) {
        return 0U;
    }
    if (bitCount >= 64) {
        return std::numeric_limits<quint64>::max();
    }
    return (1ULL << static_cast<unsigned int>(bitCount)) - 1ULL;
}

QByteArray packedBitsFromU64(const quint64 value, const int bitCount)
{
    const int byteCount = (bitCount + 7) / 8;
    QByteArray bytes(byteCount, '\0');
    for (int bit = 0; bit < bitCount; ++bit) {
        const int byteIndex = bit / 8;
        const int bitIndex = bit % 8;
        const quint64 bitValue = (value >> static_cast<unsigned int>(bit)) & 0x1ULL;
        if (bitValue != 0ULL) {
            bytes[byteIndex] = static_cast<char>(
                static_cast<unsigned char>(bytes.at(byteIndex)) |
                static_cast<unsigned char>(1U << static_cast<unsigned int>(bitIndex)));
        }
    }
    return bytes;
}

quint64 u64FromPackedBits(const QByteArray& bytes, const int bitCount)
{
    quint64 value = 0U;
    const int limitedBits = qMin(bitCount, 64);
    for (int bit = 0; bit < limitedBits; ++bit) {
        const int byteIndex = bit / 8;
        const int bitIndex = bit % 8;
        if (byteIndex >= bytes.size()) {
            break;
        }
        const quint64 bitValue = (static_cast<quint64>(byteAt(bytes, byteIndex)) >>
            static_cast<unsigned int>(bitIndex)) & 0x1ULL;
        value |= (bitValue << static_cast<unsigned int>(bit));
    }
    return value;
}

bool packedBitAt(const QByteArray& bytes, const int bitIndex)
{
    if (bitIndex < 0) {
        return false;
    }
    const int byteIndex = bitIndex / 8;
    const int offset = bitIndex % 8;
    if (byteIndex >= bytes.size()) {
        return false;
    }
    return (((byteAt(bytes, byteIndex) >> static_cast<unsigned int>(offset)) & 0x1U) != 0U);
}

void setPackedBit(QByteArray* const bytes, const int bitIndex, const bool value)
{
    if (bytes == nullptr || bitIndex < 0) {
        return;
    }
    const int byteIndex = bitIndex / 8;
    const int offset = bitIndex % 8;
    if (byteIndex >= bytes->size()) {
        const int oldSize = bytes->size();
        bytes->resize(byteIndex + 1);
        for (int index = oldSize; index < bytes->size(); ++index) {
            (*bytes)[index] = '\0';
        }
    }

    const unsigned char mask = static_cast<unsigned char>(1U << static_cast<unsigned int>(offset));
    unsigned char raw = static_cast<unsigned char>(bytes->at(byteIndex));
    if (value) {
        raw = static_cast<unsigned char>(raw | mask);
    } else {
        raw = static_cast<unsigned char>(raw & static_cast<unsigned char>(~mask));
    }
    (*bytes)[byteIndex] = static_cast<char>(raw);
}

QByteArray packedBitRange(const QByteArray& bytes, const int startBit, const int bitCount)
{
    QByteArray output((bitCount + 7) / 8, '\0');
    for (int bit = 0; bit < bitCount; ++bit) {
        setPackedBit(&output, bit, packedBitAt(bytes, startBit + bit));
    }
    return output;
}

void setPackedValue(
    QByteArray* const bytes,
    const int startBit,
    const int bitCount,
    const quint64 value)
{
    if (bytes == nullptr || startBit < 0 || bitCount < 0 || bitCount > 64) {
        return;
    }

    for (int bit = 0; bit < bitCount; ++bit) {
        const bool bitValue = (((value >> static_cast<unsigned int>(bit)) & 0x1ULL) != 0ULL);
        setPackedBit(bytes, startBit + bit, bitValue);
    }
}

void appendLeValue(QByteArray* const bytes, const quint64 value, const int byteCount)
{
    if (bytes == nullptr || byteCount <= 0 || byteCount > 8) {
        return;
    }

    for (int byte = 0; byte < byteCount; ++byte) {
        const quint64 shifted = value >> static_cast<unsigned int>(byte * 8);
        bytes->append(static_cast<char>(shifted & 0xFFULL));
    }
}

quint64 leValueFromBytes(
    const QByteArray& bytes,
    const int startIndex,
    const int byteCount)
{
    quint64 value = 0U;
    if (startIndex < 0 || byteCount <= 0 || byteCount > 8) {
        return value;
    }

    for (int byte = 0; byte < byteCount; ++byte) {
        const int index = startIndex + byte;
        if (index >= bytes.size()) {
            break;
        }
        const quint64 raw = static_cast<quint64>(byteAt(bytes, index));
        value |= raw << static_cast<unsigned int>(byte * 8);
    }
    return value;
}

bool readBinaryFile(
    const QString& path,
    QByteArray* const data,
    QString* const errorMessage)
{
    if (data == nullptr) {
        setError(errorMessage, QStringLiteral("二进制输出对象为空。"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取二进制文件: %1").arg(path));
        return false;
    }

    *data = file.readAll();
    return true;
}

struct CmsisDapProbeConfig final {
    unsigned short vendorId = kDefaultCmsisDapVendorId;
    unsigned short productId = kDefaultCmsisDapProductId;
    QString serialNumber;
};

struct TargetDebugConfig final {
    int tapIrLength = kDefaultTapIrLength;
    quint32 idcodeIr = kDefaultIdcodeIr;
    quint32 dtmcsIr = kDefaultDtmcsIr;
    quint32 dmiIr = kDefaultDmiIr;
    int dmiAddressBits = 0;
    int dmiRetryCount = kDefaultDmiRetryCount;
    int dmPollCount = kDefaultDmPollCount;
    int systemBusAccessBytes = kDefaultSystemBusAccessBytes;
    int sbaReadPollInterval = 1;
    int sbaWritePollInterval = 1;
    int sbaWritePipelineWords = 1;
    int xlen = kDefaultRiscvXlen;
};

struct DmiResponse final {
    quint32 opStatus = kDmiStatusSuccess;
    quint32 data = 0U;
    quint32 address = 0U;
};

bool loadProbeConfig(
    const QString& interfaceConfigPath,
    CmsisDapProbeConfig* const config,
    QString* const errorMessage)
{
    if (config == nullptr) {
        setError(errorMessage, QStringLiteral("调试接口配置输出对象为空。"));
        return false;
    }

    QJsonObject object;
    QString error;
    if (!readJsonObject(interfaceConfigPath, &object, &error)) {
        setError(errorMessage, error);
        return false;
    }

    CmsisDapProbeConfig parsed;
    const QJsonObject usb = object.value(QStringLiteral("usb")).toObject();
    unsigned short value = 0U;
    if (parseU16JsonValue(usb.value(QStringLiteral("vendor_id")), &value)) {
        parsed.vendorId = value;
    }
    if (parseU16JsonValue(usb.value(QStringLiteral("product_id")), &value)) {
        parsed.productId = value;
    }

    const QJsonObject probe = object.value(QStringLiteral("probe")).toObject();
    parsed.serialNumber = probe.value(QStringLiteral("serial_number")).toString().trimmed();

    *config = parsed;
    return true;
}

bool loadTargetDebugConfig(
    const QString& targetConfigPath,
    TargetDebugConfig* const config,
    QString* const errorMessage)
{
    if (config == nullptr) {
        setError(errorMessage, QStringLiteral("目标调试配置输出对象为空。"));
        return false;
    }

    QJsonObject object;
    QString error;
    if (!readJsonObject(targetConfigPath, &object, &error)) {
        setError(errorMessage, error);
        return false;
    }

    TargetDebugConfig parsed;
    const int irLength = object.value(QStringLiteral("tap"))
        .toObject()
        .value(QStringLiteral("ir_length"))
        .toInt(kDefaultTapIrLength);
    if (irLength <= 0 || irLength > 255) {
        setError(errorMessage, QStringLiteral("目标 TAP IR 长度无效。"));
        return false;
    }

    parsed.tapIrLength = irLength;

    const QJsonObject ir = object.value(QStringLiteral("ir")).toObject();
    quint32 irValue = 0U;
    if (parseU32JsonValue(ir.value(QStringLiteral("idcode")), &irValue)) {
        parsed.idcodeIr = irValue;
    }
    if (parseU32JsonValue(ir.value(QStringLiteral("dtmcs")), &irValue)) {
        parsed.dtmcsIr = irValue;
    }
    if (parseU32JsonValue(ir.value(QStringLiteral("dmi")), &irValue)) {
        parsed.dmiIr = irValue;
    }

    const QJsonObject dmi = object.value(QStringLiteral("dmi")).toObject();
    int intValue = 0;
    if (parseIntJsonValue(dmi.value(QStringLiteral("address_bits")), &intValue)) {
        parsed.dmiAddressBits = intValue;
    }
    if (parseIntJsonValue(dmi.value(QStringLiteral("retry_count")), &intValue)) {
        parsed.dmiRetryCount = intValue;
    }
    if (parseIntJsonValue(dmi.value(QStringLiteral("dm_poll_count")), &intValue)) {
        parsed.dmPollCount = intValue;
    }

    const QJsonObject memory = object.value(QStringLiteral("memory")).toObject();
    if (parseIntJsonValue(memory.value(QStringLiteral("system_bus_access_bytes")), &intValue)) {
        parsed.systemBusAccessBytes = intValue;
    }
    if (parseIntJsonValue(memory.value(QStringLiteral("sba_read_poll_interval")), &intValue)) {
        parsed.sbaReadPollInterval = intValue;
    }
    if (parseIntJsonValue(memory.value(QStringLiteral("sba_write_poll_interval")), &intValue)) {
        parsed.sbaWritePollInterval = intValue;
    }
    if (parseIntJsonValue(memory.value(QStringLiteral("sba_write_pipeline_words")), &intValue)) {
        parsed.sbaWritePipelineWords = intValue;
    }
    if (parseIntJsonValue(object.value(QStringLiteral("xlen")), &intValue)) {
        parsed.xlen = intValue;
    }

    if (parsed.dmiAddressBits < 0 || parsed.dmiAddressBits > 31) {
        setError(errorMessage, QStringLiteral("目标 DMI 地址位宽无效。"));
        return false;
    }
    if (parsed.dmiRetryCount <= 0 || parsed.dmPollCount <= 0) {
        setError(errorMessage, QStringLiteral("目标 DMI 轮询次数无效。"));
        return false;
    }
    if (parsed.systemBusAccessBytes != 1 &&
        parsed.systemBusAccessBytes != 2 &&
        parsed.systemBusAccessBytes != 4 &&
        parsed.systemBusAccessBytes != 8) {
        setError(errorMessage, QStringLiteral("目标 SBA 访问字节数无效。"));
        return false;
    }
    if (parsed.sbaReadPollInterval <= 0 || parsed.sbaReadPollInterval > 256) {
        setError(errorMessage, QStringLiteral("Invalid SBA read poll interval."));
        return false;
    }
    if (parsed.sbaWritePollInterval <= 0 || parsed.sbaWritePollInterval > 256) {
        setError(errorMessage, QStringLiteral("目标 SBA 写入 poll 间隔无效。"));
        return false;
    }
    if (parsed.sbaWritePipelineWords <= 0 || parsed.sbaWritePipelineWords > 2) {
        setError(errorMessage, QStringLiteral("目标 SBA 写入流水深度无效。"));
        return false;
    }
    if (parsed.xlen != 32 && parsed.xlen != 64) {
        setError(errorMessage, QStringLiteral("目标 XLEN 配置无效。"));
        return false;
    }

    *config = parsed;
    return true;
}

QString hidErrorString(hid_device* const handle)
{
    const QString text = wideText(hid_error(handle));
    return text.isEmpty() ? QStringLiteral("unknown HID error") : text;
}

QString hidInfoText(const hid_device_info* const info)
{
    if (info == nullptr) {
        return QString();
    }

    const QString manufacturer = wideText(info->manufacturer_string);
    const QString product = wideText(info->product_string);
    const QString serial = wideText(info->serial_number);
    return QStringLiteral("vid=0x%1 pid=0x%2 usage_page=0x%3 usage=0x%4 serial=%5 manufacturer=%6 product=%7")
        .arg(info->vendor_id, 4, 16, QChar::fromLatin1('0'))
        .arg(info->product_id, 4, 16, QChar::fromLatin1('0'))
        .arg(info->usage_page, 4, 16, QChar::fromLatin1('0'))
        .arg(info->usage, 4, 16, QChar::fromLatin1('0'))
        .arg(serial, manufacturer, product);
}

bool isLikelyCmsisDapDevice(const hid_device_info* const info)
{
    if (info == nullptr) {
        return false;
    }

    const QString product = wideText(info->product_string);
    const QString manufacturer = wideText(info->manufacturer_string);
    const QString combined = QStringLiteral("%1 %2").arg(product, manufacturer).toLower();
    return combined.contains(QStringLiteral("cmsis")) ||
        combined.contains(QStringLiteral("dap")) ||
        combined.contains(QStringLiteral("mbed"));
}

class CmsisDapDevice final {
public:
    CmsisDapDevice() = default;

    ~CmsisDapDevice()
    {
        close();
        (void)hid_exit();
    }

    CmsisDapDevice(const CmsisDapDevice&) = delete;
    CmsisDapDevice& operator=(const CmsisDapDevice&) = delete;

    bool open(const CmsisDapProbeConfig& config, QString* const errorMessage)
    {
        if (hid_init() != 0) {
            setError(errorMessage, QStringLiteral("HID 初始化失败。"));
            return false;
        }

        hid_device_info* devices = nullptr;
        for (int attempt = 0; attempt < 3 && devices == nullptr; ++attempt) {
            devices = hid_enumerate(config.vendorId, config.productId);
        }
        hid_device_info* selected = nullptr;
        QStringList candidates;
        for (hid_device_info* item = devices; item != nullptr; item = item->next) {
            candidates.append(hidInfoText(item));
            const QString serial = wideText(item->serial_number);
            const bool serialMatches = config.serialNumber.isEmpty() ||
                serial.compare(config.serialNumber, Qt::CaseInsensitive) == 0;
            if (serialMatches && selected == nullptr && isLikelyCmsisDapDevice(item)) {
                selected = item;
            }
        }

        if (selected == nullptr && devices != nullptr) {
            selected = devices;
        }

        if (selected == nullptr) {
            hid_free_enumeration(devices);
            setError(
                errorMessage,
                QStringLiteral("[DAP_NOT_ENUMERATED] 重枚举两次后仍未找到 CMSIS-DAP HID 设备: vid=0x%1 pid=0x%2。")
                    .arg(config.vendorId, 4, 16, QChar::fromLatin1('0'))
                    .arg(config.productId, 4, 16, QChar::fromLatin1('0')));
            return false;
        }

        path_ = QString::fromLocal8Bit(selected->path);
        manufacturer_ = wideText(selected->manufacturer_string);
        product_ = wideText(selected->product_string);
        serialNumber_ = wideText(selected->serial_number);
        selectedInfo_ = hidInfoText(selected);

        handle_ = hid_open_path(selected->path);
        hid_free_enumeration(devices);
        if (handle_ == nullptr) {
            setError(errorMessage, QStringLiteral("[DAP_OPEN_FAILED] 打开 CMSIS-DAP HID 设备失败，可能被占用或权限不足: %1")
                .arg(path_));
            return false;
        }

        (void)hid_set_nonblocking(handle_, 0);
        return true;
    }

    bool command(
        const QByteArray& request,
        QByteArray* const response,
        QString* const errorMessage)
    {
        if (handle_ == nullptr) {
            setError(errorMessage, QStringLiteral("CMSIS-DAP HID 设备未打开。"));
            return false;
        }
        if (request.isEmpty() || request.size() > kCmsisDapPayloadBytes) {
            setError(errorMessage, QStringLiteral("CMSIS-DAP 请求长度无效。"));
            return false;
        }

        QByteArray output(kCmsisDapReportBytes, '\0');
        for (int i = 0; i < request.size(); ++i) {
            output[i + 1] = request.at(i);
        }

        QString lastError;
        for (int attempt = 0; attempt < kCmsisDapCommandRetryCount; ++attempt) {
            unsigned char stale[kCmsisDapReportBytes] = {};
            while (hid_read_timeout(handle_, stale, sizeof(stale), 0) > 0) {
            }

            const int written = hid_write(
                handle_,
                reinterpret_cast<const unsigned char*>(output.constData()),
                static_cast<size_t>(output.size()));
            if (written < 0) {
                lastError = QStringLiteral("CMSIS-DAP 写入失败: %1").arg(hidErrorString(handle_));
                continue;
            }

            unsigned char input[kCmsisDapReportBytes] = {};
            const int readCount = hid_read_timeout(
                handle_,
                input,
                sizeof(input),
                kCmsisDapTimeoutMs);
            if (readCount <= 0) {
                lastError = QStringLiteral("CMSIS-DAP 读取超时或失败: %1").arg(hidErrorString(handle_));
                continue;
            }

            const QByteArray raw(reinterpret_cast<const char*>(input), readCount);
            int offset = -1;
            const unsigned char expectedCommand = byteAt(request, 0);
            if (raw.size() >= 1 && byteAt(raw, 0) == expectedCommand) {
                offset = 0;
            } else if (raw.size() >= 2 && byteAt(raw, 1) == expectedCommand) {
                offset = 1;
            }

            if (offset < 0) {
                lastError = QStringLiteral("CMSIS-DAP 响应命令不匹配: tx=%1 rx=%2")
                    .arg(QString::fromLatin1(request.toHex()), QString::fromLatin1(raw.toHex()));
                continue;
            }

            const QByteArray payload = raw.mid(offset);
            lastExchange_ = QStringLiteral("tx=%1 rx=%2")
                .arg(QString::fromLatin1(request.toHex()), QString::fromLatin1(payload.toHex()));
            transcript_.append(lastExchange_);
            if (response != nullptr) {
                *response = payload;
            }
            return true;
        }

        setError(errorMessage, lastError);
        return false;    }

    QString infoString(const unsigned char infoId)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapInfo));
        request.append(static_cast<char>(infoId));

        QByteArray response;
        QString error;
        if (!command(request, &response, &error) || response.size() < 2) {
            return QString();
        }

        const int length = static_cast<int>(byteAt(response, 1));
        if (length <= 0 || response.size() < (2 + length)) {
            return QString();
        }
        return QString::fromLatin1(response.mid(2, length)).trimmed();
    }

    bool expectStatusOk(
        const QByteArray& request,
        const QString& action,
        QString* const errorMessage)
    {
        QByteArray response;
        if (!command(request, &response, errorMessage)) {
            return false;
        }
        if (response.size() < 2 || byteAt(response, 1) != kDapOk) {
            setError(
                errorMessage,
                QStringLiteral("%1 失败: %2").arg(action, QString::fromLatin1(response.toHex())));
            return false;
        }
        return true;
    }

    void close()
    {
        if (handle_ != nullptr) {
            hid_close(handle_);
            handle_ = nullptr;
        }
    }

    QString path() const { return path_; }
    QString manufacturer() const { return manufacturer_; }
    QString product() const { return product_; }
    QString serialNumber() const { return serialNumber_; }
    QString selectedInfo() const { return selectedInfo_; }
    QString lastExchange() const { return lastExchange_; }
    qsizetype exchangeCount() const { return transcript_.size(); }

private:
    hid_device* handle_ = nullptr;
    QString path_;
    QString manufacturer_;
    QString product_;
    QString serialNumber_;
    QString selectedInfo_;
    QString lastExchange_;
    QStringList transcript_;
};

class CmsisDapJtagSession final {
public:
    bool initialize(const DebugServiceRequest& request, QString* const errorMessage)
    {
        if (initialized_) {
            return true;
        }

        CmsisDapProbeConfig probeConfig;
        if (!loadProbeConfig(request.interfaceConfigPath, &probeConfig, errorMessage)) {
            return false;
        }
        if (!loadTargetDebugConfig(request.targetConfigPath, &targetConfig_, errorMessage)) {
            return false;
        }
        if (!device_.open(probeConfig, errorMessage)) {
            return false;
        }

        vendorText_ = device_.infoString(0x01U);
        productText_ = device_.infoString(0x02U);
        serialText_ = device_.infoString(0x03U);
        firmwareText_ = device_.infoString(0x04U);

        if (!connectJtag(errorMessage)) {
            return false;
        }
        if (!setClock(request.adapterSpeedKhz, errorMessage)) {
            return false;
        }
        if (!configureTap(errorMessage)) {
            return false;
        }
        if (!tapReset(errorMessage)) {
            return false;
        }

        QString robustIdcodeError;
        for (int attempt = 0; attempt < 3 && !hasIdCode_; ++attempt) {
            if (attempt > 0) {
                QByteArray disconnectRequest;
                disconnectRequest.append(static_cast<char>(kDapDisconnect));
                QString disconnectError;
                (void)device_.expectStatusOk(
                    disconnectRequest, QStringLiteral("重新连接前断开 CMSIS-DAP"), &disconnectError);
                if (!connectJtag(&robustIdcodeError) ||
                    !setClock(request.adapterSpeedKhz, &robustIdcodeError) ||
                    !configureTap(&robustIdcodeError) ||
                    !tapReset(&robustIdcodeError)) {
                    continue;
                }
            }
            QString idcodeIrError;
            if (!selectIr(targetConfig_.idcodeIr, &idcodeIrError)) {
                robustIdcodeError = idcodeIrError;
            } else {
                hasIdCode_ = readIdCodeByDrScan(&idCode_, &robustIdcodeError);
                if (hasIdCode_ && !isValidJtagIdCode(idCode_)) {
                    robustIdcodeError = QStringLiteral("JTAG IDCODE invalid: %1").arg(hexU32(idCode_));
                    hasIdCode_ = false;
                }
            }
            if (!hasIdCode_) {
                quint32 fallbackIdCode = 0U;
                QString fallbackError;
                const bool fallbackOk = readIdCode(&fallbackIdCode, &fallbackError);
                if (fallbackOk && isValidJtagIdCode(fallbackIdCode)) {
                    idCode_ = fallbackIdCode;
                    robustIdcodeError.clear();
                    hasIdCode_ = true;
                } else if (fallbackOk) {
                    robustIdcodeError = QStringLiteral("JTAG IDCODE invalid: %1").arg(hexU32(fallbackIdCode));
                } else if (!fallbackError.isEmpty()) {
                    robustIdcodeError = fallbackError;
                }
            }
        }

        if (!hasIdCode_) {
            idcodeFailure_ = QStringLiteral("[JTAG_IDCODE_FAILED] TAP reset 和重新连接两次后仍无法读取 IDCODE: %1")
                .arg(robustIdcodeError);
            setError(errorMessage, idcodeFailure_);
            return false;
        }
        if (!isValidJtagIdCode(idCode_)) {
            idcodeFailure_ = QStringLiteral("JTAG IDCODE 无效: %1").arg(hexU32(idCode_));
            hasIdCode_ = false;
            setError(errorMessage, idcodeFailure_);
            return false;
        }
        initialized_ = true;
        return true;
    }

    bool resetTarget(QString* const errorMessage)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapResetTarget));
        if (!device_.expectStatusOk(request, QStringLiteral("目标复位"), errorMessage)) {
            return false;
        }
        const bool ok = tapReset(errorMessage);
        if (ok) {
            debugModuleReady_ = false;
            currentIr_ = std::numeric_limits<quint32>::max();
        }
        return ok;
    }

    bool setHaltOnReset(QString* const errorMessage)
    {
        if (!initializeDebugModule(errorMessage)) {
            return false;
        }
        return dmiWrite(
            kDmControl,
            baseDmControl() | kDmControlSetResetHaltReq,
            errorMessage);
    }

    bool disconnect(QString* const errorMessage)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapDisconnect));
        const bool ok = device_.expectStatusOk(request, QStringLiteral("断开 CMSIS-DAP"), errorMessage);
        initialized_ = false;
        debugModuleReady_ = false;
        currentIr_ = std::numeric_limits<quint32>::max();
        return ok;
    }

    QJsonObject snapshot(const DebugServiceRequest& request, const QString& targetState) const
    {
        quint64 segmentTotalLength = 0U;
        for (const MemorySegmentRequest& segment : request.segments) {
            segmentTotalLength += segment.length;
        }

        QJsonObject object;
        object.insert(QStringLiteral("target_state"), targetState);
        object.insert(QStringLiteral("operation"), operationToText(request.operation));
        object.insert(QStringLiteral("transport"), QStringLiteral("cmsis_dap_hid_jtag"));
        object.insert(QStringLiteral("hid_path"), device_.path());
        object.insert(QStringLiteral("hid_selected"), device_.selectedInfo());
        object.insert(QStringLiteral("manufacturer"), device_.manufacturer());
        object.insert(QStringLiteral("product"), device_.product());
        object.insert(QStringLiteral("serial_number"), device_.serialNumber());
        object.insert(QStringLiteral("dap_vendor"), vendorText_);
        object.insert(QStringLiteral("dap_product"), productText_);
        object.insert(QStringLiteral("dap_serial"), serialText_);
        object.insert(QStringLiteral("dap_firmware"), firmwareText_);
        object.insert(QStringLiteral("tap_ir_length"), targetConfig_.tapIrLength);
        object.insert(QStringLiteral("idcode_ir"), hexU32(targetConfig_.idcodeIr));
        object.insert(QStringLiteral("dtmcs_ir"), hexU32(targetConfig_.dtmcsIr));
        object.insert(QStringLiteral("dmi_ir"), hexU32(targetConfig_.dmiIr));
        object.insert(QStringLiteral("dtmcs"), hexU32(dtmcs_));
        object.insert(QStringLiteral("dmi_address_bits"), dmiAddressBits_);
        object.insert(QStringLiteral("dmi_idle_cycles"), dmiIdleCycles_);
        object.insert(QStringLiteral("sba_read_poll_interval"), targetConfig_.sbaReadPollInterval);
        object.insert(QStringLiteral("sba_write_poll_interval"), targetConfig_.sbaWritePollInterval);
        object.insert(QStringLiteral("sba_write_pipeline_words"), targetConfig_.sbaWritePipelineWords);
        object.insert(QStringLiteral("dmstatus"), hexU32(lastDmStatus_));
        object.insert(QStringLiteral("sbcs"), hexU32(lastSbCs_));
        object.insert(QStringLiteral("last_dmi_error"), lastDmiError_);
        object.insert(QStringLiteral("jtag_idcode_valid"), hasIdCode_);
        object.insert(QStringLiteral("jtag_idcode"), hasIdCode_ ? hexU32(idCode_) : QString());
        object.insert(QStringLiteral("jtag_idcode_error"), idcodeFailure_);
        object.insert(QStringLiteral("last_exchange"), device_.lastExchange());
        object.insert(QStringLiteral("exchange_count"), QString::number(device_.exchangeCount()));
        object.insert(QStringLiteral("address"), hexAddress(request.address));
        object.insert(QStringLiteral("entry_address"), hexAddress(request.entryAddress));
        object.insert(QStringLiteral("length"), QString::number(request.length));
        object.insert(QStringLiteral("segment_count"), request.segments.size());
        object.insert(QStringLiteral("segment_total_length"), QString::number(segmentTotalLength));
        if (!lastCsrSnapshot_.isEmpty()) {
            object.insert(QStringLiteral("csr_snapshot"), lastCsrSnapshot_);
        }
        return object;
    }

    bool prepareDebugModule(QString* const errorMessage)
    {
        return initializeDebugModule(errorMessage);
    }

    bool readMemory(
        const quint64 address,
        const quint64 length,
        const QString& progressPath,
        const quint64 progressBaseBytes,
        const quint64 progressTotalBytes,
        QByteArray* const data,
        QString* const errorMessage)
    {
        if (data == nullptr || length == 0U || length > static_cast<quint64>(std::numeric_limits<int>::max())) {
            setError(errorMessage, QStringLiteral("Memory read request is invalid."));
            return false;
        }
        if (!initializeDebugModule(errorMessage)) {
            return false;
        }
        if (!ensureHalted(errorMessage)) {
            return false;
        }
        if (!ensureSystemBusAccess(errorMessage)) {
            return false;
        }

        QByteArray output;
        output.reserve(static_cast<int>(length));
        quint64 offset = 0U;
        quint64 nextProgressBytes = 1024U;
        const quint64 totalProgressBytes = (progressTotalBytes == 0U) ? length : progressTotalBytes;
        (void)writeProgressObject(progressPath, progressBaseBytes, totalProgressBytes, QStringLiteral("read_start"));
        while (offset < length) {
            const quint64 currentAddress = address + offset;
            const int directBytes = bestDirectAccessBytes(currentAddress, length - offset);
            if (directBytes > 0) {
                if (!configureSystemBusAccess(directBytes, true, true, true, errorMessage)) {
                    return false;
                }
                if (!writeSystemBusAddress(currentAddress, errorMessage)) {
                    return false;
                }
                int readsSincePoll = 0;
                while (offset < length) {
                    const quint64 burstAddress = address + offset;
                    if ((burstAddress % static_cast<quint64>(directBytes)) != 0U ||
                        (length - offset) < static_cast<quint64>(directBytes)) {
                        break;
                    }

                    quint64 value = 0U;
                    ++readsSincePoll;
                    const bool shouldPoll = readsSincePoll >= targetConfig_.sbaReadPollInterval;
                    if (!readSystemBusDataValue(directBytes, shouldPoll, &value, errorMessage)) {
                        return false;
                    }
                    if (shouldPoll) {
                        readsSincePoll = 0;
                    }
                    appendLeValue(&output, value, directBytes);
                    offset += static_cast<quint64>(directBytes);
                    if (offset >= nextProgressBytes || offset == length) {
                        (void)writeProgressObject(progressPath, progressBaseBytes + offset, totalProgressBytes, QStringLiteral("read_progress"));
                        nextProgressBytes = offset + 1024U;
                    }
                }
                if (readsSincePoll > 0 && !waitSystemBusReady(errorMessage)) {
                    return false;
                }
            } else if (!readOneByteByWiderAccess(currentAddress, &output, errorMessage)) {
                return false;
            } else {
                ++offset;
                if (offset >= nextProgressBytes || offset == length) {
                    (void)writeProgressObject(progressPath, progressBaseBytes + offset, totalProgressBytes, QStringLiteral("read_progress"));
                    nextProgressBytes = offset + 1024U;
                }
            }
        }

        (void)writeProgressObject(progressPath, progressBaseBytes + length, totalProgressBytes, QStringLiteral("read_done"));
        *data = output;
        return true;
    }

    bool writeMemory(
        const quint64 address,
        const QByteArray& data,
        const QString& progressPath,
        const quint64 progressBaseBytes,
        const quint64 progressTotalBytes,
        QString* const errorMessage)
    {
        if (data.isEmpty()) {
            setError(errorMessage, QStringLiteral("Memory write data is empty."));
            return false;
        }
        if (!initializeDebugModule(errorMessage)) {
            return false;
        }
        if (!ensureHalted(errorMessage)) {
            return false;
        }
        if (!ensureSystemBusAccess(errorMessage)) {
            return false;
        }

        quint64 offset = 0U;
        quint64 nextProgressBytes = 1024U;
        const quint64 totalBytes = static_cast<quint64>(data.size());
        const quint64 totalProgressBytes = (progressTotalBytes == 0U) ? totalBytes : progressTotalBytes;
        (void)writeProgressObject(progressPath, progressBaseBytes, totalProgressBytes, QStringLiteral("write_start"));
        while (offset < static_cast<quint64>(data.size())) {
            const quint64 currentAddress = address + offset;
            const quint64 remaining = static_cast<quint64>(data.size()) - offset;
            const int directBytes = bestDirectAccessBytes(currentAddress, remaining);
            if (directBytes > 0) {
                if (!configureSystemBusAccess(directBytes, false, false, true, errorMessage)) {
                    return false;
                }
                if (!writeSystemBusAddress(currentAddress, errorMessage)) {
                    return false;
                }
                int writesSincePoll = 0;
                while (offset < static_cast<quint64>(data.size())) {
                    const quint64 burstAddress = address + offset;
                    const quint64 burstRemaining = static_cast<quint64>(data.size()) - offset;
                    if ((burstAddress % static_cast<quint64>(directBytes)) != 0U ||
                        burstRemaining < static_cast<quint64>(directBytes)) {
                        break;
                    }

                    const bool canPipelinePair =
                        (directBytes == 4) &&
                        (targetConfig_.sbaWritePipelineWords >= 2) &&
                        (burstRemaining >= 8U) &&
                        ((writesSincePoll + 2) <= targetConfig_.sbaWritePollInterval);
                    if (canPipelinePair) {
                        const quint32 firstValue = static_cast<quint32>(
                            leValueFromBytes(data, static_cast<int>(offset), directBytes) & 0xFFFFFFFFULL);
                        const quint32 secondValue = static_cast<quint32>(
                            leValueFromBytes(data, static_cast<int>(offset + 4U), directBytes) & 0xFFFFFFFFULL);
                        if (!writeSystemBusDataPair32(firstValue, secondValue, errorMessage)) {
                            return false;
                        }
                        writesSincePoll += 2;
                        offset += 8U;
                        if (writesSincePoll >= targetConfig_.sbaWritePollInterval) {
                            if (!waitSystemBusReady(errorMessage)) {
                                return false;
                            }
                            writesSincePoll = 0;
                        }
                        if (offset >= nextProgressBytes || offset == totalBytes) {
                            (void)writeProgressObject(progressPath, progressBaseBytes + offset, totalProgressBytes, QStringLiteral("write_progress"));
                            nextProgressBytes = offset + 1024U;
                        }
                        continue;
                    }

                    const quint64 value = leValueFromBytes(data, static_cast<int>(offset), directBytes);
                    ++writesSincePoll;
                    const bool shouldPoll = writesSincePoll >= targetConfig_.sbaWritePollInterval;
                    if (!writeSystemBusDataValue(directBytes, value, shouldPoll, errorMessage)) {
                        return false;
                    }
                    if (shouldPoll) {
                        writesSincePoll = 0;
                    }
                    offset += static_cast<quint64>(directBytes);
                    if (offset >= nextProgressBytes || offset == totalBytes) {
                        (void)writeProgressObject(progressPath, progressBaseBytes + offset, totalProgressBytes, QStringLiteral("write_progress"));
                        nextProgressBytes = offset + 1024U;
                    }
                }
                if (writesSincePoll > 0 && !waitSystemBusReady(errorMessage)) {
                    return false;
                }
            } else if (!writeOneByteByWiderAccess(
                           currentAddress,
                           static_cast<unsigned char>(data.at(static_cast<int>(offset))),
                           errorMessage)) {
                return false;
            } else {
                ++offset;
                if (offset >= nextProgressBytes || offset == totalBytes) {
                    (void)writeProgressObject(progressPath, progressBaseBytes + offset, totalProgressBytes, QStringLiteral("write_progress"));
                    nextProgressBytes = offset + 1024U;
                }
            }
        }
        (void)writeProgressObject(progressPath, progressBaseBytes + totalBytes, totalProgressBytes, QStringLiteral("write_done"));
        return true;
    }

    bool runTarget(const quint64 entryAddress, QString* const errorMessage)
    {
        if (!initializeDebugModule(errorMessage)) {
            return false;
        }
        if (!ensureHalted(errorMessage)) {
            return false;
        }
        if (!writeDpc(entryAddress, errorMessage)) {
            return false;
        }
        return resumeHart(errorMessage);
    }

    bool haltTarget(QString* const errorMessage)
    {
        if (!initializeDebugModule(errorMessage)) {
            return false;
        }
        if (!haltHart(errorMessage)) {
            return false;
        }
        captureCsrSnapshot();
        return true;
    }

private:
    struct CaptureSpec final {
        bool capture = false;
        int bitCount = 0;
    };

    void appendJtagSequence(
        QByteArray* const request,
        const int bitCount,
        const bool tms,
        const bool capture,
        const QByteArray& tdiBits,
        std::vector<CaptureSpec>* const captures) const
    {
        if (request == nullptr || bitCount <= 0 || bitCount > 64) {
            return;
        }

        unsigned char info = (bitCount == 64)
            ? 0U
            : static_cast<unsigned char>(bitCount);
        if (tms) {
            info = static_cast<unsigned char>(info | 0x40U);
        }
        if (capture) {
            info = static_cast<unsigned char>(info | 0x80U);
        }
        request->append(static_cast<char>(info));

        const int byteCount = (bitCount + 7) / 8;
        QByteArray padded = tdiBits.left(byteCount);
        while (padded.size() < byteCount) {
            padded.append('\0');
        }
        request->append(padded);

        if (captures != nullptr) {
            CaptureSpec spec;
            spec.capture = capture;
            spec.bitCount = bitCount;
            captures->push_back(spec);
        }
    }

    void appendJtagRunIdle(
        QByteArray* const body,
        std::vector<CaptureSpec>* const captures,
        const int cycles) const
    {
        if (body == nullptr || captures == nullptr) {
            return;
        }

        int remaining = cycles;
        while (remaining > 0) {
            const int chunk = qMin(remaining, 64);
            appendJtagSequence(body, chunk, false, false, QByteArray((chunk + 7) / 8, '\0'), captures);
            remaining -= chunk;
        }
    }

    void appendDrScan(
        QByteArray* const body,
        std::vector<CaptureSpec>* const captures,
        const QByteArray& tdiBits,
        const int bitCount) const
    {
        if (body == nullptr || captures == nullptr || bitCount <= 0 || bitCount > 96) {
            return;
        }

        appendJtagSequence(body, 1, true, false, QByteArray(1, '\0'), captures);
        appendJtagSequence(body, 2, false, false, QByteArray(1, '\0'), captures);

        int consumedBits = 0;
        int leadingBits = bitCount - 1;
        while (leadingBits > 0) {
            const int chunkBits = qMin(leadingBits, 64);
            appendJtagSequence(
                body,
                chunkBits,
                false,
                true,
                packedBitRange(tdiBits, consumedBits, chunkBits),
                captures);
            consumedBits += chunkBits;
            leadingBits -= chunkBits;
        }
        appendJtagSequence(body, 1, true, true, packedBitRange(tdiBits, consumedBits, 1), captures);
        appendJtagSequence(body, 1, true, false, QByteArray(1, '\0'), captures);
        appendJtagSequence(body, 1, false, false, QByteArray(1, '\0'), captures);
    }

    bool sendJtagSequences(
        const QByteArray& sequenceBody,
        const std::vector<CaptureSpec>& captures,
        QByteArray* const capturedBits,
        QString* const errorMessage)
    {
        if (captures.empty()) {
            setError(errorMessage, QStringLiteral("JTAG sequence is empty."));
            return false;
        }

        QByteArray request;
        request.append(static_cast<char>(kDapJtagSequence));
        request.append(static_cast<char>(captures.size()));
        request.append(sequenceBody);
        if (request.size() > kCmsisDapPayloadBytes) {
            setError(errorMessage, QStringLiteral("JTAG sequence exceeds CMSIS-DAP packet size."));
            return false;
        }

        QByteArray response;
        if (!device_.command(request, &response, errorMessage)) {
            return false;
        }
        if (response.size() < 2 || byteAt(response, 1) != kDapOk) {
            setError(
                errorMessage,
                QStringLiteral("JTAG sequence failed: %1").arg(QString::fromLatin1(response.toHex())));
            return false;
        }

        QByteArray output;
        int cursor = 2;
        int outputBit = 0;
        for (const CaptureSpec& spec : captures) {
            if (!spec.capture) {
                continue;
            }

            const int byteCount = (spec.bitCount + 7) / 8;
            if ((cursor + byteCount) > response.size()) {
                setError(
                    errorMessage,
                    QStringLiteral("JTAG captured data is incomplete: %1")
                        .arg(QString::fromLatin1(response.toHex())));
                return false;
            }

            const QByteArray captured = response.mid(cursor, byteCount);
            for (int bit = 0; bit < spec.bitCount; ++bit) {
                setPackedBit(&output, outputBit, packedBitAt(captured, bit));
                ++outputBit;
            }
            cursor += byteCount;
        }

        if (capturedBits != nullptr) {
            *capturedBits = output;
        }
        return true;
    }

    bool jtagRunIdle(const int cycles, QString* const errorMessage)
    {
        QByteArray body;
        std::vector<CaptureSpec> captures;
        appendJtagRunIdle(&body, &captures, cycles);
        if (captures.empty()) {
            return true;
        }
        return sendJtagSequences(body, captures, nullptr, errorMessage);
    }

    bool selectIr(const quint32 instruction, QString* const errorMessage)
    {
        QByteArray body;
        std::vector<CaptureSpec> captures;
        appendJtagSequence(&body, 2, true, false, QByteArray(1, '\0'), &captures);
        appendJtagSequence(&body, 2, false, false, QByteArray(1, '\0'), &captures);

        const int scanBits = targetConfig_.tapIrLength;
        const QByteArray irBits =
            packedBitsFromU64(static_cast<quint64>(instruction) & lowBitMask(scanBits), scanBits);
        if (scanBits > 1) {
            appendJtagSequence(
                &body,
                scanBits - 1,
                false,
                false,
                packedBitRange(irBits, 0, scanBits - 1),
                &captures);
        }
        appendJtagSequence(&body, 1, true, false, packedBitRange(irBits, scanBits - 1, 1), &captures);
        appendJtagSequence(&body, 1, true, false, QByteArray(1, '\0'), &captures);
        appendJtagSequence(&body, 1, false, false, QByteArray(1, '\0'), &captures);
        if (!sendJtagSequences(body, captures, nullptr, errorMessage)) {
            return false;
        }
        currentIr_ = instruction;
        return true;
    }

    bool scanDr(
        const QByteArray& tdiBits,
        const int bitCount,
        QByteArray* const capturedBits,
        QString* const errorMessage)
    {
        if (bitCount <= 0 || bitCount > 96) {
            setError(errorMessage, QStringLiteral("Invalid JTAG DR scan width."));
            return false;
        }

        QByteArray body;
        std::vector<CaptureSpec> captures;
        appendDrScan(&body, &captures, tdiBits, bitCount);
        return sendJtagSequences(body, captures, capturedBits, errorMessage);
    }

    QByteArray makeDmiBits(
        const quint32 address,
        const quint32 data,
        const quint32 op) const
    {
        QByteArray bits((dmiScanBits_ + 7) / 8, '\0');
        setPackedValue(&bits, static_cast<int>(kDmiOpOffset), 2, op & kDmiStatusMask);
        setPackedValue(&bits, static_cast<int>(kDmiDataOffset), 32, data);
        setPackedValue(
            &bits,
            static_cast<int>(kDmiAddressOffset),
            dmiAddressBits_,
            static_cast<quint64>(address) & lowBitMask(dmiAddressBits_));
        return bits;
    }

    DmiResponse parseDmiBits(const QByteArray& bits) const
    {
        DmiResponse response;
        response.opStatus = static_cast<quint32>(
            u64FromPackedBits(packedBitRange(bits, static_cast<int>(kDmiOpOffset), 2), 2));
        response.data = static_cast<quint32>(
            u64FromPackedBits(packedBitRange(bits, static_cast<int>(kDmiDataOffset), 32), 32));
        response.address = static_cast<quint32>(
            u64FromPackedBits(
                packedBitRange(bits, static_cast<int>(kDmiAddressOffset), dmiAddressBits_),
                dmiAddressBits_));
        return response;
    }

    QString dmiStatusText(const quint32 status) const
    {
        QString text = QStringLiteral("unknown");
        if (status == kDmiStatusSuccess) {
            text = QStringLiteral("success");
        } else if (status == kDmiStatusFailed) {
            text = QStringLiteral("failed");
        } else if (status == kDmiStatusBusy) {
            text = QStringLiteral("busy");
        }
        return text;
    }

    bool readDtmcs(QString* const errorMessage)
    {
        if (!selectIr(targetConfig_.dtmcsIr, errorMessage)) {
            return false;
        }

        QByteArray captured;
        if (!scanDr(packedBitsFromU64(0U, 32), 32, &captured, errorMessage)) {
            return false;
        }

        dtmcs_ = static_cast<quint32>(u64FromPackedBits(captured, 32));
        const quint32 version = (dtmcs_ >> kDtmcsVersionOffset) & 0x0FU;
        const int detectedAddressBits = static_cast<int>((dtmcs_ >> kDtmcsAbitsOffset) & 0x3FU);
        const quint32 dmiStatus = (dtmcs_ >> kDtmcsDmistatOffset) & kDmiStatusMask;
        dmiIdleCycles_ = qMax(1, static_cast<int>((dtmcs_ >> kDtmcsIdleOffset) & 0x07U));

        if (version == 0U || detectedAddressBits <= 0 || detectedAddressBits > 31) {
            setError(
                errorMessage,
                QStringLiteral("DTMCS is invalid: value=%1 version=%2 abits=%3")
                    .arg(hexU32(dtmcs_))
                    .arg(version)
                    .arg(detectedAddressBits));
            return false;
        }

        if (targetConfig_.dmiAddressBits > 0 && targetConfig_.dmiAddressBits != detectedAddressBits) {
            setError(
                errorMessage,
                QStringLiteral("DMI address width mismatch: config=%1 target=%2")
                    .arg(targetConfig_.dmiAddressBits)
                    .arg(detectedAddressBits));
            return false;
        }

        dmiAddressBits_ = (targetConfig_.dmiAddressBits > 0)
            ? targetConfig_.dmiAddressBits
            : detectedAddressBits;
        dmiScanBits_ = static_cast<int>(kDmiAddressOffset) + dmiAddressBits_;

        if (dmiStatus != kDmiStatusSuccess && !resetDmi(errorMessage)) {
            return false;
        }
        return true;
    }

    bool resetDmi(QString* const errorMessage)
    {
        if (!selectIr(targetConfig_.dtmcsIr, errorMessage)) {
            return false;
        }

        QByteArray captured;
        if (!scanDr(packedBitsFromU64(kDtmcsDmiReset, 32), 32, &captured, errorMessage)) {
            return false;
        }
        if (!jtagRunIdle(dmiIdleCycles_, errorMessage)) {
            return false;
        }
        lastDmiError_.clear();
        return true;
    }

    bool dmiScan(
        const quint32 address,
        const quint32 data,
        const quint32 op,
        DmiResponse* const response,
        QString* const errorMessage)
    {
        if (dmiAddressBits_ <= 0 || dmiScanBits_ <= 0) {
            setError(errorMessage, QStringLiteral("DMI scan width is not initialized."));
            return false;
        }
        if (response == nullptr) {
            setError(errorMessage, QStringLiteral("DMI response object is null."));
            return false;
        }

        if (currentIr_ != targetConfig_.dmiIr && !selectIr(targetConfig_.dmiIr, errorMessage)) {
            return false;
        }

        QByteArray captured;
        if (!scanDr(makeDmiBits(address, data, op), dmiScanBits_, &captured, errorMessage)) {
            return false;
        }
        *response = parseDmiBits(captured);
        return true;
    }

    bool dmiExchangeWithSelectedIr(
        const quint32 address,
        const quint32 data,
        const quint32 op,
        DmiResponse* const response,
        QString* const errorMessage)
    {
        if (response == nullptr) {
            setError(errorMessage, QStringLiteral("DMI response object is null."));
            return false;
        }

        for (int attempt = 0; attempt < targetConfig_.dmiRetryCount; ++attempt) {
            QByteArray body;
            std::vector<CaptureSpec> captures;
            appendDrScan(&body, &captures, makeDmiBits(address, data, op), dmiScanBits_);
            appendJtagRunIdle(&body, &captures, dmiIdleCycles_);
            appendDrScan(&body, &captures, makeDmiBits(0U, 0U, kDmiOpNop), dmiScanBits_);
            appendJtagRunIdle(&body, &captures, dmiIdleCycles_);

            QByteArray captured;
            if (!sendJtagSequences(body, captures, &captured, errorMessage)) {
                return false;
            }
            if ((captured.size() * 8) < (dmiScanBits_ * 2)) {
                setError(errorMessage, QStringLiteral("DMI batch response is incomplete."));
                return false;
            }

            const DmiResponse candidate =
                parseDmiBits(packedBitRange(captured, dmiScanBits_, dmiScanBits_));
            if (candidate.opStatus == kDmiStatusSuccess) {
                *response = candidate;
                lastDmiError_.clear();
                return true;
            }

            lastDmiError_ = QStringLiteral("DMI %1 at address %2 returned %3")
                .arg((op == kDmiOpRead) ? QStringLiteral("read") : QStringLiteral("write"))
                .arg(hexU32(address))
                .arg(dmiStatusText(candidate.opStatus));
            if (!resetDmi(errorMessage)) {
                return false;
            }
            if (!selectIr(targetConfig_.dmiIr, errorMessage)) {
                return false;
            }

            if (candidate.opStatus == kDmiStatusFailed) {
                setError(errorMessage, lastDmiError_);
                return false;
            }
        }

        setError(
            errorMessage,
            QStringLiteral("DMI access timed out at address %1 after %2 retries.")
                .arg(hexU32(address))
                .arg(targetConfig_.dmiRetryCount));
        return false;
    }

    bool dmiExchange(
        const quint32 address,
        const quint32 data,
        const quint32 op,
        DmiResponse* const response,
        QString* const errorMessage)
    {
        if (response == nullptr) {
            setError(errorMessage, QStringLiteral("DMI response object is null."));
            return false;
        }

        if (currentIr_ != targetConfig_.dmiIr && !selectIr(targetConfig_.dmiIr, errorMessage)) {
            return false;
        }
        return dmiExchangeWithSelectedIr(address, data, op, response, errorMessage);
    }

    bool dmiRead(
        const quint32 address,
        quint32* const data,
        QString* const errorMessage)
    {
        if (data == nullptr) {
            setError(errorMessage, QStringLiteral("DMI read output is null."));
            return false;
        }

        DmiResponse response;
        if (!dmiExchange(address, 0U, kDmiOpRead, &response, errorMessage)) {
            return false;
        }
        *data = response.data;
        return true;
    }

    bool dmiWrite(
        const quint32 address,
        const quint32 data,
        QString* const errorMessage)
    {
        DmiResponse response;
        return dmiExchange(address, data, kDmiOpWrite, &response, errorMessage);
    }

    bool dmiWritePair(
        const quint32 address,
        const quint32 firstData,
        const quint32 secondData,
        QString* const errorMessage)
    {
        if (dmiAddressBits_ <= 0 || dmiScanBits_ <= 0) {
            setError(errorMessage, QStringLiteral("DMI scan width is not initialized."));
            return false;
        }
        if (currentIr_ != targetConfig_.dmiIr && !selectIr(targetConfig_.dmiIr, errorMessage)) {
            return false;
        }

        QByteArray body;
        std::vector<CaptureSpec> captures;
        appendDrScan(&body, &captures, makeDmiBits(address, firstData, kDmiOpWrite), dmiScanBits_);
        appendJtagRunIdle(&body, &captures, dmiIdleCycles_);
        appendDrScan(&body, &captures, makeDmiBits(address, secondData, kDmiOpWrite), dmiScanBits_);
        appendJtagRunIdle(&body, &captures, dmiIdleCycles_);
        appendDrScan(&body, &captures, makeDmiBits(0U, 0U, kDmiOpNop), dmiScanBits_);
        appendJtagRunIdle(&body, &captures, dmiIdleCycles_);

        QByteArray captured;
        if (!sendJtagSequences(body, captures, &captured, errorMessage)) {
            return false;
        }
        if ((captured.size() * 8) < (dmiScanBits_ * 3)) {
            setError(errorMessage, QStringLiteral("DMI pipelined response is incomplete."));
            return false;
        }

        const DmiResponse firstResponse =
            parseDmiBits(packedBitRange(captured, dmiScanBits_, dmiScanBits_));
        const DmiResponse secondResponse =
            parseDmiBits(packedBitRange(captured, dmiScanBits_ * 2, dmiScanBits_));
        if (firstResponse.opStatus == kDmiStatusSuccess && secondResponse.opStatus == kDmiStatusSuccess) {
            lastDmiError_.clear();
            return true;
        }

        lastDmiError_ = QStringLiteral("DMI pipelined write returned %1/%2")
            .arg(dmiStatusText(firstResponse.opStatus), dmiStatusText(secondResponse.opStatus));
        (void)resetDmi(errorMessage);
        setError(errorMessage, lastDmiError_);
        return false;
    }

    quint32 baseDmControl() const
    {
        return kDmControlDmActive & ~kDmControlHartSelMask;
    }

    bool initializeDebugModule(QString* const errorMessage)
    {
        if (debugModuleReady_) {
            return true;
        }
        QString dmiError;
        if (!readDtmcs(&dmiError) || !resetDmi(&dmiError) ||
            !dmiWrite(kDmControl, baseDmControl(), &dmiError) ||
            !dmiRead(kDmStatus, &lastDmStatus_, &dmiError)) {
            lastDmiError_ = dmiError;
            setError(errorMessage, QStringLiteral("[DMI_NO_RESPONSE] DMI 初始化无响应: %1").arg(dmiError));
            return false;
        }

        if ((lastDmStatus_ & kDmStatusAuthenticated) == 0U) {
            setError(errorMessage, QStringLiteral("Debug module is not authenticated: %1").arg(hexU32(lastDmStatus_)));
            return false;
        }
        if ((lastDmStatus_ & kDmStatusAllNonexistent) != 0U) {
            setError(errorMessage, QStringLiteral("Selected hart does not exist: %1").arg(hexU32(lastDmStatus_)));
            return false;
        }
        if ((lastDmStatus_ & kDmStatusAnyHaveReset) != 0U) {
            if (!dmiWrite(kDmControl, baseDmControl() | kDmControlAckHaveReset, errorMessage)) {
                return false;
            }
            if (!dmiWrite(kDmControl, baseDmControl(), errorMessage)) {
                return false;
            }
        }

        debugModuleReady_ = true;
        return true;
    }

    bool pollDmStatusFor(
        const quint32 requiredMask,
        const QString& action,
        QString* const errorMessage)
    {
        for (int poll = 0; poll < targetConfig_.dmPollCount; ++poll) {
            if (!dmiRead(kDmStatus, &lastDmStatus_, errorMessage)) {
                return false;
            }
            if ((lastDmStatus_ & requiredMask) != 0U) {
                return true;
            }
            if ((lastDmStatus_ & (kDmStatusAllUnavailable | kDmStatusAllNonexistent)) != 0U) {
                setError(errorMessage, QStringLiteral("%1 failed, hart unavailable: %2").arg(action, hexU32(lastDmStatus_)));
                return false;
            }
        }

        setError(errorMessage, QStringLiteral("%1 timed out: dmstatus=%2").arg(action, hexU32(lastDmStatus_)));
        return false;
    }

    bool haltHart(QString* const errorMessage)
    {
        if (!dmiWrite(kDmControl, baseDmControl() | kDmControlHaltReq, errorMessage)) {
            return false;
        }
        if (!pollDmStatusFor(kDmStatusAllHalted | kDmStatusAnyHalted, QStringLiteral("halt"), errorMessage)) {
            return false;
        }
        return dmiWrite(kDmControl, baseDmControl(), errorMessage);
    }

    bool ensureHalted(QString* const errorMessage)
    {
        if (!dmiRead(kDmStatus, &lastDmStatus_, errorMessage)) {
            return false;
        }
        if ((lastDmStatus_ & (kDmStatusAllHalted | kDmStatusAnyHalted)) != 0U) {
            return true;
        }
        return haltHart(errorMessage);
    }

    bool resumeHart(QString* const errorMessage)
    {
        if (!dmiWrite(kDmControl, baseDmControl() | kDmControlResumeReq, errorMessage)) {
            return false;
        }
        if (!pollDmStatusFor(
                kDmStatusAllResumeAck | kDmStatusAnyResumeAck | kDmStatusAllRunning | kDmStatusAnyRunning,
                QStringLiteral("resume"),
                errorMessage)) {
            return false;
        }
        return dmiWrite(kDmControl, baseDmControl(), errorMessage);
    }

    bool clearAbstractCommandError(QString* const errorMessage)
    {
        quint32 abstractCs = 0U;
        if (!dmiRead(kDmAbstractCs, &abstractCs, errorMessage)) {
            return false;
        }
        if ((abstractCs & kAbstractCsCommandErrorMask) == 0U) {
            return true;
        }
        return dmiWrite(kDmAbstractCs, kAbstractCsCommandErrorClear, errorMessage);
    }

    bool waitAbstractCommandReady(QString* const errorMessage)
    {
        for (int poll = 0; poll < targetConfig_.dmPollCount; ++poll) {
            quint32 abstractCs = 0U;
            if (!dmiRead(kDmAbstractCs, &abstractCs, errorMessage)) {
                return false;
            }
            if ((abstractCs & kAbstractCsBusy) != 0U) {
                continue;
            }
            if ((abstractCs & kAbstractCsCommandErrorMask) != 0U) {
                (void)dmiWrite(kDmAbstractCs, kAbstractCsCommandErrorClear, errorMessage);
                setError(errorMessage, QStringLiteral("Abstract command failed: abstractcs=%1").arg(hexU32(abstractCs)));
                return false;
            }
            return true;
        }

        setError(errorMessage, QStringLiteral("Abstract command timed out."));
        return false;
    }

    bool writeDpc(const quint64 entryAddress, QString* const errorMessage)
    {
        if (!clearAbstractCommandError(errorMessage)) {
            return false;
        }
        if (!waitAbstractCommandReady(errorMessage)) {
            return false;
        }

        if (targetConfig_.xlen == 32 && entryAddress > std::numeric_limits<quint32>::max()) {
            setError(errorMessage, QStringLiteral("Entry address exceeds RV32 range: %1").arg(hexAddress(entryAddress)));
            return false;
        }

        if (!dmiWrite(kDmData0, static_cast<quint32>(entryAddress & 0xFFFFFFFFULL), errorMessage)) {
            return false;
        }
        quint32 command = kAbstractCommandAccessRegister |
            kAbstractCommandTransfer |
            kAbstractCommandWrite |
            kRiscvCsrDpc;
        if (targetConfig_.xlen == 64) {
            if (!dmiWrite(kDmData1, static_cast<quint32>((entryAddress >> 32U) & 0xFFFFFFFFULL), errorMessage)) {
                return false;
            }
            command |= kAbstractCommandAarSize64;
        } else {
            command |= kAbstractCommandAarSize32;
        }

        if (!dmiWrite(kDmCommand, command, errorMessage)) {
            return false;
        }
        return waitAbstractCommandReady(errorMessage);
    }

    bool readCsr(const quint32 csr, quint64* const value, QString* const errorMessage)
    {
        if (value == nullptr) {
            setError(errorMessage, QStringLiteral("CSR read output is null."));
            return false;
        }
        if (!clearAbstractCommandError(errorMessage)) {
            return false;
        }
        if (!waitAbstractCommandReady(errorMessage)) {
            return false;
        }

        quint32 command = kAbstractCommandAccessRegister | kAbstractCommandTransfer | csr;
        if (targetConfig_.xlen == 64) {
            command |= kAbstractCommandAarSize64;
        } else {
            command |= kAbstractCommandAarSize32;
        }

        if (!dmiWrite(kDmCommand, command, errorMessage)) {
            return false;
        }
        if (!waitAbstractCommandReady(errorMessage)) {
            return false;
        }

        quint32 low = 0U;
        if (!dmiRead(kDmData0, &low, errorMessage)) {
            return false;
        }
        quint64 parsed = low;
        if (targetConfig_.xlen == 64) {
            quint32 high = 0U;
            if (!dmiRead(kDmData1, &high, errorMessage)) {
                return false;
            }
            parsed |= (static_cast<quint64>(high) << 32U);
        }

        *value = parsed;
        return true;
    }

    void captureCsrSnapshot()
    {
        lastCsrSnapshot_ = QJsonObject();
        const struct {
            const char* name;
            quint32 csr;
        } items[] = {
            {"dpc", kRiscvCsrDpc},
            {"mepc", kRiscvCsrMepc},
            {"mcause", kRiscvCsrMcause},
            {"mtval", kRiscvCsrMtval},
        };

        for (const auto& item : items) {
            quint64 value = 0U;
            QString error;
            if (readCsr(item.csr, &value, &error)) {
                lastCsrSnapshot_.insert(QString::fromLatin1(item.name), hexAddress(value));
            } else {
                lastCsrSnapshot_.insert(
                    QStringLiteral("%1_error").arg(QString::fromLatin1(item.name)),
                    error);
            }
        }
    }

    quint32 supportedFlagForBytes(const int byteCount) const
    {
        quint32 flag = 0U;
        if (byteCount == 1) {
            flag = kSbCsAccess8;
        } else if (byteCount == 2) {
            flag = kSbCsAccess16;
        } else if (byteCount == 4) {
            flag = kSbCsAccess32;
        } else if (byteCount == 8) {
            flag = kSbCsAccess64;
        }
        return flag;
    }

    quint32 accessCodeForBytes(const int byteCount) const
    {
        quint32 code = 0U;
        if (byteCount == 2) {
            code = 1U;
        } else if (byteCount == 4) {
            code = 2U;
        } else if (byteCount == 8) {
            code = 3U;
        }
        return code;
    }

    bool waitSystemBusReady(QString* const errorMessage)
    {
        for (int poll = 0; poll < targetConfig_.dmPollCount; ++poll) {
            if (!dmiRead(kDmSbCs, &lastSbCs_, errorMessage)) {
                return false;
            }
            if ((lastSbCs_ & kSbCsBusy) != 0U) {
                continue;
            }
            if ((lastSbCs_ & kSbCsBusyError) != 0U) {
                (void)dmiWrite(kDmSbCs, kSbCsBusyError | kSbCsErrorMask, errorMessage);
                setError(errorMessage, QStringLiteral("System bus access busy error: sbcs=%1").arg(hexU32(lastSbCs_)));
                return false;
            }
            if ((lastSbCs_ & kSbCsErrorMask) != 0U) {
                (void)dmiWrite(kDmSbCs, kSbCsBusyError | kSbCsErrorMask, errorMessage);
                setError(errorMessage, QStringLiteral("System bus access error: sbcs=%1").arg(hexU32(lastSbCs_)));
                return false;
            }
            return true;
        }

        setError(errorMessage, QStringLiteral("System bus access timed out: sbcs=%1").arg(hexU32(lastSbCs_)));
        return false;
    }

    bool ensureSystemBusAccess(QString* const errorMessage)
    {
        if (!dmiWrite(kDmSbCs, kSbCsBusyError | kSbCsErrorMask, errorMessage)) {
            return false;
        }
        if (!dmiRead(kDmSbCs, &lastSbCs_, errorMessage)) {
            return false;
        }
        if ((lastSbCs_ & kSbCsAddressSizeMask) == 0U) {
            setError(errorMessage, QStringLiteral("System bus access is not implemented: sbcs=%1").arg(hexU32(lastSbCs_)));
            return false;
        }
        if ((lastSbCs_ & supportedFlagForBytes(targetConfig_.systemBusAccessBytes)) == 0U) {
            setError(
                errorMessage,
                QStringLiteral("Configured SBA width %1 bytes is unsupported: sbcs=%2")
                    .arg(targetConfig_.systemBusAccessBytes)
                    .arg(hexU32(lastSbCs_)));
            return false;
        }
        return waitSystemBusReady(errorMessage);
    }

    bool configureSystemBusAccess(
        const int byteCount,
        const bool readOnAddress,
        const bool readOnData,
        const bool autoIncrement,
        QString* const errorMessage)
    {
        if ((lastSbCs_ & supportedFlagForBytes(byteCount)) == 0U) {
            setError(errorMessage, QStringLiteral("SBA width is unsupported: %1 bytes.").arg(byteCount));
            return false;
        }

        quint32 value = (accessCodeForBytes(byteCount) << kSbCsAccessOffset) |
            kSbCsBusyError |
            kSbCsErrorMask;
        if (readOnAddress) {
            value |= kSbCsReadOnAddress;
        }
        if (readOnData) {
            value |= kSbCsReadOnData;
        }
        if (autoIncrement) {
            value |= kSbCsAutoIncrement;
        }

        if (!dmiWrite(kDmSbCs, value, errorMessage)) {
            return false;
        }
        return waitSystemBusReady(errorMessage);
    }

    bool writeSystemBusAddress(const quint64 address, QString* const errorMessage)
    {
        if (targetConfig_.xlen == 64 || address > std::numeric_limits<quint32>::max()) {
            if (!dmiWrite(kDmSbAddress1, static_cast<quint32>((address >> 32U) & 0xFFFFFFFFULL), errorMessage)) {
                return false;
            }
        }
        return dmiWrite(kDmSbAddress0, static_cast<quint32>(address & 0xFFFFFFFFULL), errorMessage);
    }

    int bestDirectAccessBytes(
        const quint64 address,
        const quint64 remainingBytes) const
    {
        const int widths[] = {8, 4, 2, 1};
        for (const int width : widths) {
            if (width > targetConfig_.systemBusAccessBytes ||
                static_cast<quint64>(width) > remainingBytes ||
                (address % static_cast<quint64>(width)) != 0U ||
                (lastSbCs_ & supportedFlagForBytes(width)) == 0U) {
                continue;
            }
            return width;
        }
        return 0;
    }

    int widerAccessBytesForByte(const quint64 address) const
    {
        const int widths[] = {8, 4, 2};
        for (const int width : widths) {
            if (width > targetConfig_.systemBusAccessBytes ||
                (lastSbCs_ & supportedFlagForBytes(width)) == 0U) {
                continue;
            }
            const quint64 baseAddress = address & ~static_cast<quint64>(width - 1);
            if (baseAddress <= address) {
                return width;
            }
        }
        return 0;
    }

    bool systemBusReadValue(
        const quint64 address,
        const int byteCount,
        quint64* const value,
        QString* const errorMessage)
    {
        if (value == nullptr) {
            setError(errorMessage, QStringLiteral("SBA read output is null."));
            return false;
        }
        if (!configureSystemBusAccess(byteCount, true, false, false, errorMessage)) {
            return false;
        }
        if (!writeSystemBusAddress(address, errorMessage)) {
            return false;
        }
        if (!waitSystemBusReady(errorMessage)) {
            return false;
        }

        quint32 low = 0U;
        if (!dmiRead(kDmSbData0, &low, errorMessage)) {
            return false;
        }
        quint64 combined = low;
        if (byteCount == 8) {
            quint32 high = 0U;
            if (!dmiRead(kDmSbData1, &high, errorMessage)) {
                return false;
            }
            combined |= static_cast<quint64>(high) << 32U;
        }
        *value = combined & lowBitMask(byteCount * 8);
        return waitSystemBusReady(errorMessage);
    }

    bool readSystemBusDataValue(
        const int byteCount,
        const bool waitAfterRead,
        quint64* const value,
        QString* const errorMessage)
    {
        if (value == nullptr) {
            setError(errorMessage, QStringLiteral("SBA data read output is null."));
            return false;
        }

        quint32 low = 0U;
        if (!dmiRead(kDmSbData0, &low, errorMessage)) {
            return false;
        }
        quint64 combined = low;
        if (byteCount == 8) {
            quint32 high = 0U;
            if (!dmiRead(kDmSbData1, &high, errorMessage)) {
                return false;
            }
            combined |= static_cast<quint64>(high) << 32U;
        }
        *value = combined & lowBitMask(byteCount * 8);
        if (!waitAfterRead) {
            return true;
        }
        return waitSystemBusReady(errorMessage);
    }

    bool systemBusWriteValue(
        const quint64 address,
        const int byteCount,
        const quint64 value,
        QString* const errorMessage)
    {
        if (!configureSystemBusAccess(byteCount, false, false, false, errorMessage)) {
            return false;
        }
        if (!writeSystemBusAddress(address, errorMessage)) {
            return false;
        }
        if (byteCount == 8) {
            if (!dmiWrite(kDmSbData1, static_cast<quint32>((value >> 32U) & 0xFFFFFFFFULL), errorMessage)) {
                return false;
            }
        }
        if (!dmiWrite(kDmSbData0, static_cast<quint32>(value & 0xFFFFFFFFULL), errorMessage)) {
            return false;
        }
        return waitSystemBusReady(errorMessage);
    }

    bool writeSystemBusDataValue(
        const int byteCount,
        const quint64 value,
        const bool waitAfterWrite,
        QString* const errorMessage)
    {
        if (byteCount == 8) {
            if (!dmiWrite(kDmSbData1, static_cast<quint32>((value >> 32U) & 0xFFFFFFFFULL), errorMessage)) {
                return false;
            }
        }
        if (!dmiWrite(kDmSbData0, static_cast<quint32>(value & 0xFFFFFFFFULL), errorMessage)) {
            return false;
        }
        if (!waitAfterWrite) {
            return true;
        }
        return waitSystemBusReady(errorMessage);
    }

    bool writeSystemBusDataPair32(
        const quint32 firstValue,
        const quint32 secondValue,
        QString* const errorMessage)
    {
        return dmiWritePair(kDmSbData0, firstValue, secondValue, errorMessage);
    }

    bool readOneByteByWiderAccess(
        const quint64 address,
        QByteArray* const output,
        QString* const errorMessage)
    {
        if (output == nullptr) {
            setError(errorMessage, QStringLiteral("Memory read output is null."));
            return false;
        }
        const int byteCount = widerAccessBytesForByte(address);
        if (byteCount <= 0) {
            setError(errorMessage, QStringLiteral("SBA cannot read unaligned byte at %1.").arg(hexAddress(address)));
            return false;
        }

        const quint64 baseAddress = address & ~static_cast<quint64>(byteCount - 1);
        quint64 value = 0U;
        if (!systemBusReadValue(baseAddress, byteCount, &value, errorMessage)) {
            return false;
        }
        const int byteOffset = static_cast<int>(address - baseAddress);
        const quint64 byteValue = (value >> static_cast<unsigned int>(byteOffset * 8)) & 0xFFULL;
        output->append(static_cast<char>(byteValue));
        return true;
    }

    bool writeOneByteByWiderAccess(
        const quint64 address,
        const unsigned char byteValue,
        QString* const errorMessage)
    {
        const int byteCount = widerAccessBytesForByte(address);
        if (byteCount <= 0) {
            setError(errorMessage, QStringLiteral("SBA cannot write unaligned byte at %1.").arg(hexAddress(address)));
            return false;
        }

        const quint64 baseAddress = address & ~static_cast<quint64>(byteCount - 1);
        quint64 value = 0U;
        if (!systemBusReadValue(baseAddress, byteCount, &value, errorMessage)) {
            return false;
        }

        const int byteOffset = static_cast<int>(address - baseAddress);
        const quint64 mask = 0xFFULL << static_cast<unsigned int>(byteOffset * 8);
        const quint64 shiftedValue = static_cast<quint64>(byteValue) << static_cast<unsigned int>(byteOffset * 8);
        const quint64 merged = (value & ~mask) | shiftedValue;
        return systemBusWriteValue(baseAddress, byteCount, merged, errorMessage);
    }

    bool connectJtag(QString* const errorMessage)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapConnect));
        request.append(static_cast<char>(kDapPortJtag));

        QByteArray response;
        if (!device_.command(request, &response, errorMessage)) {
            return false;
        }
        if (response.size() < 2 || byteAt(response, 1) != kDapPortJtag) {
            setError(
                errorMessage,
                QStringLiteral("CMSIS-DAP 未进入 JTAG 模式: %1")
                    .arg(QString::fromLatin1(response.toHex())));
            return false;
        }
        return true;
    }

    bool setClock(const int adapterSpeedKhz, QString* const errorMessage)
    {
        const int speedKhz = (adapterSpeedKhz > 0) ? adapterSpeedKhz : 100;
        QByteArray request;
        request.append(static_cast<char>(kDapSwjClock));
        appendLe32(&request, static_cast<quint32>(speedKhz * 1000));
        return device_.expectStatusOk(request, QStringLiteral("设置 JTAG 时钟"), errorMessage);
    }

    bool configureTap(QString* const errorMessage)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapJtagConfigure));
        request.append(static_cast<char>(1));
        request.append(static_cast<char>(targetConfig_.tapIrLength));
        return device_.expectStatusOk(request, QStringLiteral("配置 JTAG TAP"), errorMessage);
    }

    bool tapReset(QString* const errorMessage)
    {
        QByteArray request;
        request.append(static_cast<char>(kDapSwjSequence));
        request.append(static_cast<char>(8));
        request.append(static_cast<char>(0x1FU));
        return device_.expectStatusOk(request, QStringLiteral("复位 JTAG TAP"), errorMessage);
    }

    bool readIdCodeByDrScan(quint32* const idCode, QString* const errorMessage)
    {
        if (idCode == nullptr) {
            setError(errorMessage, QStringLiteral("IDCODE 输出对象为空。"));
            return false;
        }

        QByteArray request;
        request.append(static_cast<char>(kDapJtagSequence));
        request.append(static_cast<char>(8));

        // TLR reset: TMS=1 for at least five clocks.
        request.append(static_cast<char>(0x40U | 6U));
        request.append(static_cast<char>(0x3FU));
        // Move to Run-Test/Idle.
        request.append(static_cast<char>(1U));
        request.append(static_cast<char>(0x00U));
        // Idle -> Select-DR.
        request.append(static_cast<char>(0x40U | 1U));
        request.append(static_cast<char>(0x00U));
        // Select-DR -> Capture-DR -> Shift-DR.
        request.append(static_cast<char>(2U));
        request.append(static_cast<char>(0x00U));
        // Capture the first 31 IDCODE bits with TMS held low.
        request.append(static_cast<char>(0x80U | 31U));
        request.append(QByteArray(4, '\0'));
        // Capture the last IDCODE bit and leave Shift-DR.
        request.append(static_cast<char>(0x80U | 0x40U | 1U));
        request.append(static_cast<char>(0x00U));
        // Exit1-DR -> Update-DR -> Run-Test/Idle.
        request.append(static_cast<char>(0x40U | 1U));
        request.append(static_cast<char>(0x00U));
        request.append(static_cast<char>(1U));
        request.append(static_cast<char>(0x00U));

        QByteArray response;
        if (!device_.command(request, &response, errorMessage)) {
            return false;
        }
        if (response.size() < 6 || byteAt(response, 1) != kDapOk) {
            setError(
                errorMessage,
                QStringLiteral("JTAG DR scan 读取 IDCODE 失败: %1").arg(QString::fromLatin1(response.toHex())));
            return false;
        }

        quint32 value = 0U;
        if (response.size() >= 7) {
            int bitOffset = 0;
            appendBitsToU32(response.mid(2, 4), 31, &bitOffset, &value);
            appendBitsToU32(response.mid(6, 1), 1, &bitOffset, &value);
        } else {
            value = static_cast<quint32>(byteAt(response, 2)) |
                (static_cast<quint32>(byteAt(response, 3)) << 8U) |
                (static_cast<quint32>(byteAt(response, 4)) << 16U) |
                (static_cast<quint32>(byteAt(response, 5)) << 24U);
        }

        *idCode = value;
        return true;
    }

    bool readIdCode(quint32* const idCode, QString* const errorMessage)
    {
        if (idCode == nullptr) {
            setError(errorMessage, QStringLiteral("IDCODE 输出对象为空。"));
            return false;
        }

        QByteArray request;
        request.append(static_cast<char>(kDapJtagIdCode));
        request.append(static_cast<char>(0));

        QByteArray response;
        if (!device_.command(request, &response, errorMessage)) {
            return false;
        }
        if (response.size() < 6 || byteAt(response, 1) != kDapOk) {
            setError(
                errorMessage,
                QStringLiteral("读取 JTAG IDCODE 失败: %1").arg(QString::fromLatin1(response.toHex())));
            return false;
        }

        *idCode = static_cast<quint32>(byteAt(response, 2)) |
            (static_cast<quint32>(byteAt(response, 3)) << 8U) |
            (static_cast<quint32>(byteAt(response, 4)) << 16U) |
            (static_cast<quint32>(byteAt(response, 5)) << 24U);
        return true;
    }

    CmsisDapDevice device_;
    TargetDebugConfig targetConfig_;
    QString vendorText_;
    QString productText_;
    QString serialText_;
    QString firmwareText_;
    bool hasIdCode_ = false;
    quint32 idCode_ = 0U;
    QString idcodeFailure_;
    bool initialized_ = false;
    bool debugModuleReady_ = false;
    quint32 dtmcs_ = 0U;
    quint32 currentIr_ = std::numeric_limits<quint32>::max();
    int dmiAddressBits_ = 0;
    int dmiScanBits_ = 0;
    int dmiIdleCycles_ = 1;
    quint32 lastDmStatus_ = 0U;
    quint32 lastSbCs_ = 0U;
    QString lastDmiError_;
    QJsonObject lastCsrSnapshot_;
};

bool parseRequest(
    const QJsonObject& object,
    DebugServiceRequest* const request,
    QString* const errorCode,
    QString* const errorMessage)
{
    if (request == nullptr) {
        setError(errorCode, QStringLiteral("INTERNAL_ERROR"));
        setError(errorMessage, QStringLiteral("请求输出对象为空。"));
        return false;
    }

    if (object.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kRequestSchema)) {
        setError(errorCode, QStringLiteral("INVALID_SCHEMA"));
        setError(errorMessage, QStringLiteral("请求 schema 不匹配。"));
        return false;
    }

    DebugServiceRequest parsed;
    parsed.requestId = object.value(QStringLiteral("request_id")).toString().trimmed();
    if (parsed.requestId.isEmpty()) {
        setError(errorCode, QStringLiteral("MISSING_REQUEST_ID"));
        setError(errorMessage, QStringLiteral("请求缺少 request_id。"));
        return false;
    }

    parsed.operationText = object.value(QStringLiteral("operation")).toString().trimmed().toLower();
    parsed.operation = parseOperation(parsed.operationText);
    if (parsed.operation == DebugOperation::Unknown) {
        setError(errorCode, QStringLiteral("UNKNOWN_OPERATION"));
        setError(errorMessage, QStringLiteral("未知调试操作: %1").arg(parsed.operationText));
        return false;
    }

    parsed.responsePath = object.value(QStringLiteral("response_path")).toString().trimmed();
    if (parsed.responsePath.isEmpty()) {
        setError(errorCode, QStringLiteral("MISSING_RESPONSE_PATH"));
        setError(errorMessage, QStringLiteral("请求缺少 response_path。"));
        return false;
    }

    parsed.profile = object.value(QStringLiteral("profile")).toObject();
    if (parsed.operation != DebugOperation::Disconnect && parsed.profile.isEmpty()) {
        setError(errorCode, QStringLiteral("MISSING_PROFILE"));
        setError(errorMessage, QStringLiteral("请求缺少 profile。"));
        return false;
    }

    parsed.interfaceConfigPath = object.value(QStringLiteral("interface_config_path")).toString().trimmed();
    parsed.targetConfigPath = object.value(QStringLiteral("target_config_path")).toString().trimmed();
    parsed.adapterSpeedKhz = object.value(QStringLiteral("adapter_speed_khz")).toInt(0);
    parsed.dataPath = object.value(QStringLiteral("data_path")).toString().trimmed();
    parsed.progressPath = object.value(QStringLiteral("progress_path")).toString().trimmed();
    parsed.strategy = object.value(QStringLiteral("strategy")).toString().trimmed();
    const QJsonArray segments = object.value(QStringLiteral("segments")).toArray();
    for (const QJsonValue& segmentValue : segments) {
        const QJsonObject segmentObject = segmentValue.toObject();
        MemorySegmentRequest segment;
        if (!parseJsonU64(segmentObject.value(QStringLiteral("address")), &segment.address) ||
            !parseJsonU64(segmentObject.value(QStringLiteral("length")), &segment.length) ||
            segment.length == 0U) {
            setError(errorCode, QStringLiteral("INVALID_SEGMENT"));
            setError(errorMessage, QStringLiteral("Segment request requires valid address and length."));
            return false;
        }
        segment.dataPath = segmentObject.value(QStringLiteral("data_path")).toString().trimmed();
        if (parsed.operation == DebugOperation::Write && segment.dataPath.isEmpty()) {
            setError(errorCode, QStringLiteral("INVALID_SEGMENT_DATA"));
            setError(errorMessage, QStringLiteral("Write segment request requires data_path."));
            return false;
        }
        parsed.segments.append(segment);
    }

    if ((parsed.operation == DebugOperation::Read || parsed.operation == DebugOperation::Write) &&
        parsed.segments.isEmpty()) {
        if (!parseJsonU64(object.value(QStringLiteral("address")), &parsed.address)) {
            setError(errorCode, QStringLiteral("INVALID_ADDRESS"));
            setError(errorMessage, QStringLiteral("Read/write request requires a valid address."));
            return false;
        }
        if (!parseJsonU64(object.value(QStringLiteral("length")), &parsed.length) || parsed.length == 0U) {
            setError(errorCode, QStringLiteral("INVALID_LENGTH"));
            setError(errorMessage, QStringLiteral("Read/write request requires a valid length."));
            return false;
        }
        if (addWillOverflow(parsed.address, parsed.length)) {
            setError(errorCode, QStringLiteral("INVALID_RANGE"));
            setError(errorMessage, QStringLiteral("Read/write address range overflows."));
            return false;
        }
    }

    if (parsed.operation == DebugOperation::Write && parsed.segments.isEmpty()) {
        const QFileInfo dataFile(parsed.dataPath);
        if (parsed.dataPath.isEmpty() || !dataFile.exists() || !dataFile.isFile()) {
            setError(errorCode, QStringLiteral("INVALID_DATA_PATH"));
            setError(errorMessage, QStringLiteral("Write request requires a valid data_path."));
            return false;
        }
        if (static_cast<quint64>(dataFile.size()) != parsed.length) {
            setError(errorCode, QStringLiteral("DATA_LENGTH_MISMATCH"));
            setError(errorMessage, QStringLiteral("Write data length does not match request length."));
            return false;
        }
    }
    if (parsed.operation == DebugOperation::Write && !parsed.segments.isEmpty()) {
        for (const MemorySegmentRequest& segment : parsed.segments) {
            const QFileInfo dataFile(segment.dataPath);
            if (segment.dataPath.isEmpty() || !dataFile.exists() || !dataFile.isFile()) {
                setError(errorCode, QStringLiteral("INVALID_SEGMENT_DATA_PATH"));
                setError(errorMessage, QStringLiteral("Write segment request requires a valid data_path."));
                return false;
            }
            if (static_cast<quint64>(dataFile.size()) != segment.length) {
                setError(errorCode, QStringLiteral("SEGMENT_DATA_LENGTH_MISMATCH"));
                setError(errorMessage, QStringLiteral("Write segment data length does not match request length."));
                return false;
            }
        }
    }

    if (parsed.operation == DebugOperation::Run) {
        if (!parseJsonU64(object.value(QStringLiteral("entry_address")), &parsed.entryAddress)) {
            setError(errorCode, QStringLiteral("INVALID_ENTRY_ADDRESS"));
            setError(errorMessage, QStringLiteral("Run request requires a valid entry_address."));
            return false;
        }
    }

    QString configError;
    if (!validateConfigFiles(parsed, &configError)) {
        setError(errorCode, QStringLiteral("INVALID_CONFIG"));
        setError(errorMessage, configError);
        return false;
    }

    *request = parsed;
    return true;
}

class BoardDebugTransport {
public:
    virtual ~BoardDebugTransport() = default;

    virtual TransportResult connectTarget(const DebugServiceRequest& request) = 0;
    virtual TransportResult disconnectTarget(const DebugServiceRequest& request) = 0;
    virtual TransportResult status(const DebugServiceRequest& request) = 0;
    virtual TransportResult readMemory(const DebugServiceRequest& request) = 0;
    virtual TransportResult writeMemory(const DebugServiceRequest& request) = 0;
    virtual TransportResult resetTarget(const DebugServiceRequest& request) = 0;
    virtual TransportResult runTarget(const DebugServiceRequest& request) = 0;
    virtual TransportResult haltTarget(const DebugServiceRequest& request) = 0;
};

class SelfDevelopedBoardTransport final : public BoardDebugTransport {
public:
    TransportResult connectTarget(const DebugServiceRequest& request) override
    {
        if (session_ != nullptr) {
            QString error;
            if (!session_->initialize(request, &error)) {
                TransportResult result = hardwareFailure(QStringLiteral("CONNECT_FAILED"), error, request);
                result.snapshot = session_->snapshot(request, QStringLiteral("unknown"));
                session_.reset();
                return result;
            }

            TransportResult result;
            result.success = true;
            result.errorCode = QStringLiteral("OK");
            result.snapshot = session_->snapshot(request, QStringLiteral("connected"));
            return result;
        }

        auto nextSession = std::make_unique<CmsisDapJtagSession>();
        QString error;
        if (!nextSession->initialize(request, &error)) {
            TransportResult result = hardwareFailure(QStringLiteral("CONNECT_FAILED"), error, request);
            result.snapshot = nextSession->snapshot(request, QStringLiteral("unknown"));
            return result;
        }
        session_ = std::move(nextSession);

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session_->snapshot(request, QStringLiteral("connected"));
        return result;
    }

    TransportResult disconnectTarget(const DebugServiceRequest& request) override
    {
        if (session_ != nullptr) {
            QString disconnectError;
            (void)session_->disconnect(&disconnectError);
            session_.reset();
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot.insert(QStringLiteral("target_state"), QStringLiteral("disconnected"));
        result.snapshot.insert(QStringLiteral("operation"), operationToText(request.operation));
        result.snapshot.insert(QStringLiteral("transport"), QStringLiteral("cmsis_dap_hid_jtag"));
        result.snapshot.insert(QStringLiteral("note"), QStringLiteral("disconnect handled as idempotent success"));
        return result;
    }

    TransportResult status(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("STATUS_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session->snapshot(request, QStringLiteral("connected"));
        return result;
    }

    TransportResult readMemory(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("MEMORY_READ_CONNECT_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }

        QByteArray data;
        if (!request.segments.isEmpty()) {
            quint64 progressBase = 0U;
            quint64 progressTotal = 0U;
            for (const MemorySegmentRequest& segment : request.segments) {
                progressTotal += segment.length;
            }
            for (const MemorySegmentRequest& segment : request.segments) {
                QByteArray segmentData;
                if (!session->readMemory(
                        segment.address,
                        segment.length,
                        request.progressPath,
                        progressBase,
                        progressTotal,
                        &segmentData,
                        &error)) {
                    TransportResult result = hardwareFailure(QStringLiteral("MEMORY_READ_FAILED"), error, request);
                    result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                    session_.reset();
                    return result;
                }
                data.append(segmentData);
                progressBase += segment.length;
            }
        } else if (!session->readMemory(request.address, request.length, request.progressPath, 0U, request.length, &data, &error)) {
            TransportResult result = hardwareFailure(QStringLiteral("MEMORY_READ_FAILED"), error, request);
            result.snapshot = session->snapshot(request, QStringLiteral("connected"));
            session_.reset();
            return result;
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.data = data;
        result.snapshot = session->snapshot(request, QStringLiteral("halted"));
        return result;
    }

    TransportResult writeMemory(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("MEMORY_WRITE_CONNECT_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }

        if (!request.segments.isEmpty()) {
            quint64 progressBase = 0U;
            quint64 progressTotal = 0U;
            for (const MemorySegmentRequest& segment : request.segments) {
                progressTotal += segment.length;
            }
            for (const MemorySegmentRequest& segment : request.segments) {
                QByteArray payload;
                if (!readBinaryFile(segment.dataPath, &payload, &error)) {
                    return hardwareFailure(QStringLiteral("MEMORY_WRITE_DATA_READ_FAILED"), error, request);
                }
                if (!session->writeMemory(
                        segment.address,
                        payload,
                        request.progressPath,
                        progressBase,
                        progressTotal,
                        &error)) {
                    TransportResult result = hardwareFailure(QStringLiteral("MEMORY_WRITE_FAILED"), error, request);
                    result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                    session_.reset();
                    return result;
                }
                progressBase += static_cast<quint64>(payload.size());
            }
        } else {
            QByteArray payload;
            if (!readBinaryFile(request.dataPath, &payload, &error)) {
                return hardwareFailure(QStringLiteral("MEMORY_WRITE_DATA_READ_FAILED"), error, request);
            }
            if (!session->writeMemory(request.address, payload, request.progressPath, 0U, static_cast<quint64>(payload.size()), &error)) {
                TransportResult result = hardwareFailure(QStringLiteral("MEMORY_WRITE_FAILED"), error, request);
                result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                session_.reset();
                return result;
            }
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session->snapshot(request, QStringLiteral("halted"));
        return result;
    }

    TransportResult resetTarget(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("RESET_CONNECT_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }
        const QString resetStrategy = request.strategy.isEmpty()
            ? request.profile.value(QStringLiteral("reset_strategy")).toString().trimmed().toLower()
            : request.strategy.trimmed().toLower();
        if (resetStrategy == QStringLiteral("reset halt")) {
            if (!session->setHaltOnReset(&error)) {
                TransportResult result = hardwareFailure(QStringLiteral("RESET_HALT_REQUEST_FAILED"), error, request);
                result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                session_.reset();
                return result;
            }
            if (!session->resetTarget(&error)) {
                TransportResult result = hardwareFailure(QStringLiteral("RESET_FAILED"), error, request);
                result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                session_.reset();
                return result;
            }
            if (!session->haltTarget(&error)) {
                TransportResult result = hardwareFailure(QStringLiteral("RESET_HALT_FAILED"), error, request);
                result.snapshot = session->snapshot(request, QStringLiteral("connected"));
                session_.reset();
                return result;
            }

            TransportResult result;
            result.success = true;
            result.errorCode = QStringLiteral("OK");
            result.snapshot = session->snapshot(request, QStringLiteral("halted"));
            return result;
        }

        if (!session->resetTarget(&error)) {
            TransportResult result = hardwareFailure(QStringLiteral("RESET_FAILED"), error, request);
            result.snapshot = session->snapshot(request, QStringLiteral("connected"));
            session_.reset();
            return result;
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session->snapshot(request, QStringLiteral("reset"));
        return result;
    }

    TransportResult runTarget(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("RUN_CONNECT_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }
        if (!session->runTarget(request.entryAddress, &error)) {
            TransportResult result = hardwareFailure(QStringLiteral("RUN_FAILED"), error, request);
            result.snapshot = session->snapshot(request, QStringLiteral("connected"));
            session_.reset();
            return result;
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session->snapshot(request, QStringLiteral("running"));
        return result;
    }

    TransportResult haltTarget(const DebugServiceRequest& request) override
    {
        QString error;
        CmsisDapJtagSession* const session = ensureSession(request, &error);
        if (session == nullptr) {
            TransportResult result = hardwareFailure(QStringLiteral("HALT_CONNECT_FAILED"), error, request);
            result.snapshot = snapshotForFailure(request);
            session_.reset();
            return result;
        }
        if (!session->haltTarget(&error)) {
            TransportResult result = hardwareFailure(QStringLiteral("HALT_FAILED"), error, request);
            result.snapshot = session->snapshot(request, QStringLiteral("connected"));
            session_.reset();
            return result;
        }

        TransportResult result;
        result.success = true;
        result.errorCode = QStringLiteral("OK");
        result.snapshot = session->snapshot(request, QStringLiteral("halted"));
        return result;
    }

private:
    CmsisDapJtagSession* ensureSession(
        const DebugServiceRequest& request,
        QString* const errorMessage)
    {
        if (session_ == nullptr) {
            auto nextSession = std::make_unique<CmsisDapJtagSession>();
            if (!nextSession->initialize(request, errorMessage)) {
                return nullptr;
            }
            session_ = std::move(nextSession);
        } else if (!session_->initialize(request, errorMessage)) {
            return nullptr;
        }
        return session_.get();
    }

    QJsonObject snapshotForFailure(const DebugServiceRequest& request) const
    {
        if (session_ != nullptr) {
            return session_->snapshot(request, QStringLiteral("unknown"));
        }
        QJsonObject object;
        object.insert(QStringLiteral("target_state"), QStringLiteral("unknown"));
        object.insert(QStringLiteral("operation"), operationToText(request.operation));
        object.insert(QStringLiteral("transport"), QStringLiteral("cmsis_dap_hid_jtag"));
        return object;
    }

    TransportResult hardwareFailure(
        const QString& code,
        const QString& message,
        const DebugServiceRequest& request) const
    {
        TransportResult result;
        result.success = false;
        result.errorCode = code;
        result.errorMessage = message;
        result.snapshot.insert(QStringLiteral("target_state"), QStringLiteral("unknown"));
        result.snapshot.insert(QStringLiteral("operation"), operationToText(request.operation));
        result.snapshot.insert(QStringLiteral("address"), hexAddress(request.address));
        result.snapshot.insert(QStringLiteral("entry_address"), hexAddress(request.entryAddress));
        result.snapshot.insert(QStringLiteral("length"), QString::number(request.length));
        result.snapshot.insert(QStringLiteral("transport"), QStringLiteral("cmsis_dap_hid_jtag"));
        return result;
    }

    std::unique_ptr<CmsisDapJtagSession> session_;
};

TransportResult dispatchRequest(
    BoardDebugTransport* const transport,
    const DebugServiceRequest& request)
{
    if (transport == nullptr) {
        return makeFailureResult(
            QStringLiteral("INTERNAL_ERROR"),
            QStringLiteral("调试传输层对象为空。"),
            request);
    }

    switch (request.operation) {
    case DebugOperation::Connect:
        return transport->connectTarget(request);
    case DebugOperation::Disconnect:
        return transport->disconnectTarget(request);
    case DebugOperation::Status:
        return transport->status(request);
    case DebugOperation::Read:
        return transport->readMemory(request);
    case DebugOperation::Write:
        return transport->writeMemory(request);
    case DebugOperation::Reset:
        return transport->resetTarget(request);
    case DebugOperation::Run:
        return transport->runTarget(request);
    case DebugOperation::Halt:
        return transport->haltTarget(request);
    case DebugOperation::Unknown:
    default:
        return makeFailureResult(
            QStringLiteral("UNKNOWN_OPERATION"),
            QStringLiteral("未知调试操作。"),
            request);
    }
}

QJsonObject makeParseFailureResponse(
    const QJsonObject& rawRequest,
    const QString& errorCode,
    const QString& errorMessage)
{
    DebugServiceRequest request;
    request.requestId = rawRequest.value(QStringLiteral("request_id")).toString();
    request.operationText = rawRequest.value(QStringLiteral("operation")).toString().trimmed().toLower();
    request.operation = parseOperation(request.operationText);
    request.responsePath = rawRequest.value(QStringLiteral("response_path")).toString();
    request.profile = rawRequest.value(QStringLiteral("profile")).toObject();
    TransportResult result = makeFailureResult(errorCode, errorMessage, request);
    return makeResponse(request, result);
}

bool processRequestFile(
    const QString& requestPath,
    BoardDebugTransport* const transport,
    QString* const requestId,
    QString* const errorMessage)
{
    QJsonObject rawRequest;
    QString error;
    if (!readJsonObject(requestPath, &rawRequest, &error)) {
        setError(errorMessage, error);
        return false;
    }

    DebugServiceRequest request;
    QString errorCode;
    QString parseError;
    QJsonObject response;
    if (!parseRequest(rawRequest, &request, &errorCode, &parseError)) {
        response = makeParseFailureResponse(rawRequest, errorCode, parseError);
        request.responsePath = rawRequest.value(QStringLiteral("response_path")).toString().trimmed();
    } else {
        response = makeResponse(request, dispatchRequest(transport, request));
    }

    if (requestId != nullptr) {
        *requestId = request.requestId;
    }

    const QString responsePath = request.responsePath.trimmed();
    if (responsePath.isEmpty()) {
        setError(errorMessage, QStringLiteral("请求缺少 response_path"));
        return false;
    }

    if (!writeJsonObject(responsePath, response, &error)) {
        setError(errorMessage, error);
        return false;
    }
    if (!response.value(QStringLiteral("success")).toBool(false)) {
        setError(errorMessage, response.value(QStringLiteral("error_message")).toString());
        return false;
    }
    return true;
}

int runServerLoop(BoardDebugTransport* const transport)
{
    QTextStream input(stdin);
    QTextStream output(stdout);
    output << QStringLiteral("READY") << Qt::endl;

    while (true) {
        const QString line = input.readLine().trimmed();
        if (line.isNull()) {
            break;
        }
        if (line == QStringLiteral("__quit__")) {
            output << QStringLiteral("BYE") << Qt::endl;
            break;
        }
        if (line.isEmpty()) {
            continue;
        }

        QString requestId;
        QString error;
        if (processRequestFile(line, transport, &requestId, &error)) {
            output << QStringLiteral("DONE ") << requestId << Qt::endl;
        } else {
            output << QStringLiteral("ERROR ") << error << Qt::endl;
        }
    }
    return 0;
}

int runLocalServerLoop(BoardDebugTransport* const transport)
{
    QLocalServer server;
    const QString serverName = QString::fromLatin1(kLocalServerName);
    if (!server.listen(serverName)) {
        QLocalServer::removeServer(serverName);
        if (!server.listen(serverName)) {
            QTextStream(stderr) << QStringLiteral("local server listen failed: ")
                                << server.errorString() << Qt::endl;
            return 2;
        }
    }

    while (server.waitForNewConnection(-1)) {
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        if (socket == nullptr) {
            continue;
        }
        if (!socket->waitForReadyRead(30000)) {
            socket->write("ERROR request timeout\n");
            socket->waitForBytesWritten(1000);
            continue;
        }

        const QString line = QString::fromUtf8(socket->readLine()).trimmed();
        if (line == QStringLiteral("__ping__")) {
            socket->write("PONG\n");
            socket->waitForBytesWritten(1000);
            continue;
        }
        if (line == QStringLiteral("__quit__")) {
            socket->write("BYE\n");
            socket->waitForBytesWritten(1000);
            break;
        }
        if (line.isEmpty()) {
            socket->write("ERROR empty request\n");
            socket->waitForBytesWritten(1000);
            continue;
        }

        QString requestId;
        QString error;
        if (processRequestFile(line, transport, &requestId, &error)) {
            socket->write(QStringLiteral("DONE %1\n").arg(requestId).toUtf8());
        } else {
            socket->write(QStringLiteral("ERROR %1\n").arg(error).toUtf8());
        }
        socket->waitForBytesWritten(1000);
    }
    return 0;
}

}  // namespace

int runDebugServiceMode(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lockstep_ui_preview"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(LOCKSTEP_APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lockstep 自研片上调试服务"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption requestOption(
        QStringLiteral("request"),
        QStringLiteral("调试服务请求 JSON 路径"),
        QStringLiteral("path"));
    const QCommandLineOption serverOption(
        QStringLiteral("server"),
        QStringLiteral("以常驻服务模式运行"));
    const QCommandLineOption localServerOption(
        QStringLiteral("local-server"),
        QStringLiteral("以本机常驻服务模式运行"));
    parser.addOption(requestOption);
    parser.addOption(serverOption);
    parser.addOption(localServerOption);
    parser.process(app);

    std::unique_ptr<BoardDebugTransport> transport = std::make_unique<SelfDevelopedBoardTransport>();
    if (parser.isSet(localServerOption)) {
        return runLocalServerLoop(transport.get());
    }
    if (parser.isSet(serverOption)) {
        return runServerLoop(transport.get());
    }

    if (!parser.isSet(requestOption)) {
        QTextStream(stderr) << QStringLiteral("必须提供 --request") << Qt::endl;
        return 2;
    }

    QString error;
    QString requestId;
    if (!processRequestFile(parser.value(requestOption), transport.get(), &requestId, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }
    return 0;
}
