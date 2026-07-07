/*****************************************************************************
*  @file      main_window_shell.h
*  @brief     主窗口框架组件接口
*  Details.   声明主窗口框架组件的公共类型、数据结构和调用接口。
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

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_

#include <QHash>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTabWidget>
#include <QToolButton>
#include <QVector>

#include "top_status_bar.h"
#include "ui_contract.h"

class QDialog;
class QGroupBox;
class QLabel;
class QLineEdit;
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
    void setRamSummary(const QString& text, int progressPercent);
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

signals:
    void actionRequested(lockstep::ui::UiActionRequest request);
    void pageChanged(lockstep::ui::NavigationPage page);

private slots:
    void switchWorkbenchPage();
    void clearVisibleLog();
    void toggleLogDetached();
    void updateProjectTaskSelectionState();

private:
    QWidget* createWorkbenchShell();
    QWidget* createTopBar(QWidget* parent);
    QWidget* createSidebar(QWidget* parent);
    QWidget* createPageContainer(QWidget* parent);
    QWidget* createProjectPage();
    QWidget* createConnectionPage();
    QWidget* createModePage();
    QWidget* createRamProgramPage();
    QWidget* createEmptyPage(const QString& title);
    QWidget* createWaveformPage();
    QWidget* createProtocolPage();
    QWidget* createStatsPage();
    QWidget* createDiagnosticsPanel(QWidget* parent);
    QWidget* createLogPanel();
    QWidget* createSerialConfigPanel();
    QWidget* createSerialMonitorPanel();
    QWidget* createConnectionPanel();
    QWidget* createImagePanel();
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
    void setActionButtonsEnabled(UiAction action, bool enabled);
    void appendFormattedLog(QPlainTextEdit* view, LogLevel level, const QString& source, const QString& message);
    QString currentLogText() const;
    QPlainTextEdit* currentLogView() const;
    static NavigationPage pageForId(const QString& pageId);

    TopStatusBar* topStatusBar_;
    QStackedWidget* pageStack_;
    QHash<QString, QPushButton*> navButtons_;
    QHash<QString, NavigationPage> pageIds_;
    QTabWidget* logTabs_;
    QToolButton* logDetachButton_;
    QPlainTextEdit* logEdit_;
    QPlainTextEdit* serialOutputEdit_;
    QDialog* detachedLogDialog_;
    QPlainTextEdit* detachedLogEdit_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_
