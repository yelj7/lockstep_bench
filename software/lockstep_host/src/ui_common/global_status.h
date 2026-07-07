/*****************************************************************************
*  @file      global_status.h
*  @brief     全局状态显示模型接口
*  Details.   声明全局状态显示模型的公共类型、数据结构和调用接口。
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
