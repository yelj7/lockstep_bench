/*****************************************************************************
*  @file      ui_contract.h
*  @brief     界面数据合同模块接口
*  Details.   声明界面数据合同模块的公共类型、数据结构和调用接口。
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
    BrowseProgramImage = 10U,
    ProgramImage = 11U,
    VerifyReadback = 12U,
    RunProgram = 13U,
    StopProgram = 14U,
    ShowVerifySummary = 15U,
    ShowRunSummary = 16U,
    BrowseWaveform = 17U,
    ImportWaveform = 18U,
    ClearWaveform = 19U,
    ShowWaveformEmbedded = 20U,
    ShowWaveformDetached = 21U,
    BrowseProtocolWaveform = 22U,
    BrowseProtocolOutput = 23U,
    AnalyzeProtocol = 24U,
    RefreshSerialPorts = 25U,
    ToggleSerialMonitor = 26U,
    ClearSerialOutput = 27U,
    DetachLogWindow = 28U,
    GenerateReport = 29U,
    DeleteTask = 30U
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
