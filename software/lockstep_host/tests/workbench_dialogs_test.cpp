/**********************************************************
* 文件名: workbench_dialogs_test.cpp
* 日期: 2026-07-17
* 版本: 1.1
* 更新记录: 移除已删除的采样配置保存决策弹窗测试。
* 描述: 使用 offscreen Qt 事件循环验证按钮决策与默认取消行为。
**********************************************************/

#include <QApplication>
#include <QMessageBox>
#include <QJsonDocument>
#include <QPushButton>
#include <QTimer>

#include <iostream>

#include "workbench_dialogs.h"

namespace {

bool defaultCancelObserved = false;

void clickLater(const QString& buttonName, const QString& cancelButtonName)
{
    QTimer::singleShot(0, [buttonName, cancelButtonName]() {
        auto* const dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        QPushButton* const cancel = dialog->findChild<QPushButton*>(cancelButtonName);
        defaultCancelObserved = cancel != nullptr && dialog->defaultButton() == cancel &&
            dialog->escapeButton() == cancel;
        if (QPushButton* const button = dialog->findChild<QPushButton*>(buttonName)) button->click();
    });
}

bool expect(const bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    using namespace lockstep::apps::dialogs;

    QJsonObject existingConfig;
    existingConfig.insert(QStringLiteral("trigger_addr"), QStringLiteral("0x1000"));
    existingConfig.insert(QStringLiteral("created_at"), QStringLiteral("old"));
    QJsonObject sameConfig = existingConfig;
    sameConfig.insert(QStringLiteral("created_at"), QStringLiteral("new"));
    QJsonObject changedConfig = sameConfig;
    changedConfig.insert(QStringLiteral("trigger_addr"), QStringLiteral("0x2000"));
    const QByteArray existingBytes = QJsonDocument(existingConfig).toJson();
    if (!expect(!samplingConfigHasMeaningfulChanges(existingBytes, sameConfig),
                "timestamp-only change does not prompt") ||
        !expect(samplingConfigHasMeaningfulChanges(existingBytes, changedConfig),
                "meaningful sampling change prompts")) return 1;

    clickLater(QStringLiteral("task_load_cancel_button"), QStringLiteral("task_load_cancel_button"));
    if (!expect(!confirmTaskLoad(nullptr, QStringLiteral("目标任务"), QStringLiteral("当前任务")) &&
                    defaultCancelObserved,
                "task load defaults to cancel")) return 1;
    clickLater(QStringLiteral("task_load_confirm_button"), QStringLiteral("task_load_cancel_button"));
    if (!expect(confirmTaskLoad(nullptr, QStringLiteral("目标任务"), QStringLiteral("当前任务")),
                "task load confirm returns true")) return 1;

    clickLater(QStringLiteral("task_delete_cancel_button"), QStringLiteral("task_delete_cancel_button"));
    if (!expect(!confirmTaskDeletion(nullptr, QStringLiteral("待删除任务")) && defaultCancelObserved,
                "task deletion defaults to cancel")) return 1;
    clickLater(QStringLiteral("task_delete_confirm_button"), QStringLiteral("task_delete_cancel_button"));
    if (!expect(confirmTaskDeletion(nullptr, QStringLiteral("待删除任务")),
                "task deletion confirm returns true")) return 1;

    return 0;
}
