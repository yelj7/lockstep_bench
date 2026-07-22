/**********************************************************
* 文件名: workbench_dialogs.h
* 日期: 2026-07-17
* 版本: 1.1
* 更新记录: 移除采样配置保存决策，配置下发固定覆盖当前任务。
* 描述: 声明工作台高影响操作的统一确认交互。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_
#define LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace lockstep::apps::dialogs {

bool confirmTaskLoad(QWidget* parent, const QString& targetTaskLabel, const QString& currentTaskSummary);
bool confirmTaskDeletion(QWidget* parent, const QString& taskLabel);
bool samplingConfigHasMeaningfulChanges(const QByteArray& existingBytes, const QJsonObject& proposed);

}  // namespace lockstep::apps::dialogs

#endif  // LOCKSTEP_HOST_SRC_APPS_WORKBENCH_DIALOGS_H_
