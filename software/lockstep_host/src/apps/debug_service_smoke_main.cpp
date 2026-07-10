/**********************************************************
* 文件名: debug_service_smoke_main.cpp
* 日期: 2026-07-09
* 版本: 1.0.0.1
* 更新记录: 初版创建自研片上调试服务冒烟测试入口
* 描述: 通过 M05/M07 接口驱动自研调试服务执行连接、预检、烧写、回读、运行和中止验证。
**********************************************************/

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

#include "resource_manager.h"
#include "target_control.h"

namespace {

constexpr quint64 kDefaultDebugMemorySizeBytes = 64ULL * 1024ULL * 1024ULL;

QString nowText()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString addressText(const quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

quint64 parseHexOrDecimal(const QString& text, const quint64 defaultValue)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return defaultValue;
    }

    bool ok = false;
    quint64 value = 0U;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        value = trimmed.mid(2).toULongLong(&ok, 16);
    } else {
        value = trimmed.toULongLong(&ok, 10);
    }
    return ok ? value : defaultValue;
}

QString defaultResourceRoot()
{
    const QString appResources = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources"));
    if (QFileInfo::exists(QDir(appResources).filePath(QStringLiteral("manifest.json")))) {
        return appResources;
    }

    const QString sourceResources = QDir(QDir::currentPath()).filePath(QStringLiteral("software/lockstep_host/resources"));
    if (QFileInfo::exists(QDir(sourceResources).filePath(QStringLiteral("manifest.json")))) {
        return sourceResources;
    }
    return appResources;
}

QString defaultDebugServicePath()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("lockstep_debug_service"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    const QString fromPathExe = QStandardPaths::findExecutable(QStringLiteral("lockstep_debug_service.exe"));
    if (!fromPathExe.isEmpty()) {
        return fromPathExe;
    }
    return QString();
}

bool writeJsonFile(const QString& path, const QJsonObject& object, QString* const errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入 JSON: %1").arg(path);
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
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

QJsonArray segmentsToJson(const QList<lockstep::target_control::ImageSegment>& segments)
{
    QJsonArray array;
    for (const lockstep::target_control::ImageSegment& segment : segments) {
        QJsonObject object;
        object.insert(QStringLiteral("address"), addressText(segment.address));
        object.insert(QStringLiteral("length"), segment.data.size());
        array.append(object);
    }
    return array;
}

QJsonObject imageToJson(const lockstep::target_control::ProgramImageInfo& image)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), lockstep::target_control::toString(image.type));
    object.insert(QStringLiteral("file_name"), image.fileName);
    object.insert(QStringLiteral("sha256"), image.sha256);
    object.insert(QStringLiteral("size_bytes"), image.sizeBytes);
    object.insert(QStringLiteral("entry_address"), addressText(image.entryAddress));
    object.insert(QStringLiteral("segments"), segmentsToJson(image.segments));
    object.insert(QStringLiteral("error_message"), image.errorMessage);
    return object;
}

QJsonObject connectionToJson(const lockstep::target_control::ConnectionRecord& record)
{
    QString state = QStringLiteral("not_connected");
    if (record.state == lockstep::target_control::ConnectionState::Connected) {
        state = QStringLiteral("connected");
    } else if (record.state == lockstep::target_control::ConnectionState::Failed) {
        state = QStringLiteral("failed");
    }

    QJsonObject object;
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("profile_id"), record.profileId);
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), nowText());
    return object;
}

QJsonObject precheckToJson(const lockstep::target_control::PrecheckRecord& record)
{
    QString state = QStringLiteral("not_run");
    if (record.state == lockstep::target_control::PrecheckState::Passed) {
        state = QStringLiteral("passed");
    } else if (record.state == lockstep::target_control::PrecheckState::Failed) {
        state = QStringLiteral("failed");
    } else if (record.state == lockstep::target_control::PrecheckState::Blocked) {
        state = QStringLiteral("blocked");
    }

    QJsonObject object;
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("reset_supported"), record.resetSupported);
    object.insert(QStringLiteral("read_supported"), record.readSupported);
    object.insert(QStringLiteral("write_supported"), record.writeSupported);
    object.insert(QStringLiteral("run_supported"), record.runSupported);
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), nowText());
    return object;
}

QJsonObject writeToJson(const lockstep::target_control::WriteRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("success"), record.success);
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("segments"), segmentsToJson(record.segments));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), nowText());
    return object;
}

