/*****************************************************************************
*  @file      asset_registry.h
*  @brief     界面资源注册模块接口
*  Details.   声明界面资源注册模块的公共类型、数据结构和调用接口。
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
