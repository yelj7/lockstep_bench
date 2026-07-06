/**********************************************************
* 文件名: global_status.h
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建全局 UI 状态数据结构；v2.0 改为源工程三段式顶栏状态
* 描述: 定义浅色克隆工作台顶部状态栏数据结构
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_GLOBAL_STATUS_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_GLOBAL_STATUS_H_

#include <QString>

#include "ui_types.h"

namespace lockstep::ui {

struct GlobalStatus final {
    UiMode mode;
    QString taskStatusText;
    QString targetStatusText;
    QString programStatusText;
};

GlobalStatus makeDefaultGlobalStatus(UiMode mode);

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_GLOBAL_STATUS_H_