QJsonObject verifyToJson(const lockstep::target_control::ReadbackVerifyRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("state"), lockstep::target_control::toString(record.state));
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("expected_length"), QString::number(record.expectedLength));
    object.insert(QStringLiteral("actual_length"), QString::number(record.actualLength));
    object.insert(QStringLiteral("diff_count"), QString::number(record.diffCount));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), nowText());
    return object;
}

QJsonObject runToJson(const lockstep::target_control::RunControlRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("operation"), lockstep::target_control::toString(record.operation));
    object.insert(QStringLiteral("state"), lockstep::target_control::toString(record.state));
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("entry_address"), addressText(record.entryAddress));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("snapshot"), record.snapshot);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), nowText());
    return object;
}

void saveEvidence(const QString& outputDir, const QString& name, const QJsonObject& object)
{
    QString error;
    if (!writeJsonFile(QDir(outputDir).filePath(name), object, &error)) {
        QTextStream(stderr) << error << Qt::endl;
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lockstep_debug_service_smoke"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lockstep 自研调试服务接口冒烟测试"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption resourcesOption(QStringLiteral("resources"), QStringLiteral("资源包目录"), QStringLiteral("path"));
    const QCommandLineOption profileOption(QStringLiteral("profile"), QStringLiteral("板卡 profile ID"), QStringLiteral("id"));
    const QCommandLineOption imageOption(QStringLiteral("image"), QStringLiteral("程序镜像路径"), QStringLiteral("path"));
    const QCommandLineOption debugServiceOption(QStringLiteral("debug-service"), QStringLiteral("自研调试服务可执行文件"), QStringLiteral("path"));
    const QCommandLineOption interfaceOption(QStringLiteral("interface-cfg"), QStringLiteral("调试接口配置"), QStringLiteral("path"));
    const QCommandLineOption targetOption(QStringLiteral("target-cfg"), QStringLiteral("目标芯片配置"), QStringLiteral("path"));
    const QCommandLineOption outputOption(QStringLiteral("output-dir"), QStringLiteral("测试输出目录"), QStringLiteral("path"));
    const QCommandLineOption speedOption(QStringLiteral("adapter-speed"), QStringLiteral("JTAG 速度 kHz"), QStringLiteral("khz"), QStringLiteral("100"));
    const QCommandLineOption timeoutOption(QStringLiteral("timeout-ms"), QStringLiteral("单个调试服务操作超时"), QStringLiteral("ms"), QStringLiteral("60000"));
    const QCommandLineOption taskIdOption(QStringLiteral("task-id"), QStringLiteral("测试任务 ID"), QStringLiteral("id"), QStringLiteral("debug_service_smoke"));
    const QCommandLineOption ramLimitOption(QStringLiteral("ram-limit"), QStringLiteral("允许写入空间大小"), QStringLiteral("bytes"), QString::number(kDefaultDebugMemorySizeBytes));

    parser.addOptions({
        resourcesOption,
        profileOption,
        imageOption,
        debugServiceOption,
        interfaceOption,
        targetOption,
        outputOption,
        speedOption,
        timeoutOption,
        taskIdOption,
        ramLimitOption
    });
    parser.process(app);

    if (!parser.isSet(imageOption)) {
        QTextStream(stderr) << QStringLiteral("必须提供 --image") << Qt::endl;
        return 2;
    }

    const QString imagePath = QFileInfo(parser.value(imageOption)).absoluteFilePath();
    if (!QFileInfo::exists(imagePath)) {
        QTextStream(stderr) << QStringLiteral("程序镜像不存在: %1").arg(imagePath) << Qt::endl;
        return 2;
    }

    const QString resourceRoot = parser.isSet(resourcesOption)
        ? QFileInfo(parser.value(resourcesOption)).absoluteFilePath()
        : defaultResourceRoot();

    lockstep::resources::ResourceManager resources;
    const lockstep::resources::ResourceValidationResult validation = resources.validateResourcePack(resourceRoot);
    if (!validation.success) {
        QTextStream(stderr) << QStringLiteral("资源包校验失败: %1").arg(validation.errors.join(QStringLiteral("; "))) << Qt::endl;
        return 2;
    }

    const lockstep::resources::ManifestDefaults defaults = resources.defaults();
    const QString profileId = parser.isSet(profileOption) ? parser.value(profileOption) : defaults.testProfileId;
    lockstep::resources::BoardProfile boardProfile;
    QString error;
    if (!resources.resolveBoardProfile(profileId, &boardProfile, &error)) {
        QTextStream(stderr) << QStringLiteral("板卡 profile 解析失败: %1").arg(error) << Qt::endl;
        return 2;
    }

    lockstep::target_control::DebugProfile debugProfile;
    debugProfile.profileId = boardProfile.profileId;
    debugProfile.profileName = boardProfile.profileName;
    debugProfile.ramBaseAddress = boardProfile.ramBaseAddress;
    debugProfile.defaultRunAddress = boardProfile.defaultRunAddress;
    debugProfile.maxWritableAddress =
        debugProfile.ramBaseAddress + parseHexOrDecimal(parser.value(ramLimitOption), kDefaultDebugMemorySizeBytes);
    debugProfile.resetStrategy = boardProfile.resetStrategy;

    lockstep::target_control::DebugServiceConfig debugConfig;
    debugConfig.debugServicePath = parser.isSet(debugServiceOption)
        ? QFileInfo(parser.value(debugServiceOption)).absoluteFilePath()
        : defaultDebugServicePath();
    debugConfig.interfaceConfigPath = parser.isSet(interfaceOption)
        ? parser.value(interfaceOption)
        : QDir(validation.resourceRootPath).filePath(boardProfile.interfaceConfigPath);
    debugConfig.targetConfigPath = parser.isSet(targetOption)
        ? parser.value(targetOption)
        : QDir(validation.resourceRootPath).filePath(boardProfile.targetConfigPath);
    debugConfig.adapterSpeedKhz = static_cast<int>(parseHexOrDecimal(parser.value(speedOption), 100U));
    debugConfig.timeoutMs = static_cast<int>(parseHexOrDecimal(parser.value(timeoutOption), 60000U));

    const QString outputDir = parser.isSet(outputOption)
        ? QFileInfo(parser.value(outputOption)).absoluteFilePath()
        : QDir(QDir::currentPath()).filePath(QStringLiteral(".codex-tasks/%1_debug_service_smoke")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    if (!QDir().mkpath(outputDir)) {
        QTextStream(stderr) << QStringLiteral("无法创建输出目录: %1").arg(outputDir) << Qt::endl;
        return 2;
    }
    debugConfig.temporaryDirectoryPath = QDir(outputDir).filePath(QStringLiteral("tmp"));

    lockstep::target_control::DebugServiceAccess debugAccess(debugConfig);
    lockstep::target_control::TargetConnectionService connectionService;
    lockstep::target_control::ProgramController programController;
    const QString taskId = parser.value(taskIdOption);

    const lockstep::target_control::ConnectionRecord connection =
        connectionService.connectTarget(debugAccess, debugProfile);

    lockstep::target_control::PrecheckRecord precheck;
    lockstep::target_control::ProgramImageInfo image;
    lockstep::target_control::WriteRecord write;
    lockstep::target_control::ReadbackVerifyRecord verify;
    lockstep::target_control::RunControlRecord run;
    lockstep::target_control::RunControlRecord halt;

    if (connection.state == lockstep::target_control::ConnectionState::Connected) {
        precheck = connectionService.runPrecheck(debugAccess, debugProfile);
    } else {
        precheck.state = lockstep::target_control::PrecheckState::Blocked;
        precheck.errorMessage = QStringLiteral("连接失败，预检未执行。");
    }

    image = programController.detectImage(imagePath, debugProfile);
    const bool imageReady =
        (image.type != lockstep::target_control::ImageType::Unknown) && image.errorMessage.isEmpty();
    if (precheck.state == lockstep::target_control::PrecheckState::Passed && imageReady) {
        write = programController.programTarget(debugAccess, taskId, image);
    } else {
        write.taskId = taskId;
        write.errorMessage = QStringLiteral("预检未通过或镜像无效，烧写未执行。");
    }

    if (write.success) {
        verify = programController.verifyReadback(debugAccess, taskId, image);
    } else {
        verify.taskId = taskId;
        verify.state = lockstep::target_control::VerifyState::Failed;
        verify.errorMessage = QStringLiteral("烧写未成功，回读校验未执行。");
    }

    if (verify.state == lockstep::target_control::VerifyState::Passed) {
        run = programController.runTarget(debugAccess, taskId, image, verify);
    } else {
        run.taskId = taskId;
        run.operation = lockstep::target_control::ProgramOperation::Run;
        run.state = lockstep::target_control::RunState::NotAllowed;
        run.errorMessage = QStringLiteral("回读校验未通过，运行未执行。");
    }

    if (run.state == lockstep::target_control::RunState::Running) {
        halt = programController.haltTarget(debugAccess, taskId);
    } else {
        halt.taskId = taskId;
        halt.operation = lockstep::target_control::ProgramOperation::Halt;
        halt.state = lockstep::target_control::RunState::NotAllowed;
        halt.errorMessage = QStringLiteral("程序未进入运行态，中止未执行。");
    }

    const lockstep::target_control::ConnectionRecord disconnect =
        connectionService.disconnectTarget(debugAccess);

    const QJsonObject connectionJson = connectionToJson(connection);
    const QJsonObject precheckJson = precheckToJson(precheck);
    const QJsonObject imageJson = imageToJson(image);
    const QJsonObject writeJson = writeToJson(write);
    const QJsonObject verifyJson = verifyToJson(verify);
    const QJsonObject runJson = runToJson(run);
    const QJsonObject haltJson = runToJson(halt);
    const QJsonObject disconnectJson = connectionToJson(disconnect);

    saveEvidence(outputDir, QStringLiteral("connection_record.json"), connectionJson);
    saveEvidence(outputDir, QStringLiteral("precheck_record.json"), precheckJson);
    saveEvidence(outputDir, QStringLiteral("program_image_info.json"), imageJson);
    saveEvidence(outputDir, QStringLiteral("program_write_record.json"), writeJson);
    saveEvidence(outputDir, QStringLiteral("readback_verify_record.json"), verifyJson);
    saveEvidence(outputDir, QStringLiteral("run_control_record.json"), runJson);
    saveEvidence(outputDir, QStringLiteral("halt_control_record.json"), haltJson);

    const bool pass =
        connection.state == lockstep::target_control::ConnectionState::Connected &&
        precheck.state == lockstep::target_control::PrecheckState::Passed &&
        image.type != lockstep::target_control::ImageType::Unknown &&
        write.success &&
        verify.state == lockstep::target_control::VerifyState::Passed &&
        run.state == lockstep::target_control::RunState::Running &&
        halt.state == lockstep::target_control::RunState::Halted;

    QJsonObject summary;
    summary.insert(QStringLiteral("schema"), QStringLiteral("lockstep-debug-service-smoke-v1"));
    summary.insert(QStringLiteral("generated_at"), nowText());
    summary.insert(QStringLiteral("overall_status"), pass ? QStringLiteral("PASS") : QStringLiteral("FAILED"));
    summary.insert(QStringLiteral("resource_root"), validation.resourceRootPath);
    summary.insert(QStringLiteral("profile_id"), debugProfile.profileId);
    summary.insert(QStringLiteral("profile_name"), debugProfile.profileName);
    summary.insert(QStringLiteral("image_path"), imagePath);
    summary.insert(QStringLiteral("debug_service_path"), debugConfig.debugServicePath);
    summary.insert(QStringLiteral("interface_config_path"), debugConfig.interfaceConfigPath);
    summary.insert(QStringLiteral("target_config_path"), debugConfig.targetConfigPath);
    summary.insert(QStringLiteral("adapter_speed_khz"), debugConfig.adapterSpeedKhz);
    summary.insert(QStringLiteral("connection"), connectionJson);
    summary.insert(QStringLiteral("precheck"), precheckJson);
    summary.insert(QStringLiteral("image"), imageJson);
    summary.insert(QStringLiteral("write"), writeJson);
    summary.insert(QStringLiteral("readback"), verifyJson);
    summary.insert(QStringLiteral("run"), runJson);
    summary.insert(QStringLiteral("halt"), haltJson);
    summary.insert(QStringLiteral("disconnect"), disconnectJson);

    if (!writeJsonFile(QDir(outputDir).filePath(QStringLiteral("summary.json")), summary, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }

    QTextStream(stdout) << QStringLiteral("Summary: %1").arg(QDir(outputDir).filePath(QStringLiteral("summary.json"))) << Qt::endl;
    QTextStream(stdout) << QStringLiteral("Overall status: %1").arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAILED")) << Qt::endl;
    return pass ? 0 : 1;
}
