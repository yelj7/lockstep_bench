/**********************************************************
* 文件名: resource_manager.h
* 日期: 2026-07-14
* 版本: 1.0.0
* 更新记录: 移除外部调试服务资源字段
* 描述: 固化资源与模式配置管理模块接口。
**********************************************************/

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
    QString lockstepTraceProfileId;
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
    quint64 ramBaseAddress = 0U;
    quint64 defaultRunAddress = 0U;
    QString resetStrategy;
    bool runAfterDownload = false;
};

struct ResourceSnapshot final {
    QString resourcePackId;
    QString resourcePackVersion;
    QString manifestSha256;
    QString profileId;
    QString profileSha256;
    QString reportTemplateId;
    QString reportTemplateSha256;
    QString debugAdapterId;
    QString debugAdapterStatus;
    QString protocolRuleId;
    QString protocolRuleStatus;
    QString lockstepTraceProfileId;
    QString lockstepTraceProfileStatus;
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

QJsonObject toJson(const ResourceSnapshot& snapshot);

class ResourceManager final {
public:
    ResourceManager() = default;

    ResourceValidationResult validateResourcePack(const QString& installRootPath);
    ResourceSnapshot getModeResourceSnapshot(const QString& mode) const;

    bool resolveBoardProfile(
        const QString& profileId,
        BoardProfile* profile,
        QString* errorMessage = nullptr) const;

    ManifestDefaults defaults() const;

private:
    QString resourceRootPath_;
    QString resourcePackId_;
    QString resourcePackVersion_;
    QString manifestSha256_;
    ManifestDefaults defaults_;
    QList<ResourceItem> boardProfileItems_;
    QList<ResourceItem> reportTemplateItems_;
    QList<ResourceItem> protocolRuleItems_;
    QList<ResourceItem> lockstepTraceProfileItems_;
    QList<ResourceItem> debugAdapterItems_;
};

}  // namespace lockstep::resources

#endif  // LOCKSTEP_HOST_SRC_RESOURCES_RESOURCE_MANAGER_H_
