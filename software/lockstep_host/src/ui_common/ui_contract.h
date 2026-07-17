/**********************************************************
* 文件名: ui_contract.h
* 日期: 2026-07-14
* 版本: v1.1
* 更新记录: 增加测试报告页面模型和报告文件操作
* 描述: 声明上位机界面动作、工作台状态和报告页数据合同
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_UI_CONTRACT_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_UI_CONTRACT_H_

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

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
    DeleteTask = 30U,
    SendSerialData = 31U,
    SaveSamplingConfig = 32U,
    SendSamplingConfig = 33U,
    OpenReportHtml = 34U,
    OpenReportDirectory = 35U,
    CopyReportPath = 36U,
    OpenReportArtifact = 37U,
    NavigateToReportSource = 38U,
    StartSamplingCapture = 39U
};

enum class ReportLifecycleState : unsigned char {
    NoTask = 0U,
    NotGenerated = 1U,
    Generating = 2U,
    Current = 3U,
    Stale = 4U,
    GenerationError = 5U,
    LoadError = 6U
};

struct ReportEvidenceViewItem final {
    QString id;
    QString displayName;
    QString state;
    QString stateText;
    QString summary;
    QString recordedAt;
    QString relativePath;
    QString details;
    QStringList errorIds;
};

struct ReportDiagnosticViewItem final {
    QString id;
    QString code;
    QString severity;
    QString source;
    QString message;
    QString suggestion;
    QString occurredAt;
    QString targetPage;
};

struct ReportOptionalViewItem final {
    QString id;
    QString displayName;
    QString state;
    QString stateText;
    QString summary;
    QString recordedAt;
    QString relativePath;
};

struct ReportPageViewModel final {
    ReportLifecycleState lifecycle = ReportLifecycleState::NoTask;
    QString lifecycleText;
    QString taskName;
    QString taskId;
    QString modeText;
    QString conclusion;
    QString conclusionText;
    QString persistedConclusion;
    QString persistedConclusionText;
    QString primaryReason;
    QString reportId;
    QString generatedAt;
    QString reportRelativePath;
    QString htmlRelativePath;
    QString schemaVersion;
    QString inputDigest;
    QString reportSha256;
    int revision = 0;
    int warningCount = 0;
    int blockingCount = 0;
    bool hasTask = false;
    bool hasPersistedReport = false;
    bool stale = false;
    bool generating = false;
    QString errorMessage;
    QVector<ReportEvidenceViewItem> requiredEvidence;
    QVector<ReportDiagnosticViewItem> diagnostics;
    QVector<ReportOptionalViewItem> optionalRecords;
    QString archiveDetails;
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
