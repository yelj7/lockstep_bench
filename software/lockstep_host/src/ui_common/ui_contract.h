/**********************************************************
* 文件名: ui_contract.h
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建 UI 解耦接口合同；v2.0 改为源工程克隆工作台最小接口
* 描述: 定义浅色克隆工作台动作请求和占位状态回填接口
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_UI_CONTRACT_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_UI_CONTRACT_H_

#include <QString>
#include <QVariantMap>

#include "global_status.h"
#include "ui_types.h"

namespace lockstep::ui {

enum class UiAction : unsigned char {
    NewTask = 0U,
    SaveTask = 1U,
    LoadTaskToWorkbench = 2U,
    EditTask = 3U,
    SaveTaskEdit = 4U,
    CancelTaskEdit = 5U,
    LoadProfile = 6U,
    SaveProfile = 7U,
    StartDebugService = 8U,
    StopDebugService = 9U,
    BrowseTargetDebugTool = 10U,
    BrowseInterfaceConfig = 11U,
    BrowseTargetConfig = 12U,
    BrowseProgramImage = 13U,
    ProgramImage = 14U,
    VerifyReadback = 15U,
    RunProgram = 16U,
    StopProgram = 17U,
    ShowVerifySummary = 18U,
    ShowRunSummary = 19U,
    BrowseWaveform = 20U,
    ImportWaveform = 21U,
    ClearWaveform = 22U,
    ShowWaveformEmbedded = 23U,
    ShowWaveformDetached = 24U,
    BrowseProtocolWaveform = 25U,
    BrowseProtocolOutput = 26U,
    AnalyzeProtocol = 27U,
    RefreshSerialPorts = 28U,
    ToggleSerialMonitor = 29U,
    ClearSerialOutput = 30U,
    DetachLogWindow = 31U
};

struct UiActionRequest final {
    UiAction action = UiAction::NewTask;
    NavigationPage page = NavigationPage::Project;
    QString objectName;
    QVariantMap parameters;
};

struct UiWorkbenchState final {
    GlobalStatus topStatus;
    QString logText;
    QString serialText;
};

QString toDisplayText(UiAction action);
UiWorkbenchState makeDefaultWorkbenchState(UiMode mode);

}  // namespace lockstep::ui

Q_DECLARE_METATYPE(lockstep::ui::UiActionRequest)
Q_DECLARE_METATYPE(lockstep::ui::UiWorkbenchState)

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_UI_CONTRACT_H_
