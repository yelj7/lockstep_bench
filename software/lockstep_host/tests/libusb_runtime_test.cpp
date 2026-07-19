/**********************************************************
* 文件名: libusb_runtime_test.cpp
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增 FT601 libusb 传输与稳定身份重连回归。
* 描述: 通过可控 libusb 符号替身验证公开传输接口，无需连接真实硬件。
**********************************************************/

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <libusb.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "sampling_capture.h"

struct libusb_context {};
struct libusb_device {
    libusb_device_descriptor descriptor = {};
    quint8 bus = 0;
    quint8 address = 0;
    QByteArray ports;
    QByteArray serial;
    QByteArray product;
    bool hasControlInterface = true;
    bool hasCaptureInterface = true;
};
struct libusb_device_handle { libusb_device* device = nullptr; };

namespace {

libusb_device deviceA;
libusb_device deviceB;
libusb_device unrelatedDevice;
bool reverseOrder = false;
int claimedInterfaces = 0;
int releasedInterfaces = 0;
int lastClaimedInterface = -1;
QList<int> claimedInterfaceNumbers;
QByteArray lastWrite;
int lastWriteEndpoint = -1;
QByteArray claimedSerial;
enum class ReadMode { Data, Timeout, PartialTimeout } readMode = ReadMode::Data;
enum class StreamingInitMode { Success, ShortWrite, Error } streamingInitMode = StreamingInitMode::Success;

void initializeDevices()
{
    deviceA.descriptor.idVendor = 0x0403;
    deviceA.descriptor.idProduct = 0x601f;
    deviceA.descriptor.iSerialNumber = 1;
    deviceA.descriptor.iProduct = 2;
    deviceA.bus = 1;
    deviceA.address = 4;
    deviceA.ports = QByteArray::fromHex("0203");
    deviceA.serial = "FT601-A";
    deviceA.product = "FT601 FIFO";
    deviceB = deviceA;
    deviceB.address = 5;
    deviceB.ports = QByteArray::fromHex("0204");
    deviceB.serial = "FT601-B";
    unrelatedDevice.descriptor.idVendor = 0x1234;
    unrelatedDevice.descriptor.idProduct = 0x5678;
    claimedInterfaceNumbers.clear();
}

bool expect(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

}  // namespace

extern "C" {

int LIBUSB_CALL libusb_init(libusb_context** context)
{
    *context = new libusb_context;
    return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_exit(libusb_context* context) { delete context; }

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context*, libusb_device*** list)
{
    *list = new libusb_device*[4];
    (*list)[0] = reverseOrder ? &deviceB : &deviceA;
    (*list)[1] = &unrelatedDevice;
    (*list)[2] = reverseOrder ? &deviceA : &deviceB;
    (*list)[3] = nullptr;
    return 3;
}

void LIBUSB_CALL libusb_free_device_list(libusb_device** list, int) { delete[] list; }

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device* device,
                                             libusb_device_descriptor* descriptor)
{
    *descriptor = device->descriptor;
    return LIBUSB_SUCCESS;
}

quint8 LIBUSB_CALL libusb_get_bus_number(libusb_device* device) { return device->bus; }
quint8 LIBUSB_CALL libusb_get_device_address(libusb_device* device) { return device->address; }

int LIBUSB_CALL libusb_get_port_numbers(libusb_device* device, quint8* ports, int length)
{
    const int count = std::min(length, static_cast<int>(device->ports.size()));
    std::memcpy(ports, device->ports.constData(), static_cast<size_t>(count));
    return count;
}

int LIBUSB_CALL libusb_get_device_speed(libusb_device*) { return LIBUSB_SPEED_HIGH; }

int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device* device,
                                                     libusb_config_descriptor** config)
{
    static libusb_endpoint_descriptor controlEndpoints[2] = {};
    static libusb_endpoint_descriptor captureEndpoints[2] = {};
    static libusb_interface_descriptor alternateSettings[2] = {};
    static libusb_interface interfaces[2] = {};
    static libusb_config_descriptor descriptor = {};

    controlEndpoints[0].bEndpointAddress = device->hasControlInterface ? 0x01 : 0x03;
    controlEndpoints[0].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    controlEndpoints[1].bEndpointAddress = 0x81;
    controlEndpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_INTERRUPT;
    captureEndpoints[0].bEndpointAddress = 0x02;
    captureEndpoints[0].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
    captureEndpoints[1].bEndpointAddress = device->hasCaptureInterface ? 0x82 : 0x83;
    captureEndpoints[1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;

    alternateSettings[0].bInterfaceNumber = 0;
    alternateSettings[0].bAlternateSetting = 0;
    alternateSettings[0].bNumEndpoints = 2;
    alternateSettings[0].endpoint = controlEndpoints;
    alternateSettings[1].bInterfaceNumber = 1;
    alternateSettings[1].bAlternateSetting = 0;
    alternateSettings[1].bNumEndpoints = 2;
    alternateSettings[1].endpoint = captureEndpoints;
    interfaces[0].num_altsetting = 1;
    interfaces[0].altsetting = &alternateSettings[0];
    interfaces[1].num_altsetting = 1;
    interfaces[1].altsetting = &alternateSettings[1];
    descriptor.bNumInterfaces = 2;
    descriptor.interface = interfaces;
    *config = &descriptor;
    return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_free_config_descriptor(libusb_config_descriptor*) {}

int LIBUSB_CALL libusb_open(libusb_device* device, libusb_device_handle** handle)
{
    *handle = new libusb_device_handle{device};
    return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_close(libusb_device_handle* handle) { delete handle; }

int LIBUSB_CALL libusb_get_string_descriptor_ascii(libusb_device_handle* handle, quint8 index,
                                                    unsigned char* data, int length)
{
    const QByteArray value = index == handle->device->descriptor.iSerialNumber
        ? handle->device->serial : handle->device->product;
    const int count = std::min(length, static_cast<int>(value.size()));
    std::memcpy(data, value.constData(), static_cast<size_t>(count));
    return count;
}

int LIBUSB_CALL libusb_set_auto_detach_kernel_driver(libusb_device_handle*, int)
{
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_set_interface_alt_setting(libusb_device_handle*, int, int)
{
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_claim_interface(libusb_device_handle* handle, int interfaceNumber)
{
    ++claimedInterfaces;
    claimedSerial = handle->device->serial;
    lastClaimedInterface = interfaceNumber;
    claimedInterfaceNumbers.append(interfaceNumber);
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_release_interface(libusb_device_handle*, int)
{
    ++releasedInterfaces;
    return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle*, unsigned char endpoint,
                                     unsigned char* data, int length, int* transferred,
                                     unsigned int)
{
    if ((endpoint & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
        lastWrite = QByteArray(reinterpret_cast<const char*>(data), length);
        lastWriteEndpoint = endpoint;
        if (endpoint == 0x01 && streamingInitMode == StreamingInitMode::Error) {
            *transferred = 0;
            return LIBUSB_ERROR_IO;
        }
        if (endpoint == 0x01 && streamingInitMode == StreamingInitMode::ShortWrite) {
            *transferred = std::max(0, length - 1);
            return LIBUSB_SUCCESS;
        }
        *transferred = length;
        return LIBUSB_SUCCESS;
    }
    if (readMode == ReadMode::Timeout) {
        *transferred = 0;
        return LIBUSB_ERROR_TIMEOUT;
    }
    const QByteArray payload = readMode == ReadMode::PartialTimeout ? QByteArray("part") : QByteArray("data");
    *transferred = std::min(length, static_cast<int>(payload.size()));
    std::memcpy(data, payload.constData(), static_cast<size_t>(*transferred));
    return readMode == ReadMode::PartialTimeout ? LIBUSB_ERROR_TIMEOUT : LIBUSB_SUCCESS;
}

const char* LIBUSB_CALL libusb_error_name(int status)
{
    return status == LIBUSB_SUCCESS ? "LIBUSB_SUCCESS" : "LIBUSB_ERROR";
}

}  // extern "C"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    initializeDevices();
    lockstep::acquisition::LibusbRuntime runtime;
    QString error;
    const auto devices = runtime.initialize(&error) ? runtime.enumerate(&error)
                                                     : QList<lockstep::acquisition::LibusbDeviceInfo>();
    if (!expect(devices.size() == 2 && devices.first().serialNumber == QStringLiteral("FT601-A"),
                "enumeration filters FT601 and reads identity") ||
        !expect(devices.first().captureInterfaceAvailable && devices.first().interfaceNumber == 1 &&
                    devices.first().outEndpoint == 0x02 && devices.first().inEndpoint == 0x82 &&
                    devices.first().usbSpeed == QStringLiteral("high-speed"),
                "enumeration discovers the FT601 capture interface and endpoints") ||
        !expect(runtime.open(0, &error) && claimedInterfaces == 2 && claimedSerial == "FT601-A" &&
                    claimedInterfaceNumbers == QList<int>({0, 1}),
                "open claims the FT601 control and capture interfaces") ||
        !expect(lastWriteEndpoint == 0x01 &&
                    lastWrite == QByteArray::fromHex(
                        "0000000082020000000000400000000000000000"),
                "open sends the FT601 streaming-mode request on control endpoint 0x01")) return 1;

    int transferred = 0;
    if (!expect(runtime.writePipe(0x02, QByteArray("command"), &transferred, &error) &&
                    transferred == 7 && lastWriteEndpoint == 0x02 && lastWrite == "command",
                "bulk OUT transfers the complete command")) return 1;
    readMode = ReadMode::Data;
    if (!expect(runtime.readPipeDetailed(0x82, 64).data == "data", "bulk IN returns data")) return 1;
    readMode = ReadMode::Timeout;
    const auto timeout = runtime.readPipeDetailed(0x82, 64);
    if (!expect(timeout.pendingTimeout && !timeout.fatalError && timeout.data.isEmpty(),
                "zero-byte timeout remains pending")) return 1;
    readMode = ReadMode::PartialTimeout;
    const auto partial = runtime.readPipeDetailed(0x82, 64);
    if (!expect(partial.data == "part" && !partial.pendingTimeout && !partial.fatalError,
                "partial timeout preserves received data")) return 1;

    reverseOrder = true;
    if (!expect(runtime.reopen(&error) && claimedSerial == "FT601-A",
                "reopen follows stable serial when enumeration order changes") ||
        !expect(lastClaimedInterface == 1 && lastWriteEndpoint == 0x01,
                "reopen rediscovers both interfaces and restores streaming mode") ||
        !expect(releasedInterfaces == 2, "reopen releases both previous interfaces")) return 1;
    runtime.close();
    if (!expect(releasedInterfaces == 4, "close releases both reopened interfaces")) return 1;

    reverseOrder = false;
    deviceA.hasCaptureInterface = false;
    const auto unavailableDevices = runtime.enumerate(&error);
    if (!expect(!unavailableDevices.first().captureInterfaceAvailable,
                "enumeration reports a missing capture endpoint contract") ||
        !expect(!runtime.open(0, &error) && error.contains(QStringLiteral("0x02/0x82")),
                "open rejects a device without the capture endpoint contract")) return 1;

    deviceA.hasCaptureInterface = true;
    deviceA.hasControlInterface = false;
    error.clear();
    const auto missingControlDevices = runtime.enumerate(&error);
    if (!expect(!missingControlDevices.first().captureInterfaceAvailable,
                "enumeration reports a missing FT601 control endpoint") ||
        !expect(!runtime.open(0, &error) && error.contains(QStringLiteral("0x01")),
                "open rejects a device without the streaming control endpoint")) return 1;

    deviceA.hasControlInterface = true;
    streamingInitMode = StreamingInitMode::ShortWrite;
    error.clear();
    if (!expect(!runtime.open(0, &error) && error.contains(QStringLiteral("streaming")),
                "open rejects a short FT601 streaming-mode write")) return 1;
    streamingInitMode = StreamingInitMode::Error;
    error.clear();
    if (!expect(!runtime.open(0, &error) && error.contains(QStringLiteral("streaming")),
                "open reports an FT601 streaming-mode transfer error")) return 1;
    return 0;
}
