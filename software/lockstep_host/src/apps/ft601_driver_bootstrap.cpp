/**********************************************************
* 文件名: ft601_driver_bootstrap.cpp
* 日期: 2026-07-20
* 版本: 1.2
* 更新记录: 增加 SetupAPI 故障关闭、隐藏 Zadig 和特权结果安全回传。
* 描述: 定位随产品分发的助手，自动绑定专用 FT601 并执行 USB 自检。
**********************************************************/

#include "ft601_driver_bootstrap.h"

#include <cwchar>

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "sampling_capture.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <sddl.h>
#include <setupapi.h>
#endif

namespace lockstep::apps {
namespace {

constexpr auto kFt601Serial = "000000000001";

#if defined(Q_OS_WIN)
enum class NativeDriverState {
    NotConnected,
    Ready,
    BindingRequired,
    Error
};

QString registryStringValue(HKEY key, const wchar_t* name)
{
    wchar_t buffer[512]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return QString::fromWCharArray(buffer);
}

NativeDriverState inspectNativeFt601Driver(QString* detail)
{
    HDEVINFO devices = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        if (detail != nullptr) *detail = QStringLiteral("SetupAPI 无法枚举在线 USB 设备");
        return NativeDriverState::Error;
    }
    int matches = 0;
    bool ready = false;
    bool enumerationFailed = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(devices, index, &device)) {
            const DWORD enumerationError = GetLastError();
            if (enumerationError != ERROR_NO_MORE_ITEMS) {
                enumerationFailed = true;
                if (detail != nullptr) {
                    *detail = QStringLiteral("SetupAPI 设备枚举异常，错误码 %1").arg(enumerationError);
                }
            }
            break;
        }
        wchar_t hardwareIds[4096]{};
        if (!SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, nullptr,
                                               reinterpret_cast<PBYTE>(hardwareIds), sizeof(hardwareIds), nullptr)) {
            if (detail != nullptr) *detail = QStringLiteral("无法读取在线 USB 设备硬件 ID");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }
        bool hardwareMatch = false;
        for (const wchar_t* id = hardwareIds; *id != L'\0'; id += wcslen(id) + 1U) {
            if (_wcsicmp(id, L"USB\\VID_0403&PID_601F&MI_00") == 0) {
                hardwareMatch = true;
                break;
            }
        }
        if (!hardwareMatch) continue;
        DEVINST parent = 0;
        wchar_t parentId[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Parent(&parent, device.DevInst, 0) != CR_SUCCESS ||
            CM_Get_Device_IDW(parent, parentId, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
            if (detail != nullptr) *detail = QStringLiteral("无法读取 FT601 复合设备父节点");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }
        const QString parentInstance = QString::fromWCharArray(parentId);
        if (!parentInstance.endsWith(QStringLiteral("\\") + QString::fromLatin1(kFt601Serial),
                                     Qt::CaseInsensitive)) {
            continue;
        }
        ++matches;

        wchar_t service[256]{};
        wchar_t driverPath[512]{};
        const bool serviceOk = SetupDiGetDeviceRegistryPropertyW(
            devices, &device, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(service), sizeof(service), nullptr);
        const bool driverPathOk = SetupDiGetDeviceRegistryPropertyW(
            devices, &device, SPDRP_DRIVER, nullptr, reinterpret_cast<PBYTE>(driverPath), sizeof(driverPath), nullptr);
        if (!serviceOk) {
            if (detail != nullptr) *detail = QStringLiteral("无法读取 FT601 驱动服务属性");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }
        if (_wcsicmp(service, L"libusbK") != 0) continue;
        if (!driverPathOk) {
            if (detail != nullptr) *detail = QStringLiteral("无法读取 FT601 驱动注册表路径");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }

        const QString classPath = QStringLiteral("SYSTEM\\CurrentControlSet\\Control\\Class\\") +
                                  QString::fromWCharArray(driverPath);
        HKEY driverKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, classPath.toStdWString().c_str(), 0, KEY_READ, &driverKey) !=
            ERROR_SUCCESS) {
            if (detail != nullptr) *detail = QStringLiteral("无法打开 FT601 驱动注册表项");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }
        const QString provider = registryStringValue(driverKey, L"ProviderName");
        const QString version = registryStringValue(driverKey, L"DriverVersion");
        RegCloseKey(driverKey);
        if (provider.isEmpty() || version.isEmpty()) {
            if (detail != nullptr) *detail = QStringLiteral("无法读取 FT601 驱动提供者或版本");
            SetupDiDestroyDeviceInfoList(devices);
            return NativeDriverState::Error;
        }
        ready = provider.compare(QStringLiteral("libusbK"), Qt::CaseInsensitive) == 0 &&
                version == QStringLiteral("3.1.0.0");
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (enumerationFailed) return NativeDriverState::Error;
    if (matches == 0) return NativeDriverState::NotConnected;
    if (matches != 1) {
        if (detail != nullptr) *detail = QStringLiteral("在线 FT601 专用接口不是唯一实例");
        return NativeDriverState::Error;
    }
    return ready ? NativeDriverState::Ready : NativeDriverState::BindingRequired;
}
#endif

