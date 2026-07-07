/*****************************************************************************
*  @file      splash_widget.cpp
*  @brief     启动页组件实现
*  Details.   实现启动页组件的业务逻辑、状态转换和文件访问流程。
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

#include "splash_widget.h"

#include <QPainter>
#include <QPixmap>
#include <QTimer>

namespace lockstep::ui {

namespace {

constexpr int kMinimumWidth = 720;
constexpr int kMinimumHeight = 420;

}  // namespace

SplashWidget::SplashWidget(QWidget* const parent)
    : QWidget(parent),
      assets_(),
      progressText_(QStringLiteral("加载界面素材 / 检查配置 / 准备界面")),
      animationTimer_(new QTimer(this)),
      animationFrame_(0)
{
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setAutoFillBackground(false);
    connect(animationTimer_, &QTimer::timeout, this, [this]() {
        animationFrame_ = (animationFrame_ + 1) % 120;
        update();
    });
    animationTimer_->start(50);
}

void SplashWidget::setAssets(const AssetRegistry& assets)
{
    assets_ = assets;
    update();
}

void SplashWidget::setProgressText(const QString& text)
{
    progressText_ = text;
    update();
}

void SplashWidget::paintEvent(QPaintEvent* const event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(2, 6, 23));

    const QRect contentRect = rect().adjusted(48, 36, -48, -36);
    painter.setPen(QPen(QColor(248, 250, 252), 2));
    painter.drawText(contentRect.left(), contentRect.top(), QStringLiteral("中山大学"));
    painter.drawText(contentRect.left(), contentRect.top() + 28, QStringLiteral("锁步研发测试系统"));

    if (!assets_.sysuLogoPath().isEmpty()) {
        const QPixmap logo(assets_.sysuLogoPath());
        if (!logo.isNull()) {
            painter.drawPixmap(contentRect.right() - 88, contentRect.top(), 72, 72, logo);
        }
    } else {
        painter.setPen(QPen(QColor(45, 212, 191), 2));
        painter.drawEllipse(contentRect.right() - 88, contentRect.top(), 72, 72);
        painter.drawText(QRect(contentRect.right() - 88, contentRect.top(), 72, 72),
                         Qt::AlignCenter,
                         QStringLiteral("校徽\n占位"));
    }

    const QPoint rocketBase(contentRect.center().x(), contentRect.bottom() - 92);
    const QPoint rocketTip(rocketBase.x(), rocketBase.y() - 132);
    const QRect rocketBody(rocketBase.x() - 24, rocketBase.y() - 116, 48, 100);

    painter.setPen(QPen(QColor(56, 189, 248), 3));
    painter.setBrush(QColor(15, 23, 42));
    painter.drawRoundedRect(rocketBody, 20, 20);
    painter.drawLine(rocketBody.topLeft(), rocketTip);
    painter.drawLine(rocketBody.topRight(), rocketTip);
    painter.drawLine(rocketTip, QPoint(rocketBody.center().x(), rocketBody.top()));

    const int flameLift = 12 + (animationFrame_ % 12);
    painter.setBrush(QColor(249, 115, 22));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygon()
                        << QPoint(rocketBase.x() - 16, rocketBase.y() - 8)
                        << QPoint(rocketBase.x(), rocketBase.y() + 44 + flameLift)
                        << QPoint(rocketBase.x() + 16, rocketBase.y() - 8));

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(51, 65, 85), 1, Qt::DashLine));
    painter.drawEllipse(QPoint(contentRect.center().x(), contentRect.center().y() + 16), 210, 70);
    painter.drawEllipse(QPoint(contentRect.center().x(), contentRect.center().y() + 16), 280, 96);

    const int satelliteOffset = animationFrame_ % 80;
    painter.setBrush(QColor(56, 189, 248));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(contentRect.center().x() - 180 + satelliteOffset,
                               contentRect.center().y() - 46),
                        4,
                        4);

    painter.setPen(QPen(QColor(148, 163, 184), 1));
    painter.drawText(QRect(contentRect.left(), contentRect.bottom() - 32, contentRect.width(), 28),
                     Qt::AlignCenter,
                     progressText_);
}

}  // namespace lockstep::ui
