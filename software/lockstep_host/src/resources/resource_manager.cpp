/*****************************************************************************
*  @file      resource_manager.cpp
*  @brief     固化资源与模式配置管理模块实现
*  Details.   实现固化资源与模式配置管理模块的业务逻辑、状态转换和文件访问流程。
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

#include "resource_manager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace lockstep::resources {
namespace {

constexpr char kManifestName[] = "manifest.json";
constexpr char kResourcesName[] = "resources";

void setError(QString* const errorMessage, const QString& message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
}

QByteArray sha256File(const QString& path, QStringList* const errors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errors != nullptr) {
            errors->append(QStringLiteral("无法读取资源文件: %1").arg(path));
        }
        return QByteArray();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }
    return hash.result().toHex();
}

bool readJsonObject(const QString& path, QJsonObject* const object, QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("无法读取 JSON 文件: %1").arg(path));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("JSON 格式错误: %1").arg(path));
        return false;
    }

    *object = document.object();
    return true;
}

QString resourceRootFromInstallRoot(const QString& installRootPath)
{
    const QDir rootDir(QDir::cleanPath(installRootPath));
    const QString directManifest = rootDir.filePath(QString::fromLatin1(kManifestName));
    if (QFileInfo::exists(directManifest)) {
        return rootDir.absolutePath();
    }
    return rootDir.filePath(QString::fromLatin1(kResourcesName));
}

QStringList jsonStringArray(const QJsonValue& value)
{
    QStringList values;
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        values.append(item.toString());
    }
    return values;
}

ResourceItem itemFromJson(const QJsonObject& object)
{
    ResourceItem item;
    item.id = object.value(QStringLiteral("id")).toString();
    item.name = object.value(QStringLiteral("name")).toString();
    item.version = object.value(QStringLiteral("version")).toString();
    item.mode = object.value(QStringLiteral("mode")).toString();
    item.modes = jsonStringArray(object.value(QStringLiteral("modes")));
    item.relativePath = object.value(QStringLiteral("relative_path")).toString();
    item.sha256 = object.value(QStringLiteral("sha256")).toString();
    ResourceStatus status = ResourceStatus::Invalid;
    if (parseResourceStatus(object.value(QStringLiteral("status")).toString(), &status)) {
        item.status = status;
    }
    return item;
}

QList<ResourceItem> itemListFromJson(const QJsonArray& array)
{
    QList<ResourceItem> items;
    for (const QJsonValue& value : array) {
        items.append(itemFromJson(value.toObject()));
    }
    return items;
}

bool resourceSupportsMode(const ResourceItem& item, const QString& mode)
{
    if (item.mode == mode) {
        return true;
    }
    return item.modes.contains(mode, Qt::CaseInsensitive);
}

quint64 parseHexOrDecimal(const QString& text)
{
    const QString trimmed = text.trimmed();
    bool ok = false;
    quint64 value = 0U;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        value = trimmed.mid(2).toULongLong(&ok, 16);
    } else {
        value = trimmed.toULongLong(&ok, 10);
    }

    return ok ? value : 0U;
}

QString addressText(const quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

bool findItem(const QList<ResourceItem>& items, const QString& id, ResourceItem* const item)
{
    for (const ResourceItem& current : items) {
        if (current.id == id) {
            if (item != nullptr) {
                *item = current;
            }
            return true;
        }
    }
    return false;
}

void validateItemFiles(
    const QString& resourceRootPath,
    const QList<ResourceItem>& items,
    const bool allowPlaceholder,
    ResourceValidationResult* const result)
{
    if (result == nullptr) {
        return;
    }

    for (const ResourceItem& item : items) {
        if (item.id.isEmpty() || item.relativePath.isEmpty()) {
            result->errors.append(QStringLiteral("资源项字段不完整"));
            continue;
        }

        if (item.status == ResourceStatus::Placeholder && allowPlaceholder) {
            result->warnings.append(QStringLiteral("资源为占位状态: %1").arg(item.id));
            continue;
        }
        if (item.status == ResourceStatus::Disabled) {
            result->warnings.append(QStringLiteral("资源已禁用: %1").arg(item.id));
            continue;
        }

        const QString path = QDir(resourceRootPath).filePath(item.relativePath);
        if (!QFileInfo::exists(path)) {
            result->errors.append(QStringLiteral("资源文件缺失: %1").arg(item.id));
            continue;
        }

        if (!item.sha256.isEmpty() && !item.sha256.startsWith(QChar::fromLatin1('<'))) {
            QStringList errors;
            const QString actual = QString::fromLatin1(sha256File(path, &errors));
            if (!errors.isEmpty()) {
                result->errors.append(errors);
            } else if (actual.compare(item.sha256, Qt::CaseInsensitive) != 0) {
                result->errors.append(QStringLiteral("资源摘要不匹配: %1").arg(item.id));
            }
        }
    }
}

bool profileFromItem(
    const QString& resourceRootPath,
    const ResourceItem& item,
    BoardProfile* const profile,
    QString* const errorMessage)
{
    if (profile == nullptr) {
        setError(errorMessage, QStringLiteral("profile 输出为空"));
        return false;
    }

    const QString profilePath = QDir(resourceRootPath).filePath(item.relativePath);
    QJsonObject object;
    if (!readJsonObject(profilePath, &object, errorMessage)) {
        return false;
    }

    const QStringList required = {
        QStringLiteral("profileName"),
        QStringLiteral("host"),
        QStringLiteral("gdbPort"),
        QStringLiteral("tclPort"),
        QStringLiteral("jtagKhz"),
        QStringLiteral("interfaceConfigPath"),
        QStringLiteral("targetConfigPath"),
        QStringLiteral("targetDebugToolPath"),
        QStringLiteral("ramBaseAddress"),
        QStringLiteral("defaultRunAddress"),
        QStringLiteral("resetStrategy"),
        QStringLiteral("runAfterDownload")
    };

    for (const QString& key : required) {
        if (!object.contains(key)) {
            setError(errorMessage, QStringLiteral("profile 缺少字段: %1").arg(key));
            return false;
        }
    }

    BoardProfile parsed;
    parsed.profileId = item.id;
    parsed.profileName = object.value(QStringLiteral("profileName")).toString();
    parsed.version = item.version;
    parsed.sha256 = item.sha256;
    parsed.host = object.value(QStringLiteral("host")).toString();
    parsed.gdbPort = object.value(QStringLiteral("gdbPort")).toInt();
    parsed.tclPort = object.value(QStringLiteral("tclPort")).toInt();
    parsed.jtagKhz = object.value(QStringLiteral("jtagKhz")).toInt();
    parsed.interfaceConfigPath = object.value(QStringLiteral("interfaceConfigPath")).toString();
    parsed.targetConfigPath = object.value(QStringLiteral("targetConfigPath")).toString();
    parsed.targetDebugToolPath = object.value(QStringLiteral("targetDebugToolPath")).toString();
    parsed.ramBaseAddress = parseHexOrDecimal(object.value(QStringLiteral("ramBaseAddress")).toString());
    parsed.defaultRunAddress = parseHexOrDecimal(object.value(QStringLiteral("defaultRunAddress")).toString());
    parsed.resetStrategy = object.value(QStringLiteral("resetStrategy")).toString();
    parsed.runAfterDownload = object.value(QStringLiteral("runAfterDownload")).toBool();
    *profile = parsed;
    return true;
}

BoardProfileSummary summaryFromProfile(const BoardProfile& profile)
{
    BoardProfileSummary summary;
    summary.profileId = profile.profileId;
    summary.profileName = profile.profileName;
    summary.version = profile.version;
    summary.sha256 = profile.sha256;
    summary.addressSummary = QStringLiteral("RAM %1, entry %2")
        .arg(addressText(profile.ramBaseAddress), addressText(profile.defaultRunAddress));
    summary.runControlSummary = QStringLiteral("reset=%1, runAfterDownload=%2")
        .arg(profile.resetStrategy, profile.runAfterDownload ? QStringLiteral("true") : QStringLiteral("false"));
    return summary;
}

}  // namespace

QString toString(const ResourceStatus status)
{
    QString text;
    switch (status) {
    case ResourceStatus::Enabled:
        text = QStringLiteral("enabled");
        break;
    case ResourceStatus::Placeholder:
        text = QStringLiteral("placeholder");
        break;
    case ResourceStatus::Warning:
        text = QStringLiteral("warning");
        break;
    case ResourceStatus::Missing:
        text = QStringLiteral("missing");
        break;
    case ResourceStatus::Invalid:
        text = QStringLiteral("invalid");
        break;
    case ResourceStatus::Disabled:
        text = QStringLiteral("disabled");
        break;
    default:
        text = QStringLiteral("invalid");
        break;
    }
    return text;
}

bool parseResourceStatus(const QString& text, ResourceStatus* const status)
{
    if (status == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("enabled")) {
        *status = ResourceStatus::Enabled;
    } else if (normalized == QStringLiteral("placeholder")) {
        *status = ResourceStatus::Placeholder;
    } else if (normalized == QStringLiteral("warning")) {
        *status = ResourceStatus::Warning;
    } else if (normalized == QStringLiteral("missing")) {
        *status = ResourceStatus::Missing;
    } else if (normalized == QStringLiteral("invalid")) {
        *status = ResourceStatus::Invalid;
    } else if (normalized == QStringLiteral("disabled")) {
        *status = ResourceStatus::Disabled;
    } else {
        return false;
    }

    return true;
}

QJsonObject toJson(const BoardProfileSummary& summary)
{
    QJsonObject object;
    object.insert(QStringLiteral("profile_id"), summary.profileId);
    object.insert(QStringLiteral("profile_name"), summary.profileName);
    object.insert(QStringLiteral("version"), summary.version);
    object.insert(QStringLiteral("sha256"), summary.sha256);
    object.insert(QStringLiteral("address_summary"), summary.addressSummary);
    object.insert(QStringLiteral("run_control_summary"), summary.runControlSummary);
    return object;
}

QJsonObject toJson(const ResourceSnapshot& snapshot)
{
    QJsonObject object;
    object.insert(QStringLiteral("resource_pack_id"), snapshot.resourcePackId);
    object.insert(QStringLiteral("resource_pack_version"), snapshot.resourcePackVersion);
    object.insert(QStringLiteral("manifest_sha256"), snapshot.manifestSha256);
    object.insert(QStringLiteral("profile_id"), snapshot.profileId);
    object.insert(QStringLiteral("profile_sha256"), snapshot.profileSha256);
    object.insert(QStringLiteral("report_template_id"), snapshot.reportTemplateId);
    object.insert(QStringLiteral("report_template_sha256"), snapshot.reportTemplateSha256);
    object.insert(QStringLiteral("protocol_rule_id"), snapshot.protocolRuleId);
    object.insert(QStringLiteral("protocol_rule_status"), snapshot.protocolRuleStatus);
    return object;
}

ResourceValidationResult ResourceManager::validateResourcePack(const QString& installRootPath)
{
    ResourceValidationResult result;
    resourceRootPath_ = resourceRootFromInstallRoot(installRootPath);
    result.resourceRootPath = resourceRootPath_;

    const QString manifestPath = QDir(resourceRootPath_).filePath(QString::fromLatin1(kManifestName));
    if (!QFileInfo::exists(manifestPath)) {
        result.errors.append(QStringLiteral("manifest 缺失: %1").arg(manifestPath));
        return result;
    }

    result.manifestSha256 = QString::fromLatin1(sha256File(manifestPath, &result.errors));
    manifestSha256_ = result.manifestSha256;

    QJsonObject manifest;
    QString error;
    if (!readJsonObject(manifestPath, &manifest, &error)) {
        result.errors.append(error);
        return result;
    }

    resourcePackId_ = manifest.value(QStringLiteral("resource_pack_id")).toString();
    resourcePackVersion_ = manifest.value(QStringLiteral("resource_pack_version")).toString();

    const QJsonObject defaultsObject = manifest.value(QStringLiteral("defaults")).toObject();
    defaults_.researchProfileId = defaultsObject.value(QStringLiteral("research_profile_id")).toString();
    defaults_.testProfileId = defaultsObject.value(QStringLiteral("test_profile_id")).toString();
    defaults_.researchReportTemplateId =
        defaultsObject.value(QStringLiteral("research_report_template_id")).toString();
    defaults_.testReportTemplateId =
        defaultsObject.value(QStringLiteral("test_report_template_id")).toString();

    boardProfileItems_ = itemListFromJson(manifest.value(QStringLiteral("board_profiles")).toArray());
    debugAdapterItems_ = itemListFromJson(manifest.value(QStringLiteral("debug_adapters")).toArray());
    reportTemplateItems_ = itemListFromJson(manifest.value(QStringLiteral("report_templates")).toArray());
    protocolRuleItems_ = itemListFromJson(manifest.value(QStringLiteral("protocol_rules")).toArray());

    validateItemFiles(resourceRootPath_, boardProfileItems_, false, &result);
    validateItemFiles(resourceRootPath_, debugAdapterItems_, false, &result);
    validateItemFiles(resourceRootPath_, reportTemplateItems_, false, &result);
    validateItemFiles(resourceRootPath_, protocolRuleItems_, true, &result);

    if (resourcePackId_.isEmpty() || resourcePackVersion_.isEmpty()) {
        result.errors.append(QStringLiteral("manifest 缺少资源包标识或版本"));
    }
    if (defaults_.researchProfileId.isEmpty() || defaults_.testProfileId.isEmpty()) {
        result.errors.append(QStringLiteral("manifest 缺少默认 profile"));
    }

    result.success = result.errors.isEmpty();
    return result;
}

ResourceSnapshot ResourceManager::getModeResourceSnapshot(const QString& mode) const
{
    const QString normalized = mode.trimmed().toLower();
    ResourceSnapshot snapshot;
    snapshot.resourcePackId = resourcePackId_;
    snapshot.resourcePackVersion = resourcePackVersion_;
    snapshot.manifestSha256 = manifestSha256_;
    snapshot.profileId = (normalized == QStringLiteral("test"))
        ? defaults_.testProfileId
        : defaults_.researchProfileId;
    snapshot.reportTemplateId = (normalized == QStringLiteral("test"))
        ? defaults_.testReportTemplateId
        : defaults_.researchReportTemplateId;

    ResourceItem profileItem;
    if (findItem(boardProfileItems_, snapshot.profileId, &profileItem)) {
        snapshot.profileSha256 = profileItem.sha256;
    }

    ResourceItem templateItem;
    if (findItem(reportTemplateItems_, snapshot.reportTemplateId, &templateItem)) {
        snapshot.reportTemplateSha256 = templateItem.sha256;
    }

    if (!protocolRuleItems_.isEmpty()) {
        const ResourceItem item = protocolRuleItems_.first();
        snapshot.protocolRuleId = item.id;
        snapshot.protocolRuleStatus = toString(item.status);
    }

    return snapshot;
}

QList<BoardProfileSummary> ResourceManager::listBoardProfiles(const QString& mode) const
{
    QList<BoardProfileSummary> summaries;
    const QString normalized = mode.trimmed().toLower();
    for (const ResourceItem& item : boardProfileItems_) {
        if ((item.status == ResourceStatus::Enabled || item.status == ResourceStatus::Warning) &&
            resourceSupportsMode(item, normalized)) {
            BoardProfile profile;
            QString error;
            if (profileFromItem(resourceRootPath_, item, &profile, &error)) {
                summaries.append(summaryFromProfile(profile));
            }
        }
    }
    return summaries;
}

bool ResourceManager::resolveBoardProfile(
    const QString& profileId,
    BoardProfile* const profile,
    QString* const errorMessage) const
{
    ResourceItem item;
    if (!findItem(boardProfileItems_, profileId, &item)) {
        setError(errorMessage, QStringLiteral("profile 未注册: %1").arg(profileId));
        return false;
    }

    if (item.status != ResourceStatus::Enabled && item.status != ResourceStatus::Warning) {
        setError(errorMessage, QStringLiteral("profile 不可用: %1").arg(profileId));
        return false;
    }

    return profileFromItem(resourceRootPath_, item, profile, errorMessage);
}

bool ResourceManager::resolveReportTemplate(
    const QString& templateId,
    ResourceItem* const item,
    QString* const errorMessage) const
{
    if (!findItem(reportTemplateItems_, templateId, item)) {
        setError(errorMessage, QStringLiteral("报告模板未注册: %1").arg(templateId));
        return false;
    }
    return true;
}

bool ResourceManager::resolveProtocolRule(
    const QString& ruleId,
    ResourceItem* const item,
    QString* const errorMessage) const
{
    if (!findItem(protocolRuleItems_, ruleId, item)) {
        setError(errorMessage, QStringLiteral("协议规则未注册: %1").arg(ruleId));
        return false;
    }
    return true;
}

ManifestDefaults ResourceManager::defaults() const
{
    return defaults_;
}

QString ResourceManager::resourcePackId() const
{
    return resourcePackId_;
}

QString ResourceManager::resourcePackVersion() const
{
    return resourcePackVersion_;
}

}  // namespace lockstep::resources
