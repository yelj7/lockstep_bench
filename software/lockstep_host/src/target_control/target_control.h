/*****************************************************************************
*  @file      target_control.h
*  @brief     目标连接烧写回读运行控制模块接口
*  Details.   声明目标连接烧写回读运行控制模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_TARGET_CONTROL_TARGET_CONTROL_H_
#define LOCKSTEP_HOST_SRC_TARGET_CONTROL_TARGET_CONTROL_H_

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::target_control {

enum class ConnectionState : unsigned char {
    NotConnected = 0U,
    Connected = 1U,
    Failed = 2U
};

enum class PrecheckState : unsigned char {
    NotRun = 0U,
    Passed = 1U,
    Failed = 2U,
    Blocked = 3U
};

enum class ImageType : unsigned char {
    Unknown = 0U,
    Elf = 1U,
    Srec = 2U,
    Bin = 3U,
    IntelHex = 4U
};

enum class VerifyState : unsigned char {
    NotRun = 0U,
    Passed = 1U,
    Mismatch = 2U,
    Failed = 3U
};

enum class RunState : unsigned char {
    NotAllowed = 0U,
    Ready = 1U,
    Running = 2U,
    Halted = 3U,
    Failed = 4U
};

enum class ProgramOperation : unsigned char {
    Write = 0U,
    Readback = 1U,
    Run = 2U,
    Halt = 3U
};

enum class OperationStage : unsigned char {
    NotStarted = 0U,
    CheckDebugAccess = 1U,
    DetectImage = 2U,
    ParseWritePlan = 3U,
    WriteSegments = 4U,
    ConfirmWriteResult = 5U,
    PersistWriteRecord = 6U,
    CheckReadbackAccess = 7U,
    PrepareReadRanges = 8U,
    ReadSegments = 9U,
    CompareData = 10U,
    PersistVerifyRecord = 11U,
    CheckRunGate = 12U,
    DispatchRun = 13U,
    CaptureRunStatus = 14U,
    PersistRunRecord = 15U,
    CheckHaltAccess = 16U,
    DispatchHalt = 17U,
    CaptureHaltStatus = 18U,
    PersistHaltRecord = 19U,
    Completed = 20U,
    Failed = 21U
};

struct DebugProfile final {
    QString profileId;
    QString profileName;
    quint64 ramBaseAddress = 0U;
    quint64 defaultRunAddress = 0U;
    quint64 maxWritableAddress = 0U;
    QString resetStrategy;
};

struct DebugResult final {
    bool success = false;
    QString requestId;
    QString rawReturn;
    QString errorMessage;
    QByteArray data;
};

struct ConnectionRecord final {
    ConnectionState state = ConnectionState::NotConnected;
    QString profileId;
    QString rawReturn;
    QString errorMessage;
};

struct PrecheckRecord final {
    PrecheckState state = PrecheckState::NotRun;
    bool resetSupported = false;
    bool readSupported = false;
    bool writeSupported = false;
    bool runSupported = false;
    QString rawReturn;
    QString errorMessage;
};

struct ImageSegment final {
    quint64 address = 0U;
    QByteArray data;
};

struct ProgramImageInfo final {
    ImageType type = ImageType::Unknown;
    QString fileName;
    QString sha256;
    qint64 sizeBytes = 0;
    quint64 entryAddress = 0U;
    QList<ImageSegment> segments;
    QString errorMessage;
};

struct WriteRecord final {
    bool success = false;
    QString taskId;
    QList<ImageSegment> segments;
    QString rawReturn;
    QString errorMessage;
};

struct ReadbackVerifyRecord final {
    VerifyState state = VerifyState::NotRun;
    QString taskId;
    quint64 expectedLength = 0U;
    quint64 actualLength = 0U;
    quint64 diffCount = 0U;
    QString rawReturn;
    QString errorMessage;
};

struct RunControlRecord final {
    ProgramOperation operation = ProgramOperation::Run;
    RunState state = RunState::NotAllowed;
    QString taskId;
    quint64 entryAddress = 0U;
    QString rawReturn;
    QString snapshot;
    QString errorMessage;
};

struct OperationProgress final {
    ProgramOperation operation = ProgramOperation::Write;
    OperationStage stage = OperationStage::NotStarted;
    int percent = 0;
    QString message;
    bool canCancel = false;
};

struct ProgramGate final {
    bool connected = false;
    bool precheckPassed = false;
    bool programWritten = false;
    bool readbackPassed = false;
    bool runAllowed = false;
    QString reason;
};

QString toString(ImageType type);
QString toString(VerifyState state);
QString toString(RunState state);
QString toString(ProgramOperation operation);
QString toString(OperationStage stage);
OperationProgress makeOperationProgress(ProgramOperation operation, OperationStage stage);

class DebugAccess {
public:
    virtual ~DebugAccess() = default;

    virtual DebugResult connectTarget(const DebugProfile& profile) = 0;
    virtual DebugResult disconnectTarget() = 0;
    virtual DebugResult status() = 0;
    virtual DebugResult read(quint64 address, quint64 length) = 0;
    virtual DebugResult write(quint64 address, const QByteArray& data) = 0;
    virtual DebugResult reset(const QString& strategy) = 0;
    virtual DebugResult run(quint64 entryAddress) = 0;
    virtual DebugResult halt() = 0;
};

class InMemoryDebugAccess final : public DebugAccess {
public:
    explicit InMemoryDebugAccess(quint64 memorySizeBytes);

    DebugResult connectTarget(const DebugProfile& profile) override;
    DebugResult disconnectTarget() override;
    DebugResult status() override;
    DebugResult read(quint64 address, quint64 length) override;
    DebugResult write(quint64 address, const QByteArray& data) override;
    DebugResult reset(const QString& strategy) override;
    DebugResult run(quint64 entryAddress) override;
    DebugResult halt() override;

private:
    QByteArray memory_;
    bool connected_ = false;
    bool running_ = false;
    DebugProfile profile_;
};

class TargetConnectionService final {
public:
    ConnectionRecord connectTarget(DebugAccess& access, const DebugProfile& profile) const;
    ConnectionRecord disconnectTarget(DebugAccess& access) const;
    PrecheckRecord runPrecheck(DebugAccess& access, const DebugProfile& profile) const;
};

class ProgramController final {
public:
    ProgramImageInfo detectImage(const QString& imagePath, const DebugProfile& profile) const;
    WriteRecord programTarget(
        DebugAccess& access,
        const QString& taskId,
        const ProgramImageInfo& image) const;
    ReadbackVerifyRecord verifyReadback(
        DebugAccess& access,
        const QString& taskId,
        const ProgramImageInfo& image) const;
    RunControlRecord runTarget(
        DebugAccess& access,
        const QString& taskId,
        const ProgramImageInfo& image,
        const ReadbackVerifyRecord& verifyRecord) const;
    RunControlRecord haltTarget(
        DebugAccess& access,
        const QString& taskId) const;
    ProgramGate getProgramGate(
        const ConnectionRecord& connection,
        const PrecheckRecord& precheck,
        const WriteRecord& writeRecord,
        const ReadbackVerifyRecord& verifyRecord) const;
};

}  // namespace lockstep::target_control

#endif  // LOCKSTEP_HOST_SRC_TARGET_CONTROL_TARGET_CONTROL_H_
