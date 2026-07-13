/**********************************************************
* 文件名: ui_preview_main.cpp
* 日期: 2026-07-13
* 版本: v1.1
* 更新记录: 将 UI 预览入口正式化为上位机产品入口
* 描述: 解析运行模式并启动锁步研发测试系统主窗口
**********************************************************/

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
    QCoreApplication::setApplicationName(QStringLiteral("lockstep_host"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(LOCKSTEP_APP_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("锁步研发测试系统"));
    application.setQuitOnLastWindowClosed(false);

    const lockstep::ui::UiMode mode = parseMode(application.arguments());
    lockstep::ui::MainWindowShell window;
    lockstep::ui::SplashWidget splash;
    lockstep::apps::WorkbenchController controller(&window, mode, &application);

    lockstep::ui::UiWorkbenchState state = lockstep::ui::makeDefaultWorkbenchState(mode);
    state.topStatus.taskStatusText = QStringLiteral("任务: 未选择");
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
            QStringLiteral("上位机主窗口已启动"));

        application.setQuitOnLastWindowClosed(true);
        window.show();
    });

    return application.exec();
}
