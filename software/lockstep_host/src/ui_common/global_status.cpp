/*****************************************************************************
*  @file      global_status.cpp
*  @brief     全局状态显示模型实现
*  Details.   实现全局状态显示模型的业务逻辑、状态转换和文件访问流程。
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

#include "global_status.h"

namespace lockstep::ui {

GlobalStatus makeDefaultGlobalStatus(const UiMode mode)
{
    GlobalStatus status{
        mode,
        QStringLiteral("任务: 未加载"),
        QStringLiteral("目标: 未连接"),
        QStringLiteral("程序: 未选择")
    };

    return status;
}

}  // namespace lockstep::ui
