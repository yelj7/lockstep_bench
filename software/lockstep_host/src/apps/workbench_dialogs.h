/**********************************************************
* 文件名: workbench_dialogs.h
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增任务加载、配置保存和任务删除确认弹窗合同。
* 描述: 声明工作台高影响操作的统一确认交互。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_
#define LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace lockstep::apps::dialogs {

enum class ConfigSaveDecision : unsigned char {
    Overwrite = 0U,
    SaveAsNewTask = 1U,
    Cancel = 2U
};

bool confirmTaskLoad(QWidget* parent, const QString& targetTaskLabel, const QString& currentTaskSummary);
bool confirmTaskDeletion(QWidget* parent, const QString& taskLabel);
ConfigSaveDecision askConfigSaveDecision(QWidget* parent, const QString& taskLabel);
bool samplingConfigHasMeaningfulChanges(const QByteArray& existingBytes, const QJsonObject& proposed);

}  // namespace lockstep::apps::dialogs

#endif  // LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_
