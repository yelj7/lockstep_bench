/**********************************************************
* 文件名: ui_types.h
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建 UI 公共枚举与文本转换接口；v2.0 改为源工程工作台页面体系
* 描述: 定义浅色克隆工作台模式、页面、顶部状态和日志级别
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_UI_TYPES_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_UI_TYPES_H_

#include <QString>

namespace lockstep::ui {

enum class UiMode : unsigned char {
    Research = 0U,
    Test = 1U
};

enum class NavigationPage : unsigned char {
    Project = 0U,
    Connection = 1U,
    Mode = 2U,
    RamProgram = 3U,
    Waveform = 4U,
    Protocol = 5U,
    Stats = 6U
};

enum class LogChannel : unsigned char {
    Operation = 0U,
    Serial = 1U
};

enum class LogLevel : unsigned char {
    Info = 0U,
    Warning = 1U,
    Error = 2U
};

QString toDisplayText(UiMode mode);
QString toDisplayText(NavigationPage page);
QString toDisplayText(LogChannel channel);
QString toDisplayText(LogLevel level);

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_UI_TYPES_H_
