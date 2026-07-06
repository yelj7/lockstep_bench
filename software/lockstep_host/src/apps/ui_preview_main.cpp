/**********************************************************
* 文件名: ui_preview_main.cpp
* 日期: 2026-07-06
* 版本: v1.0
* 更新记录: 初版创建 UI 预览程序入口
* 描述: 启动 ui_common 主窗口骨架，便于查看 UI 布局效果
**********************************************************/

#include <QApplication>
#include <QTimer>
#include <QStringList>

#include "main_window_shell.h"
#include "splash_widget.h"
#include "ui_contract.h"
#include "ui_types.h"

namespace {

constexpr int kSplashDurationMs = 3000;

lockstep::ui::UiMode parseMode(const QStringList& arguments)
{
    lockstep::ui::UiMode mode = lockstep::ui::UiMode::Research;

    if (arguments.contains(QStringLiteral("--test"))) {
        mode = lockstep::ui::UiMode::Test;
    }

    return mode;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    const lockstep::ui::UiMode mode = parseMode(application.arguments());
    lockstep::ui::MainWindowShell window;
    lockstep::ui::SplashWidget splash;

    lockstep::ui::UiWorkbenchState state = lockstep::ui::makeDefaultWorkbenchState(mode);
    state.topStatus.taskStatusText = QStringLiteral("任务: UI预览任务");
    state.topStatus.targetStatusText = QStringLiteral("目标: 未连接");
    state.topStatus.programStatusText = QStringLiteral("程序: 未选择");
    window.setWorkbenchState(state);

    QObject::connect(
        &window,
        &lockstep::ui::MainWindowShell::actionRequested,
        &window,
        [&window](const lockstep::ui::UiActionRequest& request) {
            window.appendLog(
                lockstep::ui::LogChannel::Operation,
                lockstep::ui::LogLevel::Info,
                QStringLiteral("UI"),
                QStringLiteral("动作请求: %1 / 页面: %2")
                    .arg(lockstep::ui::toDisplayText(request.action),
                         lockstep::ui::toDisplayText(request.page)));
        });

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

    splash.resize(860, 500);
    splash.show();
    QTimer::singleShot(kSplashDurationMs, &window, [&splash, &window]() {
        splash.close();
        window.show();
    });

    return application.exec();
}
