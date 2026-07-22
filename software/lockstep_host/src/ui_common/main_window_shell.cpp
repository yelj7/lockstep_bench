/**********************************************************
* 文件名: main_window_shell.cpp
* 日期: 2026-07-14
* 版本: v1.5
* 更新记录: 采样页仅保留下发按钮，程序运行同步携带当前采样配置。
* 描述: 实现上位机主窗口框架及各工作页面
**********************************************************/

#include "main_window_shell.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ui_theme.h"

namespace lockstep::ui {

namespace {

constexpr int kMinimumWidth = 1280;
constexpr int kMinimumHeight = 720;
constexpr int kSidebarWidth = 224;
constexpr int kDiagnosticsMinHeight = 140;
constexpr int kDiagnosticsMaxHeight = 188;
constexpr int kDetachedLogWidth = 900;
constexpr int kDetachedLogHeight = 420;
constexpr int kProgramPageLeftInitialWidth = 450;
constexpr int kProgramPageRightInitialWidth = 540;
constexpr int kProgramActionButtonHeight = 34;
constexpr double kResponsiveScaleMax = 1.32;
constexpr int kTaskIdRole = Qt::UserRole + 1;
constexpr int kTaskDescriptionRole = Qt::UserRole + 2;
constexpr int kTaskBasicInfoRole = Qt::UserRole + 3;
constexpr int kSamplingSampleCount = 4096;
constexpr int kSamplingPretrigger = 2047;
constexpr int kSamplingPosttrigger = 2049;
constexpr int kSamplingTriggerCount = 1;
constexpr int kSamplingPostAfterTrigger = 2048;
constexpr int kSamplingSampleWordBits = 1024;
constexpr int kSamplingSampleRateHz = 120000000;
constexpr int kSamplingMismatchBits = 5;

const QStringList& samplingMismatchDescriptions()
{
    static const QStringList descriptions = {
        QStringLiteral("计数器输出不匹配"),
        QStringLiteral("trace 输出不匹配"),
        QStringLiteral("debug 输出不匹配"),
        QStringLiteral("irq 输出不匹配"),
        QStringLiteral("AHB master 输出不匹配")
    };
    return descriptions;
}

bool protocolGroupUnavailable(const TraceGroupViewItem& group)
{
    const QString status = group.status.trimmed().toLower();
    return status.contains(QStringLiteral("unavailable")) ||
        status.contains(QStringLiteral("not_available")) ||
        status.contains(QStringLiteral("not_captured")) ||
        status.contains(QStringLiteral("missing")) ||
        status.contains(QStringLiteral("invalid"));
}

QString protocolActivityBreakdown(const TraceGroupViewItem& group)
{
    int reads = 0;
    int writes = 0;
    int errors = 0;
    for (const QString& transaction : group.transactions) {
        const QString upper = transaction.toUpper();
        if (upper.contains(QStringLiteral(" READ "))) ++reads;
        if (upper.contains(QStringLiteral(" WRITE "))) ++writes;
        if (upper.contains(QStringLiteral("RESP=ERROR")) ||
            upper.contains(QStringLiteral("RESP=RETRY")) ||
            upper.contains(QStringLiteral("RESP=SPLIT")) ||
            upper.contains(QStringLiteral("FRAME_ERROR=1")) ||
            upper.contains(QStringLiteral("CRC_ERROR"))) {
            ++errors;
        }
    }

    QStringList parts;
    if (reads > 0) parts.append(QStringLiteral("读 %1").arg(reads));
    if (writes > 0) parts.append(QStringLiteral("写 %1").arg(writes));
    if (errors > 0) parts.append(QStringLiteral("异常 %1").arg(errors));
    return parts.isEmpty() ? QStringLiteral("已检测到有效事务") : parts.join(QStringLiteral(" · "));
}

QStringList representativeProtocolTransactions(const QStringList& transactions)
{
    constexpr int kMaximumRepresentativeEvents = 4;
    QStringList result;
    const auto appendUnique = [&result](const QString& transaction) {
        if (!transaction.isEmpty() && !result.contains(transaction)) result.append(transaction);
    };
    for (const QString& transaction : transactions) {
        const QString upper = transaction.toUpper();
        if (upper.contains(QStringLiteral("MISMATCH")) ||
            upper.contains(QStringLiteral("RESP=ERROR")) ||
            upper.contains(QStringLiteral("RESP=RETRY")) ||
            upper.contains(QStringLiteral("RESP=SPLIT")) ||
            upper.contains(QStringLiteral("FRAME_ERROR=1")) ||
            upper.contains(QStringLiteral("CRC_ERROR"))) {
            appendUnique(transaction);
            if (result.size() >= kMaximumRepresentativeEvents) return result;
        }
    }
    if (!transactions.isEmpty()) appendUnique(transactions.first());
    if (transactions.size() > 2) appendUnique(transactions.at(transactions.size() / 2));
    if (transactions.size() > 1) appendUnique(transactions.last());
    return result.mid(0, kMaximumRepresentativeEvents);
}

void addRepresentativeProtocolEvents(
    QTreeWidgetItem* const parent,
    const QStringList& transactions)
{
    if (parent == nullptr) return;
    const QStringList representatives = representativeProtocolTransactions(transactions);
    for (const QString& transaction : representatives) {
        const QRegularExpressionMatch range =
            QRegularExpression(QStringLiteral("^\\[([^]]+)\\]\\s*(.*)$")).match(transaction);
        const QString time = range.hasMatch() ? range.captured(1) : QStringLiteral("-");
        const QString behavior = range.hasMatch() ? range.captured(2) : transaction;
        QTreeWidgetItem* const item = new QTreeWidgetItem(
            parent, {QStringLiteral("代表事件"), time, behavior});
        item->setToolTip(2, transaction);
    }
}

int scaledMetric(const int value, const double scale)
{
    return qMax(1, qRound(static_cast<double>(value) * qBound(1.0, scale, kResponsiveScaleMax)));
}

double scaleForWindowSize(const QSize& size)
{
    const double widthScale = static_cast<double>(qMax(size.width(), kMinimumWidth)) /
                              static_cast<double>(kMinimumWidth);
    const double heightScale = static_cast<double>(qMax(size.height(), kMinimumHeight)) /
                               static_cast<double>(kMinimumHeight);
    return qBound(1.0, qMin(widthScale, heightScale), kResponsiveScaleMax);
}

class WorkbenchComboBox final : public QComboBox {
public:
    explicit WorkbenchComboBox(QWidget* const parent = nullptr)
        : QComboBox(parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent* const event) override
    {
        if (event != nullptr) {
            event->ignore();
        }
    }

    void paintEvent(QPaintEvent* const event) override
    {
        QComboBox::paintEvent(event);

        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QRect arrowRect =
            style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
        if (!arrowRect.isValid()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(isEnabled() ? QColor(QStringLiteral("#64748b")) : QColor(QStringLiteral("#9ca3af")));
        pen.setWidthF(1.8);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        const QPoint center = arrowRect.center();
        QPainterPath path;
        path.moveTo(center.x() - 4, center.y() - 2);
        path.lineTo(center.x(), center.y() + 2);
        path.lineTo(center.x() + 4, center.y() - 2);
        painter.drawPath(path);
    }
};

struct WaveformTimeAxis final {
    qint64 start = 0;
    qint64 end = 400;
    QString unit = QStringLiteral("ns");
};

struct WaveformProtocolRow final {
    QString groupId;
    QString name;
    QString status;
    QString reason;
    QStringList transactions;
    TraceFieldViewItem field;
    bool group = false;
    bool child = false;
    bool section = false;
    bool expanded = false;
    int groupIndex = 0;
    int childIndex = -1;
};

class WaveformDisplayWidget final : public QWidget {
public:
    explicit WaveformDisplayWidget(QWidget* const parent = nullptr)
        : QWidget(parent),
          statusText_(),
          pathText_(),
          timeRangeText_(),
          groups_(),
          samples_(),
          expandedGroups_(),
          selectedRow_(0),
          rowHeight_(kDefaultRowHeight),
          verticalOffset_(0),
          horizontalZoom_(1.0),
          timeUnitsPerPixel_(0.0),
          visibleStartTime_(0.0),
          hoverActive_(false),
          hoverPoint_(),
          hoverTime_(0),
          horizontalScrollBar_(new QScrollBar(Qt::Horizontal, this)),
          renderedProtocolEvents_(0),
          firstProtocolEventRect_(),
          panning_(false),
          panStart_(),
          panStartOffset_(0.0)
    {
        setObjectName(QStringLiteral("waveform_display_widget"));
        setMinimumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("协议波形时间轴"));
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        horizontalScrollBar_->setObjectName(QStringLiteral("waveform_time_scrollbar"));
        horizontalScrollBar_->setAccessibleName(QStringLiteral("波形时间轴水平滚动条"));
        horizontalScrollBar_->setSingleStep(10000);
        horizontalScrollBar_->setStyleSheet(QStringLiteral(
            "QScrollBar#waveform_time_scrollbar:horizontal {"
            "  height: 9px; background: #e5ebf1; border: 1px solid #c7d1dc;"
            "}"
            "QScrollBar#waveform_time_scrollbar::handle:horizontal {"
            "  min-width: 36px; background: #8799ac; border: 1px solid #71869a;"
            "}"
            "QScrollBar#waveform_time_scrollbar::handle:horizontal:hover { background: #6f8498; }"
            "QScrollBar#waveform_time_scrollbar::sub-line:horizontal,"
            "QScrollBar#waveform_time_scrollbar::add-line:horizontal {"
            "  width: 9px; background: #d5dee7; border: 1px solid #b8c4d0;"
            "}"
            "QScrollBar#waveform_time_scrollbar::sub-page:horizontal,"
            "QScrollBar#waveform_time_scrollbar::add-page:horizontal { background: #e5ebf1; }"));
        connect(horizontalScrollBar_, &QScrollBar::valueChanged, this, [this](const int value) {
            if (horizontalScrollBar_->maximum() <= 0) return;
            const WaveformTimeAxis base = baseTimeAxis();
            const WaveformTimeAxis visible = timeAxis();
            const qint64 scrollableSpan = qMax<qint64>(0,
                (base.end - base.start) - (visible.end - visible.start));
            visibleStartTime_ = static_cast<double>(base.start) +
                static_cast<double>(value) * static_cast<double>(scrollableSpan) /
                    static_cast<double>(horizontalScrollBar_->maximum());
            if (hoverActive_) hoverTime_ = timeAtX(hoverPoint_.x());
            updateAccessibleDescription();
            update();
        });
    }

