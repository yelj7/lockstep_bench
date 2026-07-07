/*****************************************************************************
*  @file      splash_widget.h
*  @brief     启动页组件接口
*  Details.   声明启动页组件的公共类型、数据结构和调用接口。
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
