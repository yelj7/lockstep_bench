/**********************************************************
* 文件名: ui_theme.h
* 日期: 2026-07-06
* 版本: v1.0
* 更新记录: 初版创建 UI 主题接口
* 描述: 集中提供锁步上位机工作台的 Qt Widgets 视觉样式
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_

#include <QString>

class QWidget;

namespace lockstep::ui {

class UiTheme final {
public:
    UiTheme() = delete;
    ~UiTheme() = delete;

    [[nodiscard]] static QString workbenchStyleSheet();
    static void applyWorkbenchStyle(QWidget* widget);
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_UI_THEME_H_
