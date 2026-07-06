/**********************************************************
* 文件名: splash_widget.h
* 日期: 2026-07-06
* 版本: v1.0
* 更新记录: 初版创建启动加载页占位组件
* 描述: 提供校徽、火箭、卫星轨道等可替换素材的启动页占位
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_SPLASH_WIDGET_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_SPLASH_WIDGET_H_

#include <QWidget>

#include "asset_registry.h"

class QTimer;

namespace lockstep::ui {

class SplashWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SplashWidget(QWidget* parent = nullptr);

    void setAssets(const AssetRegistry& assets);
    void setProgressText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    AssetRegistry assets_;
    QString progressText_;
    QTimer* animationTimer_;
    int animationFrame_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_SPLASH_WIDGET_H_
