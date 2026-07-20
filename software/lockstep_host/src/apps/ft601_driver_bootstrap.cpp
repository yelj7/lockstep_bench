/**********************************************************
* 文件名: ft601_driver_bootstrap.cpp
* 日期: 2026-07-20
* 版本: 1.0
* 更新记录: 新增 Windows FT601 libusbK 启动绑定实现。
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
#include <shellapi.h>
#include <sddl.h>
#endif

namespace lockstep::apps {
namespace {

constexpr auto kFt601Serial = "000000000001";

bool materializeEmbeddedBootstrap(const QString& directory, QString* script, QString* zadig,
                                  QString* error)
{
    *script = QDir(directory).filePath(QStringLiteral("ensure_ft601_libusbk.ps1"));
    *zadig = QDir(directory).filePath(QStringLiteral("zadig-2.9.exe"));
    if (!QFile::copy(QStringLiteral(":/ft601_driver/ensure_ft601_libusbk.ps1"), *script) ||
        !QFile::copy(QStringLiteral(":/ft601_driver/zadig-2.9.exe"), *zadig)) {
        if (error != nullptr) *error = QStringLiteral("上位机内嵌 FT601 驱动组件不完整");
        return false;
    }
    QFile::setPermissions(*script, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QFile::setPermissions(*zadig, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return true;
}

Ft601DriverBootstrapResult runBootstrapScript(const QString& script, const QString& zadig,
                                               const bool alreadyElevated)
{
    Ft601DriverBootstrapResult result;
    QProcess process;
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
    if (!process.waitForFinished(330000)) {
        process.kill();
        process.waitForFinished(5000);
        result.status = QStringLiteral("helper_timeout");
        result.message = QStringLiteral("FT601 libusbK 自动绑定超时");
        return result;
    }
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
        result.message = QString::fromLocal8Bit(stderrBytes);
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

bool launchElevatedProduct(const QString& productExecutable, QString* error)
{
    const std::wstring executable = QDir::toNativeSeparators(productExecutable).toStdWString();
    const std::wstring parameters = L"--elevated-ft601-driver-bootstrap";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info)) {
        if (error != nullptr) *error = QStringLiteral("FT601 驱动安装需要管理员授权");
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(info.hProcess, 330000);
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    if (waitResult != WAIT_OBJECT_0 || exitCode != 0) {
        if (error != nullptr) *error = QStringLiteral("管理员 FT601 驱动绑定未完成");
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
        if (!launchElevatedProduct(QFileInfo(productExecutable).absoluteFilePath(), &result.message)) {
            result.status = QStringLiteral("elevation_failed");
            return result;
        }
        result = runBootstrapScript(script, zadig, false);
        result.changed = result.success;
    }
    if (result.success && result.connected && !runUsbSelfCheck(&result.message)) {
        result.success = false;
        result.status = QStringLiteral("usb_self_check_failed");
    }
    return result;
#endif
}

int runElevatedFt601LibusbKBootstrap()
{
#if !defined(Q_OS_WIN)
    return 0;
#else
    QString error;
    QString runDirectory;
    if (!createAdministratorOnlyDirectory(&runDirectory, &error)) return 42;
    QString script;
    QString zadig;
    if (!materializeEmbeddedBootstrap(runDirectory, &script, &zadig, &error)) {
        QDir(runDirectory).removeRecursively();
        return 44;
    }
    const Ft601DriverBootstrapResult result = runBootstrapScript(script, zadig, true);
    QDir(runDirectory).removeRecursively();
    return result.success ? 0 : 45;
#endif
}

}  // namespace lockstep::apps
