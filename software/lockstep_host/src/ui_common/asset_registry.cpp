/**********************************************************
* 文件名: asset_registry.cpp
* 日期: 2026-07-06
* 版本: v1.0
* 更新记录: 初版创建 UI 素材占位注册表实现
* 描述: 保存可替换素材路径；默认使用空路径表示占位绘制
**********************************************************/

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