bool materializeEmbeddedBootstrap(const QString& directory, QString* script, QString* zadig,
                                  QString* error)
{
    *script = QDir(directory).filePath(QStringLiteral("ensure_ft601_libusbk.ps1"));
    *zadig = QDir(directory).filePath(QStringLiteral("zadig-2.9.exe"));
    const QString zadigConfiguration = QDir(directory).filePath(QStringLiteral("zadig.ini"));
    if (!QFile::copy(QStringLiteral(":/ft601_driver/ensure_ft601_libusbk.ps1"), *script) ||
        !QFile::copy(QStringLiteral(":/ft601_driver/zadig-2.9.exe"), *zadig) ||
        !QFile::copy(QStringLiteral(":/ft601_driver/zadig.ini"), zadigConfiguration)) {
        if (error != nullptr) *error = QStringLiteral("上位机内嵌 FT601 驱动组件不完整");
        return false;
    }
    QFile::setPermissions(*script, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::setPermissions(*zadig, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(zadigConfiguration, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

Ft601DriverBootstrapResult runBootstrapScript(const QString& script, const QString& zadig,
                                               const bool alreadyElevated)
{
    Ft601DriverBootstrapResult result;
    QProcess process;
#if defined(Q_OS_WIN)
    HANDLE helperJob = nullptr;
#endif
    process.setProgram(QStringLiteral("powershell.exe"));
    QStringList arguments{QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"),
                          QStringLiteral("-ExecutionPolicy"),
                          QStringLiteral("Bypass"),
                          QStringLiteral("-File"),
                          script,
                          QStringLiteral("-ExpectedSerial"),
                          QString::fromLatin1(kFt601Serial),
                          QStringLiteral("-ZadigPath"),
                          zadig};
    if (alreadyElevated) arguments.append(QStringLiteral("-AlreadyElevated"));
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(10000)) {
        result.status = QStringLiteral("helper_start_failed");
        result.message = process.errorString();
        return result;
    }
#if defined(Q_OS_WIN)
    if (alreadyElevated) {
        helperJob = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        HANDLE helperProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                           static_cast<DWORD>(process.processId()));
        const bool jobReady = helperJob != nullptr &&
            SetInformationJobObject(helperJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            helperProcess != nullptr && AssignProcessToJobObject(helperJob, helperProcess);
        if (helperProcess != nullptr) CloseHandle(helperProcess);
        if (!jobReady) {
            if (helperJob != nullptr) CloseHandle(helperJob);
            process.kill();
            process.waitForFinished(5000);
            result.status = QStringLiteral("helper_job_failed");
            result.message = QStringLiteral("无法建立 FT601 驱动进程树清理边界");
            return result;
        }
    }
#endif
    if (!process.waitForFinished(330000)) {
#if defined(Q_OS_WIN)
        if (helperJob != nullptr) {
            CloseHandle(helperJob);
            helperJob = nullptr;
        }
#endif
        process.kill();
        process.waitForFinished(5000);
        result.status = QStringLiteral("helper_timeout");
        result.message = QStringLiteral("FT601 libusbK 自动绑定超时");
        return result;
    }
#if defined(Q_OS_WIN)
    if (helperJob != nullptr) CloseHandle(helperJob);
#endif
    const QByteArray stdoutBytes = process.readAllStandardOutput().trimmed();
    const QByteArray stderrBytes = process.readAllStandardError().trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stdoutBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.status = QStringLiteral("invalid_helper_result");
        result.message = QString::fromLocal8Bit(stderrBytes);
        return result;
    }
    const QJsonObject object = document.object();
    result.status = object.value(QStringLiteral("status")).toString();
    result.connected = result.status != QStringLiteral("not_connected");
    result.changed = object.value(QStringLiteral("changed")).toBool(false);
    result.success = object.value(QStringLiteral("success")).toBool(false) || !result.connected;
    if (!result.success) {
        result.message = object.value(QStringLiteral("error")).toString();
        if (result.message.isEmpty()) result.message = QString::fromLocal8Bit(stderrBytes);
        if (result.message.isEmpty()) result.message = QStringLiteral("FT601 libusbK 自动绑定失败");
    }
    return result;
}

bool runUsbSelfCheck(QString* error)
{
    acquisition::LibusbRuntime runtime;
    if (!runtime.initialize(error)) return false;
    const QList<acquisition::LibusbDeviceInfo> devices = runtime.enumerate(error);
    QList<acquisition::LibusbDeviceInfo> matches;
    for (const acquisition::LibusbDeviceInfo& device : devices) {
        if (device.vendorId == 0x0403U && device.productId == 0x601fU &&
            device.serialNumber == QString::fromLatin1(kFt601Serial) && device.captureInterfaceAvailable &&
            device.outEndpoint == 0x02U && device.inEndpoint == 0x82U) {
            matches.append(device);
        }
    }
    if (matches.size() != 1) {
        if (error != nullptr) *error = QStringLiteral("FT601 USB 自检未找到唯一的 0x02/0x82 采集接口");
        return false;
    }
    if (!runtime.open(matches.constFirst().index, error)) return false;
    runtime.close();
    return true;
}

#if defined(Q_OS_WIN)
bool isTrustedLocalProgramData(const QString& programData, QString* error)
{
    const std::wstring nativeParent = QDir::toNativeSeparators(programData).toStdWString();
    HANDLE directoryHandle = CreateFileW(nativeParent.c_str(), FILE_READ_ATTRIBUTES,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directoryHandle == INVALID_HANDLE_VALUE) {
        if (error != nullptr) *error = QStringLiteral("无法打开 ProgramData 目录句柄");
        return false;
    }
    std::wstring finalBuffer(32768U, L'\0');
    const DWORD finalLength = GetFinalPathNameByHandleW(directoryHandle, finalBuffer.data(),
                                                        static_cast<DWORD>(finalBuffer.size()),
                                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    BY_HANDLE_FILE_INFORMATION information{};
    const BOOL informationOk = GetFileInformationByHandle(directoryHandle, &information);
    CloseHandle(directoryHandle);
    if (finalLength == 0U || finalLength >= finalBuffer.size() || !informationOk ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        if (error != nullptr) *error = QStringLiteral("ProgramData 目录句柄验证失败");
        return false;
    }
    finalBuffer.resize(finalLength);
    QString finalPath = QString::fromStdWString(finalBuffer);
    if (finalPath.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
        if (error != nullptr) *error = QStringLiteral("ProgramData 不得位于 UNC 路径");
        return false;
    }
    if (finalPath.startsWith(QStringLiteral("\\\\?\\"))) finalPath.remove(0, 4);
    const QString normalizedFinal = QDir::cleanPath(QDir::fromNativeSeparators(finalPath));
    const QString normalizedExpected = QDir::cleanPath(QDir::fromNativeSeparators(programData));
    if (normalizedFinal.compare(normalizedExpected, Qt::CaseInsensitive) != 0) {
        if (error != nullptr) *error = QStringLiteral("ProgramData 父链包含重定向或重解析");
        return false;
    }

    std::wstring volumeRoot(MAX_PATH, L'\0');
    const std::wstring finalNative = QDir::toNativeSeparators(normalizedFinal).toStdWString();
    if (!GetVolumePathNameW(finalNative.c_str(), volumeRoot.data(), static_cast<DWORD>(volumeRoot.size()))) {
        if (error != nullptr) *error = QStringLiteral("无法解析 ProgramData 卷根");
        return false;
    }
    volumeRoot.resize(wcslen(volumeRoot.c_str()));
    if (GetDriveTypeW(volumeRoot.c_str()) != DRIVE_FIXED) {
        if (error != nullptr) *error = QStringLiteral("ProgramData 不在本地固定卷");
        return false;
    }
    if (volumeRoot.size() < 2U || volumeRoot[1] != L':') return false;
    const std::wstring driveName = volumeRoot.substr(0, 2);
    std::wstring deviceTarget(32768U, L'\0');
    const DWORD targetLength = QueryDosDeviceW(driveName.c_str(), deviceTarget.data(),
                                               static_cast<DWORD>(deviceTarget.size()));
    if (targetLength == 0U) return false;
    deviceTarget.resize(wcslen(deviceTarget.c_str()));
    if (deviceTarget.rfind(L"\\??\\", 0U) == 0U) {
        if (error != nullptr) *error = QStringLiteral("ProgramData 位于 SUBST 或重定向卷");
        return false;
    }
    return true;
}

bool createAdministratorOnlyDirectory(QString* directory, QString* error)
{
    const QString programData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!isTrustedLocalProgramData(programData, error)) return false;

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    constexpr wchar_t kAdminOnlySddl[] =
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kAdminOnlySddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        if (error != nullptr) *error = QStringLiteral("无法创建管理员目录安全描述符");
        return false;
    }
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.lpSecurityDescriptor = descriptor;
    securityAttributes.bInheritHandle = FALSE;
    *directory = QDir(programData).filePath(
        QStringLiteral("LockstepDriverBootstrap-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const std::wstring nativeDirectory = QDir::toNativeSeparators(*directory).toStdWString();
    const BOOL created = CreateDirectoryW(nativeDirectory.c_str(), &securityAttributes);
    LocalFree(descriptor);
    if (!created) {
        if (error != nullptr) *error = QStringLiteral("无法原子创建管理员驱动启动目录");
        directory->clear();
        return false;
    }
    return true;
}

QByteArray bootstrapResultJson(const Ft601DriverBootstrapResult& result)
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), QStringLiteral("lockstep-ft601-bootstrap-v1"));
    object.insert(QStringLiteral("success"), result.success);
    object.insert(QStringLiteral("connected"), result.connected);
    object.insert(QStringLiteral("changed"), result.changed);
    object.insert(QStringLiteral("status"), result.status);
    if (!result.message.isEmpty()) object.insert(QStringLiteral("error"), result.message);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool validResultPipeName(const QString& pipeName)
{
    const QString prefix = QStringLiteral("\\\\.\\pipe\\LockstepFt601-");
    if (!pipeName.startsWith(prefix) || pipeName.size() != prefix.size() + 36) return false;
    const QString uuidText = pipeName.mid(prefix.size());
    return !QUuid(QStringLiteral("{") + uuidText + QStringLiteral("}")).isNull();
}

bool writeElevatedResult(const QString& pipeName, const Ft601DriverBootstrapResult& result)
{
    if (!validResultPipeName(pipeName)) return false;
    const std::wstring nativePipe = pipeName.toStdWString();
    if (!WaitNamedPipeW(nativePipe.c_str(), 10000)) return false;
    HANDLE pipe = CreateFileW(nativePipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;
    const QByteArray payload = bootstrapResultJson(result);
    DWORD written = 0;
    const BOOL writeOk = WriteFile(pipe, payload.constData(), static_cast<DWORD>(payload.size()), &written, nullptr);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return writeOk && written == static_cast<DWORD>(payload.size());
}

bool parseElevatedResult(const QByteArray& payload, Ft601DriverBootstrapResult* result, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = QStringLiteral("管理员驱动绑定返回了无效结果");
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("lockstep-ft601-bootstrap-v1")) {
        if (error != nullptr) *error = QStringLiteral("管理员驱动绑定结果合同不匹配");
        return false;
    }
    result->success = object.value(QStringLiteral("success")).toBool(false);
    result->connected = object.value(QStringLiteral("connected")).toBool(false);
    result->changed = object.value(QStringLiteral("changed")).toBool(false);
    result->status = object.value(QStringLiteral("status")).toString();
    result->message = object.value(QStringLiteral("error")).toString();
    return true;
}

void cancelAndDrainOverlapped(HANDLE handle, OVERLAPPED* overlapped)
{
    CancelIoEx(handle, overlapped);
    DWORD ignored = 0;
    GetOverlappedResult(handle, overlapped, &ignored, TRUE);
}

bool launchElevatedProduct(const QString& productExecutable, Ft601DriverBootstrapResult* elevatedResult,
                           QString* error)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    constexpr wchar_t kResultPipeSddl[] = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kResultPipeSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
        if (error != nullptr) *error = QStringLiteral("无法创建 FT601 结果管道安全描述符");
        return false;
    }
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.lpSecurityDescriptor = descriptor;
    securityAttributes.bInheritHandle = FALSE;
    const QString pipeName = QStringLiteral("\\\\.\\pipe\\LockstepFt601-") +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::wstring nativePipe = pipeName.toStdWString();
    HANDLE pipe = CreateNamedPipeW(nativePipe.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   1, 0, 65536, 0,
                                   &securityAttributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error != nullptr) *error = QStringLiteral("无法创建 FT601 管理员结果管道");
        return false;
    }
    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (connectEvent == nullptr) {
        CloseHandle(pipe);
        if (error != nullptr) *error = QStringLiteral("无法创建 FT601 结果管道连接事件");
        return false;
    }
    OVERLAPPED connectOverlapped{};
    connectOverlapped.hEvent = connectEvent;
    const BOOL connectedImmediately = ConnectNamedPipe(pipe, &connectOverlapped);
    const DWORD connectError = connectedImmediately ? ERROR_SUCCESS : GetLastError();
    bool connectPending = !connectedImmediately && connectError == ERROR_IO_PENDING;
    if (connectedImmediately) SetEvent(connectEvent);
    if (!connectedImmediately && connectError == ERROR_PIPE_CONNECTED) SetEvent(connectEvent);
    if (!connectedImmediately && connectError != ERROR_IO_PENDING && connectError != ERROR_PIPE_CONNECTED) {
        CloseHandle(connectEvent);
        CloseHandle(pipe);
        if (error != nullptr) *error = QStringLiteral("无法监听 FT601 管理员结果管道");
        return false;
    }

    const std::wstring executable = QDir::toNativeSeparators(productExecutable).toStdWString();
    const std::wstring parameters =
        (QStringLiteral("--elevated-ft601-driver-bootstrap=") + pipeName).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        if (connectPending) cancelAndDrainOverlapped(pipe, &connectOverlapped);
        CloseHandle(connectEvent);
        CloseHandle(pipe);
        if (error != nullptr) *error = QStringLiteral("FT601 驱动安装需要管理员授权");
        return false;
    }
    HANDLE connectWaitHandles[]{connectEvent, info.hProcess};
    const DWORD connectWait = WaitForMultipleObjects(2, connectWaitHandles, FALSE, 330000);
    QByteArray payload;
    if (connectWait == WAIT_OBJECT_0) {
        DWORD ignored = 0;
        const bool connectionCompleted = !connectPending ||
            GetOverlappedResult(pipe, &connectOverlapped, &ignored, FALSE);
        connectPending = false;
        if (connectionCompleted) {
            ULONG clientProcessId = 0;
            const DWORD elevatedProcessId = GetProcessId(info.hProcess);
            if (!GetNamedPipeClientProcessId(pipe, &clientProcessId) || clientProcessId != elevatedProcessId) {
                if (error != nullptr) *error = QStringLiteral("FT601 管理员结果管道客户端身份不匹配");
                CloseHandle(connectEvent);
                CloseHandle(pipe);
                WaitForSingleObject(info.hProcess, 10000);
                CloseHandle(info.hProcess);
                return false;
            }
            QByteArray buffer(65536, Qt::Uninitialized);
            HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (readEvent != nullptr) {
                OVERLAPPED readOverlapped{};
                readOverlapped.hEvent = readEvent;
                DWORD bytesRead = 0;
                const BOOL readImmediately = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                      &bytesRead, &readOverlapped);
                const DWORD readError = readImmediately ? ERROR_SUCCESS : GetLastError();
                bool readPending = !readImmediately && readError == ERROR_IO_PENDING;
                if (readPending) {
                    HANDLE readWaitHandles[]{readEvent, info.hProcess};
                    const DWORD readWait = WaitForMultipleObjects(2, readWaitHandles, FALSE, 10000);
                    if (readWait == WAIT_OBJECT_0) {
                        if (!GetOverlappedResult(pipe, &readOverlapped, &bytesRead, FALSE)) bytesRead = 0;
                        readPending = false;
                    }
                }
                if (readPending) cancelAndDrainOverlapped(pipe, &readOverlapped);
                CloseHandle(readEvent);
                if (bytesRead > 0) payload = buffer.left(static_cast<int>(bytesRead));
            } else if (error != nullptr) {
                *error = QStringLiteral("无法创建 FT601 结果管道读取事件");
            }
        }
    } else if (connectPending) {
        cancelAndDrainOverlapped(pipe, &connectOverlapped);
        connectPending = false;
    }
    CloseHandle(connectEvent);
    CloseHandle(pipe);

    const DWORD waitResult = WaitForSingleObject(info.hProcess, 10000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    const bool resultParsed = !payload.isEmpty() && parseElevatedResult(payload, elevatedResult, error);
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0) {
        if (error != nullptr && !elevatedResult->message.isEmpty()) {
            *error = elevatedResult->message;
        } else if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("管理员 FT601 驱动绑定未完成，退出码 %1").arg(exitCode);
        }
        return false;
    }
    if (!resultParsed) {
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("管理员 FT601 驱动绑定未返回结构化结果");
        }
        return false;
    }
    return true;
}
#endif

}  // namespace

