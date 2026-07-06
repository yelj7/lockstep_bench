/**********************************************************
* 文件名: main_window_shell.h
* 日期: 2026-07-06
* 版本: v3.0
* 更新记录: v1.0 初版创建主窗口 UI 骨架；v2.0 创建解耦接口；v3.0 从零复刻源工程工作台结构
* 描述: 以浅色方式复刻源工程 MainWindow 的四区工作台布局并剥离业务依赖
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_MAIN_WINDOW_SHELL_H_

#include <QHash>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>

#include "top_status_bar.h"
#include "ui_contract.h"

class QDialog;
class QGroupBox;
class QLabel;
class QLineEdit;
class QWidget;

namespace lockstep::ui {

class MainWindowShell final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindowShell(QWidget* parent = nullptr);

    void setWorkbenchState(const UiWorkbenchState& state);
    void setTopStatus(const GlobalStatus& status);
    void appendLog(LogChannel channel, LogLevel level, const QString& source, const QString& message);
    void setSerialPlaceholderText(const QString& text);

signals:
    void actionRequested(lockstep::ui::UiActionRequest request);
    void pageChanged(lockstep::ui::NavigationPage page);

private slots:
    void switchWorkbenchPage();
    void clearVisibleLog();
    void toggleLogDetached();

private:
    QWidget* createWorkbenchShell();
    QWidget* createTopBar(QWidget* parent);
    QWidget* createSidebar(QWidget* parent);
    QWidget* createPageContainer(QWidget* parent);
    QWidget* createProjectPage();
    QWidget* createConnectionPage();
    QWidget* createModePage();
    QWidget* createRamProgramPage();
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
