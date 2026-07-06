/**********************************************************
* 文件名: asset_registry.h
* 日期: 2026-07-06
* 版本: v1.0
* 更新记录: 初版创建 UI 素材占位注册表
* 描述: 为校徽、火箭、卫星等可替换素材提供统一引用接口
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_ASSET_REGISTRY_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_ASSET_REGISTRY_H_

#include <QString>

namespace lockstep::ui {

class AssetRegistry final {
public:
    AssetRegistry();

    [[nodiscard]] QString sysuLogoPath() const;
    [[nodiscard]] QString sysuWordmarkPath() const;
    [[nodiscard]] QString rocketLaunchPath() const;
    [[nodiscard]] QString satellitePath() const;
    [[nodiscard]] QString orbitLinesPath() const;
    [[nodiscard]] QString pageWatermarkPath() const;

    void setSysuLogoPath(const QString& path);
    void setSysuWordmarkPath(const QString& path);
    void setRocketLaunchPath(const QString& path);
    void setSatellitePath(const QString& path);
    void setOrbitLinesPath(const QString& path);
    void setPageWatermarkPath(const QString& path);

private:
    QString sysuLogoPath_;
    QString sysuWordmarkPath_;
    QString rocketLaunchPath_;
    QString satellitePath_;
    QString orbitLinesPath_;
    QString pageWatermarkPath_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_ASSET_REGISTRY_H_
