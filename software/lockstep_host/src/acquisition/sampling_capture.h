/**********************************************************
* 文件名: sampling_capture.h
* 日期: 2026-07-14
* 版本: v2.1
* 更新记录: 将 FT601 传输迁移为 libusb。
* 描述: 声明帧编解码、采集会话、libusb 枚举和标量 VCD 产物导出。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_ACQUISITION_SAMPLING_CAPTURE_H_
#define LOCKSTEP_HOST_SRC_ACQUISITION_SAMPLING_CAPTURE_H_

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QSharedPointer>
#include <QString>

namespace lockstep::acquisition {

constexpr quint32 kCaptureFrameMagic = 0x3243534cU;
constexpr quint16 kCaptureProtocolVersion = 2U;
constexpr quint32 kCaptureFrameHeaderBytes = 32U;
constexpr quint32 kCaptureSampleWordBits = 1024U;
constexpr quint32 kCapturePhysicalChannels = 1024U;
constexpr quint32 kCaptureSampleBytes = 128U;

enum class CaptureFrameType : quint16 {
    HelloRequest = 0x0001,
    HelloResponse = 0x8001,
    ConfigCapture = 0x0002,
    ArmCapture = 0x0003,
    StopCapture = 0x0004,
    GetStatus = 0x0005,
    StatusResponse = 0x8005,
    CaptureMeta = 0x8100,
    SampleData = 0x8101,
    CaptureEnd = 0x8102,
    ErrorResponse = 0x80ff
};

struct CaptureFrameHeader final {
    quint32 magic = kCaptureFrameMagic;
    quint16 version = kCaptureProtocolVersion;
    CaptureFrameType type = CaptureFrameType::HelloRequest;
    quint32 headerLength = kCaptureFrameHeaderBytes;
    quint32 payloadLength = 0;
    quint32 sequence = 0;
    quint32 captureId = 0;
    quint32 flags = 0;
    quint32 crc32 = 0;
};

struct CaptureFrame final {
    CaptureFrameHeader header;
    QByteArray payload;
};

struct CaptureDecodeResult final {
    bool success = true;
    QString error;
    QList<CaptureFrame> frames;
};

struct CapturePipeReadResult final {
    QByteArray data;
    bool pendingTimeout = false;
    bool fatalError = false;
    int status = 0;
    quint8 pipeId = 0;
    int requestedBytes = 0;
    int transferredBytes = 0;
    QString error;
};

class CaptureFrameCodec final {
public:
    QByteArray encode(CaptureFrameType type, const QByteArray& payload, quint32 sequence,
                      quint32 captureId = 0, quint32 flags = 0) const;
    CaptureDecodeResult feed(const QByteArray& bytes);
    void reset();

private:
    QByteArray buffer_;
};

struct SamplingCaptureConfig final {
    quint32 sampleRateHz = 120'000'000U;
    quint32 sampleCount = 4096U;
    quint32 pretriggerCount = 2047U;
    quint32 posttriggerCount = 2049U;
    quint32 protocolGroupMask = 0x1ffU;
    quint32 inputInvertMask = 0U;
    quint32 triggerMask = 0U;
    quint32 triggerValue = 0U;
    quint32 triggerEdgeRise = 0U;
    quint32 triggerEdgeFall = 0U;
    quint32 mode = 0U;
    quint32 triggerTimeoutSamples = 1'200'000'000U;

    bool validate(QString* error) const;
    QByteArray toPayload() const;
};

struct CaptureStatusV2 final {
    quint32 state = 0;
    quint32 requestSequence = 0;
    quint32 captureId = 0;
    quint32 samplesSeen = 0;
    quint32 samplesUploaded = 0;
    quint32 deviceStatusFlags = 0;
    quint32 lastErrorCode = 0;
    quint32 commandState = 0;
    quint32 captureState = 0;
    quint32 captureFlags = 0;
    quint32 pretriggerSamples = 0;
    quint32 posttriggerSamples = 0;
    quint32 frameSourceState = 0;
    quint32 txGeneratorState = 0;
    quint32 ft601State = 0;
    quint32 txBytes = 0;
};

struct CaptureSessionResult final {
    bool success = false;
    QString phase;
    QString message;
    CaptureStatusV2 status;
    bool recoveryAttempted = false;
    bool recoverySucceeded = false;
};

struct SamplingCaptureRecord final {
    quint32 captureId = 0;
    quint32 sampleRateHz = 0;
    quint32 requestedSampleCount = 0;
    quint32 actualSampleCount = 0;
    quint32 windowStartIndex = 0;
    quint32 triggerIndex = 0xffffffffU;
    quint32 stopReason = 0;
    quint32 deviceStatusFlags = 0;
    QList<QByteArray> samples;
};

class SamplingCaptureAssembler final {
public:
    bool append(const CaptureFrame& frame, QString* error);
    bool complete() const;
    SamplingCaptureRecord record() const;

private:
    SamplingCaptureRecord record_;
    QByteArray sampleBytes_;
    quint32 expectedSequence_ = 0;
    quint32 payloadBytes_ = 0;
    bool hasMeta_ = false;
    bool hasEnd_ = false;
};

struct LibusbDeviceInfo final {
    quint32 index = 0;
    quint16 vendorId = 0;
    quint16 productId = 0;
    quint8 busNumber = 0;
    quint8 deviceAddress = 0;
    bool captureInterfaceAvailable = false;
    int interfaceNumber = -1;
    int alternateSetting = 0;
    quint8 outEndpoint = 0;
    quint8 inEndpoint = 0;
    QString usbSpeed;
    QString captureInterfaceError;
    QString serialNumber;
    QString description;
};

class CaptureTransport {
public:
    virtual ~CaptureTransport() = default;
    virtual bool writePipe(quint8 pipeId, const QByteArray& bytes, int* transferred, QString* error) = 0;
    virtual QByteArray readPipe(quint8 pipeId, int maximumBytes, QString* error) = 0;
    virtual CapturePipeReadResult readPipeDetailed(quint8 pipeId, int maximumBytes)
    {
        CapturePipeReadResult result;
        result.pipeId = pipeId;
        result.requestedBytes = maximumBytes;
        QString error;
        result.data = readPipe(pipeId, maximumBytes, &error);
        result.transferredBytes = result.data.size();
        result.error = error;
        result.fatalError = result.data.isEmpty() && !error.isEmpty();
        return result;
    }
    virtual bool reopen(QString* error)
    {
        if (error != nullptr) *error = QStringLiteral("传输不支持重新打开");
        return false;
    }
    virtual bool isOpen() const = 0;
};

class LibusbRuntime final : public CaptureTransport {
public:
    bool initialize(QString* error);
    QList<LibusbDeviceInfo> enumerate(QString* error) const;
    bool open(quint32 index, QString* error);
    bool writePipe(quint8 pipeId, const QByteArray& bytes, int* transferred, QString* error) override;
    QByteArray readPipe(quint8 pipeId, int maximumBytes, QString* error) override;
    CapturePipeReadResult readPipeDetailed(quint8 pipeId, int maximumBytes) override;
    bool reopen(QString* error) override;
    void close();
    bool isInitialized() const;
    bool isOpen() const override;

private:
    class Private;
    QSharedPointer<Private> d_;
};

bool exportScalarVcd(const SamplingCaptureRecord& capture, const QString& taskRootPath,
                     QString* vcdPath, QString* sidecarPath, QString* error);

class SamplingCaptureSession final {
public:
    bool configure(CaptureTransport* transport, const SamplingCaptureConfig& config,
                   QString* error) const;
    bool armAndWaitAccepted(CaptureTransport* transport, quint32 commandSequence,
                            quint32* captureId, QString* error) const;
    bool collect(CaptureTransport* transport, const QString& taskRootPath, int timeoutMs,
                 SamplingCaptureRecord* record, QString* error) const;
    bool queryStatus(CaptureTransport* transport, CaptureStatusV2* status, QString* error) const;
    bool stopAndRecover(CaptureTransport* transport, CaptureStatusV2* status, QString* error) const;
    CaptureSessionResult runDetailed(CaptureTransport* transport, const SamplingCaptureConfig& config,
                                     const QString& taskRootPath, int timeoutMs,
                                     SamplingCaptureRecord* record) const;
    bool run(CaptureTransport* transport, const SamplingCaptureConfig& config,
             const QString& taskRootPath, int timeoutMs, SamplingCaptureRecord* record,
             QString* error) const;
};

}  // namespace lockstep::acquisition

#endif  // LOCKSTEP_HOST_SRC_ACQUISITION_SAMPLING_CAPTURE_H_
