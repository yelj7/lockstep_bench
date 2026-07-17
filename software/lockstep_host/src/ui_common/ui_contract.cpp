/**********************************************************
* 文件名: ui_contract.cpp
* 日期: 2026-07-14
* 版本: v1.1
* 更新记录: 增加测试报告文件操作显示文本
* 描述: 实现界面动作显示文本和默认工作台状态
**********************************************************/

#include "ui_contract.h"

namespace lockstep::ui {

QString toDisplayText(const UiAction action)
{
    switch (action) {
    case UiAction::NewTask: return QStringLiteral("新建验证任务");
    case UiAction::SaveTask: return QStringLiteral("保存验证任务");
    case UiAction::LoadTaskToWorkbench: return QStringLiteral("加载到工作台");
    case UiAction::DeleteTask: return QStringLiteral("删除验证任务");
    case UiAction::EditTask: return QStringLiteral("修改任务");
    case UiAction::SaveTaskEdit: return QStringLiteral("保存修改");
    case UiAction::CancelTaskEdit: return QStringLiteral("放弃修改");
    case UiAction::StartDebugService: return QStringLiteral("启动片上调试器");
    case UiAction::StopDebugService: return QStringLiteral("停止片上调试器");
    case UiAction::BrowseProgramImage: return QStringLiteral("选择程序镜像");
    case UiAction::ProgramImage: return QStringLiteral("程序烧录");
    case UiAction::VerifyReadback: return QStringLiteral("回读校验");
    case UiAction::RunProgram: return QStringLiteral("程序运行");
    case UiAction::StopProgram: return QStringLiteral("程序终止");
    case UiAction::ShowVerifySummary: return QStringLiteral("回读校验摘要");
    case UiAction::ShowRunSummary: return QStringLiteral("运行摘要");
    case UiAction::BrowseWaveform: return QStringLiteral("导入 VCD");
    case UiAction::ImportWaveform: return QStringLiteral("重新解析");
    case UiAction::ClearWaveform: return QStringLiteral("刷新视图");
    case UiAction::ShowWaveformEmbedded: return QStringLiteral("嵌入显示");
    case UiAction::ShowWaveformDetached: return QStringLiteral("独立窗口");
    case UiAction::BrowseProtocolWaveform: return QStringLiteral("查看解析结果");
    case UiAction::BrowseProtocolOutput: return QStringLiteral("查看诊断");
    case UiAction::AnalyzeProtocol: return QStringLiteral("解析当前 VCD");
    case UiAction::RefreshSerialPorts: return QStringLiteral("刷新串口");
    case UiAction::ToggleSerialMonitor: return QStringLiteral("打开串口");
    case UiAction::ClearSerialOutput: return QStringLiteral("清空输出");
    case UiAction::DetachLogWindow: return QStringLiteral("弹出独立窗口");
    case UiAction::GenerateReport: return QStringLiteral("生成报告");
    case UiAction::SendSerialData: return QStringLiteral("发送串口数据");
    case UiAction::SaveSamplingConfig: return QStringLiteral("保存采样配置");
    case UiAction::SendSamplingConfig: return QStringLiteral("下发采样配置");
    case UiAction::StartSamplingCapture: return QStringLiteral("开始采集");
    case UiAction::OpenReportHtml: return QStringLiteral("打开报告");
    case UiAction::OpenReportDirectory: return QStringLiteral("打开报告目录");
    case UiAction::CopyReportPath: return QStringLiteral("复制报告路径");
    case UiAction::OpenReportArtifact: return QStringLiteral("查看记录");
    case UiAction::NavigateToReportSource: return QStringLiteral("转到来源页面");
    case UiAction::LoadProfile: return QStringLiteral("加载资源配置");
    case UiAction::SaveProfile: return QStringLiteral("保存资源配置");
    default: return QStringLiteral("未知动作");
    }
}

UiWorkbenchState makeDefaultWorkbenchState(const UiMode mode)
{
    UiWorkbenchState state;
    state.topStatus = makeDefaultGlobalStatus(mode);
    state.logText = QStringLiteral("[INFO] UI 预览窗口已启动\n[INFO] 等待加载工作区任务");
    return state;
}

}  // namespace lockstep::ui
