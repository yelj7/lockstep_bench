/*****************************************************************************
*  @file      ui_types.h
*  @brief     界面公共类型定义接口
*  Details.   声明界面公共类型定义的公共类型、数据结构和调用接口。
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
    FaultInjection = 4U,
    SamplingConfig = 5U,
    ProgramRun = 6U,
    Waveform = 7U,
    Protocol = 8U,
    Stats = 9U
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
