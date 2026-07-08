/*****************************************************************************
*  @file      ui_contract.cpp
*  @brief     界面数据合同模块实现
*  Details.   实现界面数据合同模块的业务逻辑、状态转换和文件访问流程。
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

#include "ui_contract.h"

namespace lockstep::ui {

QString toDisplayText(const UiAction action)
{
    QString text;

    switch (action) {
    case UiAction::NewTask:
        text = QStringLiteral("新建验证任务");
        break;
    case UiAction::SaveTask:
        text = QStringLiteral("保存为验证任务");
        break;
    case UiAction::LoadTaskToWorkbench:
        text = QStringLiteral("加载到工作台");
        break;
    case UiAction::DeleteTask:
        text = QStringLiteral("删除验证任务");
        break;
    case UiAction::EditTask:
        text = QStringLiteral("修改任务");
        break;
    case UiAction::SaveTaskEdit:
        text = QStringLiteral("保存修改");
        break;
    case UiAction::CancelTaskEdit:
        text = QStringLiteral("放弃修改");
        break;
    case UiAction::StartDebugService:
        text = QStringLiteral("启动片上调试器");
        break;
    case UiAction::StopDebugService:
        text = QStringLiteral("停止片上调试器");
        break;
    case UiAction::BrowseProgramImage:
        text = QStringLiteral("选择程序镜像");
        break;
    case UiAction::ProgramImage:
        text = QStringLiteral("程序烧录");
        break;
    case UiAction::VerifyReadback:
        text = QStringLiteral("回读校验");
        break;
    case UiAction::RunProgram:
        text = QStringLiteral("程序运行");
        break;
    case UiAction::StopProgram:
        text = QStringLiteral("程序中止");
        break;
    case UiAction::ShowVerifySummary:
        text = QStringLiteral("回读校验摘要");
        break;
    case UiAction::ShowRunSummary:
        text = QStringLiteral("运行摘要");
        break;
    case UiAction::BrowseWaveform:
        text = QStringLiteral("读取当前任务波形");
        break;
    case UiAction::ImportWaveform:
        text = QStringLiteral("重新解析");
        break;
    case UiAction::ClearWaveform:
        text = QStringLiteral("刷新视图");
        break;
    case UiAction::ShowWaveformEmbedded:
        text = QStringLiteral("嵌入显示");
        break;
    case UiAction::ShowWaveformDetached:
        text = QStringLiteral("独立窗口");
        break;
    case UiAction::BrowseProtocolWaveform:
        text = QStringLiteral("查看解析结果");
        break;
    case UiAction::BrowseProtocolOutput:
        text = QStringLiteral("查看诊断");
        break;
    case UiAction::AnalyzeProtocol:
        text = QStringLiteral("解析当前 VCD");
        break;
    case UiAction::RefreshSerialPorts:
        text = QStringLiteral("刷新串口");
        break;
    case UiAction::ToggleSerialMonitor:
        text = QStringLiteral("打开串口");
        break;
    case UiAction::ClearSerialOutput:
        text = QStringLiteral("清空输出");
        break;
    case UiAction::DetachLogWindow:
        text = QStringLiteral("弹出独立窗口");
        break;
    case UiAction::GenerateReport:
        text = QStringLiteral("生成报告");
        break;
    default:
        text = QStringLiteral("未知动作");
        break;
    }

    return text;
}

UiWorkbenchState makeDefaultWorkbenchState(const UiMode mode)
{
    UiWorkbenchState state;
    state.topStatus = makeDefaultGlobalStatus(mode);
    state.logText = QStringLiteral("[INFO] UI 预览窗口已启动\n[INFO] 当前为纯 UI 占位，按钮只发出动作请求");
    state.serialText = QStringLiteral("[SERIAL] 串口监控占位：当前未接入真实串口");
    return state;
}

}  // namespace lockstep::ui