Ft601DriverBootstrapResult ensureFt601LibusbK(const QString& productExecutable)
{
    Ft601DriverBootstrapResult result;
#if !defined(Q_OS_WIN)
    result.success = true;
    result.status = QStringLiteral("not_required");
    return result;
#else
    const NativeDriverState nativeState = inspectNativeFt601Driver(&result.message);
    if (nativeState == NativeDriverState::NotConnected) {
        result.success = true;
        result.connected = false;
        result.status = QStringLiteral("not_connected");
        return result;
    }
    if (nativeState == NativeDriverState::Error) {
        result.status = QStringLiteral("native_driver_check_failed");
        return result;
    }
    if (nativeState == NativeDriverState::Ready) {
        result.connected = true;
        result.status = QStringLiteral("ready");
        result.success = runUsbSelfCheck(&result.message);
        if (!result.success) result.status = QStringLiteral("usb_self_check_failed");
        return result;
    }

    QTemporaryDir bootstrapDir(QDir::temp().filePath(QStringLiteral("lockstep-ft601-bootstrap-XXXXXX")));
    if (!bootstrapDir.isValid()) {
        result.status = QStringLiteral("bootstrap_temp_failed");
        result.message = QStringLiteral("无法创建 FT601 驱动启动临时目录");
        return result;
    }
    QString script;
    QString zadig;
    if (!materializeEmbeddedBootstrap(bootstrapDir.path(), &script, &zadig, &result.message)) {
        result.status = QStringLiteral("embedded_bootstrap_missing");
        return result;
    }
    result = runBootstrapScript(script, zadig, false);
    if (result.status == QStringLiteral("elevation_required")) {
        Ft601DriverBootstrapResult elevatedResult;
        if (!launchElevatedProduct(QFileInfo(productExecutable).absoluteFilePath(), &elevatedResult,
                                   &result.message)) {
            result.status = QStringLiteral("elevation_failed");
            return result;
        }
        result = elevatedResult;
        result.message.clear();
        const NativeDriverState installedState = inspectNativeFt601Driver(&result.message);
        if (installedState != NativeDriverState::Ready) {
            result.success = false;
            result.status = QStringLiteral("post_install_driver_check_failed");
            if (result.message.isEmpty()) {
                result.message = QStringLiteral("管理员绑定结束后 FT601 未处于 libusbK 3.1.0.0 状态");
            }
            return result;
        }
        result.success = true;
        result.connected = true;
        result.changed = true;
        result.status = QStringLiteral("ready");
    }
    if (result.success && result.connected && !runUsbSelfCheck(&result.message)) {
        result.success = false;
        result.status = QStringLiteral("usb_self_check_failed");
    }
    return result;
#endif
}

int runElevatedFt601LibusbKBootstrap(const QString& resultPipeName)
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(resultPipeName);
    return 0;
#else
    if (!validResultPipeName(resultPipeName)) return 41;
    const auto finish = [&resultPipeName](const Ft601DriverBootstrapResult& result, const int exitCode) {
        return writeElevatedResult(resultPipeName, result) ? exitCode : 46;
    };
    Ft601DriverBootstrapResult result;
    QString error;
    QString runDirectory;
    if (!createAdministratorOnlyDirectory(&runDirectory, &error)) {
        result.status = QStringLiteral("admin_directory_failed");
        result.message = error;
        return finish(result, 42);
    }
    QString script;
    QString zadig;
    if (!materializeEmbeddedBootstrap(runDirectory, &script, &zadig, &error)) {
        QDir(runDirectory).removeRecursively();
        result.status = QStringLiteral("embedded_bootstrap_missing");
        result.message = error;
        return finish(result, 44);
    }
    result = runBootstrapScript(script, zadig, true);
    QDir(runDirectory).removeRecursively();
    return finish(result, result.success ? 0 : 45);
#endif
}

}  // namespace lockstep::apps
