/**********************************************************
* 文件名: workbench_dialogs.cpp
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增任务加载、配置保存和任务删除确认弹窗实现。
* 描述: 提供键盘可取消且默认保护当前数据的模态确认框。
**********************************************************/

#include "workbench_dialogs.h"

#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPushButton>

#include "ui_theme.h"

namespace lockstep::apps::dialogs {
namespace {

QPushButton* addButton(QMessageBox* const dialog, const QString& text, const QString& objectName,
                       const QMessageBox::ButtonRole role)
{
    QPushButton* const button = dialog->addButton(text, role);
    button->setObjectName(objectName);
    return button;
}

void makeDefaultCancel(QMessageBox* const dialog, QPushButton* const cancelButton)
{
    cancelButton->setProperty("primary_button", true);
    cancelButton->setProperty("primaryButton", true);
    cancelButton->setDefault(true);
    cancelButton->setAutoDefault(true);
    dialog->setDefaultButton(cancelButton);
    dialog->setEscapeButton(cancelButton);
    lockstep::ui::UiTheme::applyWorkbenchStyle(dialog);
}

}  // namespace

bool confirmTaskLoad(QWidget* const parent, const QString& targetTaskLabel,
                     const QString& currentTaskSummary)
{
    QMessageBox dialog(parent);
    dialog.setObjectName(QStringLiteral("task_load_confirmation_dialog"));
    dialog.setWindowTitle(QStringLiteral("加载到工作台"));
    dialog.setIcon(QMessageBox::Question);
    dialog.setText(QStringLiteral("确认加载验证任务“%1”？").arg(targetTaskLabel));
    dialog.setInformativeText(
        QStringLiteral("加载新任务会替换当前工作台配置。\n%1").arg(currentTaskSummary));
    QPushButton* const cancelButton = addButton(
        &dialog, QStringLiteral("取消"), QStringLiteral("task_load_cancel_button"), QMessageBox::RejectRole);
    QPushButton* const loadButton = addButton(
        &dialog, QStringLiteral("加载"), QStringLiteral("task_load_confirm_button"), QMessageBox::AcceptRole);
    loadButton->setAutoDefault(false);
    makeDefaultCancel(&dialog, cancelButton);
    dialog.exec();
    return dialog.clickedButton() == loadButton;
}

bool confirmTaskDeletion(QWidget* const parent, const QString& taskLabel)
{
    QMessageBox dialog(parent);
    dialog.setObjectName(QStringLiteral("task_delete_confirmation_dialog"));
    dialog.setWindowTitle(QStringLiteral("删除验证任务"));
    dialog.setIcon(QMessageBox::Warning);
    dialog.setText(QStringLiteral("确认删除验证任务“%1”？").arg(taskLabel));
    dialog.setInformativeText(QStringLiteral("任务目录中的输入、证据、日志和报告会全部删除，且无法撤销。"));
    QPushButton* const cancelButton = addButton(
        &dialog, QStringLiteral("取消"), QStringLiteral("task_delete_cancel_button"), QMessageBox::RejectRole);
    QPushButton* const deleteButton = addButton(
        &dialog, QStringLiteral("删除"), QStringLiteral("task_delete_confirm_button"), QMessageBox::DestructiveRole);
    deleteButton->setAutoDefault(false);
    makeDefaultCancel(&dialog, cancelButton);
    dialog.exec();
    return dialog.clickedButton() == deleteButton;
}

ConfigSaveDecision askConfigSaveDecision(QWidget* const parent, const QString& taskLabel)
{
    QMessageBox dialog(parent);
    dialog.setObjectName(QStringLiteral("sampling_config_save_decision_dialog"));
    dialog.setWindowTitle(QStringLiteral("保存采样配置"));
    dialog.setIcon(QMessageBox::Question);
    dialog.setText(QStringLiteral("采样配置已发生改动。"));
    dialog.setInformativeText(
        QStringLiteral("请选择覆盖当前任务“%1”，或将改动另存为新任务。\n"
                       "覆盖会清空与旧配置关联的日志、证据、波形和报告。")
            .arg(taskLabel));
    QPushButton* const overwriteButton = addButton(
        &dialog, QStringLiteral("覆盖当前任务"), QStringLiteral("sampling_config_overwrite_button"),
        QMessageBox::AcceptRole);
    QPushButton* const saveAsButton = addButton(
        &dialog, QStringLiteral("另存为新任务"), QStringLiteral("sampling_config_save_as_button"),
        QMessageBox::ActionRole);
    QPushButton* const cancelButton = addButton(
        &dialog, QStringLiteral("取消"), QStringLiteral("sampling_config_cancel_button"),
        QMessageBox::RejectRole);
    overwriteButton->setAutoDefault(false);
    saveAsButton->setAutoDefault(false);
    makeDefaultCancel(&dialog, cancelButton);
    dialog.exec();
    if (dialog.clickedButton() == overwriteButton) return ConfigSaveDecision::Overwrite;
    if (dialog.clickedButton() == saveAsButton) return ConfigSaveDecision::SaveAsNewTask;
    return ConfigSaveDecision::Cancel;
}

bool samplingConfigHasMeaningfulChanges(const QByteArray& existingBytes, const QJsonObject& proposed)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(existingBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return true;
    QJsonObject existing = document.object();
    QJsonObject normalizedProposed = proposed;
    existing.remove(QStringLiteral("created_at"));
    normalizedProposed.remove(QStringLiteral("created_at"));
    return existing != normalizedProposed;
}

}  // namespace lockstep::apps::dialogs
