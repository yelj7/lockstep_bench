/**********************************************************
* 文件名: top_status_bar.h
* 日期: 2026-07-06
* 版本: v2.0
* 更新记录: v1.0 初版创建顶部全局状态栏；v2.0 改为源工程三段式状态栏
* 描述: 显示工作台标题、任务状态、目标状态和程序状态
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_TOP_STATUS_BAR_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_TOP_STATUS_BAR_H_

#include <QLabel>
#include <QWidget>

#include "global_status.h"

namespace lockstep::ui {

class TopStatusBar final : public QWidget {
    Q_OBJECT

public:
    explicit TopStatusBar(QWidget* parent = nullptr);

public slots:
    void setStatus(const GlobalStatus& status);

private:
    void buildLayout();
    [[nodiscard]] QLabel* createValueLabel(const QString& objectName);

    QLabel* taskValue_;
    QLabel* targetValue_;
    QLabel* programValue_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_TOP_STATUS_BAR_H_
