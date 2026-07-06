/**********************************************************
* 文件名: ui_types.cpp
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建 UI 公共枚举文本转换实现；v2.0 改为源工程工作台页面文本
* 描述: 为浅色克隆工作台公共类型提供集中式中文显示文本
**********************************************************/

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
        text = QStringLiteral("项目与任务");
        break;
    case NavigationPage::Connection:
        text = QStringLiteral("目标连接");
        break;
    case NavigationPage::Mode:
        text = QStringLiteral("工作模式");
        break;
    case NavigationPage::RamProgram:
        text = QStringLiteral("RAM程序烧录");
        break;
    case NavigationPage::Waveform:
        text = QStringLiteral("波形分析仪");
        break;
    case NavigationPage::Protocol:
        text = QStringLiteral("协议解析");
        break;
    case NavigationPage::Stats:
        text = QStringLiteral("性能统计");
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
