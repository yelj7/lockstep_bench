/*****************************************************************************
*  @file      ui_theme.h
*  @brief     界面主题样式模块接口
*  Details.   声明界面主题样式模块的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_

#include <QString>

class QWidget;

namespace lockstep::ui {

class UiTheme final {
public:
    UiTheme() = delete;
    ~UiTheme() = delete;

    [[nodiscard]] static QString workbenchStyleSheet(double scale = 1.0);
    static void applyWorkbenchStyle(QWidget* widget, double scale = 1.0);
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_