    void setTrace(
        const QString& statusText,
        const QString& pathText,
        const QString& timeRangeText,
        const QVector<TraceGroupViewItem>& groups,
        const QVector<TraceSampleViewItem>& samples)
    {
        const quint64 previousFingerprint = sampleFingerprint(samples_);
        const quint64 nextFingerprint = sampleFingerprint(samples);
        const bool traceContextChanged = pathText_ != pathText || previousFingerprint != nextFingerprint;
        statusText_ = statusText;
        pathText_ = pathText;
        timeRangeText_ = timeRangeText;
        groups_ = groups;
        samples_ = samples;
        if (traceContextChanged) {
            expandedGroups_.clear();
            horizontalZoom_ = 1.0;
            timeUnitsPerPixel_ = 0.0;
            visibleStartTime_ = static_cast<double>(baseTimeAxis().start);
            hoverActive_ = false;
        }
        verticalOffset_ = qBound(0, verticalOffset_, maxVerticalOffset());
        selectedRow_ = qBound(0, selectedRow_, qMax(0, rows().size() - 1));
        updateAccessibleDescription();
        updateHorizontalScrollBar();
        update();
    }

protected:
    void paintEvent(QPaintEvent* const event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect bounds = rect();
        painter.fillRect(bounds, canvasColor());
        painter.setPen(gridMajorColor());
        painter.drawRect(bounds.adjusted(0, 0, -1, -1));

        const int leftWidth = leftPaneWidth();
        const QRect leftHeader(0, 0, leftWidth, kRulerHeight);
        const QRect rulerRect(leftWidth, 0, width() - leftWidth, kRulerHeight);
        painter.fillRect(leftHeader, rulerSurfaceColor());
        painter.fillRect(rulerRect, rulerSurfaceColor());
        painter.setPen(gridMajorColor());
        painter.drawLine(leftWidth, 0, leftWidth, height());
        painter.drawLine(0, kRulerHeight - 1, width(), kRulerHeight - 1);
        painter.setPen(textMutedColor());
        painter.drawText(leftHeader.adjusted(12, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         hasLoadedData() ? QStringLiteral("协议 / 信号") : QStringLiteral("协议通道"));
        drawRuler(&painter, rulerRect);

        const QVector<WaveformProtocolRow> visibleRows = rows();
        renderedProtocolEvents_ = 0;
        firstProtocolEventRect_ = QRect();
        painter.setClipRect(QRect(0, kRulerHeight, width(),
                                  qMax(0, height() - kRulerHeight - kHorizontalScrollBarHeight)));
        for (int index = 0; index < visibleRows.size(); ++index) {
            const int y = kRulerHeight + index * rowHeight_ - verticalOffset_;
            if (y + rowHeight_ < kRulerHeight) {
                continue;
            }
            if (y > height() - kHorizontalScrollBarHeight) {
                break;
            }
            drawRow(&painter, QRect(0, y, width(), rowHeight_), leftWidth, visibleRows.at(index), index);
        }
        painter.setClipping(false);
        setProperty("waveformRenderedProtocolEvents", renderedProtocolEvents_);
        setProperty("waveformFirstProtocolEventRect", firstProtocolEventRect_);

        drawRemainingGrid(&painter, leftWidth, visibleRows.size());
        drawScrollBar(&painter, QRect(width() - 8, kRulerHeight, 6,
                                     height() - kRulerHeight - kHorizontalScrollBarHeight));
        drawHoverCursor(&painter);
        updateHorizontalScrollBar();
    }

    void mousePressEvent(QMouseEvent* const event) override
    {
        if (event == nullptr || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        setFocus(Qt::MouseFocusReason);
        const int leftWidth = leftPaneWidth();
        const int index = rowIndexAt(event->pos().y());
        const QVector<WaveformProtocolRow> visibleRows = rows();
        if (index < 0 || index >= visibleRows.size()) {
            QWidget::mousePressEvent(event);
            return;
        }

        selectedRow_ = index;
        if (event->pos().x() >= leftWidth) {
            panning_ = true;
            panStart_ = event->pos();
            panStartOffset_ = visibleStartTime_;
            setCursor(Qt::ClosedHandCursor);
            update();
        } else if (visibleRows.at(index).group) {
            toggleGroup(visibleRows.at(index).groupId);
        } else {
            update();
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* const event) override
    {
        if (event == nullptr) {
            return;
        }

        if (panning_ && event->buttons().testFlag(Qt::LeftButton)) {
            visibleStartTime_ = clampVisibleStart(
                panStartOffset_ + static_cast<double>(panStart_.x() - event->pos().x()) *
                    effectiveTimeUnitsPerPixel());
            if (hoverActive_) hoverTime_ = timeAtX(hoverPoint_.x());
            update();
            event->accept();
            return;
        }

        updateHover(event->pos());

        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* const event) override
    {
        hoverActive_ = false;
        setProperty("waveformCursorText", QString());
        update();
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* const event) override
    {
        if (panning_ && event != nullptr && event->button() == Qt::LeftButton) {
            panning_ = false;
            unsetCursor();
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* const event) override
    {
        if (event != nullptr && event->button() == Qt::RightButton && event->pos().x() >= leftPaneWidth()) {
            horizontalZoom_ = 1.0;
            timeUnitsPerPixel_ = 0.0;
            visibleStartTime_ = static_cast<double>(baseTimeAxis().start);
            updateAccessibleDescription();
            update();
            event->accept();
            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent* const event) override
    {
        if (event == nullptr) {
            return;
        }

        const QVector<WaveformProtocolRow> visibleRows = rows();
        if (visibleRows.isEmpty()) {
            QWidget::keyPressEvent(event);
            return;
        }

        switch (event->key()) {
        case Qt::Key_Down:
            selectedRow_ = qMin(visibleRows.size() - 1, selectedRow_ + 1);
            ensureSelectedVisible();
            update();
            event->accept();
            return;
        case Qt::Key_Up:
            selectedRow_ = qMax(0, selectedRow_ - 1);
            ensureSelectedVisible();
            update();
            event->accept();
            return;
        case Qt::Key_Home:
            selectedRow_ = 0;
            ensureSelectedVisible();
            update();
            event->accept();
            return;
        case Qt::Key_End:
            selectedRow_ = visibleRows.size() - 1;
            ensureSelectedVisible();
            update();
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (visibleRows.at(selectedRow_).group) {
                toggleGroup(visibleRows.at(selectedRow_).groupId);
                event->accept();
                return;
            }
            break;
        case Qt::Key_Left:
            if (visibleRows.at(selectedRow_).group &&
                expandedGroups_.contains(visibleRows.at(selectedRow_).groupId)) {
                expandedGroups_.remove(visibleRows.at(selectedRow_).groupId);
                verticalOffset_ = qBound(0, verticalOffset_, maxVerticalOffset());
                update();
                event->accept();
                return;
            }
            break;
        case Qt::Key_Right:
            if (visibleRows.at(selectedRow_).group &&
                !expandedGroups_.contains(visibleRows.at(selectedRow_).groupId)) {
                expandedGroups_.insert(visibleRows.at(selectedRow_).groupId);
                update();
                event->accept();
                return;
            }
            break;
        default:
            break;
        }

        QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* const event) override
    {
        if (event == nullptr) {
            return;
        }

        const QPoint angleDelta = event->angleDelta();
        const QPoint pixelDelta = event->pixelDelta();
        const bool horizontalWheel =
            qAbs(angleDelta.x()) > qAbs(angleDelta.y()) ||
            qAbs(pixelDelta.x()) > qAbs(pixelDelta.y()) ||
            event->modifiers().testFlag(Qt::ShiftModifier);
        const int wheelX = pixelDelta.isNull() ? angleDelta.x() : pixelDelta.x();
        const int wheelY = pixelDelta.isNull() ? angleDelta.y() : pixelDelta.y();
        const int delta = horizontalWheel ? (wheelX != 0 ? wheelX : wheelY) : wheelY;

        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            if (delta != 0) {
                zoomTimeAxisAt(delta > 0 ? 1.6 : 1.0 / 1.6, event->position().x());
                update();
            }
            event->accept();
            return;
        }

        if (event->modifiers().testFlag(Qt::AltModifier)) {
            if (delta != 0) zoomRowsAt(delta > 0 ? 3 : -3, event->position().y());
            event->accept();
            return;
        }

        if (horizontalWheel && delta != 0) {
            panByPixels(-delta);
            event->accept();
            return;
        }

        const int maxOffset = maxVerticalOffset();
        if (maxOffset > 0 && delta != 0) {
            verticalOffset_ = qBound(0, verticalOffset_ - delta / 4, maxOffset);
            update();
            event->accept();
            return;
        }

        QWidget::wheelEvent(event);
    }

private:
    static constexpr int kRulerHeight = 28;
    static constexpr int kDefaultRowHeight = 27;
    static constexpr int kHorizontalScrollBarHeight = 9;

    static QColor canvasColor()
    {
        return QColor(QStringLiteral("#ffffff"));
    }

    static quint64 sampleFingerprint(const QVector<TraceSampleViewItem>& samples)
    {
        quint64 fingerprint = static_cast<quint64>(samples.size());
        for (const TraceSampleViewItem& sample : samples) {
            fingerprint = (fingerprint * 1099511628211ULL) ^ static_cast<quint64>(sample.time);
            fingerprint = (fingerprint * 1099511628211ULL) ^ static_cast<quint64>(qHash(sample.valueHex));
            fingerprint = (fingerprint * 1099511628211ULL) ^ static_cast<quint64>(sample.unknown);
        }
        return fingerprint;
    }

    static QColor rulerSurfaceColor()
    {
        return QColor(QStringLiteral("#fbfcfe"));
    }

    static QColor gridMajorColor()
    {
        return QColor(QStringLiteral("#dbe2ea"));
    }

    static QColor gridMinorColor()
    {
        return QColor(QStringLiteral("#edf2f7"));
    }

    static QColor signalWaveColor()
    {
        return QColor(QStringLiteral("#315b75"));
    }

    static QColor busFillColor()
    {
        return QColor(QStringLiteral("#eef4f8"));
    }

    static QColor busStrokeColor()
    {
        return QColor(QStringLiteral("#496f88"));
    }

    static QColor protocolFillColor()
    {
        return QColor(QStringLiteral("#edf3f7"));
    }

    static QColor protocolStrokeColor()
    {
        return QColor(QStringLiteral("#5b7080"));
    }

    static QColor mismatchFillColor()
    {
        return QColor(QStringLiteral("#fff1f0"));
    }

    static QColor mismatchStrokeColor()
    {
        return QColor(QStringLiteral("#ff4d4f"));
    }

    static QColor selectedRowColor()
    {
        return QColor(QStringLiteral("#e8f0f5"));
    }

    static QColor sectionSurfaceColor()
    {
        return QColor(QStringLiteral("#eef2f7"));
    }

    static QColor unavailableColor()
    {
        return QColor(QStringLiteral("#94a3b8"));
    }

    static QColor textStrongColor()
    {
        return QColor(QStringLiteral("#111827"));
    }

    static QColor textMutedColor()
    {
        return QColor(QStringLiteral("#64748b"));
    }

    static QColor okColor()
    {
        return QColor(QStringLiteral("#64748b"));
    }

    static QColor okFillColor()
    {
        return QColor(QStringLiteral("#f4f6f8"));
    }

    static QColor groupColor(const QString& groupId, const int index)
    {
        Q_UNUSED(groupId);
        Q_UNUSED(index);
        return protocolStrokeColor();
    }

    static QStringList fixedGroupIds()
    {
        return {
            QStringLiteral("ahb"),
            QStringLiteral("uart"),
            QStringLiteral("spi"),
            QStringLiteral("can"),
            QStringLiteral("i2c"),
            QStringLiteral("eth"),
            QStringLiteral("usb"),
            QStringLiteral("jtag"),
            QStringLiteral("mismatch"),
        };
    }

    static QString displayNameForId(const QString& groupId)
    {
        if (groupId == QStringLiteral("mismatch")) {
            return QStringLiteral("Mismatch");
        }
        return groupId.toUpper();
    }

    const TraceGroupViewItem* findGroup(const QString& groupId) const
    {
        for (const TraceGroupViewItem& group : groups_) {
            const QString id = group.id.trimmed().toLower();
            const QString display = group.displayName.trimmed().toLower();
            if (id == groupId || display == groupId) {
                return &group;
            }
        }
        return nullptr;
    }

    QVector<WaveformProtocolRow> rows() const
    {
        QVector<WaveformProtocolRow> result;
        const QStringList groupIds = fixedGroupIds();
        result.reserve(groupIds.size() * 6);

        for (int groupIndex = 0; groupIndex < groupIds.size(); ++groupIndex) {
            const QString groupId = groupIds.at(groupIndex);
            const TraceGroupViewItem* const group = findGroup(groupId);
            WaveformProtocolRow row;
            row.groupId = groupId;
            row.name = group == nullptr ? displayNameForId(groupId) :
                (group->displayName.trimmed().isEmpty() ? displayNameForId(groupId) : group->displayName.trimmed().toUpper());
            if (groupId == QStringLiteral("mismatch")) {
                row.name = QStringLiteral("Mismatch");
            }
            row.status = group == nullptr ? QStringLiteral("not_captured") : group->status;
            row.reason = group == nullptr ? QString() : group->reason;
            row.transactions = group == nullptr ? QStringList() : group->transactions;
            row.group = true;
            row.expanded = expandedGroups_.contains(groupId);
            row.groupIndex = groupIndex;
            result.append(row);

            if (!row.expanded) {
                continue;
            }

            const QVector<TraceFieldViewItem> fields = group == nullptr
                ? QVector<TraceFieldViewItem>() : group->fields;
            for (int fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
                WaveformProtocolRow fieldRow;
                fieldRow.groupId = groupId;
                fieldRow.field = fields.at(fieldIndex);
                fieldRow.name = fieldRow.field.displayName.isEmpty()
                    ? fieldRow.field.name : fieldRow.field.displayName;
                fieldRow.status = row.status;
                fieldRow.reason = row.reason;
                fieldRow.child = true;
                fieldRow.groupIndex = groupIndex;
                fieldRow.childIndex = fieldIndex;
                result.append(fieldRow);
            }
        }
        return result;
    }

    bool hasLoadedData() const
    {
        return !samples_.isEmpty();
    }

    int leftPaneWidth() const
    {
        return qBound(190, width() / 4, 245);
    }

    WaveformTimeAxis baseTimeAxis() const
    {
        WaveformTimeAxis axis;
        static const QRegularExpression rangeExpression(QStringLiteral("(-?\\d+)\\s*\\.\\.\\s*(-?\\d+)\\s*(\\S+)"));
        const QRegularExpressionMatch match = rangeExpression.match(timeRangeText_);
        if (match.hasMatch()) {
            bool startOk = false;
            bool endOk = false;
            const qint64 start = match.captured(1).toLongLong(&startOk);
            const qint64 end = match.captured(2).toLongLong(&endOk);
            if (startOk && endOk && end > start) {
                axis.start = start;
                axis.end = end;
                axis.unit = match.captured(3);
            }
        }

        return axis;
    }

    WaveformTimeAxis timeAxis() const
    {
        const WaveformTimeAxis base = baseTimeAxis();
        WaveformTimeAxis axis = base;
        const int waveWidth = qMax(1, width() - leftPaneWidth());
        const qint64 visibleSpan = qMax<qint64>(1, qRound64(effectiveTimeUnitsPerPixel() * waveWidth));
        axis.start = qRound64(clampVisibleStart(visibleStartTime_));
        axis.end = qMin(base.end, axis.start + visibleSpan);
        if (axis.end <= axis.start) axis.end = axis.start + 1;
        return axis;
    }

    double fitTimeUnitsPerPixel() const
    {
        const WaveformTimeAxis base = baseTimeAxis();
        return static_cast<double>(qMax<qint64>(1, base.end - base.start)) /
            static_cast<double>(qMax(1, width() - leftPaneWidth()));
    }

    double effectiveTimeUnitsPerPixel() const
    {
        return timeUnitsPerPixel_ > 0.0 ? timeUnitsPerPixel_ : fitTimeUnitsPerPixel();
    }

    double minimumTimeUnitsPerPixel() const
    {
        return 1.0 / static_cast<double>(qMax(1, width() - leftPaneWidth()));
    }

    double clampVisibleStart(const double value) const
    {
        const WaveformTimeAxis base = baseTimeAxis();
        const double visibleSpan = effectiveTimeUnitsPerPixel() *
            static_cast<double>(qMax(1, width() - leftPaneWidth()));
        const double maximum = qMax(static_cast<double>(base.start),
                                    static_cast<double>(base.end) - visibleSpan);
        return qBound(static_cast<double>(base.start), value, maximum);
    }

    void panByPixels(const int delta)
    {
        visibleStartTime_ = clampVisibleStart(
            visibleStartTime_ + static_cast<double>(delta) * effectiveTimeUnitsPerPixel());
        if (hoverActive_) hoverTime_ = timeAtX(hoverPoint_.x());
        update();
    }

    void zoomTimeAxisAt(const double factor, const double widgetX)
    {
        const int leftWidth = leftPaneWidth();
        const int waveWidth = qMax(1, width() - leftWidth);
        const double cursorPixels = qBound(0.0, widgetX - static_cast<double>(leftWidth),
                                           static_cast<double>(waveWidth));
        const double oldScale = effectiveTimeUnitsPerPixel();
        const double anchorTime = visibleStartTime_ + cursorPixels * oldScale;
        const double newScale = qBound(minimumTimeUnitsPerPixel(), oldScale / factor,
                                       fitTimeUnitsPerPixel());
        timeUnitsPerPixel_ = newScale;
        visibleStartTime_ = clampVisibleStart(anchorTime - cursorPixels * newScale);
        horizontalZoom_ = fitTimeUnitsPerPixel() / newScale;
        if (hoverActive_) hoverTime_ = timeAtX(hoverPoint_.x());
        updateAccessibleDescription();
    }

    void drawRuler(QPainter* const painter, const QRect& rect) const
    {
        if (painter == nullptr || rect.width() <= 0) {
            return;
        }

        const WaveformTimeAxis axis = timeAxis();
        constexpr int majorTicks = 8;
        constexpr int minorPerMajor = 5;
        painter->setPen(gridMajorColor());
        for (int index = 0; index <= majorTicks * minorPerMajor; ++index) {
            const int x = rect.left() + rect.width() * index / (majorTicks * minorPerMajor);
            const bool major = (index % minorPerMajor) == 0;
            painter->drawLine(x, rect.bottom() - (major ? 10 : 5), x, rect.bottom());
            if (major) {
                const qint64 time = axis.start + (axis.end - axis.start) * (index / minorPerMajor) / majorTicks;
                painter->setPen(QColor(QStringLiteral("#4b5563")));
                painter->drawText(QRect(x + 3, rect.top() + 1, 70, 15),
                                  Qt::AlignLeft | Qt::AlignVCenter,
                                  QStringLiteral("%1%2").arg(time).arg(axis.unit));
                painter->setPen(gridMajorColor());
            }
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(busStrokeColor());
        painter->drawRect(QRect(rect.left() - 2, rect.top(), 4, rect.height()));
        painter->drawRoundedRect(QRect(rect.left() - 10, rect.top(), 20, 14), 2, 2);
        painter->setPen(QColor(QStringLiteral("#ffffff")));
        painter->drawText(QRect(rect.left() - 10, rect.top(), 20, 14),
                          Qt::AlignCenter,
                          QStringLiteral("0"));
    }

    void drawRow(
        QPainter* const painter,
        const QRect& rowRect,
        const int leftWidth,
        const WaveformProtocolRow& row,
        const int index) const
    {
        if (painter == nullptr) {
            return;
        }

        if (row.section) {
            painter->fillRect(rowRect, sectionSurfaceColor());
            painter->setPen(gridMajorColor());
            painter->drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());

            QFont sectionFont = painter->font();
            sectionFont.setBold(true);
            painter->setFont(sectionFont);
            painter->setPen(textMutedColor());
            painter->drawText(QRect(12, rowRect.top(), leftWidth - 24, rowRect.height()),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              row.name);
            drawGrid(painter, QRect(leftWidth + 1, rowRect.top(), rowRect.width() - leftWidth - 2, rowRect.height()));
            return;
        }

        painter->fillRect(rowRect, row.child ? canvasColor() : rulerSurfaceColor());
        if (hasFocus() && index == selectedRow_) {
            painter->fillRect(rowRect, selectedRowColor());
        }

        painter->setPen(gridMajorColor());
        painter->drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());

        if (row.group) {
            painter->setPen(Qt::NoPen);
            const QString normalizedStatus = row.status.trimmed().toLower();
            const bool mismatchProblem = row.groupId == QStringLiteral("mismatch") &&
                (normalizedStatus.contains(QStringLiteral("event")) ||
                 normalizedStatus.contains(QStringLiteral("error")) ||
                 normalizedStatus.contains(QStringLiteral("fault")));
            painter->setBrush(mismatchProblem
                ? mismatchStrokeColor() : groupColor(row.groupId, row.groupIndex));
            painter->drawRect(QRect(2, rowRect.top() + 5, 4, rowRect.height() - 10));
            drawDisclosure(painter, QRect(10, rowRect.top() + 8, 11, 11), row.expanded);
        }

        QFont nameFont = painter->font();
        nameFont.setFamily(QStringLiteral("Consolas"));
        nameFont.setBold(row.group);
        painter->setFont(nameFont);
        painter->setPen(textStrongColor());
        const int nameLeft = row.child ? 38 : 25;
        const QRect nameRect(nameLeft, rowRect.top(), leftWidth - nameLeft - 24, rowRect.height());
        painter->drawText(nameRect,
                          Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(row.name, Qt::ElideRight, nameRect.width()));

        if (hasLoadedData() && row.group) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(statusColor(row.status));
            painter->drawEllipse(QRect(leftWidth - 18, rowRect.center().y() - 3, 6, 6));
        }

        drawTrack(painter, QRect(leftWidth + 1, rowRect.top(), rowRect.width() - leftWidth - 2, rowRect.height()), row);
    }

    static void drawDisclosure(QPainter* const painter, const QRect& rect, const bool expanded)
    {
        if (painter == nullptr) {
            return;
        }

        const QPoint center = rect.center();
        QPainterPath path;
        if (expanded) {
            path.moveTo(center.x() - 4, center.y() - 2);
            path.lineTo(center.x(), center.y() + 2);
            path.lineTo(center.x() + 4, center.y() - 2);
        } else {
            path.moveTo(center.x() - 2, center.y() - 4);
            path.lineTo(center.x() + 2, center.y());
            path.lineTo(center.x() - 2, center.y() + 4);
        }

        QPen pen(textMutedColor());
        pen.setWidthF(1.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    static QColor statusColor(const QString& status)
    {
        const QString normalized = status.trimmed().toLower();
        if (normalized.contains(QStringLiteral("mismatch")) ||
            normalized.contains(QStringLiteral("error")) ||
            normalized.contains(QStringLiteral("fault"))) {
            return mismatchStrokeColor();
        }
        if (normalized.contains(QStringLiteral("event")) ||
            normalized.contains(QStringLiteral("missing")) ||
            normalized.contains(QStringLiteral("invalid")) ||
            normalized.contains(QStringLiteral("stale"))) {
            return protocolStrokeColor();
        }
        if (normalized == QStringLiteral("complete") || normalized == QStringLiteral("ok")) {
            return okColor();
        }
        return unavailableColor();
    }

    static bool unavailableStatus(const QString& status)
    {
        const QString normalized = status.trimmed().toLower();
        return normalized.contains(QStringLiteral("not_captured")) ||
            normalized.contains(QStringLiteral("missing")) ||
            normalized.contains(QStringLiteral("invalid")) ||
            normalized.contains(QStringLiteral("not_available"));
    }

    void drawTrack(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row) const
    {
        if (painter == nullptr || rect.width() <= 0) {
            return;
        }

        drawGrid(painter, rect);
        if (!hasLoadedData()) {
            return;
        }

        if (row.group) {
            if (!row.transactions.isEmpty()) {
                drawTransactions(painter, rect.adjusted(0, 4, 0, -4), row);
            } else if (unavailableStatus(row.status)) {
                drawUnavailable(painter, rect, row.reason);
            } else if (row.groupId == QStringLiteral("mismatch")) {
                drawEmptyActivity(painter, rect, QStringLiteral("处理器内无Mismatch"));
            }
            return;
        }

        if (unavailableStatus(row.status)) {
            drawUnavailable(painter, rect, row.reason);
            return;
        }

        if (row.field.width > 1) {
            drawBusField(painter, rect, row);
        } else {
            drawDigitalField(painter, rect, row);
        }
    }

    static void drawGrid(QPainter* const painter, const QRect& rect)
    {
        constexpr int majorTicks = 8;
        constexpr int minorPerMajor = 5;
        for (int index = 0; index <= majorTicks * minorPerMajor; ++index) {
            const int x = rect.left() + rect.width() * index / (majorTicks * minorPerMajor);
            painter->setPen((index % minorPerMajor) == 0 ? gridMajorColor() : gridMinorColor());
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
    }

    static void drawUnavailable(QPainter* const painter, const QRect& rect, const QString& reason)
    {
        QPen pen(unavailableColor());
        pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->drawLine(rect.left() + 18, rect.center().y(), rect.right() - 18, rect.center().y());
        if (!reason.trimmed().isEmpty()) {
            painter->setPen(textMutedColor());
            painter->drawText(rect.adjusted(24, 0, -10, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(reason.trimmed(), Qt::ElideRight, rect.width() - 34));
        }
    }

    static void drawEmptyActivity(QPainter* const painter, const QRect& rect, const QString& text)
    {
        painter->setPen(QPen(protocolStrokeColor(), 1));
        painter->drawLine(rect.left() + 18, rect.center().y(), rect.right() - 18, rect.center().y());
        painter->setPen(textMutedColor());
        painter->drawText(rect.adjusted(24, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    static QString groupHexDigits(const QString& hex)
    {
        QString grouped;
        grouped.reserve(hex.size() + hex.size() / 4);
        for (int index = 0; index < hex.size(); ++index) {
            if (index > 0 && ((hex.size() - index) % 4) == 0) {
                grouped.append(QLatin1Char('_'));
            }
            grouped.append(hex.at(index));
        }
        return grouped;
    }

    static int hexNibble(const QChar value)
    {
        const ushort code = value.toUpper().unicode();
        if (code >= '0' && code <= '9') {
            return static_cast<int>(code - '0');
        }
        if (code >= 'A' && code <= 'F') {
            return static_cast<int>(code - 'A' + 10);
        }
        return 0;
    }

    static quint64 fieldValue(const TraceSampleViewItem& sample, const TraceFieldViewItem& field)
    {
        quint64 value = 0U;
        const int width = qBound(1, field.width, 64);
        for (int bit = 0; bit < width; ++bit) {
            const int packedBit = field.lsb + bit;
            const int nibbleIndex = sample.valueHex.size() - 1 - packedBit / 4;
            if (nibbleIndex < 0 || nibbleIndex >= sample.valueHex.size()) {
                continue;
            }
            if ((hexNibble(sample.valueHex.at(nibbleIndex)) & (1 << (packedBit % 4))) != 0) {
                value |= quint64(1) << bit;
            }
        }
        return value;
    }

    static QString fieldValueText(const TraceSampleViewItem& sample, const TraceFieldViewItem& field)
    {
        const QString hex = QStringLiteral("%1")
            .arg(fieldValue(sample, field), qMax(1, (field.width + 3) / 4), 16, QLatin1Char('0'))
            .toUpper();
        return QStringLiteral("0x%1").arg(groupHexDigits(hex));
    }

    static int timeToX(const qint64 time, const WaveformTimeAxis& axis, const QRect& rect)
    {
        const qint64 span = qMax<qint64>(1, axis.end - axis.start);
        const double ratio = qBound(
            0.0,
            static_cast<double>(time - axis.start) / static_cast<double>(span),
            1.0);
        return rect.left() + qRound(ratio * static_cast<double>(rect.width()));
    }

    int firstVisibleSampleIndex(const qint64 time) const
    {
        int low = 0;
        int high = samples_.size();
        while (low < high) {
            const int middle = low + (high - low) / 2;
            if (samples_.at(middle).time <= time) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        return qMax(0, low - 1);
    }

    void drawBusField(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row) const
    {
        const WaveformTimeAxis axis = timeAxis();
        const int top = rect.top() + 7;
        const int bottom = rect.bottom() - 7;
        const int first = firstVisibleSampleIndex(axis.start);
        if (first < 0 || first >= samples_.size()) return;

        const auto sameValue = [&](const int left, const int right) {
            return samples_.at(left).unknown == samples_.at(right).unknown &&
                fieldValue(samples_.at(left), row.field) == fieldValue(samples_.at(right), row.field);
        };
        const auto drawStableSegment = [&](const qint64 startTime, const qint64 endTime,
                                           const int sampleIndex, const bool leftTransition,
                                           const bool rightTransition) {
            const int rawX0 = timeToX(qMax(startTime, axis.start), axis, rect);
            const int rawX1 = timeToX(qMin(endTime, axis.end), axis, rect);
            const int x0 = rawX0 + (leftTransition ? 4 : 0);
            const int x1 = rawX1 - (rightTransition ? 4 : 0);
            if (x1 <= x0) return;
            const QRect blockRect(x0, top, x1 - x0, qMax(2, bottom - top));
            QPen busPen(samples_.at(sampleIndex).unknown ? unavailableColor() : busStrokeColor());
            busPen.setStyle(samples_.at(sampleIndex).unknown ? Qt::DashLine : Qt::SolidLine);
            painter->setPen(busPen);
            painter->fillRect(blockRect.adjusted(0, 1, 0, -1), busFillColor());
            painter->drawLine(blockRect.left(), blockRect.top(), blockRect.right(), blockRect.top());
            painter->drawLine(blockRect.left(), blockRect.bottom(), blockRect.right(), blockRect.bottom());
            painter->setPen(QColor(QStringLiteral("#27475c")));
            painter->drawText(blockRect.adjusted(6, 0, -6, 0), Qt::AlignCenter,
                              painter->fontMetrics().elidedText(
                                  samples_.at(sampleIndex).unknown
                                      ? QStringLiteral("X")
                                      : fieldValueText(samples_.at(sampleIndex), row.field),
                                  Qt::ElideRight, qMax(0, blockRect.width() - 12)));
        };
        const auto drawTransition = [&](const qint64 time) {
            const int x = timeToX(time, axis, rect);
            painter->setPen(QPen(busStrokeColor(), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(x - 4, top, x + 4, bottom);
            painter->drawLine(x - 4, bottom, x + 4, top);
        };

        int runSample = first;
        qint64 runStart = axis.start;
        bool leftTransition = samples_.at(first).time > axis.start;
        for (int index = first + 1; index < samples_.size() && samples_.at(index).time <= axis.end; ++index) {
            if (sameValue(runSample, index)) continue;
            const qint64 transitionTime = samples_.at(index).time;
            drawStableSegment(runStart, transitionTime, runSample, leftTransition, true);
            drawTransition(transitionTime);
            runSample = index;
            runStart = transitionTime;
            leftTransition = true;
        }
        drawStableSegment(runStart, axis.end, runSample, leftTransition, false);
    }

    void drawDigitalField(
        QPainter* const painter,
        const QRect& rect,
        const WaveformProtocolRow& row) const
    {
        const WaveformTimeAxis axis = timeAxis();
        const int high = rect.top() + 8;
        const int low = rect.bottom() - 8;
        for (int index = firstVisibleSampleIndex(axis.start); index < samples_.size(); ++index) {
            const qint64 startTime = samples_.at(index).time;
            const qint64 endTime = index + 1 < samples_.size()
                ? samples_.at(index + 1).time : axis.end;
            if (endTime < axis.start || startTime > axis.end) {
                if (startTime > axis.end) {
                    break;
                }
                continue;
            }
            const bool highValue = fieldValue(samples_.at(index), row.field) != 0U;
            const int y = samples_.at(index).unknown ? rect.center().y() : (highValue ? high : low);
            const int x0 = timeToX(qMax(startTime, axis.start), axis, rect);
            const int x1 = timeToX(qMin(qMax(startTime + 1, endTime), axis.end), axis, rect);
            const QColor color = row.field.errorSignal && highValue
                ? mismatchStrokeColor() : signalWaveColor();
            QPen segmentPen(samples_.at(index).unknown ? unavailableColor() : color,
                            row.field.errorSignal && highValue ? 2.0 : 1.2);
            segmentPen.setStyle(samples_.at(index).unknown ? Qt::DashLine : Qt::SolidLine);
            painter->setPen(segmentPen);
            painter->drawLine(x0, y, x1, y);
            if (!samples_.at(index).unknown && index + 1 < samples_.size() &&
                !samples_.at(index + 1).unknown) {
                const bool nextHigh = fieldValue(samples_.at(index + 1), row.field) != 0U;
                if (nextHigh != highValue) {
                    const QColor edgeColor = row.field.errorSignal && (highValue || nextHigh)
                        ? mismatchStrokeColor() : signalWaveColor();
                    painter->setPen(QPen(edgeColor, 1.2));
                    painter->drawLine(x1, highValue ? high : low, x1, nextHigh ? high : low);
                }
            }
        }
    }

    QRect transactionBlockRect(
        const QRect& rect,
        const WaveformProtocolRow& row,
        const int index) const
    {
        if (index < 0 || index >= row.transactions.size()) return QRect();
        const WaveformTimeAxis axis = timeAxis();
        const qint64 span = qMax<qint64>(1, axis.end - axis.start);
        qint64 start = 0;
        qint64 end = 0;
        const bool hasTime = parseTransactionRange(row.transactions.at(index), &start, &end);
        int x = rect.left() + 24 + ((index * 67 + row.groupIndex * 19) % qMax(1, rect.width() - 120));
        int blockWidth = qMax(64, rect.width() / 8);
        if (hasTime) {
            if (end < axis.start || start > axis.end) return QRect();
            const qint64 clampedStart = qBound(axis.start, start, axis.end);
            const qint64 clampedEnd = qBound(axis.start, qMax(start + 1, end), axis.end);
            x = clampedStart >= axis.end
                ? qMax(rect.left(), rect.right() - 44)
                : rect.left() + static_cast<int>(
                      (clampedStart - axis.start) * rect.width() / span);
            const int availableWidth = rect.right() - x - 4;
            if (availableWidth <= 4) return QRect();
            blockWidth = qMin(
                qMax(44, static_cast<int>(
                             qMax<qint64>(1, clampedEnd - clampedStart) * rect.width() / span)),
                availableWidth);
        }
        const int lane = index % 2;
        const QRect blockRect(x, rect.top() + lane * 2, blockWidth, rect.height() - lane * 2);
        return blockRect.width() > 4 ? blockRect : QRect();
    }

    void drawTransactions(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row) const
    {
        for (int index = 0; index < row.transactions.size(); ++index) {
            const QRect blockRect = transactionBlockRect(rect, row, index);
            if (blockRect.isEmpty()) continue;
            if (firstProtocolEventRect_.isEmpty()) firstProtocolEventRect_ = blockRect;
            const QString eventText = row.transactions.at(index).toUpper();
            const bool mismatch = row.groupId == QStringLiteral("mismatch") ||
                eventText.contains(QStringLiteral("MISMATCH")) ||
                eventText.contains(QStringLiteral("RESP=ERROR")) ||
                eventText.contains(QStringLiteral("RESP=RETRY")) ||
                eventText.contains(QStringLiteral("RESP=SPLIT"));
            painter->setPen(mismatch ? mismatchStrokeColor() : protocolStrokeColor());
            painter->setBrush(mismatch ? mismatchFillColor() : protocolFillColor());
            painter->drawRoundedRect(blockRect, 2, 2);
            painter->setPen(mismatch ? QColor(QStringLiteral("#a8071a")) : QColor(QStringLiteral("#27475c")));
            painter->drawText(blockRect.adjusted(6, 0, -6, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(transactionLabel(row.transactions.at(index)),
                                                                Qt::ElideRight,
                                                                blockRect.width() - 12));
            ++renderedProtocolEvents_;
        }
    }

    static bool parseTransactionRange(const QString& text, qint64* const start, qint64* const end)
    {
        static const QRegularExpression rangeExpression(QStringLiteral("\\[(-?\\d+)\\.\\.(-?\\d+)\\]"));
        const QRegularExpressionMatch match = rangeExpression.match(text);
        if (!match.hasMatch()) {
            return false;
        }
        bool startOk = false;
        bool endOk = false;
        const qint64 parsedStart = match.captured(1).toLongLong(&startOk);
        const qint64 parsedEnd = match.captured(2).toLongLong(&endOk);
        if (!startOk || !endOk) {
            return false;
        }
        if (start != nullptr) {
            *start = parsedStart;
        }
        if (end != nullptr) {
            *end = parsedEnd;
        }
        return true;
    }

    static QString transactionLabel(const QString& text)
    {
        static const QRegularExpression ahbLabelExpression(
            QStringLiteral("AHB\\s+(READ|WRITE)\\s+(0x[0-9A-Fa-f]+)"));
        const QRegularExpressionMatch ahbMatch = ahbLabelExpression.match(text);
        if (ahbMatch.hasMatch()) {
            return QStringLiteral("AHB %1 %2")
                .arg(ahbMatch.captured(1),
                     QStringLiteral("0x%1").arg(ahbMatch.captured(2).mid(2).toUpper()));
        }
        const int bracketEnd = text.indexOf(QLatin1Char(']'));
        const QString label = bracketEnd >= 0 ? text.mid(bracketEnd + 1).trimmed() : text.trimmed();
        return label.isEmpty() ? QStringLiteral("event") : label;
    }

    QString transactionDetails(const QString& text) const
    {
        qint64 start = 0;
        qint64 end = 0;
        parseTransactionRange(text, &start, &end);
        static const QRegularExpression ahbExpression(QStringLiteral(
            "AHB\\s+(READ|WRITE)\\s+(0x[0-9A-Fa-f]+)\\s+DATA=(0x[0-9A-Fa-f]+)\\s+RESP=([A-Za-z0-9x]+)"));
        const QRegularExpressionMatch match = ahbExpression.match(text);
        if (match.hasMatch()) {
            const auto formattedHex = [](const QString& value) {
                return QStringLiteral("0x%1").arg(value.mid(2).toUpper());
            };
            QString response = match.captured(4).toUpper();
            if (response == QStringLiteral("0") || response == QStringLiteral("0X0")) response = QStringLiteral("OKAY");
            else if (response == QStringLiteral("1") || response == QStringLiteral("0X1")) response = QStringLiteral("ERROR");
            else if (response == QStringLiteral("2") || response == QStringLiteral("0X2")) response = QStringLiteral("RETRY");
            else if (response == QStringLiteral("3") || response == QStringLiteral("0X3")) response = QStringLiteral("SPLIT");
            return QStringLiteral("AHB %1\nADDR=%2\nDATA=%3\nRESP=%4\n时间: %5..%6 %7")
                .arg(match.captured(1), formattedHex(match.captured(2)), formattedHex(match.captured(3)),
                     response, QString::number(start), QString::number(end), timeAxis().unit);
        }
        return QStringLiteral("%1\n时间: %2..%3 %4")
            .arg(transactionLabel(text), QString::number(start), QString::number(end), timeAxis().unit);
    }

    void drawRemainingGrid(QPainter* const painter, const int leftWidth, const int rowCount) const
    {
        const int y = kRulerHeight + rowCount * rowHeight_ - verticalOffset_;
        const int bodyBottom = height() - kHorizontalScrollBarHeight;
        if (y >= bodyBottom) {
            return;
        }
        const QRect leftRect(0, qMax(kRulerHeight, y), leftWidth,
                             bodyBottom - qMax(kRulerHeight, y));
        const QRect waveRect(leftWidth + 1, leftRect.top(), width() - leftWidth - 2, leftRect.height());
        painter->fillRect(leftRect, QColor(QStringLiteral("#fbfcfe")));
        drawGrid(painter, waveRect);
        painter->setPen(QColor(QStringLiteral("#eef2f7")));
        for (int rowY = leftRect.top(); rowY < height(); rowY += rowHeight_) {
            painter->drawLine(0, rowY, width(), rowY);
        }
    }

    void drawScrollBar(QPainter* const painter, const QRect& rect) const
    {
        const int maxOffset = maxVerticalOffset();
        if (maxOffset <= 0 || rect.height() <= 20) {
            return;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#eef2f7")));
        painter->drawRoundedRect(rect, 3, 3);
        const int contentHeight = rows().size() * rowHeight_;
        const int thumbHeight = qMax(28, rect.height() * rect.height() / qMax(1, contentHeight));
        const int thumbY = rect.top() + (rect.height() - thumbHeight) * verticalOffset_ / maxOffset;
        painter->setBrush(QColor(QStringLiteral("#aeb9c8")));
        painter->drawRoundedRect(QRect(rect.left(), thumbY, rect.width(), thumbHeight), 3, 3);
    }

    void updateHorizontalScrollBar()
    {
        if (horizontalScrollBar_ == nullptr) return;
        horizontalScrollBar_->setGeometry(
            leftPaneWidth(), height() - kHorizontalScrollBarHeight,
            qMax(0, width() - leftPaneWidth() - 8), kHorizontalScrollBarHeight);
        const WaveformTimeAxis base = baseTimeAxis();
        const WaveformTimeAxis visible = timeAxis();
        const qint64 fullSpan = qMax<qint64>(1, base.end - base.start);
        const qint64 visibleSpan = qBound<qint64>(1, visible.end - visible.start, fullSpan);
        const qint64 scrollableSpan = qMax<qint64>(0, fullSpan - visibleSpan);
        constexpr int kNormalizedMaximum = 1000000;
        const QSignalBlocker blocker(horizontalScrollBar_);
        horizontalScrollBar_->setRange(0, scrollableSpan > 0 ? kNormalizedMaximum : 0);
        horizontalScrollBar_->setPageStep(scrollableSpan > 0
            ? qMax(1, qRound(static_cast<double>(kNormalizedMaximum) * visibleSpan / scrollableSpan))
            : 1);
        horizontalScrollBar_->setValue(scrollableSpan > 0
            ? qRound(static_cast<double>(kNormalizedMaximum) *
                     static_cast<double>(visible.start - base.start) / scrollableSpan)
            : 0);
        horizontalScrollBar_->setEnabled(scrollableSpan > 0);
        horizontalScrollBar_->show();
    }

    qint64 timeAtX(const int x) const
    {
        const int left = leftPaneWidth();
        const int waveWidth = qMax(1, width() - left);
        const WaveformTimeAxis axis = timeAxis();
        const double ratio = qBound(0.0,
                                    static_cast<double>(x - left) / static_cast<double>(waveWidth),
                                    1.0);
        return axis.start + qRound64(ratio * static_cast<double>(axis.end - axis.start));
    }

    QString hoverText() const
    {
        if (!hoverActive_ || samples_.isEmpty()) return QString();
        const QVector<WaveformProtocolRow> visibleRows = rows();
        const int rowIndex = rowIndexAt(hoverPoint_.y());
        if (rowIndex < 0 || rowIndex >= visibleRows.size()) return QString();
        const int sampleIndex = firstVisibleSampleIndex(hoverTime_);
        if (sampleIndex < 0 || sampleIndex >= samples_.size()) return QString();

        const WaveformProtocolRow& row = visibleRows.at(rowIndex);
        if (row.group && !row.transactions.isEmpty()) {
            const QRect rowRect(0, kRulerHeight + rowIndex * rowHeight_ - verticalOffset_,
                                width(), rowHeight_);
            const QRect trackRect(leftPaneWidth() + 1, rowRect.top(),
                                  rowRect.width() - leftPaneWidth() - 2, rowRect.height());
            for (int index = 0; index < row.transactions.size(); ++index) {
                if (transactionBlockRect(trackRect.adjusted(0, 4, 0, -4), row, index)
                        .contains(hoverPoint_)) {
                    return transactionDetails(row.transactions.at(index));
                }
            }
        }
        const TraceSampleViewItem& sample = samples_.at(sampleIndex);
        QString signalName = row.name;
        QString valueText;
        if (row.child) {
            valueText = sample.unknown
                ? QStringLiteral("X")
                : (row.field.width == 1
                    ? QString::number(fieldValue(sample, row.field))
                    : fieldValueText(sample, row.field));
        } else {
            const QString compact = sample.valueHex.size() > 16
                ? QStringLiteral("...%1").arg(sample.valueHex.right(16)) : sample.valueHex;
            valueText = sample.unknown ? QStringLiteral("X") : QStringLiteral("0x%1").arg(compact);
            signalName += QStringLiteral(" / 样本低 64 bit");
        }
        return QStringLiteral("时间: %1 %2\n信号: %3\n数值: %4")
            .arg(hoverTime_).arg(timeAxis().unit, signalName, valueText);
    }

    void updateHover(const QPoint& point)
    {
        hoverActive_ = hasLoadedData() && point.x() >= leftPaneWidth() &&
            point.x() < width() && point.y() >= kRulerHeight &&
            point.y() < height() - kHorizontalScrollBarHeight;
        hoverPoint_ = point;
        if (hoverActive_) hoverTime_ = timeAtX(point.x());
        setProperty("waveformCursorText", hoverText());
        updateAccessibleDescription();
        update();
    }

    void drawHoverCursor(QPainter* const painter) const
    {
        if (painter == nullptr || !hoverActive_ || hoverPoint_.x() < leftPaneWidth()) return;
        const QString text = hoverText();
        if (text.isEmpty()) return;

        painter->setPen(QPen(QColor(QStringLiteral("#476b82")), 1.0, Qt::DashLine));
        painter->drawLine(hoverPoint_.x(), 0, hoverPoint_.x(), height());
        painter->setBrush(QColor(QStringLiteral("#476b82")));
        painter->setPen(Qt::NoPen);
        painter->drawRect(QRect(hoverPoint_.x() - 2, 0, 4, kRulerHeight));

        const QStringList lines = text.split(QLatin1Char('\n'));
        const QFontMetrics metrics(painter->font());
        int textWidth = 0;
        for (const QString& line : lines) textWidth = qMax(textWidth, metrics.horizontalAdvance(line));
        const QSize boxSize(textWidth + 20, lines.size() * metrics.lineSpacing() + 16);
        QPoint boxPoint(hoverPoint_.x() + 12, hoverPoint_.y() + 12);
        if (boxPoint.x() + boxSize.width() > width() - 8) boxPoint.setX(hoverPoint_.x() - boxSize.width() - 12);
        if (boxPoint.y() + boxSize.height() > height() - 8) boxPoint.setY(hoverPoint_.y() - boxSize.height() - 12);
        const QRect boxRect(boxPoint, boxSize);
        painter->setPen(QPen(QColor(QStringLiteral("#9aaaba")), 1));
        painter->setBrush(QColor(QStringLiteral("#ffffff")));
        painter->drawRoundedRect(boxRect, 3, 3);
        painter->setPen(textStrongColor());
        for (int index = 0; index < lines.size(); ++index) {
            painter->drawText(boxRect.left() + 10,
                              boxRect.top() + 9 + metrics.ascent() + index * metrics.lineSpacing(),
                              lines.at(index));
        }
    }

    int rowIndexAt(const int y) const
    {
        if (y < kRulerHeight) {
            return -1;
        }
        return (y - kRulerHeight + verticalOffset_) / rowHeight_;
    }

    int maxVerticalOffset() const
    {
        return qMax(0, rows().size() * rowHeight_ -
                         qMax(0, height() - kRulerHeight - kHorizontalScrollBarHeight));
    }

    void zoomRowsAt(const int delta, const double widgetY)
    {
        const int oldHeight = rowHeight_;
        const int newHeight = qBound(18, oldHeight + delta, 96);
        if (newHeight == oldHeight) return;
        const double contentY = qMax(0.0, widgetY - kRulerHeight + verticalOffset_);
        const double rowPosition = contentY / static_cast<double>(oldHeight);
        rowHeight_ = newHeight;
        verticalOffset_ = qBound(0,
                                 qRound(rowPosition * newHeight - qMax(0.0, widgetY - kRulerHeight)),
                                 maxVerticalOffset());
        setProperty("waveformRowHeight", rowHeight_);
        updateAccessibleDescription();
        update();
    }

    void ensureSelectedVisible()
    {
        const int bodyHeight = qMax(0, height() - kRulerHeight - kHorizontalScrollBarHeight);
        const int rowTop = selectedRow_ * rowHeight_;
        const int rowBottom = rowTop + rowHeight_;
        if (rowTop < verticalOffset_) {
            verticalOffset_ = rowTop;
        } else if (rowBottom > verticalOffset_ + bodyHeight) {
            verticalOffset_ = rowBottom - bodyHeight;
        }
        verticalOffset_ = qBound(0, verticalOffset_, maxVerticalOffset());
    }

    void toggleGroup(const QString& groupId)
    {
        if (expandedGroups_.contains(groupId)) {
            expandedGroups_.remove(groupId);
        } else {
            expandedGroups_.insert(groupId);
        }
        verticalOffset_ = qBound(0, verticalOffset_, maxVerticalOffset());
        updateAccessibleDescription();
        update();
    }

    void updateAccessibleDescription()
    {
        const QString cursor = hoverText();
        setProperty("waveformZoom", horizontalZoom_);
        setProperty("waveformRowHeight", rowHeight_);
        const WaveformTimeAxis visible = timeAxis();
        setProperty("waveformVisibleStart", visible.start);
        setProperty("waveformVisibleEnd", visible.end);
        setProperty("waveformScrollMaximum", horizontalScrollBar_->maximum());
        setAccessibleDescription(
            QStringLiteral("9 个协议组，%1 个已展开；缩放 %2x；方向键选择，左右键展开或折叠。%3")
                .arg(expandedGroups_.size())
                .arg(horizontalZoom_, 0, 'f', 1)
                .arg(cursor.isEmpty() ? QString() : QStringLiteral(" 鼠标指针: %1").arg(cursor)));
    }

    QString statusText_;
    QString pathText_;
    QString timeRangeText_;
    QVector<TraceGroupViewItem> groups_;
    QVector<TraceSampleViewItem> samples_;
    QSet<QString> expandedGroups_;
    int selectedRow_;
    int rowHeight_;
    int verticalOffset_;
    double horizontalZoom_;
    double timeUnitsPerPixel_;
    double visibleStartTime_;
    bool hoverActive_;
    QPoint hoverPoint_;
    qint64 hoverTime_;
    QScrollBar* horizontalScrollBar_;
    mutable int renderedProtocolEvents_;
    mutable QRect firstProtocolEventRect_;
    bool panning_;
    QPoint panStart_;
    double panStartOffset_;
};

QLabel* pageTitle(const QString& text, QWidget* const parent)
{
    QLabel* const label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("page_title"));
    return label;
}

QLabel* mutedLabel(const QString& text, QWidget* const parent)
{
    QLabel* const label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted_label"));
    label->setProperty("mutedLabel", true);
    label->setWordWrap(true);
    return label;
}

QGroupBox* panelBox(const QString& title, QWidget* const parent)
{
    QGroupBox* const group = new QGroupBox(title, parent);
    group->setProperty("panelBox", true);
    group->setAttribute(Qt::WA_StyledBackground, true);
    return group;
}

QScrollArea* scrollPage(QWidget* const content)
{
    QScrollArea* const scroll = new QScrollArea(content->parentWidget());
    scroll->setObjectName(QStringLiteral("workbench_scroll_page"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

QString pageIdForPage(const NavigationPage page)
{
    QString pageId;

    switch (page) {
    case NavigationPage::Project:
        pageId = QStringLiteral("project");
        break;
    case NavigationPage::Connection:
        pageId = QStringLiteral("connection");
        break;
    case NavigationPage::Mode:
        pageId = QStringLiteral("mode");
        break;
    case NavigationPage::RamProgram:
        pageId = QStringLiteral("ram_program");
        break;
    case NavigationPage::FaultInjection:
        pageId = QStringLiteral("fault_injection");
        break;
    case NavigationPage::SamplingConfig:
        pageId = QStringLiteral("sampling_config");
        break;
    case NavigationPage::ProgramRun:
        pageId = QStringLiteral("ram_program");
        break;
    case NavigationPage::Waveform:
        pageId = QStringLiteral("waveform");
        break;
    case NavigationPage::Protocol:
        pageId = QStringLiteral("protocol");
        break;
    case NavigationPage::Stats:
        pageId = QStringLiteral("stats");
        break;
    default:
        pageId = QStringLiteral("project");
        break;
    }

    return pageId;
}

}  // namespace

MainWindowShell::MainWindowShell(QWidget* const parent)
    : QMainWindow(parent),
      topStatusBar_(nullptr),
      pageStack_(nullptr),
      uiScale_(1.0),
      navButtons_(),
      pageIds_(),
      logTabs_(nullptr),
      logDetachButton_(nullptr),
      diagnosticsPanel_(nullptr),
      logClearButton_(nullptr),
      logEdit_(nullptr),
      serialOutputEdit_(nullptr),
      detachedLogDialog_(nullptr),
      detachedLogTabs_(nullptr),
      detachedLogEdit_(nullptr),
      detachedSerialOutputEdit_(nullptr),
      waveformDisplayWidget_(nullptr),
      detachedWaveformDialog_(nullptr),
      detachedWaveformDisplayWidget_(nullptr),
      waveformStatusText_(),
      waveformPathText_(),
      waveformTimeRangeText_(),
      waveformGroups_(),
      waveformSamples_()
{
    qRegisterMetaType<UiActionRequest>("lockstep::ui::UiActionRequest");
    qRegisterMetaType<UiWorkbenchState>("lockstep::ui::UiWorkbenchState");

    setCentralWidget(createWorkbenchShell());
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setWindowTitle(QStringLiteral("锁步研发测试系统上位机"));
    setWorkbenchState(makeDefaultWorkbenchState(UiMode::Test));
    setActivePage(QStringLiteral("project"));
    applyResponsiveScale();
}

void MainWindowShell::resizeEvent(QResizeEvent* const event)
{
    QMainWindow::resizeEvent(event);
    applyResponsiveScale();
}

void MainWindowShell::applyResponsiveScale()
{
    const double nextScale = scaleForWindowSize(size());
    const bool styleNeedsRefresh = (qAbs(nextScale - uiScale_) >= 0.02);
    uiScale_ = nextScale;

    if ((centralWidget() != nullptr) && styleNeedsRefresh) {
        UiTheme::applyWorkbenchStyle(centralWidget(), uiScale_);
    }
    if (topStatusBar_ != nullptr) {
        topStatusBar_->applyScale(uiScale_);
    }

    QSplitter* const reportSplit = findChild<QSplitter*>(QStringLiteral("report_main_split"));
    if (reportSplit != nullptr) {
        const bool compactReport = width() < 1500;
        reportSplit->setOrientation(compactReport ? Qt::Vertical : Qt::Horizontal);
        reportSplit->setMinimumHeight(scaledMetric(compactReport ? 360 : 220, uiScale_));
    }

    QFrame* const sidebar = findChild<QFrame*>(QStringLiteral("sidebar"));
    if (sidebar != nullptr) {
        sidebar->setFixedWidth(scaledMetric(kSidebarWidth, uiScale_));
    }

    if (diagnosticsPanel_ != nullptr) {
        const int diagnosticsHeight = qBound(
            scaledMetric(kDiagnosticsMinHeight, uiScale_),
            static_cast<int>(static_cast<double>(height()) * 0.22),
            scaledMetric(300, uiScale_));
        diagnosticsPanel_->setMinimumHeight(diagnosticsHeight);
        diagnosticsPanel_->setMaximumHeight(diagnosticsHeight);
    }

    if (logClearButton_ != nullptr) {
        logClearButton_->setFixedSize(scaledMetric(70, uiScale_), scaledMetric(24, uiScale_));
    }
    if (logDetachButton_ != nullptr) {
        const int buttonSize = scaledMetric(26, uiScale_);
        logDetachButton_->setFixedSize(buttonSize, buttonSize);
        logDetachButton_->setIconSize(QSize(scaledMetric(18, uiScale_), scaledMetric(18, uiScale_)));
    }
    if (logEdit_ != nullptr) {
        logEdit_->setMinimumHeight(scaledMetric(86, uiScale_));
    }
    if (serialOutputEdit_ != nullptr) {
        serialOutputEdit_->setMinimumHeight(scaledMetric(86, uiScale_));
    }

    for (QProgressBar* const progress : findChildren<QProgressBar*>()) {
        progress->setFixedHeight(scaledMetric(14, uiScale_));
    }

    QLineEdit* const programPathEdit = findChild<QLineEdit*>(QStringLiteral("program_image_path_edit"));
    if (programPathEdit != nullptr) {
        programPathEdit->setFixedHeight(scaledMetric(kProgramActionButtonHeight, uiScale_));
    }

    for (QPushButton* const button : findChildren<QPushButton*>()) {
        const QVariant page = button->property("uiPage");
        if (page.isValid() && (page.toInt() == static_cast<int>(NavigationPage::RamProgram))) {
            button->setFixedHeight(scaledMetric(kProgramActionButtonHeight, uiScale_));
        }
    }

    QPlainTextEdit* const taskDescriptionEdit =
        findChild<QPlainTextEdit*>(QStringLiteral("task_description_edit"));
    if (taskDescriptionEdit != nullptr) {
        taskDescriptionEdit->setMinimumHeight(scaledMetric(48, uiScale_));
        taskDescriptionEdit->setMaximumHeight(scaledMetric(66, uiScale_));
    }
    QPlainTextEdit* const taskBasicInfoEdit =
        findChild<QPlainTextEdit*>(QStringLiteral("task_basic_info_edit"));
    if (taskBasicInfoEdit != nullptr) {
        taskBasicInfoEdit->setMinimumHeight(scaledMetric(86, uiScale_));
    }
}

void MainWindowShell::setWorkbenchState(const UiWorkbenchState& state)
{
    setTopStatus(state.topStatus);

    if ((logEdit_ != nullptr) && !state.logText.isEmpty()) {
        logEdit_->setPlainText(state.logText);
        logEdit_->moveCursor(QTextCursor::End);
    }
    if ((serialOutputEdit_ != nullptr) && !state.serialText.isEmpty()) {
        serialOutputEdit_->setPlainText(state.serialText);
        serialOutputEdit_->moveCursor(QTextCursor::End);
    }
}

void MainWindowShell::setTopStatus(const GlobalStatus& status)
{
    if (topStatusBar_ != nullptr) {
        topStatusBar_->setStatus(status);
    }
}

void MainWindowShell::appendLog(
    const LogChannel channel,
    const LogLevel level,
    const QString& source,
    const QString& message)
{
    QPlainTextEdit* const view = (channel == LogChannel::Serial) ? serialOutputEdit_ : logEdit_;
    appendFormattedLog(view, level, source, message);
    if ((level == LogLevel::Error) && (topStatusBar_ != nullptr)) {
        GlobalStatus status = makeDefaultGlobalStatus(UiMode::Test);
        status.taskStatusText = QStringLiteral("任务: 阻断");
        status.targetStatusText = QStringLiteral("目标: 待检查");
        status.programStatusText = QStringLiteral("最近错误: %1").arg(message);
        topStatusBar_->setStatus(status);
    }
}

void MainWindowShell::setSerialPlaceholderText(const QString& text)
{
    if (serialOutputEdit_ != nullptr) {
        serialOutputEdit_->setPlainText(text);
    }
    if (detachedSerialOutputEdit_ != nullptr) {
        detachedSerialOutputEdit_->setPlainText(text);
    }
}

QString MainWindowShell::programImagePath() const
{
    const QWidget* const currentPage = (pageStack_ == nullptr) ? nullptr : pageStack_->currentWidget();
    const QLineEdit* const activeEdit =
        (currentPage == nullptr) ? nullptr : currentPage->findChild<QLineEdit*>(QStringLiteral("program_image_path_edit"));
    const QLineEdit* const edit =
        (activeEdit == nullptr) ? findChild<QLineEdit*>(QStringLiteral("program_image_path_edit")) : activeEdit;
    return (edit == nullptr) ? QString() : edit->text().trimmed();
}

void MainWindowShell::setProgramImagePath(const QString& path)
{
    const QList<QLineEdit*> edits = findChildren<QLineEdit*>(QStringLiteral("program_image_path_edit"));
    for (QLineEdit* const edit : edits) {
        edit->setText(path);
    }
}

void MainWindowShell::setProjectView(const QString& workspaceName, const QStringList& taskLines)
{
    QTreeWidget* const tree = findChild<QTreeWidget*>(QStringLiteral("project_browser_tree"));
    if (tree == nullptr) {
        return;
    }

    tree->clear();
    QTreeWidgetItem* const rootItem = new QTreeWidgetItem(tree, {workspaceName});
    QTreeWidgetItem* const tasksItem = new QTreeWidgetItem(rootItem, {QStringLiteral("tasks")});
    if (taskLines.isEmpty()) {
        new QTreeWidgetItem(tasksItem, {QStringLiteral("无任务")});
    } else {
        for (const QString& line : taskLines) {
            new QTreeWidgetItem(tasksItem, {line});
        }
    }
    new QTreeWidgetItem(rootItem, {QStringLiteral("reports")});
    tree->expandAll();
}

void MainWindowShell::setProjectTasks(
    const QString& workspaceName,
    const QVector<ProjectTaskViewItem>& tasks,
    const QString& selectedTaskId)
{
    QTreeWidget* const tree = findChild<QTreeWidget*>(QStringLiteral("project_browser_tree"));
    if (tree == nullptr) {
        return;
    }

    const QSignalBlocker blocker(tree);
    tree->clear();
    QTreeWidgetItem* const rootItem = new QTreeWidgetItem(tree, {workspaceName});
    QTreeWidgetItem* const tasksItem = new QTreeWidgetItem(rootItem, {QStringLiteral("tasks")});

    QTreeWidgetItem* selectedItem = nullptr;
    if (tasks.isEmpty()) {
        QTreeWidgetItem* const emptyItem = new QTreeWidgetItem(tasksItem, {QStringLiteral("当前工作区没有验证任务")});
        emptyItem->setFlags(Qt::ItemIsEnabled);
    } else {
        for (const ProjectTaskViewItem& task : tasks) {
            QTreeWidgetItem* const taskItem = new QTreeWidgetItem(tasksItem, {task.taskName});
            taskItem->setData(0, kTaskIdRole, task.taskId);
            taskItem->setData(0, kTaskDescriptionRole, task.description);
            taskItem->setData(0, kTaskBasicInfoRole, task.basicInfo);
            taskItem->setToolTip(0, QStringLiteral("%1\n%2").arg(task.taskId, task.updatedAtText));
            new QTreeWidgetItem(taskItem, {QStringLiteral("inputs")});
            new QTreeWidgetItem(taskItem, {QStringLiteral("evidence")});
            new QTreeWidgetItem(taskItem, {QStringLiteral("reports")});
            if (task.taskId == selectedTaskId) {
                selectedItem = taskItem;
            }
        }
    }

    new QTreeWidgetItem(rootItem, {QStringLiteral("system_logs")});
    rootItem->setExpanded(true);
    tasksItem->setExpanded(true);
    if (selectedItem != nullptr) {
        tree->setCurrentItem(selectedItem);
    }

    updateProjectTaskSelectionState();
}

void MainWindowShell::setTaskDetail(
    const QString& taskName,
    const QString& description,
    const QString& basicInfo)
{
    QLineEdit* const nameEdit = findChild<QLineEdit*>(QStringLiteral("task_name_edit"));
    QPlainTextEdit* const descriptionEdit = findChild<QPlainTextEdit*>(QStringLiteral("task_description_edit"));
    QPlainTextEdit* const infoEdit = findChild<QPlainTextEdit*>(QStringLiteral("task_basic_info_edit"));
    if (nameEdit != nullptr) {
        nameEdit->setText(taskName);
    }
    if (descriptionEdit != nullptr) {
        descriptionEdit->setPlainText(description);
    }
    if (infoEdit != nullptr) {
        infoEdit->setPlainText(basicInfo);
    }
}

void MainWindowShell::setTaskDetailEditing(const bool editing)
{
    QLineEdit* const nameEdit = findChild<QLineEdit*>(QStringLiteral("task_name_edit"));
    QPlainTextEdit* const descriptionEdit = findChild<QPlainTextEdit*>(QStringLiteral("task_description_edit"));
    if (nameEdit != nullptr) {
        nameEdit->setReadOnly(!editing);
    }
    if (descriptionEdit != nullptr) {
        descriptionEdit->setReadOnly(!editing);
    }
    setActionButtonsEnabled(UiAction::SaveTaskEdit, editing);
    setActionButtonsEnabled(UiAction::CancelTaskEdit, editing);
    setActionButtonsEnabled(UiAction::EditTask, !editing && !selectedProjectTaskId().isEmpty());
    setActionButtonsEnabled(UiAction::DeleteTask, !editing && !selectedProjectTaskId().isEmpty());
}

QString MainWindowShell::selectedProjectTaskId() const
{
    const QTreeWidget* const tree = findChild<QTreeWidget*>(QStringLiteral("project_browser_tree"));
    const QTreeWidgetItem* const item = (tree == nullptr) ? nullptr : tree->currentItem();
    return (item == nullptr) ? QString() : item->data(0, kTaskIdRole).toString();
}

QString MainWindowShell::taskNameText() const
{
    const QLineEdit* const edit = findChild<QLineEdit*>(QStringLiteral("task_name_edit"));
    return (edit == nullptr) ? QString() : edit->text().trimmed();
}

QString MainWindowShell::taskDescriptionText() const
{
    const QPlainTextEdit* const edit = findChild<QPlainTextEdit*>(QStringLiteral("task_description_edit"));
    return (edit == nullptr) ? QString() : edit->toPlainText();
}

void MainWindowShell::setWorkflowStatusText(const QString& text)
{
    QLabel* const label = findChild<QLabel*>(QStringLiteral("workflow_status_label"));
    if (label != nullptr) {
        label->setText(text);
    }
}

void MainWindowShell::setRamSummary(
    const QString& text,
    const int writeProgressPercent,
    const int readbackProgressPercent)
{
    setProgramSummaryPage(false);

    const QList<QProgressBar*> writeProgressBars =
        findChildren<QProgressBar*>(QStringLiteral("program_write_progress_bar"));
    const QList<QProgressBar*> readbackProgressBars =
        findChildren<QProgressBar*>(QStringLiteral("readback_verify_progress_bar"));
    const QList<QPlainTextEdit*> summaries = findChildren<QPlainTextEdit*>(QStringLiteral("ram_summary_edit"));
    for (QProgressBar* const progress : writeProgressBars) {
        progress->setValue(qBound(0, writeProgressPercent, 100));
    }
    for (QProgressBar* const progress : readbackProgressBars) {
        progress->setValue(qBound(0, readbackProgressPercent, 100));
    }
    for (QPlainTextEdit* const summary : summaries) {
        summary->setPlainText(text);
    }
}

void MainWindowShell::setRunSummary(
    const QString& text,
    const int runProgressPercent,
    const int stopProgressPercent)
{
    const QList<QProgressBar*> runProgressBars =
        findChildren<QProgressBar*>(QStringLiteral("program_run_progress_bar"));
    const QList<QProgressBar*> stopProgressBars =
        findChildren<QProgressBar*>(QStringLiteral("program_stop_progress_bar"));
    const QList<QPlainTextEdit*> summaries = findChildren<QPlainTextEdit*>(QStringLiteral("run_summary_edit"));
    for (QProgressBar* const progress : runProgressBars) {
        progress->setValue(qBound(0, runProgressPercent, 100));
    }
    for (QProgressBar* const progress : stopProgressBars) {
        progress->setValue(qBound(0, stopProgressPercent, 100));
    }
    for (QPlainTextEdit* const summary : summaries) {
        summary->setPlainText(text);
    }
}

void MainWindowShell::setProgramSummaryPage(const bool runSummary)
{
    QStackedWidget* const stack = findChild<QStackedWidget*>(QStringLiteral("program_summary_stack"));
    if (stack != nullptr) {
        stack->setCurrentIndex(runSummary ? 1 : 0);
    }

    QPushButton* const ramButton = findChild<QPushButton*>(QStringLiteral("program_summary_ram_button"));
    if (ramButton != nullptr) {
        ramButton->setChecked(!runSummary);
    }
    QPushButton* const runButton = findChild<QPushButton*>(QStringLiteral("program_summary_run_button"));
    if (runButton != nullptr) {
        runButton->setChecked(runSummary);
    }
}

void MainWindowShell::setActionButtonText(const UiAction action, const QString& text)
{
    const QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton* const button : buttons) {
        if (button != nullptr && button->property("uiAction").toInt() == static_cast<int>(action)) {
            button->setText(text);
        }
    }
}

void MainWindowShell::setWaveformTraceView(
    const QString& statusText,
    const QString& pathText,
    const QString& timeRangeText,
    const QVector<TraceGroupViewItem>& groups,
    const QVector<TraceSampleViewItem>& samples,
    const QStringList& keyBehaviors,
    const QStringList& diagnostics)
{
    Q_UNUSED(keyBehaviors);
    Q_UNUSED(diagnostics);

    QLabel* const statusLabel = findChild<QLabel*>(QStringLiteral("waveform_status_label"));
    QLabel* const timeRangeLabel = findChild<QLabel*>(QStringLiteral("waveform_time_range_label"));
    QLineEdit* const pathEdit = findChild<QLineEdit*>(QStringLiteral("waveform_trace_path_edit"));

    waveformStatusText_ = statusText;
    waveformPathText_ = pathText;
    waveformTimeRangeText_ = timeRangeText;
    waveformGroups_ = groups;
    waveformSamples_ = samples;

    if (statusLabel != nullptr) {
        statusLabel->setText(statusText);
    }
    if (timeRangeLabel != nullptr) {
        timeRangeLabel->setText(timeRangeText.isEmpty() ? QStringLiteral("时间范围: 未知") : QStringLiteral("时间范围: %1").arg(timeRangeText));
    }
    if (pathEdit != nullptr) {
        pathEdit->setText(pathText);
    }
    applyWaveformTraceToDisplay(waveformDisplayWidget_);
    applyWaveformTraceToDisplay(detachedWaveformDisplayWidget_);
}

void MainWindowShell::setProtocolAnalysisView(
    const QString& statusText,
    const QString& analysisPath,
    const QStringList& keyBehaviors,
    const QStringList& diagnostics)
{
    QLabel* const statusLabel = findChild<QLabel*>(QStringLiteral("protocol_status_label"));
    QLineEdit* const analysisEdit = findChild<QLineEdit*>(QStringLiteral("protocol_analysis_path_edit"));
    QTreeWidget* const keyTree = findChild<QTreeWidget*>(QStringLiteral("protocol_key_behaviors_tree"));
    QPlainTextEdit* const diagnosticsEdit = findChild<QPlainTextEdit*>(QStringLiteral("protocol_diagnostics_edit"));

    if (statusLabel != nullptr) {
        statusLabel->setText(QStringLiteral("%1 · %2 条协议事件")
                                 .arg(statusText)
                                 .arg(keyBehaviors.size()));
    }
    if (analysisEdit != nullptr) {
        analysisEdit->setText(analysisPath);
    }
    if (keyTree != nullptr) {
        keyTree->clear();
        int totalEvents = 0;
        int activeProtocolCount = 0;
        QStringList inactiveProtocols;
        QStringList unavailableProtocols;
        const TraceGroupViewItem* mismatchGroup = nullptr;
        for (const TraceGroupViewItem& group : waveformGroups_) {
            if (group.id == QStringLiteral("mismatch")) {
                mismatchGroup = &group;
                continue;
            }
            totalEvents += group.transactions.size();
            if (protocolGroupUnavailable(group)) {
                unavailableProtocols.append(QStringLiteral("%1: %2")
                    .arg(group.displayName, group.reason));
            } else if (group.transactions.isEmpty()) {
                inactiveProtocols.append(group.displayName);
            } else {
                ++activeProtocolCount;
            }
        }
        if (mismatchGroup != nullptr) totalEvents += mismatchGroup->transactions.size();

        QTreeWidgetItem* const overview = new QTreeWidgetItem(
            keyTree,
            {QStringLiteral("采集概览"), QStringLiteral("%1 条事件").arg(totalEvents),
             QStringLiteral("%1 个活跃协议").arg(activeProtocolCount)});
        QFont overviewFont = overview->font(0);
        overviewFont.setBold(true);
        for (int column = 0; column < 3; ++column) overview->setFont(column, overviewFont);
        if (!inactiveProtocols.isEmpty()) {
            new QTreeWidgetItem(
                overview,
                {QStringLiteral("未见活动"), QStringLiteral("%1 个").arg(inactiveProtocols.size()),
                 inactiveProtocols.join(QStringLiteral("、"))});
        }
        if (!unavailableProtocols.isEmpty()) {
            QTreeWidgetItem* const unavailable = new QTreeWidgetItem(
                overview,
                {QStringLiteral("设计缺口"), QStringLiteral("%1 个").arg(unavailableProtocols.size()),
                 unavailableProtocols.join(QStringLiteral("；"))});
            unavailable->setForeground(1, QBrush(QColor(QStringLiteral("#ad6800"))));
        }
        overview->setExpanded(true);

        const bool mismatchDetected = mismatchGroup != nullptr && !mismatchGroup->transactions.isEmpty();
        QTreeWidgetItem* const mismatchItem = new QTreeWidgetItem(
            keyTree,
            {QStringLiteral("锁步一致性"),
             mismatchDetected ? QStringLiteral("异常 · %1 条").arg(mismatchGroup->transactions.size())
                              : QStringLiteral("正常"),
             mismatchDetected ? QStringLiteral("检测到处理器内 Mismatch")
                              : QStringLiteral("处理器内无Mismatch")});
        const QColor mismatchColor(mismatchDetected ? QStringLiteral("#b42318")
                                                     : QStringLiteral("#2f7d4a"));
        mismatchItem->setForeground(1, QBrush(mismatchColor));
        mismatchItem->setForeground(2, QBrush(mismatchColor));
        if (mismatchDetected) {
            addRepresentativeProtocolEvents(mismatchItem, mismatchGroup->transactions);
            mismatchItem->setExpanded(true);
        }

        for (const TraceGroupViewItem& group : waveformGroups_) {
            if (group.id == QStringLiteral("mismatch") || group.transactions.isEmpty() ||
                protocolGroupUnavailable(group)) {
                continue;
            }
            QTreeWidgetItem* const protocolItem = new QTreeWidgetItem(
                keyTree,
                {group.displayName, QStringLiteral("%1 条").arg(group.transactions.size()),
                 protocolActivityBreakdown(group)});
            addRepresentativeProtocolEvents(protocolItem, group.transactions);
        }

        if (waveformGroups_.isEmpty() && !keyBehaviors.isEmpty()) {
            QTreeWidgetItem* const legacyItem = new QTreeWidgetItem(
                keyTree,
                {QStringLiteral("协议事件"), QStringLiteral("%1 条").arg(keyBehaviors.size()),
                 QStringLiteral("缺少协议分组信息")});
            addRepresentativeProtocolEvents(legacyItem, keyBehaviors);
        }
        keyTree->setProperty("protocolEventCount", totalEvents > 0 ? totalEvents : keyBehaviors.size());
        keyTree->setProperty("activeProtocolCount", activeProtocolCount);
        keyTree->setProperty("mismatchDetected", mismatchDetected);
    }
    if (diagnosticsEdit != nullptr) {
        diagnosticsEdit->setPlainText(diagnostics.isEmpty() ? QStringLiteral("暂无诊断。") : diagnostics.join(QLatin1Char('\n')));
    }
}

void MainWindowShell::setConnectionSummary(const QString& profileName, const QString& statusText)
{
    QLineEdit* const profileEdit = findChild<QLineEdit*>(QStringLiteral("profile_name_edit"));
    if (profileEdit != nullptr) {
        profileEdit->setText(profileName);
    }
    setWorkflowStatusText(statusText);
}

void MainWindowShell::setConnectionProfileDetails(
    const QString& profileName,
    const QString& host,
    const int tclPort,
    const int gdbPort,
    const int jtagKhz,
    const QString& ramBaseAddress,
    const QString& resetStrategy,
    const QString& statusText)
{
    QLineEdit* const profileEdit = findChild<QLineEdit*>(QStringLiteral("profile_name_edit"));
    QLineEdit* const hostEdit = findChild<QLineEdit*>(QStringLiteral("target_host_edit"));
    QSpinBox* const tclPortSpin = findChild<QSpinBox*>(QStringLiteral("target_tcl_port_spin"));
    QSpinBox* const gdbPortSpin = findChild<QSpinBox*>(QStringLiteral("target_gdb_port_spin"));
    QSpinBox* const jtagSpin = findChild<QSpinBox*>(QStringLiteral("target_jtag_khz_spin"));
    QLineEdit* const ramBaseEdit = findChild<QLineEdit*>(QStringLiteral("ram_base_address_edit"));
    QComboBox* const resetCombo = findChild<QComboBox*>(QStringLiteral("reset_strategy_combo"));

    if (profileEdit != nullptr) {
        profileEdit->setText(profileName);
    }
    if (hostEdit != nullptr) {
        hostEdit->setText(host);
    }
    if (tclPortSpin != nullptr) {
        tclPortSpin->setValue(tclPort);
    }
    if (gdbPortSpin != nullptr) {
        gdbPortSpin->setValue(gdbPort);
    }
    if (jtagSpin != nullptr) {
        jtagSpin->setValue(jtagKhz);
    }
    if (ramBaseEdit != nullptr) {
        ramBaseEdit->setText(ramBaseAddress);
    }
    if (resetCombo != nullptr) {
        const int index = resetCombo->findText(resetStrategy);
        if (index >= 0) {
            resetCombo->setCurrentIndex(index);
        }
    }
    setWorkflowStatusText(statusText);
}

void MainWindowShell::setConnectionDiagnostics(
    const QString& serviceState,
    const QString& targetState,
    const QString& precheckState,
    const QString& jtagIdcode,
    const QString& debugModule,
    const QString& sbaState,
    const QString& errorText,
    const QString& rawText)
{
    const QList<QPair<QString, QString>> labels = {
        {QStringLiteral("debug_service_state_label"), serviceState},
        {QStringLiteral("target_connection_state_label"), targetState},
        {QStringLiteral("precheck_state_label"), precheckState},
        {QStringLiteral("jtag_idcode_label"), jtagIdcode},
        {QStringLiteral("debug_module_label"), debugModule},
        {QStringLiteral("sba_state_label"), sbaState},
        {QStringLiteral("connection_error_label"), errorText.isEmpty() ? QStringLiteral("无") : errorText},
    };

    for (const QPair<QString, QString>& item : labels) {
        QLabel* const label = findChild<QLabel*>(item.first);
        if (label != nullptr) {
            label->setText(item.second);
        }
    }

    QPlainTextEdit* const rawEdit = findChild<QPlainTextEdit*>(QStringLiteral("connection_raw_return_edit"));
    if (rawEdit != nullptr) {
        rawEdit->setPlainText(rawText);
    }
}

void MainWindowShell::setSerialPorts(
    const QStringList& displayNames,
    const QStringList& portNames,
    const QString& statusText)
{
    QComboBox* const combo = findChild<QComboBox*>(QStringLiteral("serial_port_combo"));
    if (combo != nullptr) {
        const QString previousPort = combo->currentData().toString();
        const QString previousText = combo->currentText();
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (int index = 0; index < displayNames.size(); ++index) {
            const QString portName = (index < portNames.size()) ? portNames.at(index) : displayNames.at(index);
            combo->addItem(displayNames.at(index), portName);
        }
        if (!previousPort.isEmpty()) {
            const int index = combo->findData(previousPort);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        } else if (!previousText.isEmpty()) {
            const int index = combo->findText(previousText);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        }
    }
    setSerialStatus(statusText, false);
}

void MainWindowShell::setSerialStatus(const QString& statusText, const bool opened)
{
    QLabel* const label = findChild<QLabel*>(QStringLiteral("serial_status_label"));
    if (label != nullptr) {
        label->setText(statusText);
    }

    const QComboBox* const combo = findChild<QComboBox*>(QStringLiteral("serial_port_combo"));
    bool hasAvailablePort = false;
    if (combo != nullptr) {
        for (int index = 0; index < combo->count(); ++index) {
            if (!combo->itemData(index).toString().trimmed().isEmpty()) {
                hasAvailablePort = true;
                break;
            }
        }
    }

    const QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton* const button : buttons) {
        if (button != nullptr &&
            button->property("uiAction").toInt() == static_cast<int>(UiAction::ToggleSerialMonitor)) {
            button->setText(opened ? QStringLiteral("关闭串口") : QStringLiteral("打开串口"));
            button->setEnabled(opened || hasAvailablePort);
        }
    }
}

QString MainWindowShell::selectedSerialPortName() const
{
    const QComboBox* const combo = findChild<QComboBox*>(QStringLiteral("serial_port_combo"));
    if (combo == nullptr) {
        return QString();
    }
    const QString dataText = combo->currentData().toString().trimmed();
    return dataText.isEmpty() ? combo->currentText().trimmed() : dataText;
}

int MainWindowShell::selectedSerialBaudRate() const
{
    const QComboBox* const combo = findChild<QComboBox*>(QStringLiteral("serial_baud_combo"));
    bool ok = false;
    const int baudRate = (combo == nullptr) ? 0 : combo->currentText().trimmed().toInt(&ok);
    return ok ? baudRate : 115200;
}

void MainWindowShell::switchWorkbenchPage()
{
    const QPushButton* const button = qobject_cast<QPushButton*>(sender());
    if (button != nullptr) {
        setActivePage(button->property("pageId").toString());
    }
}

void MainWindowShell::clearVisibleLog()
{
    QPlainTextEdit* const view = currentLogView();
    if (view != nullptr) {
        view->clear();
    }
    if (view == logEdit_ && detachedLogEdit_ != nullptr) {
        detachedLogEdit_->clear();
    }
    if (view == serialOutputEdit_ && detachedSerialOutputEdit_ != nullptr) {
        detachedSerialOutputEdit_->clear();
    }
}

void MainWindowShell::toggleLogDetached()
{
    emitAction(UiAction::DetachLogWindow, pageForId(QStringLiteral("project")), QStringLiteral("log_detach_button"));

    if (detachedLogDialog_ != nullptr) {
        detachedLogDialog_->raise();
        detachedLogDialog_->activateWindow();
        return;
    }

    detachedLogDialog_ = new QDialog(this);
    detachedLogDialog_->setAttribute(Qt::WA_DeleteOnClose, true);
    detachedLogDialog_->setWindowTitle(QStringLiteral("Log"));
    detachedLogDialog_->resize(kDetachedLogWidth, kDetachedLogHeight);
    UiTheme::applyWorkbenchStyle(detachedLogDialog_);

    QVBoxLayout* const layout = new QVBoxLayout(detachedLogDialog_);
    layout->setContentsMargins(8, 8, 8, 8);

    detachedLogTabs_ = new QTabWidget(detachedLogDialog_);
    detachedLogTabs_->setObjectName(QStringLiteral("diagnostics_output_tabs"));
    detachedLogTabs_->setDocumentMode(true);
    detachedLogTabs_->tabBar()->setDrawBase(false);

    QWidget* const logPage = new QWidget(detachedLogTabs_);
    QVBoxLayout* const logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 6, 0, 0);
    detachedLogEdit_ = new QPlainTextEdit(logPage);
    detachedLogEdit_->setReadOnly(true);
    detachedLogEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    detachedLogEdit_->setPlainText((logEdit_ == nullptr) ? QString() : logEdit_->toPlainText());
    logLayout->addWidget(detachedLogEdit_, 1);

    QWidget* const serialPage = new QWidget(detachedLogTabs_);
    QVBoxLayout* const serialLayout = new QVBoxLayout(serialPage);
    serialLayout->setContentsMargins(0, 6, 0, 0);
    serialLayout->setSpacing(6);
    detachedSerialOutputEdit_ = new QPlainTextEdit(serialPage);
    detachedSerialOutputEdit_->setReadOnly(true);
    detachedSerialOutputEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    detachedSerialOutputEdit_->setPlainText((serialOutputEdit_ == nullptr) ? QString() : serialOutputEdit_->toPlainText());
    serialLayout->addWidget(detachedSerialOutputEdit_, 1);
    QWidget* const sendRow = new QWidget(serialPage);
    QHBoxLayout* const sendLayout = new QHBoxLayout(sendRow);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(6);
    QLineEdit* const sendInput = new QLineEdit(sendRow);
    sendInput->setPlaceholderText(QStringLiteral("输入要发送到串口的数据"));
    QPushButton* const sendButton = new QPushButton(QStringLiteral("发送"), sendRow);
    sendButton->setFixedWidth(54);
    sendLayout->addWidget(sendInput, 1);
    sendLayout->addWidget(sendButton);
    serialLayout->addWidget(sendRow, 0);

    const auto sendDetachedSerialText = [this, sendInput]() {
        if (sendInput == nullptr) {
            return;
        }
        UiActionRequest request;
        request.action = UiAction::SendSerialData;
        request.page = NavigationPage::Connection;
        request.objectName = QStringLiteral("detached_serial_send_input");
        request.parameters.insert(QStringLiteral("pageId"), pageIdForPage(NavigationPage::Connection));
        request.parameters.insert(QStringLiteral("actionText"), toDisplayText(UiAction::SendSerialData));
        request.parameters.insert(QStringLiteral("serialText"), sendInput->text());
        sendInput->clear();
        emit actionRequested(request);
    };
    connect(sendButton, &QPushButton::clicked, this, sendDetachedSerialText);
    connect(sendInput, &QLineEdit::returnPressed, this, sendDetachedSerialText);

    detachedLogTabs_->addTab(logPage, QStringLiteral("Log"));
    detachedLogTabs_->addTab(serialPage, QStringLiteral("串口监控"));
    if (logTabs_ != nullptr) {
        detachedLogTabs_->setCurrentIndex(logTabs_->currentIndex());
    }
    layout->addWidget(detachedLogTabs_, 1);

    connect(detachedLogDialog_, &QDialog::finished, this, [this]() {
        detachedLogDialog_ = nullptr;
        detachedLogTabs_ = nullptr;
        detachedLogEdit_ = nullptr;
        detachedSerialOutputEdit_ = nullptr;
    });
    detachedLogDialog_->show();
}

void MainWindowShell::showWaveformEmbedded()
{
    if (detachedWaveformDialog_ != nullptr) {
        detachedWaveformDialog_->close();
    }
    if (waveformDisplayWidget_ != nullptr) {
        waveformDisplayWidget_->show();
        applyWaveformTraceToDisplay(waveformDisplayWidget_);
    }
}

void MainWindowShell::showWaveformDetached()
{
    if (detachedWaveformDialog_ != nullptr) {
        detachedWaveformDialog_->raise();
        detachedWaveformDialog_->activateWindow();
        return;
    }

    detachedWaveformDialog_ = new QDialog(this);
    detachedWaveformDialog_->setAttribute(Qt::WA_DeleteOnClose, true);
    detachedWaveformDialog_->setWindowTitle(
        QStringLiteral("波形与协议 - lockstep_ui_preview"));
    detachedWaveformDialog_->setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    detachedWaveformDialog_->setSizeGripEnabled(true);
    detachedWaveformDialog_->setMinimumSize(760, 460);
    detachedWaveformDialog_->resize(1280, 760);
    detachedWaveformDialog_->setObjectName(QStringLiteral("waveform_detached_dialog"));

    QVBoxLayout* const layout = new QVBoxLayout(detachedWaveformDialog_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(0);

    detachedWaveformDisplayWidget_ = new WaveformDisplayWidget(detachedWaveformDialog_);
    layout->addWidget(detachedWaveformDisplayWidget_, 1);
    applyWaveformTraceToDisplay(detachedWaveformDisplayWidget_);

    connect(detachedWaveformDialog_, &QDialog::finished, this, [this]() {
        detachedWaveformDialog_ = nullptr;
        detachedWaveformDisplayWidget_ = nullptr;
    });

    detachedWaveformDialog_->show();
    detachedWaveformDialog_->raise();
    detachedWaveformDialog_->activateWindow();
}

void MainWindowShell::updateProjectTaskSelectionState()
{
    const QString taskId = selectedProjectTaskId();
    const bool hasTaskSelection = !taskId.isEmpty();
    const QTreeWidget* const tree = findChild<QTreeWidget*>(QStringLiteral("project_browser_tree"));
    const QTreeWidgetItem* const item = (tree == nullptr) ? nullptr : tree->currentItem();

    if (hasTaskSelection && item != nullptr) {
        setTaskDetail(
            item->text(0),
            item->data(0, kTaskDescriptionRole).toString(),
            item->data(0, kTaskBasicInfoRole).toString());
    } else {
        setTaskDetail(
            QString(),
            QString(),
            QStringLiteral("请选择一个验证任务。"));
    }

    setActionButtonsEnabled(UiAction::LoadTaskToWorkbench, hasTaskSelection);
    setActionButtonsEnabled(UiAction::DeleteTask, hasTaskSelection);
    setActionButtonsEnabled(UiAction::EditTask, hasTaskSelection);
    setActionButtonsEnabled(UiAction::SaveTaskEdit, false);
    setActionButtonsEnabled(UiAction::CancelTaskEdit, false);

    QLineEdit* const nameEdit = findChild<QLineEdit*>(QStringLiteral("task_name_edit"));
    QPlainTextEdit* const descriptionEdit = findChild<QPlainTextEdit*>(QStringLiteral("task_description_edit"));
    if (nameEdit != nullptr) {
        nameEdit->setReadOnly(true);
    }
    if (descriptionEdit != nullptr) {
        descriptionEdit->setReadOnly(true);
    }
}

QWidget* MainWindowShell::createWorkbenchShell()
{
    QWidget* const root = new QWidget(this);
    root->setObjectName(QStringLiteral("workbench_shell"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    UiTheme::applyWorkbenchStyle(root);

    QVBoxLayout* const layout = new QVBoxLayout(root);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);
    layout->addWidget(createTopBar(root));

    QWidget* const body = new QWidget(root);
    body->setObjectName(QStringLiteral("workbench_body"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* const bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(createSidebar(body));
    bodyLayout->addWidget(createPageContainer(body), 1);
    layout->addWidget(body, 1);

    return root;
}

QWidget* MainWindowShell::createTopBar(QWidget* const parent)
{
    topStatusBar_ = new TopStatusBar(parent);
    return topStatusBar_;
}

QWidget* MainWindowShell::createSidebar(QWidget* const parent)
{
    QFrame* const sidebar = new QFrame(parent);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(kSidebarWidth);

    QVBoxLayout* const layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(3);

    QLabel* const caption = new QLabel(QStringLiteral("WORKSPACE"), sidebar);
    caption->setObjectName(QStringLiteral("sidebar_caption"));
    layout->addWidget(caption);

    const QList<QPair<QString, QString>> nav = {
        {QStringLiteral("project"), QStringLiteral("任务管理")},
        {QStringLiteral("connection"), QStringLiteral("目标连接")},
        {QStringLiteral("mode"), QStringLiteral("工作模式")},
        {QStringLiteral("fault_injection"), QStringLiteral("错误注入")},
        {QStringLiteral("sampling_config"), QStringLiteral("采样配置")},
        {QStringLiteral("ram_program"), QStringLiteral("程序烧录与运行")},
        {QStringLiteral("waveform"), QStringLiteral("波形显示")},
        {QStringLiteral("protocol"), QStringLiteral("协议解析")},
        {QStringLiteral("stats"), QStringLiteral("测试报告")},
    };

    for (const QPair<QString, QString>& item : nav) {
        layout->addWidget(createNavButton(item.first, item.second, sidebar));
    }

    layout->addStretch(1);
    return sidebar;
}

QWidget* MainWindowShell::createPageContainer(QWidget* const parent)
{
    QWidget* const container = new QWidget(parent);
    container->setObjectName(QStringLiteral("page_container"));
    container->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* const layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    pageStack_ = new QStackedWidget(container);
    pageStack_->setObjectName(QStringLiteral("page_stack"));
    pageStack_->setAttribute(Qt::WA_StyledBackground, true);
    pageStack_->setMinimumHeight(72);
    pageStack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    addWorkbenchPage(QStringLiteral("project"), NavigationPage::Project, createProjectPage());
    addWorkbenchPage(QStringLiteral("connection"), NavigationPage::Connection, createConnectionPage());
    addWorkbenchPage(QStringLiteral("mode"), NavigationPage::Mode, createModePage());
    addWorkbenchPage(QStringLiteral("fault_injection"), NavigationPage::FaultInjection, createEmptyPage(QStringLiteral("错误注入")));
    addWorkbenchPage(QStringLiteral("sampling_config"), NavigationPage::SamplingConfig, createSamplingConfigPage());
    addWorkbenchPage(QStringLiteral("ram_program"), NavigationPage::RamProgram, createRamProgramPage());
    addWorkbenchPage(QStringLiteral("waveform"), NavigationPage::Waveform, createWaveformPage());
    addWorkbenchPage(QStringLiteral("protocol"), NavigationPage::Protocol, createProtocolPage());
    addWorkbenchPage(QStringLiteral("stats"), NavigationPage::Stats, createStatsPage());
    layout->addWidget(pageStack_, 9);

    layout->addWidget(createDiagnosticsPanel(container));
    return container;
}

QWidget* MainWindowShell::createProjectPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_project"));
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(10);

    QHBoxLayout* const header = new QHBoxLayout();
    header->addWidget(pageTitle(QStringLiteral("任务管理"), content));
    header->addStretch(1);
    header->addWidget(createActionButton(UiAction::NewTask, NavigationPage::Project, content, false));
    header->addWidget(createActionButton(UiAction::SaveTask, NavigationPage::Project, content, true));
    QPushButton* const loadButton = createActionButton(UiAction::LoadTaskToWorkbench, NavigationPage::Project, content, false);
    loadButton->setEnabled(false);
    header->addWidget(loadButton);
    QPushButton* const deleteButton = createActionButton(UiAction::DeleteTask, NavigationPage::Project, content, false);
    deleteButton->setEnabled(false);
    header->addWidget(deleteButton);
    layout->addLayout(header);

    QGridLayout* const mainGrid = new QGridLayout();
    mainGrid->setSpacing(14);

    QGroupBox* const browser = panelBox(QStringLiteral("项目浏览器"), content);
    browser->setMinimumWidth(420);
    browser->setMinimumHeight(180);
    browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* const browserLayout = new QVBoxLayout(browser);
    QTreeWidget* const projectTree = new QTreeWidget(browser);
    projectTree->setObjectName(QStringLiteral("project_browser_tree"));
    projectTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    projectTree->setHeaderLabel(QStringLiteral("工作区内容"));
    projectTree->setSelectionMode(QAbstractItemView::SingleSelection);
    projectTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QTreeWidgetItem* const rootItem = new QTreeWidgetItem(projectTree, {QStringLiteral("UI 占位工作区")});
    QTreeWidgetItem* const tasksItem = new QTreeWidgetItem(rootItem, {QStringLiteral("tasks")});
    new QTreeWidgetItem(tasksItem, {QStringLiteral("验证任务占位")});
    new QTreeWidgetItem(rootItem, {QStringLiteral("reports")});
    projectTree->expandAll();
    connect(projectTree, &QTreeWidget::itemSelectionChanged, this, &MainWindowShell::updateProjectTaskSelectionState);
    browserLayout->addWidget(projectTree, 1);
    mainGrid->addWidget(browser, 0, 0);

    QGroupBox* const detail = panelBox(QStringLiteral("任务详情"), content);
    detail->setMinimumHeight(180);
    detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* const detailLayout = new QVBoxLayout(detail);
    QGridLayout* const detailGrid = new QGridLayout();
    detailGrid->setHorizontalSpacing(10);
    detailGrid->setVerticalSpacing(12);
    detailGrid->setColumnStretch(0, 0);
    detailGrid->setColumnStretch(1, 1);
    detailGrid->setRowStretch(0, 0);
    detailGrid->setRowStretch(1, 0);
    detailGrid->setRowStretch(2, 1);
    QLineEdit* const taskNameEdit = new QLineEdit(detail);
    taskNameEdit->setObjectName(QStringLiteral("task_name_edit"));
    taskNameEdit->setReadOnly(true);
    taskNameEdit->setText(QStringLiteral("验证任务占位"));
    QPlainTextEdit* const taskDescriptionEdit = new QPlainTextEdit(detail);
    taskDescriptionEdit->setObjectName(QStringLiteral("task_description_edit"));
    taskDescriptionEdit->setReadOnly(true);
    taskDescriptionEdit->setMinimumHeight(48);
    taskDescriptionEdit->setMaximumHeight(66);
    taskDescriptionEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    taskDescriptionEdit->setPlainText(QStringLiteral("这里仅展示 UI 占位，不读取真实工作区。"));
    QPlainTextEdit* const taskBasicInfoEdit = new QPlainTextEdit(detail);
    taskBasicInfoEdit->setObjectName(QStringLiteral("task_basic_info_edit"));
    taskBasicInfoEdit->setReadOnly(true);
    taskBasicInfoEdit->setMinimumHeight(86);
    taskBasicInfoEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    taskBasicInfoEdit->setPlainText(QStringLiteral("模式: 占位\n状态: 未加载"));
    detailGrid->addWidget(new QLabel(QStringLiteral("任务名称"), detail), 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    detailGrid->addWidget(taskNameEdit, 0, 1);
    detailGrid->addWidget(new QLabel(QStringLiteral("描述"), detail), 1, 0, Qt::AlignLeft | Qt::AlignTop);
    detailGrid->addWidget(taskDescriptionEdit, 1, 1);
    detailGrid->addWidget(new QLabel(QStringLiteral("基本信息"), detail), 2, 0, Qt::AlignLeft | Qt::AlignTop);
    detailGrid->addWidget(taskBasicInfoEdit, 2, 1);
    detailLayout->addLayout(detailGrid, 1);
    QHBoxLayout* const editButtons = new QHBoxLayout();
    editButtons->addStretch(1);
    editButtons->addWidget(createActionButton(UiAction::EditTask, NavigationPage::Project, detail, false));
    editButtons->addWidget(createActionButton(UiAction::SaveTaskEdit, NavigationPage::Project, detail, true));
    editButtons->addWidget(createActionButton(UiAction::CancelTaskEdit, NavigationPage::Project, detail, false));
    detailLayout->addLayout(editButtons);
    setActionButtonsEnabled(UiAction::LoadTaskToWorkbench, false);
    setActionButtonsEnabled(UiAction::DeleteTask, false);
    setActionButtonsEnabled(UiAction::EditTask, false);
    setActionButtonsEnabled(UiAction::SaveTaskEdit, false);
    setActionButtonsEnabled(UiAction::CancelTaskEdit, false);
    mainGrid->addWidget(detail, 0, 1);

    mainGrid->setColumnStretch(0, 1);
    mainGrid->setColumnStretch(1, 2);
    mainGrid->setRowStretch(0, 1);
    layout->addLayout(mainGrid, 1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createConnectionPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_connection"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("目标连接"), content));
    layout->addWidget(createControlPanel());
    layout->addWidget(createSerialConfigPanel());
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createModePage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_mode"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("工作模式"), content));

    QGroupBox* const group = panelBox(QStringLiteral("处理器工作模式期望"), content);
    QFormLayout* const form = new QFormLayout(group);
    QComboBox* const modeCombo = new WorkbenchComboBox(group);
    modeCombo->addItem(QStringLiteral("双核锁步模式"));
    modeCombo->addItem(QStringLiteral("双核独立模式"));
    form->addRow(QStringLiteral("模式"), modeCombo);
    form->addRow(QStringLiteral("控制状态"), mutedLabel(QStringLiteral("仅记录期望模式，控制通道未接入。"), group));
    layout->addWidget(group);
    layout->addWidget(createTodoCard(QStringLiteral("TODO: 工作模式控制后端"),
                                     QStringLiteral("目标配置(profile)尚未声明寄存器写入、程序约定或外部开关；当前不得声称目标已切换成功。"),
                                     content));
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createRamProgramPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_ram_program"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(12);
    layout->addWidget(pageTitle(QStringLiteral("程序烧录与运行"), content));

    QSplitter* const split = new QSplitter(Qt::Horizontal, content);
    split->setChildrenCollapsible(false);

    QWidget* const left = new QWidget(split);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    QGroupBox* const imagePanel = panelBox(QStringLiteral("程序镜像"), left);
    imagePanel->setObjectName(QStringLiteral("image_load"));
    QFormLayout* const imageLayout = new QFormLayout(imagePanel);
    imageLayout->setContentsMargins(2, 2, 2, 2);
    imageLayout->setHorizontalSpacing(8);
    imageLayout->setVerticalSpacing(6);
    imageLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QLineEdit* const programImageEdit = new QLineEdit(imagePanel);
    programImageEdit->setObjectName(QStringLiteral("program_image_path_edit"));
    programImageEdit->setFixedHeight(kProgramActionButtonHeight);
    QPushButton* const browseProgramButton =
        createActionButton(UiAction::BrowseProgramImage, NavigationPage::RamProgram, imagePanel, false);
    browseProgramButton->setFixedHeight(kProgramActionButtonHeight);
    imageLayout->addRow(
        QStringLiteral("程序"),
        createPathInputRow(
            imagePanel,
            programImageEdit,
            browseProgramButton));
    leftLayout->addWidget(imagePanel);

    QGroupBox* const operationPanel = panelBox(QStringLiteral("程序操作"), left);
    QGridLayout* const operationLayout = new QGridLayout(operationPanel);
    operationLayout->setContentsMargins(2, 2, 2, 2);
    operationLayout->setHorizontalSpacing(8);
    operationLayout->setVerticalSpacing(6);
    operationLayout->setColumnStretch(0, 1);
    operationLayout->setColumnStretch(1, 1);
    QPushButton* const programButton =
        createActionButton(UiAction::ProgramImage, NavigationPage::RamProgram, operationPanel, false);
    QPushButton* const verifyButton =
        createActionButton(UiAction::VerifyReadback, NavigationPage::RamProgram, operationPanel, false);
    QPushButton* const runButton =
        createActionButton(UiAction::RunProgram, NavigationPage::RamProgram, operationPanel, true);
    QPushButton* const stopButton =
        createActionButton(UiAction::StopProgram, NavigationPage::RamProgram, operationPanel, false);
    stopButton->setProperty("danger_button", true);
    for (QPushButton* const button : {programButton, verifyButton, runButton, stopButton}) {
        button->setFixedHeight(kProgramActionButtonHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    operationLayout->addWidget(programButton, 0, 0);
    operationLayout->addWidget(verifyButton, 0, 1);
    operationLayout->addWidget(runButton, 1, 0);
    operationLayout->addWidget(stopButton, 1, 1);
    leftLayout->addWidget(operationPanel);

    QGroupBox* const progressPanel = panelBox(QStringLiteral("进度"), left);
    QFormLayout* const progressLayout = new QFormLayout(progressPanel);
    progressLayout->setContentsMargins(2, 0, 2, 0);
    progressLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    progressLayout->setHorizontalSpacing(8);
    progressLayout->setVerticalSpacing(4);
    QProgressBar* const writeProgress = new QProgressBar(progressPanel);
    writeProgress->setObjectName(QStringLiteral("program_write_progress_bar"));
    writeProgress->setRange(0, 100);
    writeProgress->setValue(0);
    QProgressBar* const readbackProgress = new QProgressBar(progressPanel);
    readbackProgress->setObjectName(QStringLiteral("readback_verify_progress_bar"));
    readbackProgress->setRange(0, 100);
    readbackProgress->setValue(0);
    QProgressBar* const runProgress = new QProgressBar(progressPanel);
    runProgress->setObjectName(QStringLiteral("program_run_progress_bar"));
    runProgress->setRange(0, 100);
    runProgress->setValue(0);
    QProgressBar* const stopProgress = new QProgressBar(progressPanel);
    stopProgress->setObjectName(QStringLiteral("program_stop_progress_bar"));
    stopProgress->setRange(0, 100);
    stopProgress->setValue(0);
    for (QProgressBar* const progress : {writeProgress, readbackProgress, runProgress, stopProgress}) {
        progress->setFixedHeight(14);
    }
    progressLayout->addRow(QStringLiteral("烧录进度"), writeProgress);
    progressLayout->addRow(QStringLiteral("回读进度"), readbackProgress);
    progressLayout->addRow(QStringLiteral("运行进度"), runProgress);
    progressLayout->addRow(QStringLiteral("终止进度"), stopProgress);
    leftLayout->addWidget(progressPanel);
    leftLayout->addStretch(1);
    split->addWidget(left);

    QGroupBox* const right = panelBox(QStringLiteral("摘要"), split);
    QVBoxLayout* const rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(10, 12, 10, 10);
    rightLayout->setSpacing(10);

    QWidget* const summaryButtonRow = new QWidget(right);
    QHBoxLayout* const summaryButtonLayout = new QHBoxLayout(summaryButtonRow);
    summaryButtonLayout->setContentsMargins(0, 0, 0, 0);
    summaryButtonLayout->setSpacing(8);
    QPushButton* const ramSummaryButton = new QPushButton(QStringLiteral("烧录与回读摘要"), summaryButtonRow);
    ramSummaryButton->setObjectName(QStringLiteral("program_summary_ram_button"));
    ramSummaryButton->setProperty("summaryTab", true);
    ramSummaryButton->setCheckable(true);
    ramSummaryButton->setChecked(true);
    QPushButton* const runSummaryButton = new QPushButton(QStringLiteral("程序运行与终止摘要"), summaryButtonRow);
    runSummaryButton->setObjectName(QStringLiteral("program_summary_run_button"));
    runSummaryButton->setProperty("summaryTab", true);
    runSummaryButton->setCheckable(true);
    summaryButtonLayout->addWidget(ramSummaryButton, 0);
    summaryButtonLayout->addWidget(runSummaryButton, 0);
    summaryButtonLayout->addStretch(1);
    rightLayout->addWidget(summaryButtonRow, 0);

    QStackedWidget* const summaryStack = new QStackedWidget(right);
    summaryStack->setObjectName(QStringLiteral("program_summary_stack"));
    QPlainTextEdit* const readbackView = new QPlainTextEdit(summaryStack);
    readbackView->setObjectName(QStringLiteral("ram_summary_edit"));
    readbackView->setReadOnly(true);
    readbackView->setPlainText(QStringLiteral("烧录记录和回读校验摘要将在这里显示。"));
    QPlainTextEdit* const runSummaryView = new QPlainTextEdit(summaryStack);
    runSummaryView->setObjectName(QStringLiteral("run_summary_edit"));
    runSummaryView->setReadOnly(true);
    runSummaryView->setPlainText(QStringLiteral("程序运行与终止摘要将在这里显示。"));
    summaryStack->addWidget(readbackView);
    summaryStack->addWidget(runSummaryView);
    summaryStack->setCurrentIndex(0);
    rightLayout->addWidget(summaryStack, 1);

    connect(ramSummaryButton, &QPushButton::clicked, this, [this]() {
        setProgramSummaryPage(false);
    });
    connect(runSummaryButton, &QPushButton::clicked, this, [this]() {
        setProgramSummaryPage(true);
    });
    split->addWidget(right);

    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);
    split->setSizes({kProgramPageLeftInitialWidth, kProgramPageRightInitialWidth});
    layout->addWidget(split, 1);
    return content;
}

QWidget* MainWindowShell::createSamplingConfigPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_sampling_config"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(12);
    layout->addWidget(pageTitle(QStringLiteral("采样配置"), content));

    QGroupBox* const triggerPanel = panelBox(QStringLiteral("触发条件"), content);
    QGridLayout* const triggerLayout = new QGridLayout(triggerPanel);
    triggerLayout->setColumnStretch(0, 0);
    triggerLayout->setColumnStretch(1, 1);
    triggerLayout->setHorizontalSpacing(10);
    triggerLayout->setVerticalSpacing(10);

    QLineEdit* const triggerAddrEdit = new QLineEdit(triggerPanel);
    triggerAddrEdit->setObjectName(QStringLiteral("sampling_trigger_addr_edit"));
    triggerAddrEdit->setText(QStringLiteral("0x00000000"));
    triggerAddrEdit->setPlaceholderText(QStringLiteral("0x00000000"));
    triggerAddrEdit->setClearButtonEnabled(true);
    triggerLayout->addWidget(new QLabel(QStringLiteral("PC addr == x"), triggerPanel), 0, 0);
    triggerLayout->addWidget(triggerAddrEdit, 0, 1);

    QLabel* const mismatchTitle = new QLabel(QStringLiteral("会引起触发的mismatch："), triggerPanel);
    triggerLayout->addWidget(mismatchTitle, 1, 0, 1, 2);

    QWidget* const mismatchGrid = new QWidget(triggerPanel);
    mismatchGrid->setObjectName(QStringLiteral("sampling_mismatch_grid"));
    QGridLayout* const mismatchLayout = new QGridLayout(mismatchGrid);
    mismatchLayout->setContentsMargins(0, 0, 0, 0);
    mismatchLayout->setHorizontalSpacing(28);
    mismatchLayout->setVerticalSpacing(8);
    const QStringList& mismatchNames = samplingMismatchDescriptions();
    for (int bit = 0; bit < kSamplingMismatchBits; ++bit) {
        QCheckBox* const bitCheck = new QCheckBox(
            QStringLiteral("mismatch [%1] : %2").arg(bit).arg(mismatchNames.at(bit)),
            mismatchGrid);
        bitCheck->setObjectName(QStringLiteral("sampling_mismatch_bit_%1_check").arg(bit));
        bitCheck->setChecked(true);
        bitCheck->setProperty("mismatchBit", bit);
        mismatchLayout->addWidget(bitCheck, bit / 2, bit % 2);
    }
    triggerLayout->addWidget(mismatchGrid, 2, 0, 1, 2);
    triggerLayout->addWidget(
        mutedLabel(QStringLiteral("触发逻辑：(valid == 1 && ready == 1 && addr == x) || 勾选的 mismatch 任意一位 0->1。"), triggerPanel),
        3,
        0,
        1,
        2);
    triggerLayout->addWidget(
        mutedLabel(QStringLiteral("采样窗口：采样窗口为4096点，触发前2047点，触发时1点触发后2048点。"), triggerPanel),
        4,
        0,
        1,
        2);
    layout->addWidget(triggerPanel);

    QGroupBox* const actionPanel = panelBox(QStringLiteral("配置操作"), content);
    QHBoxLayout* const actionLayout = new QHBoxLayout(actionPanel);
    actionLayout->addStretch(1);
    QPushButton* const sendButton =
        createActionButton(UiAction::SendSamplingConfig, NavigationPage::SamplingConfig, actionPanel, true);
    sendButton->setMinimumHeight(38);
    actionLayout->addWidget(sendButton);
    layout->addWidget(actionPanel);
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createEmptyPage(const QString& title)
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_empty_%1").arg(title));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(title, content));
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createWaveformPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_waveform"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(8);

    QHBoxLayout* const header = new QHBoxLayout();
    header->setSpacing(8);
    QWidget* const titleControls = new QWidget(content);
    titleControls->setObjectName(QStringLiteral("waveform_title_controls"));
    titleControls->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const titleControlsLayout = new QVBoxLayout(titleControls);
    titleControlsLayout->setContentsMargins(0, 0, 0, 0);
    titleControlsLayout->setSpacing(8);
    QHBoxLayout* const titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(pageTitle(QStringLiteral("波形显示"), titleControls));
    QLabel* const statusLabel = mutedLabel(QStringLiteral("等待读取当前任务波形。"), titleControls);
    statusLabel->setObjectName(QStringLiteral("waveform_status_label"));
    statusLabel->setWordWrap(false);
    statusLabel->setMinimumWidth(120);
    titleRow->addWidget(statusLabel);
    titleRow->addStretch(1);
    QPushButton* const embeddedButton = createActionButton(UiAction::ShowWaveformEmbedded, NavigationPage::Waveform, titleControls, false);
    QPushButton* const detachedButton = createActionButton(UiAction::ShowWaveformDetached, NavigationPage::Waveform, titleControls, false);
    embeddedButton->setText(QStringLiteral("内嵌显示"));
    detachedButton->setText(QStringLiteral("独立显示"));
    connect(embeddedButton, &QPushButton::clicked, this, &MainWindowShell::showWaveformEmbedded);
    connect(detachedButton, &QPushButton::clicked, this, &MainWindowShell::showWaveformDetached);
    titleRow->addWidget(embeddedButton);
    titleRow->addWidget(detachedButton);
    titleControlsLayout->addLayout(titleRow);
    header->addWidget(titleControls, 0, Qt::AlignTop);

    QWidget* const inputPanel = new QWidget(content);
    inputPanel->setObjectName(QStringLiteral("waveform_inputs_header_panel"));
    QGridLayout* const inputGrid = new QGridLayout(inputPanel);
    inputGrid->setContentsMargins(0, 0, 0, 0);
    inputGrid->setHorizontalSpacing(6);
    inputGrid->setVerticalSpacing(4);
    QLabel* const label = new QLabel(QStringLiteral("波形文件"), inputPanel);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QLineEdit* const edit = new QLineEdit(inputPanel);
    edit->setObjectName(QStringLiteral("waveform_trace_path_edit"));
    edit->setReadOnly(true);
    edit->setPlaceholderText(QStringLiteral("当前任务 waveform/lockstep_trace.vcd"));
    QPushButton* const importButton =
        createActionButton(UiAction::BrowseWaveform, NavigationPage::Waveform, inputPanel, true);
    importButton->setObjectName(QStringLiteral("waveform_import_vcd_button"));
    importButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    importButton->setToolTip(QStringLiteral("选择 VCD 并导入当前任务"));
    inputGrid->addWidget(label, 0, 0);
    inputGrid->addWidget(edit, 0, 1);
    inputGrid->addWidget(importButton, 0, 2);
    inputGrid->setColumnStretch(1, 1);
    header->addWidget(inputPanel, 1);
    layout->addLayout(header);

    QLabel* const timeRangeLabel = mutedLabel(QStringLiteral("时间范围: 未知"), content);
    timeRangeLabel->setObjectName(QStringLiteral("waveform_time_range_label"));
    timeRangeLabel->setWordWrap(false);
    layout->addWidget(timeRangeLabel);

    QWidget* const analyzerPanel = new QWidget(content);
    analyzerPanel->setObjectName(QStringLiteral("waveform_analyzer_panel"));
    analyzerPanel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const analyzerLayout = new QVBoxLayout(analyzerPanel);
    analyzerLayout->setContentsMargins(0, 0, 0, 0);
    analyzerLayout->setSpacing(0);
    waveformDisplayWidget_ = new WaveformDisplayWidget(analyzerPanel);
    analyzerLayout->addWidget(waveformDisplayWidget_, 1);
    applyWaveformTraceToDisplay(waveformDisplayWidget_);
    layout->addWidget(analyzerPanel, 1);
    return content;
}

QWidget* MainWindowShell::createProtocolPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_protocol"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    QHBoxLayout* const header = new QHBoxLayout();
    header->addWidget(pageTitle(QStringLiteral("协议解析"), content));
    QLabel* const statusLabel = mutedLabel(QStringLiteral("等待解析当前任务固定 VCD。"), content);
    statusLabel->setObjectName(QStringLiteral("protocol_status_label"));
    statusLabel->setWordWrap(false);
    header->addWidget(statusLabel);
    header->addStretch(1);
    layout->addLayout(header);

    QGroupBox* const inputPanel = panelBox(QStringLiteral("当前任务协议解析"), content);
    QFormLayout* const form = new QFormLayout(inputPanel);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QLineEdit* const vcdEdit = new QLineEdit(inputPanel);
    vcdEdit->setObjectName(QStringLiteral("protocol_analysis_path_edit"));
    vcdEdit->setReadOnly(true);
    vcdEdit->setPlaceholderText(QStringLiteral("当前任务 waveform/lockstep_trace_analysis.json"));
    form->addRow(QStringLiteral("解析结果"),
                 createPathInputRow(inputPanel,
                                    vcdEdit,
                                    createActionButton(UiAction::BrowseProtocolWaveform, NavigationPage::Protocol, inputPanel, false)));
    QWidget* const buttonRow = new QWidget(inputPanel);
    QHBoxLayout* const buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(createActionButton(UiAction::AnalyzeProtocol, NavigationPage::Protocol, inputPanel, true));
    buttonLayout->addStretch(1);
    form->addRow(QString(), buttonRow);
    layout->addWidget(inputPanel);

    QSplitter* const splitter = new QSplitter(Qt::Horizontal, content);
    QGroupBox* const keyPanel = panelBox(QStringLiteral("关键行为摘要"), splitter);
    QVBoxLayout* const keyLayout = new QVBoxLayout(keyPanel);
    QTreeWidget* const keyTree = new QTreeWidget(keyPanel);
    keyTree->setObjectName(QStringLiteral("protocol_key_behaviors_tree"));
    keyTree->setColumnCount(3);
    keyTree->setHeaderLabels({QStringLiteral("协议 / 类别"), QStringLiteral("状态 / 数量"), QStringLiteral("关键行为")});
    keyTree->setRootIsDecorated(true);
    keyTree->setAlternatingRowColors(true);
    keyTree->setUniformRowHeights(true);
    keyTree->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    keyTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    keyTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    keyLayout->addWidget(keyTree);
    QGroupBox* const diagnosticsPanel = panelBox(QStringLiteral("诊断"), splitter);
    QVBoxLayout* const diagnosticsLayout = new QVBoxLayout(diagnosticsPanel);
    QPlainTextEdit* const diagnosticsEdit = new QPlainTextEdit(diagnosticsPanel);
    diagnosticsEdit->setObjectName(QStringLiteral("protocol_diagnostics_edit"));
    diagnosticsEdit->setReadOnly(true);
    diagnosticsEdit->setPlaceholderText(QStringLiteral("缺字段、未采集和 VCD 合同问题会显示在这里。"));
    diagnosticsLayout->addWidget(diagnosticsEdit);
    splitter->addWidget(keyPanel);
    splitter->addWidget(diagnosticsPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createStatsPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_report"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(10);

    QHBoxLayout* const header = new QHBoxLayout();
    header->addWidget(pageTitle(QStringLiteral("测试报告"), content));
    QLabel* const taskLabel = mutedLabel(QStringLiteral("未选择任务"), content);
    taskLabel->setObjectName(QStringLiteral("report_task_label"));
    header->addWidget(taskLabel);
    QLabel* const lifecycleLabel = mutedLabel(QStringLiteral("未生成"), content);
    lifecycleLabel->setObjectName(QStringLiteral("report_lifecycle_label"));
    header->addWidget(lifecycleLabel);
    header->addStretch(1);
    header->addWidget(createActionButton(UiAction::OpenReportHtml, NavigationPage::Stats, content, false));
    header->addWidget(createActionButton(UiAction::OpenReportDirectory, NavigationPage::Stats, content, false));
    header->addWidget(createActionButton(UiAction::CopyReportPath, NavigationPage::Stats, content, false));
    header->addWidget(createActionButton(UiAction::GenerateReport, NavigationPage::Stats, content, true));
    layout->addLayout(header);

    QFrame* const banner = new QFrame(content);
    banner->setObjectName(QStringLiteral("report_conclusion_banner"));
    banner->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* const bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(16, 12, 16, 12);
    QLabel* const conclusionIcon = new QLabel(banner);
    conclusionIcon->setObjectName(QStringLiteral("report_conclusion_icon"));
    conclusionIcon->setFixedSize(36, 36);
    conclusionIcon->setAlignment(Qt::AlignCenter);
    bannerLayout->addWidget(conclusionIcon);
    QVBoxLayout* const conclusionLayout = new QVBoxLayout();
    QLabel* const conclusionLabel = new QLabel(QStringLiteral("无可评估任务"), banner);
    conclusionLabel->setObjectName(QStringLiteral("report_conclusion_label"));
    QFont conclusionFont = conclusionLabel->font();
    conclusionFont.setPointSize(18);
    conclusionFont.setBold(true);
    conclusionLabel->setFont(conclusionFont);
    QLabel* const reasonLabel = mutedLabel(QStringLiteral("请先创建或加载验证任务。"), banner);
    reasonLabel->setObjectName(QStringLiteral("report_reason_label"));
    reasonLabel->setWordWrap(true);
    conclusionLayout->addWidget(conclusionLabel);
    conclusionLayout->addWidget(reasonLabel);
    bannerLayout->addLayout(conclusionLayout, 1);
    QLabel* const countLabel = new QLabel(QStringLiteral("阻断 0  |  警告 0"), banner);
    countLabel->setObjectName(QStringLiteral("report_count_label"));
    countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bannerLayout->addWidget(countLabel);
    layout->addWidget(banner);

    const auto configureTable = [](QTableWidget* const table, const QStringList& headers) {
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->setWordWrap(true);
    };

    QSplitter* const mainSplit = new QSplitter(Qt::Horizontal, content);
    mainSplit->setObjectName(QStringLiteral("report_main_split"));
    mainSplit->setMinimumHeight(220);
    QGroupBox* const evidencePanel = panelBox(QStringLiteral("强制证据链"), mainSplit);
    QVBoxLayout* const evidenceLayout = new QVBoxLayout(evidencePanel);
    QTableWidget* const evidenceTable = new QTableWidget(3, 6, evidencePanel);
    evidenceTable->setObjectName(QStringLiteral("report_evidence_table"));
    configureTable(evidenceTable, {QStringLiteral("步骤"), QStringLiteral("状态"), QStringLiteral("摘要"),
                                   QStringLiteral("记录时间"), QStringLiteral("相对路径"), QStringLiteral("操作")});
    evidenceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    evidenceLayout->addWidget(evidenceTable);
    for (int row = 0; row < 3; ++row) {
        QWidget* const actions = new QWidget(evidenceTable);
        QHBoxLayout* const actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(0, 0, 0, 0);
        actionLayout->setSpacing(2);
        QToolButton* const openButton = createToolButton(
            actions, style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("查看证据文件"));
        openButton->setObjectName(QStringLiteral("report_evidence_open_%1").arg(row));
        connect(openButton, &QToolButton::clicked, this, [this, openButton]() {
            UiActionRequest request;
            request.action = UiAction::OpenReportArtifact;
            request.page = NavigationPage::Stats;
            request.objectName = openButton->objectName();
            request.parameters.insert(QStringLiteral("relativePath"), openButton->property("relativePath"));
            emit actionRequested(request);
        });
        QToolButton* const navigateButton = createToolButton(
            actions, style()->standardIcon(QStyle::SP_ArrowForward), QStringLiteral("转到程序烧录与运行"));
        connect(navigateButton, &QToolButton::clicked, this, [this, navigateButton]() {
            emitAction(UiAction::NavigateToReportSource, NavigationPage::Stats, navigateButton->objectName());
        });
        actionLayout->addWidget(openButton);
        actionLayout->addWidget(navigateButton);
        evidenceTable->setCellWidget(row, 5, actions);
    }
    mainSplit->addWidget(evidencePanel);

    QGroupBox* const diagnosticsPanel = panelBox(QStringLiteral("结论依据与问题"), mainSplit);
    QVBoxLayout* const diagnosticsLayout = new QVBoxLayout(diagnosticsPanel);
    QTreeWidget* const diagnosticsTree = new QTreeWidget(diagnosticsPanel);
    diagnosticsTree->setObjectName(QStringLiteral("report_diagnostics_tree"));
    diagnosticsTree->setHeaderLabels({QStringLiteral("级别"), QStringLiteral("ID/代码"), QStringLiteral("来源"),
                                      QStringLiteral("消息"), QStringLiteral("建议动作")});
    diagnosticsTree->setAlternatingRowColors(true);
    diagnosticsTree->setRootIsDecorated(false);
    diagnosticsTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    diagnosticsTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(diagnosticsTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        UiActionRequest request;
        request.action = UiAction::NavigateToReportSource;
        request.page = NavigationPage::Stats;
        request.objectName = QStringLiteral("report_diagnostic");
        if (item != nullptr) {
            request.parameters.insert(QStringLiteral("targetPage"), item->data(0, Qt::UserRole));
        }
        emit actionRequested(request);
    });
    diagnosticsLayout->addWidget(diagnosticsTree);
    mainSplit->addWidget(diagnosticsPanel);
    mainSplit->setStretchFactor(0, 3);
    mainSplit->setStretchFactor(1, 2);
    layout->addWidget(mainSplit);

    QGroupBox* const optionalPanel = panelBox(QStringLiteral("补充记录（不影响通过判定）"), content);
    QVBoxLayout* const optionalLayout = new QVBoxLayout(optionalPanel);
    QTableWidget* const optionalTable = new QTableWidget(4, 6, optionalPanel);
    optionalTable->setObjectName(QStringLiteral("report_optional_table"));
    configureTable(optionalTable, {QStringLiteral("记录项"), QStringLiteral("状态"), QStringLiteral("摘要"),
                                   QStringLiteral("记录时间"), QStringLiteral("相对路径"), QStringLiteral("操作")});
    optionalTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    optionalLayout->addWidget(optionalTable);
    for (int row = 0; row < 4; ++row) {
        QToolButton* const openButton = createToolButton(
            optionalTable, style()->standardIcon(QStyle::SP_DirOpenIcon), QStringLiteral("查看补充记录"));
        openButton->setObjectName(QStringLiteral("report_optional_open_%1").arg(row));
        connect(openButton, &QToolButton::clicked, this, [this, openButton]() {
            UiActionRequest request;
            request.action = UiAction::OpenReportArtifact;
            request.page = NavigationPage::Stats;
            request.objectName = openButton->objectName();
            request.parameters.insert(QStringLiteral("relativePath"), openButton->property("relativePath"));
            emit actionRequested(request);
        });
        optionalTable->setCellWidget(row, 5, openButton);
    }
    layout->addWidget(optionalPanel);

    QGroupBox* const archivePanel = panelBox(QStringLiteral("归档与可追溯信息"), content);
    archivePanel->setCheckable(true);
    archivePanel->setChecked(false);
    QVBoxLayout* const archiveLayout = new QVBoxLayout(archivePanel);
    QPlainTextEdit* const archiveEdit = new QPlainTextEdit(archivePanel);
    archiveEdit->setObjectName(QStringLiteral("report_archive_edit"));
    archiveEdit->setReadOnly(true);
    archiveEdit->setMaximumHeight(130);
    archiveEdit->setVisible(false);
    connect(archivePanel, &QGroupBox::toggled, archiveEdit, &QWidget::setVisible);
    archiveLayout->addWidget(archiveEdit);
    layout->addWidget(archivePanel);
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createDiagnosticsPanel(QWidget* const parent)
{
    QWidget* const panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("diagnostics_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    diagnosticsPanel_ = panel;
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(4);
    layout->addWidget(createLogPanel(), 1);
    panel->setMinimumHeight(kDiagnosticsMinHeight);
    panel->setMaximumHeight(kDiagnosticsMaxHeight);
    return panel;
}

QWidget* MainWindowShell::createLogPanel()
{
    QFrame* const group = new QFrame(this);
    group->setObjectName(QStringLiteral("raw_log"));
    group->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);

    QWidget* const contentWidget = new QWidget(group);
    contentWidget->setObjectName(QStringLiteral("log_content_widget"));
    contentWidget->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    logTabs_ = new QTabWidget(contentWidget);
    logTabs_->setObjectName(QStringLiteral("diagnostics_output_tabs"));
    logTabs_->setDocumentMode(true);
    logTabs_->tabBar()->setDrawBase(false);
    QWidget* const corner = new QWidget(logTabs_);
    corner->setObjectName(QStringLiteral("log_tab_corner"));
    corner->setAttribute(Qt::WA_StyledBackground, true);
    corner->setMinimumHeight(28);
    QHBoxLayout* const cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(6, 0, 4, 0);
    cornerLayout->setSpacing(6);
    QPushButton* const clearButton = new QPushButton(QStringLiteral("清空窗口"), corner);
    clearButton->setObjectName(QStringLiteral("log_clear_button"));
    clearButton->setFixedSize(70, 24);
    logClearButton_ = clearButton;
    logDetachButton_ = new QToolButton(corner);
    logDetachButton_->setObjectName(QStringLiteral("log_detach_button"));
    logDetachButton_->setToolTip(QStringLiteral("弹出独立窗口"));
    logDetachButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    logDetachButton_->setIconSize(QSize(18, 18));
    logDetachButton_->setAutoRaise(true);
    logDetachButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    logDetachButton_->setCursor(Qt::PointingHandCursor);
    logDetachButton_->setFixedSize(26, 26);
    cornerLayout->addWidget(clearButton);
    cornerLayout->addWidget(logDetachButton_);
    logTabs_->setCornerWidget(corner, Qt::TopRightCorner);

    QWidget* const logPage = new QWidget(logTabs_);
    logPage->setObjectName(QStringLiteral("diagnostics_log_page"));
    logPage->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 8, 0, 0);
    logLayout->setSpacing(0);
    logEdit_ = new QPlainTextEdit(logPage);
    logEdit_->setObjectName(QStringLiteral("workbench_log_edit"));
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(4000);
    logEdit_->setMinimumHeight(86);
    logEdit_->setPlaceholderText(QStringLiteral("关键流程日志和失败原文显示在这里。"));
    logLayout->addWidget(logEdit_, 1);
    logTabs_->addTab(logPage, QStringLiteral("Log"));
    logTabs_->addTab(createSerialMonitorPanel(), QStringLiteral("串口监控"));
    contentLayout->addWidget(logTabs_, 1);
    layout->addWidget(contentWidget, 1);

    connect(clearButton, &QPushButton::clicked, this, &MainWindowShell::clearVisibleLog);
    connect(logDetachButton_, &QToolButton::clicked, this, &MainWindowShell::toggleLogDetached);
    return group;
}

QWidget* MainWindowShell::createSerialConfigPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("串口配置"), this);
    group->setObjectName(QStringLiteral("serial_config_panel"));
    QFormLayout* const layout = new QFormLayout(group);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QComboBox* const portCombo = new WorkbenchComboBox(group);
    portCombo->setObjectName(QStringLiteral("serial_port_combo"));
    portCombo->addItem(QStringLiteral("未刷新"));
    QComboBox* const baudCombo = new WorkbenchComboBox(group);
    baudCombo->setObjectName(QStringLiteral("serial_baud_combo"));
    baudCombo->addItems({
        QStringLiteral("1200"),
        QStringLiteral("2400"),
        QStringLiteral("4800"),
        QStringLiteral("9600"),
        QStringLiteral("19200"),
        QStringLiteral("38400"),
        QStringLiteral("57600"),
        QStringLiteral("115200"),
        QStringLiteral("230400"),
        QStringLiteral("460800"),
        QStringLiteral("500000"),
        QStringLiteral("921600"),
        QStringLiteral("1000000"),
        QStringLiteral("1500000"),
        QStringLiteral("2000000")
    });
    baudCombo->setCurrentText(QStringLiteral("115200"));
    QComboBox* const displayModeCombo = new WorkbenchComboBox(group);
    displayModeCombo->setObjectName(QStringLiteral("serial_display_mode_combo"));
    displayModeCombo->addItems({QStringLiteral("文本"), QStringLiteral("HEX")});
    QPushButton* const refreshButton = createActionButton(UiAction::RefreshSerialPorts, NavigationPage::Connection, group, false);
    QPushButton* const openButton = createActionButton(UiAction::ToggleSerialMonitor, NavigationPage::Connection, group, false);
    QPushButton* const clearButton = createActionButton(UiAction::ClearSerialOutput, NavigationPage::Connection, group, false);
    openButton->setEnabled(false);

    QWidget* const portRow = new QWidget(group);
    QHBoxLayout* const portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(6);
    portLayout->addWidget(portCombo, 1);
    portLayout->addWidget(refreshButton);

    QWidget* const actionRow = new QWidget(group);
    QHBoxLayout* const actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(6);
    actionLayout->addWidget(openButton);
    actionLayout->addWidget(clearButton);
    actionLayout->addStretch(1);

    QLabel* const serialStatusLabel = mutedLabel(QStringLiteral("串口未刷新。"), group);
    serialStatusLabel->setObjectName(QStringLiteral("serial_status_label"));

    layout->addRow(QStringLiteral("串口"), portRow);
    layout->addRow(QStringLiteral("波特率"), baudCombo);
    layout->addRow(QStringLiteral("显示"), displayModeCombo);
    layout->addRow(QStringLiteral("操作"), actionRow);
    layout->addRow(QStringLiteral("串口状态"), serialStatusLabel);
    return group;
}

QWidget* MainWindowShell::createSerialMonitorPanel()
{
    QWidget* const panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("serial_monitor_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(6);
    serialOutputEdit_ = new QPlainTextEdit(panel);
    serialOutputEdit_->setObjectName(QStringLiteral("serial_output_edit"));
    serialOutputEdit_->setReadOnly(true);
    serialOutputEdit_->setMinimumHeight(86);
    serialOutputEdit_->setMaximumBlockCount(4000);
    serialOutputEdit_->setPlaceholderText(QString());
    layout->addWidget(serialOutputEdit_, 1);

    QWidget* const sendRow = new QWidget(panel);
    sendRow->setObjectName(QStringLiteral("serial_send_row"));
    QHBoxLayout* const sendLayout = new QHBoxLayout(sendRow);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(6);
    QLineEdit* const sendInput = new QLineEdit(sendRow);
    sendInput->setObjectName(QStringLiteral("serial_send_input"));
    sendInput->setPlaceholderText(QStringLiteral("输入要发送到串口的数据"));
    QPushButton* const sendButton =
        createActionButton(UiAction::SendSerialData, NavigationPage::Connection, sendRow, true);
    sendButton->setText(QStringLiteral("发送"));
    sendButton->setFixedWidth(54);
    sendLayout->addWidget(sendInput, 1);
    sendLayout->addWidget(sendButton);
    layout->addWidget(sendRow, 0);
    connect(sendInput, &QLineEdit::returnPressed, this, [this]() {
        emitAction(UiAction::SendSerialData, NavigationPage::Connection, QStringLiteral("serial_send_input"));
    });
    return panel;
}

QWidget* MainWindowShell::createControlPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("研发调试控制"), this);
    group->setObjectName(QStringLiteral("run_control"));
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QHBoxLayout* const debugRow = new QHBoxLayout();
    debugRow->addWidget(createActionButton(UiAction::StartDebugService, NavigationPage::Connection, group, false));
    debugRow->addWidget(createActionButton(UiAction::StopDebugService, NavigationPage::Connection, group, false));
    debugRow->addStretch(1);
    layout->addLayout(debugRow);
    QFormLayout* const diagnosticsLayout = new QFormLayout();
    diagnosticsLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const QList<QPair<QString, QString>> diagnostics = {
        {QStringLiteral("debug_service_state_label"), QStringLiteral("未连接")},
        {QStringLiteral("precheck_state_label"), QStringLiteral("未执行")},
    };
    const QStringList titles = {
        QStringLiteral("调试服务"),
        QStringLiteral("自检结论"),
    };
    for (int index = 0; index < diagnostics.size(); ++index) {
        QLabel* const value = mutedLabel(diagnostics.at(index).second, group);
        value->setObjectName(diagnostics.at(index).first);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        diagnosticsLayout->addRow(titles.at(index), value);
    }
    layout->addLayout(diagnosticsLayout);

    QLabel* const status = new QLabel(QStringLiteral("研发调试就绪"), group);
    status->setObjectName(QStringLiteral("workflow_status_label"));
    layout->addWidget(status);
    return group;
}

QWidget* MainWindowShell::createTodoCard(const QString& title, const QString& body, QWidget* const parent)
{
    QGroupBox* const group = panelBox(title, parent);
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QLabel* const label = mutedLabel(body, group);
    label->setObjectName(QStringLiteral("todo_card_label"));
    layout->addWidget(label);
    return group;
}

QWidget* MainWindowShell::createMetricCard(
    const QString& title,
    const QString& value,
    const QString& detail,
    QWidget* const parent)
{
    QGroupBox* const group = panelBox(title, parent);
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QLabel* const valueLabel = new QLabel(value, group);
    valueLabel->setProperty("metricValue", true);
    layout->addWidget(valueLabel);
    layout->addWidget(mutedLabel(detail, group));
    return group;
}

QPushButton* MainWindowShell::createNavButton(const QString& pageId, const QString& title, QWidget* const parent)
{
    QPushButton* const button = new QPushButton(title, parent);
    button->setObjectName(QStringLiteral("nav_%1").arg(pageId));
    button->setProperty("pageId", pageId);
    button->setCheckable(true);
    button->setProperty("navButton", true);
    navButtons_.insert(pageId, button);
    connect(button, &QPushButton::clicked, this, &MainWindowShell::switchWorkbenchPage);
    return button;
}

QPushButton* MainWindowShell::createActionButton(
    const UiAction action,
    const NavigationPage page,
    QWidget* const parent,
    const bool primary)
{
    QPushButton* const button = new QPushButton(toDisplayText(action), parent);
    button->setObjectName(QStringLiteral("action_%1_%2")
                              .arg(static_cast<int>(page))
                              .arg(static_cast<int>(action)));
    button->setProperty("uiAction", static_cast<int>(action));
    button->setProperty("uiPage", static_cast<int>(page));
    if (primary) {
        button->setProperty("primary_button", true);
        button->setProperty("primaryButton", true);
    }
    connect(button, &QPushButton::clicked, this, [this, action, page, button]() {
        emitAction(action, page, button->objectName());
    });
    return button;
}

QToolButton* MainWindowShell::createToolButton(QWidget* const parent, const QIcon& icon, const QString& tooltip)
{
    QToolButton* const button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QWidget* MainWindowShell::createPathInputRow(QWidget* const parent, QLineEdit* const edit, QAbstractButton* const button)
{
    QWidget* const row = new QWidget(parent);
    QHBoxLayout* const layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    return row;
}

void MainWindowShell::addWorkbenchPage(const QString& pageId, const NavigationPage page, QWidget* const pageWidget)
{
    pageIds_.insert(pageId, page);
    pageWidget->setProperty("workbenchPageId", pageId);
    pageWidget->setProperty("workbenchPageContent", true);
    pageWidget->setAttribute(Qt::WA_StyledBackground, true);
    pageStack_->addWidget(pageWidget);
}

void MainWindowShell::setActivePage(const QString& pageId)
{
    for (int index = 0; index < pageStack_->count(); ++index) {
        if (pageStack_->widget(index)->property("workbenchPageId").toString() == pageId) {
            pageStack_->setCurrentIndex(index);
            break;
        }
    }
    for (auto it = navButtons_.begin(); it != navButtons_.end(); ++it) {
        it.value()->setChecked(it.key() == pageId);
    }
    emit pageChanged(pageForId(pageId));
}

void MainWindowShell::emitAction(const UiAction action, const NavigationPage page, const QString& objectName)
{
    if ((action == UiAction::ProgramImage) || (action == UiAction::VerifyReadback) ||
        (action == UiAction::ShowVerifySummary)) {
        setProgramSummaryPage(false);
    }
    if ((action == UiAction::RunProgram) || (action == UiAction::StopProgram) ||
        (action == UiAction::ShowRunSummary)) {
        setProgramSummaryPage(true);
    }

    UiActionRequest request;
    request.action = action;
    request.page = page;
    request.objectName = objectName;
    request.parameters.insert(QStringLiteral("pageId"), pageIdForPage(page));
    request.parameters.insert(QStringLiteral("actionText"), toDisplayText(action));
    if ((action == UiAction::LoadTaskToWorkbench) ||
        (action == UiAction::DeleteTask) ||
        (action == UiAction::EditTask) ||
        (action == UiAction::SaveTaskEdit) ||
        (action == UiAction::CancelTaskEdit)) {
        request.parameters.insert(QStringLiteral("taskId"), selectedProjectTaskId());
    }
    if (action == UiAction::SaveTaskEdit) {
        request.parameters.insert(QStringLiteral("taskName"), taskNameText());
        request.parameters.insert(QStringLiteral("description"), taskDescriptionText());
    }
    if (action == UiAction::SendSerialData) {
        QLineEdit* const serialInput = findChild<QLineEdit*>(QStringLiteral("serial_send_input"));
        if (serialInput != nullptr) {
            request.parameters.insert(QStringLiteral("serialText"), serialInput->text());
            serialInput->clear();
        }
    }
    if (action == UiAction::SendSamplingConfig || action == UiAction::RunProgram) {
        QLineEdit* const triggerAddrEdit = findChild<QLineEdit*>(QStringLiteral("sampling_trigger_addr_edit"));
        int mismatchMask = 0;
        for (int bit = 0; bit < kSamplingMismatchBits; ++bit) {
            QCheckBox* const bitCheck =
                findChild<QCheckBox*>(QStringLiteral("sampling_mismatch_bit_%1_check").arg(bit));
            if (bitCheck != nullptr && bitCheck->isChecked()) {
                mismatchMask |= (1 << bit);
            }
        }
        request.parameters.insert(
            QStringLiteral("trigger_addr"),
            triggerAddrEdit == nullptr ? QStringLiteral("0x00000000") : triggerAddrEdit->text().trimmed());
        request.parameters.insert(QStringLiteral("sample_count"), kSamplingSampleCount);
        request.parameters.insert(QStringLiteral("pretrigger"), kSamplingPretrigger);
        request.parameters.insert(QStringLiteral("posttrigger"), kSamplingPosttrigger);
        request.parameters.insert(QStringLiteral("trigger_count"), kSamplingTriggerCount);
        request.parameters.insert(QStringLiteral("post_after_trigger"), kSamplingPostAfterTrigger);
        request.parameters.insert(QStringLiteral("sample_word_bits"), kSamplingSampleWordBits);
        request.parameters.insert(QStringLiteral("sample_rate_hz"), kSamplingSampleRateHz);
        request.parameters.insert(QStringLiteral("mismatch_enable"), mismatchMask != 0);
        request.parameters.insert(QStringLiteral("mismatch_mask"), mismatchMask);
        request.parameters.insert(QStringLiteral("mismatch_source"), QStringLiteral("mismatch[4:0]"));
        request.parameters.insert(QStringLiteral("trigger_logic"), QStringLiteral("valid_ready_addr_or_mismatch_rise"));
    }
    emit actionRequested(request);
}

void MainWindowShell::setActionButtonsEnabled(const UiAction action, const bool enabled)
{
    const QList<QPushButton*> buttons = findChildren<QPushButton*>();
    for (QPushButton* const button : buttons) {
        if (button != nullptr && button->property("uiAction").toInt() == static_cast<int>(action)) {
            button->setEnabled(enabled);
        }
    }
}

void MainWindowShell::setReportPageState(const ReportPageViewModel& model)
{
    QLabel* const taskLabel = findChild<QLabel*>(QStringLiteral("report_task_label"));
    QLabel* const lifecycleLabel = findChild<QLabel*>(QStringLiteral("report_lifecycle_label"));
    QLabel* const conclusionLabel = findChild<QLabel*>(QStringLiteral("report_conclusion_label"));
    QLabel* const reasonLabel = findChild<QLabel*>(QStringLiteral("report_reason_label"));
    QLabel* const countLabel = findChild<QLabel*>(QStringLiteral("report_count_label"));
    QLabel* const conclusionIcon = findChild<QLabel*>(QStringLiteral("report_conclusion_icon"));
    QFrame* const banner = findChild<QFrame*>(QStringLiteral("report_conclusion_banner"));
    if (taskLabel != nullptr) {
        taskLabel->setText(model.hasTask
            ? QStringLiteral("%1  ·  %2  ·  %3").arg(model.taskName, model.taskId, model.modeText)
            : QStringLiteral("未选择任务"));
    }
    if (lifecycleLabel != nullptr) {
        lifecycleLabel->setText(model.lifecycleText);
        lifecycleLabel->setToolTip(model.errorMessage);
    }
    if (conclusionLabel != nullptr) {
        conclusionLabel->setText(model.conclusionText);
    }
    if (conclusionIcon != nullptr) {
        QStyle::StandardPixmap pixmap = QStyle::SP_MessageBoxInformation;
        if (model.conclusion == QStringLiteral("pass")) {
            pixmap = QStyle::SP_DialogApplyButton;
        } else if (model.conclusion == QStringLiteral("fail")) {
            pixmap = QStyle::SP_MessageBoxCritical;
        } else if (model.conclusion == QStringLiteral("blocked")) {
            pixmap = QStyle::SP_MessageBoxWarning;
        }
        conclusionIcon->setPixmap(style()->standardIcon(pixmap).pixmap(28, 28));
    }
    if (reasonLabel != nullptr) {
        QString reason = model.primaryReason;
        if (model.hasPersistedReport) {
            const QString comparison = QStringLiteral("当前预检：%1；落盘快照：%2。 ")
                .arg(model.conclusionText, model.persistedConclusionText);
            reason.prepend(model.stale
                ? QStringLiteral("报告已过期，请重新生成。 ") + comparison
                : comparison);
        }
        if (!model.errorMessage.isEmpty()) {
            reason.append(QStringLiteral("  操作错误: %1").arg(model.errorMessage));
        }
        reasonLabel->setText(reason);
    }
    if (countLabel != nullptr) {
        countLabel->setText(QStringLiteral("阻断 %1  |  警告 %2\n%3")
                                .arg(model.blockingCount)
                                .arg(model.warningCount)
                                .arg(model.generatedAt.isEmpty() ? QStringLiteral("尚未落盘") : model.generatedAt));
    }
    if (banner != nullptr) {
        QString border = QStringLiteral("#1f4b5f");
        QString background = QStringLiteral("#eef5f7");
        if (model.conclusion == QStringLiteral("pass")) {
            border = QStringLiteral("#2f7d4a");
            background = QStringLiteral("#eef8f1");
        } else if (model.conclusion == QStringLiteral("fail")) {
            border = QStringLiteral("#b42318");
            background = QStringLiteral("#fff1f0");
        } else if (model.conclusion == QStringLiteral("blocked")) {
            border = QStringLiteral("#ad6800");
            background = QStringLiteral("#fff7e6");
        }
        banner->setStyleSheet(QStringLiteral(
            "QFrame#report_conclusion_banner { border-left: 7px solid %1; background: %2; } ")
                                  .arg(border, background));
    }

    const QList<QPushButton*> actionButtons = findChildren<QPushButton*>();
    for (QPushButton* const button : actionButtons) {
        const UiAction action = static_cast<UiAction>(button->property("uiAction").toInt());
        if (action == UiAction::GenerateReport) {
            button->setEnabled(model.hasTask && !model.generating);
            button->setText(model.generating
                ? QStringLiteral("正在生成...")
                : (model.hasPersistedReport ? QStringLiteral("重新生成报告") : QStringLiteral("生成报告")));
        } else if (action == UiAction::OpenReportHtml || action == UiAction::OpenReportDirectory ||
                   action == UiAction::CopyReportPath) {
            button->setEnabled(model.hasPersistedReport && !model.generating);
        }
    }

    const auto stateBrush = [](const QString& state) {
        if (state == QStringLiteral("passed") || state == QStringLiteral("available")) {
            return QBrush(QColor(QStringLiteral("#2f7d4a")));
        }
        if (state == QStringLiteral("failed") || state == QStringLiteral("error") ||
            state == QStringLiteral("critical")) {
            return QBrush(QColor(QStringLiteral("#b42318")));
        }
        if (state == QStringLiteral("blocked") || state == QStringLiteral("blocking") ||
            state == QStringLiteral("warning")) {
            return QBrush(QColor(QStringLiteral("#ad6800")));
        }
        return QBrush(QColor(QStringLiteral("#52616b")));
    };
    const auto setCell = [](QTableWidget* const table, const int row, const int column, const QString& text) {
        QTableWidgetItem* item = table->item(row, column);
        if (item == nullptr) {
            item = new QTableWidgetItem();
            table->setItem(row, column, item);
        }
        item->setText(text);
        item->setToolTip(text);
    };

    QTableWidget* const evidenceTable = findChild<QTableWidget*>(QStringLiteral("report_evidence_table"));
    if (evidenceTable != nullptr) {
        for (int row = 0; row < qMin(evidenceTable->rowCount(), model.requiredEvidence.size()); ++row) {
            const ReportEvidenceViewItem& item = model.requiredEvidence.at(row);
            setCell(evidenceTable, row, 0, item.displayName);
            setCell(evidenceTable, row, 1, item.stateText);
            setCell(evidenceTable, row, 2, item.summary);
            setCell(evidenceTable, row, 3, item.recordedAt);
            setCell(evidenceTable, row, 4, item.relativePath);
            evidenceTable->item(row, 1)->setForeground(stateBrush(item.state));
            QToolButton* const openButton = findChild<QToolButton*>(
                QStringLiteral("report_evidence_open_%1").arg(row));
            if (openButton != nullptr) {
                openButton->setProperty("relativePath", item.relativePath);
                openButton->setEnabled(!item.relativePath.isEmpty() && item.state != QStringLiteral("not_run"));
            }
        }
    }

    QTreeWidget* const diagnosticsTree = findChild<QTreeWidget*>(QStringLiteral("report_diagnostics_tree"));
    if (diagnosticsTree != nullptr) {
        diagnosticsTree->clear();
        if (model.diagnostics.isEmpty()) {
            new QTreeWidgetItem(diagnosticsTree,
                                {QStringLiteral("信息"), QStringLiteral("无"), QStringLiteral("报告"),
                                 QStringLiteral("当前没有需要处理的问题"), QString()});
        } else {
            for (const ReportDiagnosticViewItem& item : model.diagnostics) {
                QTreeWidgetItem* const treeItem = new QTreeWidgetItem(
                    diagnosticsTree,
                    {item.severity, item.id.isEmpty() ? item.code : item.id, item.source,
                     item.message, item.suggestion});
                treeItem->setToolTip(3, item.message);
                treeItem->setToolTip(4, item.suggestion);
                treeItem->setData(0, Qt::UserRole, item.targetPage);
                treeItem->setForeground(0, stateBrush(item.severity));
            }
        }
    }

    QTableWidget* const optionalTable = findChild<QTableWidget*>(QStringLiteral("report_optional_table"));
    if (optionalTable != nullptr) {
        for (int row = 0; row < qMin(optionalTable->rowCount(), model.optionalRecords.size()); ++row) {
            const ReportOptionalViewItem& item = model.optionalRecords.at(row);
            setCell(optionalTable, row, 0, item.displayName);
            setCell(optionalTable, row, 1, item.stateText);
            setCell(optionalTable, row, 2, item.summary);
            setCell(optionalTable, row, 3, item.recordedAt);
            setCell(optionalTable, row, 4, item.relativePath);
            optionalTable->item(row, 1)->setForeground(stateBrush(item.state));
            QToolButton* const openButton = findChild<QToolButton*>(
                QStringLiteral("report_optional_open_%1").arg(row));
            if (openButton != nullptr) {
                openButton->setProperty("relativePath", item.relativePath);
                openButton->setEnabled(item.state == QStringLiteral("available") && !item.relativePath.isEmpty());
            }
        }
    }
    QPlainTextEdit* const archiveEdit = findChild<QPlainTextEdit*>(QStringLiteral("report_archive_edit"));
    if (archiveEdit != nullptr) {
        archiveEdit->setPlainText(model.archiveDetails);
    }
}

void MainWindowShell::showPage(const NavigationPage page)
{
    for (auto it = pageIds_.constBegin(); it != pageIds_.constEnd(); ++it) {
        if (it.value() == page) {
            setActivePage(it.key());
            return;
        }
    }
}

void MainWindowShell::applyWaveformTraceToDisplay(QWidget* const widget) const
{
    if (widget == nullptr) {
        return;
    }

    WaveformDisplayWidget* const display = static_cast<WaveformDisplayWidget*>(widget);
    display->setTrace(
        waveformStatusText_, waveformPathText_, waveformTimeRangeText_, waveformGroups_, waveformSamples_);
}

void MainWindowShell::appendFormattedLog(
    QPlainTextEdit* const view,
    const LogLevel level,
    const QString& source,
    const QString& message)
{
    if (view == nullptr) {
        return;
    }
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  toDisplayText(level),
                                  source,
                                  message);
    view->appendPlainText(line);
    view->moveCursor(QTextCursor::End);
    if (view == logEdit_ && detachedLogEdit_ != nullptr) {
        detachedLogEdit_->appendPlainText(line);
        detachedLogEdit_->moveCursor(QTextCursor::End);
    }
    if (view == serialOutputEdit_ && detachedSerialOutputEdit_ != nullptr) {
        detachedSerialOutputEdit_->appendPlainText(line);
        detachedSerialOutputEdit_->moveCursor(QTextCursor::End);
    }
}

QString MainWindowShell::currentLogText() const
{
    const QPlainTextEdit* const view = currentLogView();
    return (view == nullptr) ? QString() : view->toPlainText();
}

QPlainTextEdit* MainWindowShell::currentLogView() const
{
    if (logTabs_ == nullptr) {
        return nullptr;
    }
    return (logTabs_->currentIndex() == 1) ? serialOutputEdit_ : logEdit_;
}

NavigationPage MainWindowShell::pageForId(const QString& pageId)
{
    if (pageId == QStringLiteral("connection")) {
        return NavigationPage::Connection;
    }
    if (pageId == QStringLiteral("mode")) {
        return NavigationPage::Mode;
    }
    if (pageId == QStringLiteral("ram_program")) {
        return NavigationPage::RamProgram;
    }
    if (pageId == QStringLiteral("fault_injection")) {
        return NavigationPage::FaultInjection;
    }
    if (pageId == QStringLiteral("sampling_config")) {
        return NavigationPage::SamplingConfig;
    }
    if (pageId == QStringLiteral("program_run")) {
        return NavigationPage::RamProgram;
    }
    if (pageId == QStringLiteral("waveform")) {
        return NavigationPage::Waveform;
    }
    if (pageId == QStringLiteral("protocol")) {
        return NavigationPage::Protocol;
    }
    if (pageId == QStringLiteral("stats")) {
        return NavigationPage::Stats;
    }
    return NavigationPage::Project;
}

}  // namespace lockstep::ui
