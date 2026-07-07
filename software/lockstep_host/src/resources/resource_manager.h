/*****************************************************************************
*  @file      resource_manager.h
*  @brief     固化资源与模式配置管理模块接口
*  Details.   声明固化资源与模式配置管理模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_RESOURCES_RESOURCE_MANAGER_H_
#define LOCKSTEP_HOST_SRC_RESOURCES_RESOURCE_MANAGER_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::resources {

enum class ResourceStatus : unsigned char {
    Enabled = 0U,
    Placeholder = 1U,
    Warning = 2U,
    Missing = 3U,
    Invalid = 4U,
    Disabled = 5U
};

struct ResourceItem final {
    QString id;
    QString name;
    QString version;
    QString mode;
    QStringList modes;
    QString relativePath;
    QString sha256;
    ResourceStatus status = ResourceStatus::Invalid;
};

struct ManifestDefaults final {
    QString researchProfileId;
    QString testProfileId;
    QString researchReportTemplateId;
    QString testReportTemplateId;
};

struct BoardProfile final {
    QString profileId;
    QString profileName;
    QString version;
    QString sha256;
    QString host;
    int gdbPort = 0;
    int tclPort = 0;
    int jtagKhz = 0;
    QString interfaceConfigPath;
    QString targetConfigPath;
    QString targetDebugToolPath;
    quint64 ramBaseAddress = 0U;
    quint64 defaultRunAddress = 0U;
    QString resetStrategy;
    bool runAfterDownload = false;
};

struct BoardProfileSummary final {
    QString profileId;
    QString profileName;
    QString version;
    QString sha256;
    QString addressSummary;
    QString runControlSummary;
};

struct ResourceSnapshot final {
    QString resourcePackId;
    QString resourcePackVersion;
    QString manifestSha256;
    QString profileId;
    QString profileSha256;
    QString reportTemplateId;
    QString reportTemplateSha256;
    QString protocolRuleId;
    QString protocolRuleStatus;
};

struct ResourceValidationResult final {
    bool success = false;
    QString resourceRootPath;
    QString manifestSha256;
    QStringList errors;
    QStringList warnings;
};

QString toString(ResourceStatus status);
bool parseResourceStatus(const QString& text, ResourceStatus* status);

QJsonObject toJson(const BoardProfileSummary& summary);
QJsonObject toJson(const ResourceSnapshot& snapshot);

class ResourceManager final {
public:
    ResourceManager() = default;

    ResourceValidationResult validateResourcePack(const QString& installRootPath);
    ResourceSnapshot getModeResourceSnapshot(const QString& mode) const;

    QList<BoardProfileSummary> listBoardProfiles(const QString& mode) const;
    bool resolveBoardProfile(
        const QString& profileId,
        BoardProfile* profile,
        QString* errorMessage = nullptr) const;

    bool resolveReportTemplate(
        const QString& templateId,
        ResourceItem* item,
        QString* errorMessage = nullptr) const;

    bool resolveProtocolRule(
        const QString& ruleId,
        ResourceItem* item,
        QString* errorMessage = nullptr) const;

    ManifestDefaults defaults() const;
    QString resourcePackId() const;
    QString resourcePackVersion() const;

private:
    QString resourceRootPath_;
    QString resourcePackId_;
    QString resourcePackVersion_;
    QString manifestSha256_;
    ManifestDefaults defaults_;
    QList<ResourceItem> boardProfileItems_;
    QList<ResourceItem> reportTemplateItems_;
    QList<ResourceItem> protocolRuleItems_;
    QList<ResourceItem> debugAdapterItems_;
};

}  // namespace lockstep::resources

#endif  // LOCKSTEP_HOST_SRC_RESOURCES_RESOURCE_MANAGER_H_
