/*****************************************************************************
*  @file      asset_registry.cpp
*  @brief     界面资源注册模块实现
*  Details.   实现界面资源注册模块的业务逻辑、状态转换和文件访问流程。
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

#include "asset_registry.h"

namespace lockstep::ui {

AssetRegistry::AssetRegistry() = default;

QString AssetRegistry::sysuLogoPath() const
{
    return sysuLogoPath_;
}

QString AssetRegistry::sysuWordmarkPath() const
{
    return sysuWordmarkPath_;
}

QString AssetRegistry::rocketLaunchPath() const
{
    return rocketLaunchPath_;
}

QString AssetRegistry::satellitePath() const
{
    return satellitePath_;
}

QString AssetRegistry::orbitLinesPath() const
{
    return orbitLinesPath_;
}

QString AssetRegistry::pageWatermarkPath() const
{
    return pageWatermarkPath_;
}

void AssetRegistry::setSysuLogoPath(const QString& path)
{
    sysuLogoPath_ = path;
}

void AssetRegistry::setSysuWordmarkPath(const QString& path)
{
    sysuWordmarkPath_ = path;
}

void AssetRegistry::setRocketLaunchPath(const QString& path)
{
    rocketLaunchPath_ = path;
}

void AssetRegistry::setSatellitePath(const QString& path)
{
    satellitePath_ = path;
}

void AssetRegistry::setOrbitLinesPath(const QString& path)
{
    orbitLinesPath_ = path;
}

void AssetRegistry::setPageWatermarkPath(const QString& path)
{
    pageWatermarkPath_ = path;
}

}  // namespace lockstep::ui
