/*****************************************************************************
*  @file      ui_preview_main.cpp
*  @brief     UI预览程序入口实现
*  Details.   实现UI预览程序启动、模式解析和主窗口创建流程。
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

#include <QApplication>
#include <QStringList>
#include <QTimer>

#include "main_window_shell.h"
#include "splash_widget.h"
#include "ui_contract.h"
#include "ui_types.h"
#include "workbench_controller.h"
#include "workspace_selection_dialog.h"

namespace {

constexpr int kSplashDurationMs = 1000;

lockstep::ui::UiMode parseMode(const QStringList& arguments)
{
    lockstep::ui::UiMode mode = lockstep::ui::UiMode::Test;

    if (arguments.contains(QStringLiteral("--research"))) {
        mode = lockstep::ui::UiMode::Research;
    }

    return mode;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);

    const lockstep::ui::UiMode mode = parseMode(application.arguments());
    lockstep::ui::MainWindowShell window;
    lockstep::ui::SplashWidget splash;
    lockstep::apps::WorkbenchController controller(&window, mode, &application);

    lockstep::ui::UiWorkbenchState state = lockstep::ui::makeDefaultWorkbenchState(mode);
    state.topStatus.taskStatusText = QStringLiteral("任务: UI预览任务");
    state.topStatus.targetStatusText = QStringLiteral("目标: 未连接");
    state.topStatus.programStatusText = QStringLiteral("程序: 未选择");
    window.setWorkbenchState(state);

    splash.resize(860, 500);
    splash.show();
    QTimer::singleShot(kSplashDurationMs, &application, [&application, &splash, &window, &controller]() {
        splash.close();
        QString workspaceRoot;
        if (!lockstep::ui::WorkspaceSelectionDialog::selectWorkspaceRoot(nullptr, &workspaceRoot)) {
            application.quit();
            return;
        }

        if (!controller.initialize(workspaceRoot)) {
            application.quit();
            return;
        }

        window.appendLog(
            lockstep::ui::LogChannel::Operation,
            lockstep::ui::LogLevel::Info,
            QStringLiteral("UI"),
            QStringLiteral("UI 预览窗口已启动"));
        window.appendLog(
            lockstep::ui::LogChannel::Serial,
            lockstep::ui::LogLevel::Info,
            QStringLiteral("SERIAL"),
            QStringLiteral("串口监控占位：等待板卡连接"));

        application.setQuitOnLastWindowClosed(true);
        window.show();
    });

    return application.exec();
}
