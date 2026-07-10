/*****************************************************************************
*  @file      top_status_bar.h
*  @brief     顶部状态栏组件接口
*  Details.   声明顶部状态栏组件的公共类型、数据结构和调用接口。
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

    void applyScale(double scale);

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
