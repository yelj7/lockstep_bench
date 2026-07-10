/*****************************************************************************
*  @file      ui_types.cpp
*  @brief     界面公共类型定义实现
*  Details.   实现界面公共类型定义的业务逻辑、状态转换和文件访问流程。
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

#include "ui_types.h"

namespace lockstep::ui {

QString toDisplayText(const UiMode mode)
{
    QString text;

    switch (mode) {
    case UiMode::Research:
        text = QStringLiteral("研发模式");
        break;
    case UiMode::Test:
        text = QStringLiteral("测试模式");
        break;
    default:
        text = QStringLiteral("未知模式");
        break;
    }

    return text;
}

QString toDisplayText(const NavigationPage page)
{
    QString text;

    switch (page) {
    case NavigationPage::Project:
        text = QStringLiteral("任务管理");
        break;
    case NavigationPage::Connection:
        text = QStringLiteral("目标连接");
        break;
    case NavigationPage::Mode:
        text = QStringLiteral("工作模式");
        break;
    case NavigationPage::RamProgram:
        text = QStringLiteral("程序烧录");
        break;
    case NavigationPage::FaultInjection:
        text = QStringLiteral("错误注入");
        break;
    case NavigationPage::SamplingConfig:
        text = QStringLiteral("采样配置");
        break;
    case NavigationPage::ProgramRun:
        text = QStringLiteral("程序运行");
        break;
    case NavigationPage::Waveform:
        text = QStringLiteral("波形显示");
        break;
    case NavigationPage::Protocol:
        text = QStringLiteral("协议解析");
        break;
    case NavigationPage::Stats:
        text = QStringLiteral("测试报告");
        break;
    default:
        text = QStringLiteral("未知页面");
        break;
    }

    return text;
}

QString toDisplayText(const LogChannel channel)
{
    QString text;

    switch (channel) {
    case LogChannel::Operation:
        text = QStringLiteral("Log");
        break;
    case LogChannel::Serial:
        text = QStringLiteral("串口监控");
        break;
    default:
        text = QStringLiteral("未知日志");
        break;
    }

    return text;
}

QString toDisplayText(const LogLevel level)
{
    QString text;

    switch (level) {
    case LogLevel::Info:
        text = QStringLiteral("INFO");
        break;
    case LogLevel::Warning:
        text = QStringLiteral("WARN");
        break;
    case LogLevel::Error:
        text = QStringLiteral("ERROR");
        break;
    default:
        text = QStringLiteral("UNKNOWN");
        break;
    }

    return text;
}

}  // namespace lockstep::ui
