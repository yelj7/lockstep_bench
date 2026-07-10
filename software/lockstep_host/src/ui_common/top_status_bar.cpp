/*****************************************************************************
*  @file      top_status_bar.cpp
*  @brief     顶部状态栏组件实现
*  Details.   实现顶部状态栏组件的业务逻辑、状态转换和文件访问流程。
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

#include "top_status_bar.h"

#include <QHBoxLayout>
#include <QSizePolicy>

#include <QtGlobal>

namespace lockstep::ui {

namespace {

constexpr int kTopBarHeight = 40;

int scaled(const int value, const double scale)
{
    return qMax(1, qRound(static_cast<double>(value) * qBound(1.0, scale, 1.35)));
}

}  // namespace

TopStatusBar::TopStatusBar(QWidget* const parent)
    : QWidget(parent),
      taskValue_(nullptr),
      targetValue_(nullptr),
      programValue_(nullptr)
{
    buildLayout();
    setStatus(makeDefaultGlobalStatus(UiMode::Test));
}

void TopStatusBar::setStatus(const GlobalStatus& status)
{
    taskValue_->setText(status.taskStatusText);
    targetValue_->setText(status.targetStatusText);
    programValue_->setText(status.programStatusText);
}

void TopStatusBar::applyScale(const double scale)
{
    setFixedHeight(scaled(kTopBarHeight, scale));

    QHBoxLayout* const barLayout = qobject_cast<QHBoxLayout*>(layout());
    if (barLayout != nullptr) {
        barLayout->setContentsMargins(
            scaled(16, scale),
            scaled(5, scale),
            scaled(16, scale),
            scaled(5, scale));
        barLayout->setSpacing(scaled(8, scale));
    }
}

void TopStatusBar::buildLayout()
{
    setObjectName(QStringLiteral("top_bar"));
    setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout* const layout = new QHBoxLayout(this);

    QLabel* const title = new QLabel(QStringLiteral("Lockstep Workbench  |  锁步验证工作台"), this);
    title->setObjectName(QStringLiteral("workbench_title"));
    title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    layout->addWidget(title);
    layout->addStretch(1);

    taskValue_ = createValueLabel(QStringLiteral("top_task_status_label"));
    targetValue_ = createValueLabel(QStringLiteral("top_target_status_label"));
    programValue_ = createValueLabel(QStringLiteral("top_program_status_label"));
    layout->addWidget(taskValue_);
    layout->addWidget(targetValue_);
    layout->addWidget(programValue_);
    applyScale(1.0);
}

QLabel* TopStatusBar::createValueLabel(const QString& objectName)
{
    QLabel* const label = new QLabel(this);
    label->setObjectName(objectName);
    label->setProperty("statusPill", true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return label;
}

}  // namespace lockstep::ui
