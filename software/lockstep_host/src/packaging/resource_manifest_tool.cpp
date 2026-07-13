/**********************************************************
* 文件名: resource_manifest_tool.cpp
* 日期: 2026-07-13
* 版本: v1.0
* 更新记录: 初版创建安装资源清单生成工具
* 描述: 将构建出的调试服务写入资源树并更新其版本、状态和 SHA-256
**********************************************************/

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

namespace {

constexpr char kDebugServiceResourceId[] = "debug.self_service.executable";
constexpr char kDebugServiceRelativePath[] =
    "debug_adapters/self_debug_service/lockstep_debug_service";

bool copyFileReplacing(const QString& sourcePath, const QString& targetPath, QString* const error)
{
    const QFileInfo targetInfo(targetPath);
    QDir directory;
    if (!directory.mkpath(targetInfo.absolutePath())) {
        *error = QStringLiteral("无法创建资源目录: %1").arg(targetInfo.absolutePath());
        return false;
    }
    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        *error = QStringLiteral("无法替换旧调试服务: %1").arg(targetPath);
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        *error = QStringLiteral("无法复制调试服务: %1 -> %2").arg(sourcePath, targetPath);
        return false;
    }
    return true;
}

QString sha256File(const QString& path, QString* const error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取调试服务: %1").arg(path);
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool updateManifest(
    const QString& manifestPath,
    const QString& version,
    const QString& sha256,
    QString* const error)
{
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取资源清单: %1").arg(manifestPath);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    manifestFile.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("资源清单 JSON 无效: %1").arg(parseError.errorString());
        return false;
    }

    QJsonObject root = document.object();
    QJsonArray adapters = root.value(QStringLiteral("debug_adapters")).toArray();
    bool found = false;
    for (int index = 0; index < adapters.size(); ++index) {
        QJsonObject adapter = adapters.at(index).toObject();
        if (adapter.value(QStringLiteral("id")).toString() !=
            QString::fromLatin1(kDebugServiceResourceId)) {
            continue;
        }
        adapter.insert(QStringLiteral("version"), version);
        adapter.insert(QStringLiteral("sha256"), sha256);
        adapter.insert(QStringLiteral("status"), QStringLiteral("enabled"));
        adapters.replace(index, adapter);
        found = true;
        break;
    }
    if (!found) {
        *error = QStringLiteral("资源清单缺少调试服务项: %1")
                     .arg(QString::fromLatin1(kDebugServiceResourceId));
        return false;
    }

    root.insert(QStringLiteral("debug_adapters"), adapters);
    document.setObject(root);
    QSaveFile output(manifestPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("无法写入资源清单: %1").arg(manifestPath);
        return false;
    }
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (output.write(payload) != payload.size() || !output.commit()) {
        *error = QStringLiteral("资源清单写入失败: %1").arg(manifestPath);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lockstep_resource_manifest_tool"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption manifestOption(
        QStringLiteral("manifest"), QStringLiteral("待更新的资源清单"), QStringLiteral("path"));
    const QCommandLineOption serviceOption(
        QStringLiteral("debug-service"), QStringLiteral("构建出的调试服务"), QStringLiteral("path"));
    const QCommandLineOption versionOption(
        QStringLiteral("version"), QStringLiteral("调试服务版本"), QStringLiteral("version"));
    parser.addOptions({manifestOption, serviceOption, versionOption});
    parser.process(application);

    if (!parser.isSet(manifestOption) || !parser.isSet(serviceOption) ||
        !parser.isSet(versionOption)) {
        QTextStream(stderr) << "必须指定 --manifest、--debug-service 和 --version\n";
        return 2;
    }

    const QString manifestPath = QFileInfo(parser.value(manifestOption)).absoluteFilePath();
    const QString servicePath = QFileInfo(parser.value(serviceOption)).absoluteFilePath();
    const QString version = parser.value(versionOption).trimmed();
    if (!QFileInfo::exists(manifestPath) || !QFileInfo(servicePath).isFile() ||
        version.isEmpty()) {
        QTextStream(stderr) << "输入清单、可执行调试服务或版本无效\n";
        return 2;
    }

    const QString resourceRoot = QFileInfo(manifestPath).absolutePath();
    const QString installedServicePath =
        QDir(resourceRoot).filePath(QString::fromLatin1(kDebugServiceRelativePath));
    QString error;
    if (!copyFileReplacing(servicePath, installedServicePath, &error)) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    QFile::setPermissions(
        installedServicePath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
            QFileDevice::ExeOther);

    const QString sha256 = sha256File(installedServicePath, &error);
    if (sha256.isEmpty() || !updateManifest(manifestPath, version, sha256, &error)) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    QTextStream(stdout) << sha256 << '\n';
    return 0;
}
