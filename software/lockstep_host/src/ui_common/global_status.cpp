/**********************************************************
* 文件名: global_status.cpp
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建全局 UI 状态默认值；v2.0 改为源工程三段式顶栏默认状态
* 描述: 提供浅色克隆工作台初始化使用的默认状态
**********************************************************/

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
