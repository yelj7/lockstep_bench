/**********************************************************
* 文件名: main_window_shell.h
* 日期: 2026-07-14
* 版本: v1.2
* 更新记录: 增加协议束字段和真实波形采样视图模型
* 描述: 声明上位机主窗口框架、报告和协议波形组件。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_

#include <QHash>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QVector>

#include "top_status_bar.h"
#include "ui_contract.h"

class QDialog;
class QGroupBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QComboBox;
class QWidget;

namespace lockstep::ui {

struct ProjectTaskViewItem final {
    QString taskId;
    QString taskName;
    QString description;
    QString statusText;
    QString updatedAtText;
    QString basicInfo;
};

struct TraceFieldViewItem final {
    QString name;
    QString displayName;
    int lsb = -1;
    int width = 1;
    bool errorSignal = false;
};

struct TraceSampleViewItem final {
    qint64 time = 0;
    QString valueHex;
    bool unknown = false;
};

struct TraceGroupViewItem final {
    QString id;
    QString displayName;
    QString status;
    QString reason;
    QVector<TraceFieldViewItem> fields;
    QStringList transactions;
};

class MainWindowShell final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindowShell(QWidget* parent = nullptr);

    void setWorkbenchState(const UiWorkbenchState& state);
    void setTopStatus(const GlobalStatus& status);
    void appendLog(LogChannel channel, LogLevel level, const QString& source, const QString& message);
    void setSerialPlaceholderText(const QString& text);
    [[nodiscard]] QString programImagePath() const;
    void setProgramImagePath(const QString& path);
    void setProjectView(const QString& workspaceName, const QStringList& taskLines);
    void setProjectTasks(
        const QString& workspaceName,
        const QVector<ProjectTaskViewItem>& tasks,
        const QString& selectedTaskId);
    void setTaskDetail(const QString& taskName, const QString& description, const QString& basicInfo);
    void setTaskDetailEditing(bool editing);
    [[nodiscard]] QString selectedProjectTaskId() const;
    [[nodiscard]] QString taskNameText() const;
    [[nodiscard]] QString taskDescriptionText() const;
    void setWorkflowStatusText(const QString& text);
    void setRamSummary(const QString& text, int writeProgressPercent, int readbackProgressPercent);
    void setRunSummary(const QString& text, int runProgressPercent, int stopProgressPercent);
    void setActionButtonText(UiAction action, const QString& text);
    void setActionButtonsEnabled(UiAction action, bool enabled);
    void setReportPageState(const ReportPageViewModel& model);
    void showPage(NavigationPage page);
    void setWaveformTraceView(
        const QString& statusText,
        const QString& pathText,
        const QString& timeRangeText,
        const QVector<TraceGroupViewItem>& groups,
        const QVector<TraceSampleViewItem>& samples,
        const QStringList& keyBehaviors,
        const QStringList& diagnostics);
    void setProtocolAnalysisView(
        const QString& statusText,
        const QString& analysisPath,
        const QStringList& keyBehaviors,
        const QStringList& diagnostics);
    void setConnectionSummary(const QString& profileName, const QString& statusText);
    void setConnectionProfileDetails(
        const QString& profileName,
        const QString& host,
        int tclPort,
        int gdbPort,
        int jtagKhz,
        const QString& ramBaseAddress,
        const QString& resetStrategy,
        const QString& statusText);
    void setConnectionDiagnostics(
        const QString& serviceState,
        const QString& targetState,
        const QString& precheckState,
        const QString& jtagIdcode,
        const QString& debugModule,
        const QString& sbaState,
        const QString& errorText,
        const QString& rawText);
    void setSerialPorts(const QStringList& displayNames, const QStringList& portNames, const QString& statusText);
    void setSerialStatus(const QString& statusText, bool opened);
    [[nodiscard]] QString selectedSerialPortName() const;
    [[nodiscard]] int selectedSerialBaudRate() const;

signals:
    void actionRequested(lockstep::ui::UiActionRequest request);
    void pageChanged(lockstep::ui::NavigationPage page);

private slots:
    void switchWorkbenchPage();
    void clearVisibleLog();
    void toggleLogDetached();
    void showWaveformEmbedded();
    void showWaveformDetached();
    void updateProjectTaskSelectionState();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* createWorkbenchShell();
    QWidget* createTopBar(QWidget* parent);
    QWidget* createSidebar(QWidget* parent);
    QWidget* createPageContainer(QWidget* parent);
    QWidget* createProjectPage();
    QWidget* createConnectionPage();
    QWidget* createModePage();
    QWidget* createRamProgramPage();
    QWidget* createSamplingConfigPage();
    QWidget* createEmptyPage(const QString& title);
    QWidget* createWaveformPage();
    QWidget* createProtocolPage();
    QWidget* createStatsPage();
    QWidget* createDiagnosticsPanel(QWidget* parent);
    QWidget* createLogPanel();
    QWidget* createSerialConfigPanel();
    QWidget* createSerialMonitorPanel();
    QWidget* createControlPanel();
    QWidget* createTodoCard(const QString& title, const QString& body, QWidget* parent);
    QWidget* createMetricCard(const QString& title, const QString& value, const QString& detail, QWidget* parent);
    QPushButton* createNavButton(const QString& pageId, const QString& title, QWidget* parent);
    QPushButton* createActionButton(UiAction action, NavigationPage page, QWidget* parent, bool primary);
    QToolButton* createToolButton(QWidget* parent, const QIcon& icon, const QString& tooltip);
    QWidget* createPathInputRow(QWidget* parent, QLineEdit* edit, QAbstractButton* button);
    void addWorkbenchPage(const QString& pageId, NavigationPage page, QWidget* pageWidget);
    void setActivePage(const QString& pageId);
    void emitAction(UiAction action, NavigationPage page, const QString& objectName);
    void setProgramSummaryPage(bool runSummary);
    void applyWaveformTraceToDisplay(QWidget* widget) const;
    void appendFormattedLog(QPlainTextEdit* view, LogLevel level, const QString& source, const QString& message);
    void applyResponsiveScale();
    QString currentLogText() const;
    QPlainTextEdit* currentLogView() const;
    static NavigationPage pageForId(const QString& pageId);

    TopStatusBar* topStatusBar_;
    QStackedWidget* pageStack_;
    double uiScale_;
    QHash<QString, QPushButton*> navButtons_;
    QHash<QString, NavigationPage> pageIds_;
    QTabWidget* logTabs_;
    QToolButton* logDetachButton_;
    QWidget* diagnosticsPanel_;
    QPushButton* logClearButton_;
    QPlainTextEdit* logEdit_;
    QPlainTextEdit* serialOutputEdit_;
    QDialog* detachedLogDialog_;
    QTabWidget* detachedLogTabs_;
    QPlainTextEdit* detachedLogEdit_;
    QPlainTextEdit* detachedSerialOutputEdit_;
    QWidget* waveformDisplayWidget_;
    QDialog* detachedWaveformDialog_;
    QWidget* detachedWaveformDisplayWidget_;
    QString waveformStatusText_;
    QString waveformPathText_;
    QString waveformTimeRangeText_;
    QVector<TraceGroupViewItem> waveformGroups_;
    QVector<TraceSampleViewItem> waveformSamples_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_
