/*****************************************************************************
*  @file      main_window_shell.cpp
*  @brief     主窗口框架组件实现
*  Details.   实现主窗口框架组件的业务逻辑、状态转换和文件访问流程。
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

#include "main_window_shell.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
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
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ui_theme.h"

namespace lockstep::ui {

namespace {

constexpr int kMinimumWidth = 1280;
constexpr int kMinimumHeight = 720;
constexpr int kSidebarWidth = 224;
constexpr int kDiagnosticsMinHeight = 112;
constexpr int kDiagnosticsMaxHeight = 150;
constexpr int kDetachedLogWidth = 900;
constexpr int kDetachedLogHeight = 420;
constexpr int kProgramPageLeftInitialWidth = 420;
constexpr int kProgramPageRightInitialWidth = 560;
constexpr int kTaskIdRole = Qt::UserRole + 1;
constexpr int kTaskDescriptionRole = Qt::UserRole + 2;
constexpr int kTaskBasicInfoRole = Qt::UserRole + 3;

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
          expandedGroups_(),
          selectedRow_(0),
          verticalOffset_(0),
          horizontalZoom_(1.0)
    {
        setObjectName(QStringLiteral("waveform_display_widget"));
        setMinimumHeight(390);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

    void setTrace(
        const QString& statusText,
        const QString& pathText,
        const QString& timeRangeText,
        const QVector<TraceGroupViewItem>& groups)
    {
        statusText_ = statusText;
        pathText_ = pathText;
        timeRangeText_ = timeRangeText;
        groups_ = groups;
        verticalOffset_ = qBound(0, verticalOffset_, maxVerticalOffset());
        selectedRow_ = qBound(0, selectedRow_, qMax(0, rows().size() - 1));
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
        painter.setClipRect(QRect(0, kRulerHeight, width(), height() - kRulerHeight));
        for (int index = 0; index < visibleRows.size(); ++index) {
            const int y = kRulerHeight + index * kRowHeight - verticalOffset_;
            if (y + kRowHeight < kRulerHeight) {
                continue;
            }
            if (y > height()) {
                break;
            }
            drawRow(&painter, QRect(0, y, width(), kRowHeight), leftWidth, visibleRows.at(index), index);
        }
        painter.setClipping(false);

        drawRemainingGrid(&painter, leftWidth, visibleRows.size());
        drawScrollBar(&painter, QRect(width() - 8, kRulerHeight, 6, height() - kRulerHeight));
    }

    void mousePressEvent(QMouseEvent* const event) override
    {
        if (event == nullptr || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        setFocus(Qt::MouseFocusReason);
        const int index = rowIndexAt(event->pos().y());
        const QVector<WaveformProtocolRow> visibleRows = rows();
        if (index < 0 || index >= visibleRows.size()) {
            QWidget::mousePressEvent(event);
            return;
        }

        selectedRow_ = index;
        if (visibleRows.at(index).group) {
            toggleGroup(visibleRows.at(index).groupId);
        } else {
            update();
        }
        event->accept();
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

        const int delta = event->pixelDelta().isNull() ? event->angleDelta().y() : event->pixelDelta().y();
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            if (delta != 0) {
                horizontalZoom_ = qBound(0.5, horizontalZoom_ * (delta > 0 ? 1.15 : 1.0 / 1.15), 8.0);
                update();
            }
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
    static constexpr int kRowHeight = 27;

    static QColor canvasColor()
    {
        return QColor(QStringLiteral("#ffffff"));
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
        return QColor(QStringLiteral("#0f766e"));
    }

    static QColor busFillColor()
    {
        return QColor(QStringLiteral("#e8f1ff"));
    }

    static QColor busStrokeColor()
    {
        return QColor(QStringLiteral("#1677ff"));
    }

    static QColor protocolFillColor()
    {
        return QColor(QStringLiteral("#fff7e6"));
    }

    static QColor protocolStrokeColor()
    {
        return QColor(QStringLiteral("#fa8c16"));
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
        return QColor(QStringLiteral("#f0e9ff"));
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
        return QColor(QStringLiteral("#22c55e"));
    }

    static QColor okFillColor()
    {
        return QColor(QStringLiteral("#f0fdf4"));
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

    static QStringList fallbackFields(const QString& groupId)
    {
        if (groupId == QStringLiteral("ahb")) {
            return {QStringLiteral("addr[31:0]"), QStringLiteral("data[127:0]"), QStringLiteral("burst")};
        }
        if (groupId == QStringLiteral("uart")) {
            return {QStringLiteral("rx"), QStringLiteral("tx")};
        }
        if (groupId == QStringLiteral("spi")) {
            return {QStringLiteral("sclk"), QStringLiteral("mosi"), QStringLiteral("miso"), QStringLiteral("cs")};
        }
        if (groupId == QStringLiteral("can")) {
            return {QStringLiteral("rx"), QStringLiteral("tx")};
        }
        if (groupId == QStringLiteral("i2c")) {
            return {QStringLiteral("scl"), QStringLiteral("sda")};
        }
        if (groupId == QStringLiteral("eth")) {
            return {QStringLiteral("rx"), QStringLiteral("tx")};
        }
        if (groupId == QStringLiteral("usb")) {
            return {QStringLiteral("dp"), QStringLiteral("dm")};
        }
        if (groupId == QStringLiteral("jtag")) {
            return {QStringLiteral("tck"), QStringLiteral("tms"), QStringLiteral("tdi"), QStringLiteral("tdo")};
        }
        return {
            QStringLiteral("mismatch[4]"),
            QStringLiteral("mismatch[3]"),
            QStringLiteral("mismatch[2]"),
            QStringLiteral("mismatch[1]"),
            QStringLiteral("mismatch[0]"),
        };
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

        if (hasLoadedData()) {
            WaveformProtocolRow signalSection;
            signalSection.name = QStringLiteral("信号波形");
            signalSection.section = true;
            result.append(signalSection);

            for (int groupIndex = 0; groupIndex < groupIds.size(); ++groupIndex) {
                const QString groupId = groupIds.at(groupIndex);
                const TraceGroupViewItem* const group = findGroup(groupId);
                const QStringList fields = group != nullptr && !group->fields.isEmpty() ? group->fields : fallbackFields(groupId);
                const int visibleFieldCount = qMin(fields.size(), groupId == QStringLiteral("mismatch") ? 5 : 4);
                for (int fieldIndex = 0; fieldIndex < visibleFieldCount; ++fieldIndex) {
                    WaveformProtocolRow fieldRow;
                    fieldRow.groupId = groupId;
                    fieldRow.name = QStringLiteral("%1: %2").arg(displayNameForId(groupId), fields.at(fieldIndex));
                    fieldRow.status = group == nullptr ? QStringLiteral("not_captured") : group->status;
                    fieldRow.reason = group == nullptr ? QString() : group->reason;
                    fieldRow.child = true;
                    fieldRow.groupIndex = groupIndex;
                    fieldRow.childIndex = fieldIndex;
                    result.append(fieldRow);
                }
            }

            WaveformProtocolRow protocolSection;
            protocolSection.name = QStringLiteral("协议解析");
            protocolSection.section = true;
            result.append(protocolSection);
        }

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

            const QStringList fields = group != nullptr && !group->fields.isEmpty() ? group->fields : fallbackFields(groupId);
            for (int fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
                WaveformProtocolRow fieldRow;
                fieldRow.groupId = groupId;
                fieldRow.name = fields.at(fieldIndex);
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
        return !groups_.isEmpty();
    }

    int leftPaneWidth() const
    {
        return qBound(190, width() / 4, 245);
    }

    WaveformTimeAxis timeAxis() const
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

        const qint64 span = qMax<qint64>(1, axis.end - axis.start);
        axis.end = axis.start + qMax<qint64>(1, static_cast<qint64>(span / horizontalZoom_));
        return axis;
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
        painter->drawText(QRect(rect.left() - 10, rect.top(), 20, 14), Qt::AlignCenter, QStringLiteral("0"));
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
            } else {
                drawProtocolActivity(painter, rect.adjusted(0, 5, 0, -5), row);
            }
            return;
        }

        if (unavailableStatus(row.status)) {
            drawUnavailable(painter, rect, row.reason);
            return;
        }

        if (isBusField(row.name)) {
            drawBusField(painter, rect, row);
        } else {
            drawDigitalField(painter, rect, row.groupIndex + row.childIndex, row.name);
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

    static bool isBusField(const QString& name)
    {
        return name.contains(QStringLiteral(":")) ||
            name.contains(QStringLiteral("addr"), Qt::CaseInsensitive) ||
            name.contains(QStringLiteral("data"), Qt::CaseInsensitive);
    }

    static QString sampleFieldValue(const QString& name, const int index)
    {
        if (name.contains(QStringLiteral("127:0"))) {
            return QStringLiteral("0x%1").arg(0x1000 + index * 0x40, 32, 16, QLatin1Char('0'));
        }
        if (name.contains(QStringLiteral("31:0"))) {
            return QStringLiteral("0x%1").arg(0x80000000ULL + static_cast<quint64>(index) * 0x20ULL, 8, 16, QLatin1Char('0'));
        }
        return QStringLiteral("0x%1").arg(index + 1, 2, 16, QLatin1Char('0'));
    }

    static void drawBusField(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row)
    {
        const int blockCount = qMax(2, qMin(4, rect.width() / 150));
        const int span = qMax(1, rect.width() - 60);
        for (int index = 0; index < blockCount; ++index) {
            const int x = rect.left() + 24 + span * index / blockCount;
            const QRect blockRect(x, rect.top() + 6, qMin(qMax(70, span / (blockCount + 1)), rect.right() - x - 8), rect.height() - 12);
            if (blockRect.width() <= 20) {
                continue;
            }
            painter->setPen(busStrokeColor());
            painter->setBrush(busFillColor());
            painter->drawRoundedRect(blockRect, 2, 2);
            painter->setPen(QColor(QStringLiteral("#0958d9")));
            painter->drawText(blockRect.adjusted(6, 0, -6, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(sampleFieldValue(row.name, index),
                                                                Qt::ElideRight,
                                                                blockRect.width() - 12));
        }
    }

    static void drawDigitalField(QPainter* const painter, const QRect& rect, const int seed, const QString& name)
    {
        QPainterPath path;
        const int high = rect.top() + 8;
        const int low = rect.bottom() - 8;
        const int densitySeed = static_cast<int>(qHash(name) % 7U);
        const int step = qMax(16, rect.width() / (14 + densitySeed + (seed % 5)));
        int x = rect.left() + 20;
        int y = (seed % 2 == 0) ? high : low;
        path.moveTo(x, y);
        while (x < rect.right() - 18) {
            const int nextX = qMin(rect.right() - 18, x + step);
            path.lineTo(nextX, y);
            y = (y == high) ? low : high;
            path.lineTo(nextX, y);
            x = nextX;
        }
        QPen pen(signalWaveColor());
        pen.setWidthF(1.2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    }

    void drawProtocolActivity(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row) const
    {
        const bool mismatch = row.groupId == QStringLiteral("mismatch");
        const QString normalizedStatus = row.status.trimmed().toLower();
        const bool mismatchEvent =
            mismatch &&
            (normalizedStatus.contains(QStringLiteral("mismatch")) ||
             normalizedStatus.contains(QStringLiteral("event")) ||
             normalizedStatus.contains(QStringLiteral("error")) ||
             normalizedStatus.contains(QStringLiteral("fault")));
        const QColor fill = mismatchEvent ? mismatchFillColor() : (mismatch ? okFillColor() : protocolFillColor());
        const QColor stroke = mismatchEvent ? mismatchStrokeColor() : (mismatch ? okColor() : protocolStrokeColor());
        const QColor text = mismatchEvent ? QColor(QStringLiteral("#a8071a")) :
            (mismatch ? QColor(QStringLiteral("#166534")) : QColor(QStringLiteral("#ad4e00")));
        const int blockCount = mismatch ? 2 : qMax(2, qMin(4, rect.width() / 220));
        const int blockWidth = qMax(78, qMin(150, rect.width() / 9));

        for (int index = 0; index < blockCount; ++index) {
            const int usableWidth = qMax(1, rect.width() - blockWidth - 42);
            const int x = rect.left() + 22 + ((index * 137 + row.groupIndex * 31) % usableWidth);
            const QRect blockRect(x, rect.top(), qMin(blockWidth, rect.right() - x - 4), rect.height());
            if (blockRect.width() <= 18) {
                continue;
            }

            painter->setPen(stroke);
            painter->setBrush(fill);
            painter->drawRoundedRect(blockRect, 2, 2);
            painter->setPen(text);
            const QString label = mismatch
                ? (mismatchEvent ? QStringLiteral("mismatch") : QStringLiteral("no mismatch"))
                : QStringLiteral("%1 event").arg(displayNameForId(row.groupId));
            painter->drawText(blockRect.adjusted(6, 0, -6, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(label, Qt::ElideRight, blockRect.width() - 12));
        }
    }

    void drawTransactions(QPainter* const painter, const QRect& rect, const WaveformProtocolRow& row) const
    {
        const WaveformTimeAxis axis = timeAxis();
        const qint64 span = qMax<qint64>(1, axis.end - axis.start);
        const int count = qMin(row.transactions.size(), 5);
        for (int index = 0; index < count; ++index) {
            qint64 start = 0;
            qint64 end = 0;
            const bool hasTime = parseTransactionRange(row.transactions.at(index), &start, &end);
            int x = rect.left() + 24 + ((index * 67 + row.groupIndex * 19) % qMax(1, rect.width() - 120));
            int blockWidth = qMax(64, rect.width() / 8);
            if (hasTime) {
                const qint64 clampedStart = qBound(axis.start, start, axis.end);
                const qint64 clampedEnd = qBound(axis.start, qMax(start + 1, end), axis.end);
                x = rect.left() + static_cast<int>((clampedStart - axis.start) * rect.width() / span);
                blockWidth = qMin(qMax(56, static_cast<int>((clampedEnd - clampedStart) * rect.width() / span)), rect.right() - x - 4);
            }

            const QRect blockRect(x, rect.top(), blockWidth, rect.height());
            if (blockRect.width() <= 16) {
                continue;
            }
            const bool mismatch = row.groupId == QStringLiteral("mismatch") ||
                row.transactions.at(index).contains(QStringLiteral("mismatch"), Qt::CaseInsensitive);
            painter->setPen(mismatch ? mismatchStrokeColor() : protocolStrokeColor());
            painter->setBrush(mismatch ? mismatchFillColor() : protocolFillColor());
            painter->drawRoundedRect(blockRect, 2, 2);
            painter->setPen(mismatch ? QColor(QStringLiteral("#a8071a")) : QColor(QStringLiteral("#ad4e00")));
            painter->drawText(blockRect.adjusted(6, 0, -6, 0),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(transactionLabel(row.transactions.at(index)),
                                                                Qt::ElideRight,
                                                                blockRect.width() - 12));
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
        const int bracketEnd = text.indexOf(QLatin1Char(']'));
        const QString label = bracketEnd >= 0 ? text.mid(bracketEnd + 1).trimmed() : text.trimmed();
        return label.isEmpty() ? QStringLiteral("event") : label;
    }

    void drawRemainingGrid(QPainter* const painter, const int leftWidth, const int rowCount) const
    {
        const int y = kRulerHeight + rowCount * kRowHeight - verticalOffset_;
        if (y >= height()) {
            return;
        }
        const QRect leftRect(0, qMax(kRulerHeight, y), leftWidth, height() - qMax(kRulerHeight, y));
        const QRect waveRect(leftWidth + 1, leftRect.top(), width() - leftWidth - 2, leftRect.height());
        painter->fillRect(leftRect, QColor(QStringLiteral("#fbfcfe")));
        drawGrid(painter, waveRect);
        painter->setPen(QColor(QStringLiteral("#eef2f7")));
        for (int rowY = leftRect.top(); rowY < height(); rowY += kRowHeight) {
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
        const int contentHeight = rows().size() * kRowHeight;
        const int thumbHeight = qMax(28, rect.height() * rect.height() / qMax(1, contentHeight));
        const int thumbY = rect.top() + (rect.height() - thumbHeight) * verticalOffset_ / maxOffset;
        painter->setBrush(QColor(QStringLiteral("#aeb9c8")));
        painter->drawRoundedRect(QRect(rect.left(), thumbY, rect.width(), thumbHeight), 3, 3);
    }

    int rowIndexAt(const int y) const
    {
        if (y < kRulerHeight) {
            return -1;
        }
        return (y - kRulerHeight + verticalOffset_) / kRowHeight;
    }

    int maxVerticalOffset() const
    {
        return qMax(0, rows().size() * kRowHeight - qMax(0, height() - kRulerHeight));
    }

    void ensureSelectedVisible()
    {
        const int bodyHeight = qMax(0, height() - kRulerHeight);
        const int rowTop = selectedRow_ * kRowHeight;
        const int rowBottom = rowTop + kRowHeight;
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
        update();
    }

    QString statusText_;
    QString pathText_;
    QString timeRangeText_;
    QVector<TraceGroupViewItem> groups_;
    QSet<QString> expandedGroups_;
    int selectedRow_;
    int verticalOffset_;
    double horizontalZoom_;
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
        pageId = QStringLiteral("program_run");
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
      navButtons_(),
      pageIds_(),
      logTabs_(nullptr),
      logDetachButton_(nullptr),
      logEdit_(nullptr),
      serialOutputEdit_(nullptr),
      detachedLogDialog_(nullptr),
      detachedLogEdit_(nullptr),
      waveformDisplayWidget_(nullptr),
      detachedWaveformDialog_(nullptr),
      detachedWaveformDisplayWidget_(nullptr),
      waveformStatusText_(),
      waveformPathText_(),
      waveformTimeRangeText_(),
      waveformGroups_()
{
    qRegisterMetaType<UiActionRequest>("lockstep::ui::UiActionRequest");
    qRegisterMetaType<UiWorkbenchState>("lockstep::ui::UiWorkbenchState");

    setCentralWidget(createWorkbenchShell());
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setWindowTitle(QStringLiteral("锁步研发测试系统上位机"));
    setWorkbenchState(makeDefaultWorkbenchState(UiMode::Test));
    setActivePage(QStringLiteral("project"));
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
    const QList<QLineEdit*> runPathEdits =
        findChildren<QLineEdit*>(QStringLiteral("program_run_image_path_edit"));
    for (QLineEdit* const edit : runPathEdits) {
        edit->setText(path);
    }
    const QString fileName = QFileInfo(path).fileName();
    const QList<QLineEdit*> runNameEdits =
        findChildren<QLineEdit*>(QStringLiteral("program_run_image_name_edit"));
    for (QLineEdit* const edit : runNameEdits) {
        edit->setText(fileName);
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

void MainWindowShell::setWaveformTraceView(
    const QString& statusText,
    const QString& pathText,
    const QString& timeRangeText,
    const QVector<TraceGroupViewItem>& groups,
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
    QPlainTextEdit* const keyEdit = findChild<QPlainTextEdit*>(QStringLiteral("protocol_key_behaviors_edit"));
    QPlainTextEdit* const diagnosticsEdit = findChild<QPlainTextEdit*>(QStringLiteral("protocol_diagnostics_edit"));

    if (statusLabel != nullptr) {
        statusLabel->setText(statusText);
    }
    if (analysisEdit != nullptr) {
        analysisEdit->setText(analysisPath);
    }
    if (keyEdit != nullptr) {
        keyEdit->setPlainText(keyBehaviors.isEmpty() ? QStringLiteral("暂无关键行为。") : keyBehaviors.join(QLatin1Char('\n')));
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

    detachedLogEdit_ = new QPlainTextEdit(detachedLogDialog_);
    detachedLogEdit_->setReadOnly(true);
    detachedLogEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    detachedLogEdit_->setPlainText(currentLogText());

    QVBoxLayout* const layout = new QVBoxLayout(detachedLogDialog_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(detachedLogEdit_);

    connect(detachedLogDialog_, &QDialog::finished, this, [this]() {
        detachedLogDialog_ = nullptr;
        detachedLogEdit_ = nullptr;
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
    detachedWaveformDialog_->setWindowTitle(QStringLiteral("波形分析仪"));
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
        {QStringLiteral("ram_program"), QStringLiteral("程序烧录")},
        {QStringLiteral("fault_injection"), QStringLiteral("错误注入")},
        {QStringLiteral("sampling_config"), QStringLiteral("采集配置")},
        {QStringLiteral("program_run"), QStringLiteral("程序运行")},
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
    addWorkbenchPage(QStringLiteral("ram_program"), NavigationPage::RamProgram, createRamProgramPage());
    addWorkbenchPage(QStringLiteral("fault_injection"), NavigationPage::FaultInjection, createEmptyPage(QStringLiteral("错误注入")));
    addWorkbenchPage(QStringLiteral("sampling_config"), NavigationPage::SamplingConfig, createEmptyPage(QStringLiteral("采集配置")));
    addWorkbenchPage(QStringLiteral("program_run"), NavigationPage::ProgramRun, createProgramRunPage());
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
    layout->setSpacing(12);

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
    return createProgramOperationPage(QStringLiteral("程序烧录"), NavigationPage::RamProgram, false);
}

QWidget* MainWindowShell::createProgramRunPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_program_run"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(12);
    layout->addWidget(pageTitle(QStringLiteral("程序运行"), content));

    QSplitter* const split = new QSplitter(content);
    split->setChildrenCollapsible(false);

    QWidget* const left = new QWidget(split);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(createRunImagePanel());

    QGroupBox* const controlPanel = panelBox(QStringLiteral("程序运行控制"), left);
    QGridLayout* const controlLayout = new QGridLayout(controlPanel);
    controlLayout->setSpacing(8);
    controlLayout->setColumnStretch(0, 1);
    controlLayout->setColumnStretch(1, 1);
    QPushButton* const runButton = createActionButton(UiAction::RunProgram, NavigationPage::ProgramRun, controlPanel, true);
    QPushButton* const stopButton = createActionButton(UiAction::StopProgram, NavigationPage::ProgramRun, controlPanel, false);
    stopButton->setProperty("danger_button", true);
    for (QPushButton* const button : {runButton, stopButton}) {
        button->setMinimumHeight(42);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    controlLayout->addWidget(runButton, 0, 0);
    controlLayout->addWidget(stopButton, 0, 1);
    leftLayout->addWidget(controlPanel);

    QWidget* const progressPanel = new QWidget(left);
    QFormLayout* const progressLayout = new QFormLayout(progressPanel);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(6);
    QProgressBar* const runProgress = new QProgressBar(progressPanel);
    runProgress->setObjectName(QStringLiteral("program_run_progress_bar"));
    runProgress->setRange(0, 100);
    runProgress->setValue(0);
    QProgressBar* const stopProgress = new QProgressBar(progressPanel);
    stopProgress->setObjectName(QStringLiteral("program_stop_progress_bar"));
    stopProgress->setRange(0, 100);
    stopProgress->setValue(0);
    progressLayout->addRow(QStringLiteral("运行进度"), runProgress);
    progressLayout->addRow(QStringLiteral("终止进度"), stopProgress);
    leftLayout->addWidget(progressPanel);
    leftLayout->addStretch(1);
    split->addWidget(left);

    QGroupBox* const right = panelBox(QStringLiteral("运行摘要"), split);
    QVBoxLayout* const rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(10, 2, 10, 10);
    rightLayout->setSpacing(3);
    QHBoxLayout* const summaryTools = new QHBoxLayout();
    summaryTools->setContentsMargins(0, 0, 0, 0);
    QPushButton* const clearSummaryButton = new QPushButton(QStringLiteral("清空窗口"), right);
    clearSummaryButton->setObjectName(QStringLiteral("log_clear_button"));
    clearSummaryButton->setFixedSize(70, 22);
    summaryTools->addStretch(1);
    summaryTools->addWidget(clearSummaryButton);
    rightLayout->addLayout(summaryTools);

    QPlainTextEdit* const runSummaryView = new QPlainTextEdit(right);
    runSummaryView->setObjectName(QStringLiteral("run_summary_edit"));
    runSummaryView->setReadOnly(true);
    runSummaryView->setMinimumHeight(180);
    runSummaryView->setPlainText(QStringLiteral("程序运行控制摘要将在这里显示。"));
    rightLayout->addWidget(runSummaryView, 1);
    connect(clearSummaryButton, &QPushButton::clicked, runSummaryView, &QPlainTextEdit::clear);

    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);
    split->setSizes({kProgramPageLeftInitialWidth, kProgramPageRightInitialWidth});
    layout->addWidget(split, 1);
    return content;
}

QWidget* MainWindowShell::createProgramOperationPage(
    const QString& title,
    const NavigationPage page,
    const bool includeRunControls)
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(includeRunControls ? QStringLiteral("page_program_run") : QStringLiteral("page_ram_program"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(12);
    layout->addWidget(pageTitle(title, content));

    QSplitter* const split = new QSplitter(content);
    split->setChildrenCollapsible(false);

    QWidget* const left = new QWidget(split);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(createImagePanel(page));

    QGroupBox* const actionPanel = panelBox(QStringLiteral("烧录与回读"), left);
    QGridLayout* const actionLayout = new QGridLayout(actionPanel);
    actionLayout->setSpacing(8);
    actionLayout->setColumnStretch(0, 1);
    actionLayout->setColumnStretch(1, 1);
    QPushButton* const programButton = createActionButton(UiAction::ProgramImage, page, actionPanel, true);
    QPushButton* const verifyButton = createActionButton(UiAction::VerifyReadback, page, actionPanel, false);
    for (QPushButton* const button : {programButton, verifyButton}) {
        button->setMinimumHeight(42);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    actionLayout->addWidget(programButton, 0, 0);
    actionLayout->addWidget(verifyButton, 0, 1);
    leftLayout->addWidget(actionPanel);
    QWidget* const progressPanel = new QWidget(left);
    QFormLayout* const progressLayout = new QFormLayout(progressPanel);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(6);
    QProgressBar* const writeProgress = new QProgressBar(progressPanel);
    writeProgress->setObjectName(QStringLiteral("program_write_progress_bar"));
    writeProgress->setRange(0, 100);
    writeProgress->setValue(0);
    QProgressBar* const readbackProgress = new QProgressBar(progressPanel);
    readbackProgress->setObjectName(QStringLiteral("readback_verify_progress_bar"));
    readbackProgress->setRange(0, 100);
    readbackProgress->setValue(0);
    progressLayout->addRow(QStringLiteral("烧写进度"), writeProgress);
    progressLayout->addRow(QStringLiteral("回读进度"), readbackProgress);
    leftLayout->addWidget(progressPanel);
    leftLayout->addStretch(1);
    split->addWidget(left);

    QGroupBox* const right = panelBox(QStringLiteral("烧录与回读摘要"), split);
    QVBoxLayout* const rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(10, 2, 10, 10);
    rightLayout->setSpacing(3);
    QPushButton* const clearSummaryButton = new QPushButton(QStringLiteral("清空窗口"), right);
    clearSummaryButton->setObjectName(QStringLiteral("log_clear_button"));
    clearSummaryButton->setFixedSize(70, 22);
    QHBoxLayout* const summaryTools = new QHBoxLayout();
    summaryTools->setContentsMargins(0, 0, 0, 0);
    summaryTools->addStretch(1);
    summaryTools->addWidget(clearSummaryButton);
    rightLayout->addLayout(summaryTools);
    QPlainTextEdit* const readbackView = new QPlainTextEdit(right);
    readbackView->setObjectName(QStringLiteral("ram_summary_edit"));
    readbackView->setReadOnly(true);
    readbackView->setMinimumHeight(180);
    readbackView->setPlainText(QStringLiteral("烧录记录和回读校验摘要将在这里显示。"));
    rightLayout->addWidget(readbackView, 1);
    connect(clearSummaryButton, &QPushButton::clicked, readbackView, &QPlainTextEdit::clear);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 4);
    split->setSizes({kProgramPageLeftInitialWidth, kProgramPageRightInitialWidth});
    layout->addWidget(split, 1);
    return content;
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
    inputGrid->addWidget(label, 0, 0);
    inputGrid->addWidget(edit, 0, 1);
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
    QGroupBox* const keyPanel = panelBox(QStringLiteral("关键行为"), splitter);
    QVBoxLayout* const keyLayout = new QVBoxLayout(keyPanel);
    QPlainTextEdit* const keyEdit = new QPlainTextEdit(keyPanel);
    keyEdit->setObjectName(QStringLiteral("protocol_key_behaviors_edit"));
    keyEdit->setReadOnly(true);
    keyEdit->setPlaceholderText(QStringLiteral("M12 输出的总线、外设和 mismatch 行为会显示在这里。"));
    keyLayout->addWidget(keyEdit);
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
    QHBoxLayout* const header = new QHBoxLayout();
    header->addWidget(pageTitle(QStringLiteral("测试报告"), content));
    header->addStretch(1);
    header->addWidget(createActionButton(UiAction::GenerateReport, NavigationPage::Stats, content, true));
    layout->addLayout(header);
    layout->addWidget(createTodoCard(QStringLiteral("报告依据"),
                                     QStringLiteral("测试报告综合 PL 允许结果寄存器、结果 JSON、烧写记录、回读校验和运行控制证据；当前先接入结果 JSON 与强制证据，PL 寄存器读取保留接口占位。"),
                                     content));
    QGridLayout* const grid = new QGridLayout();
    grid->addWidget(createMetricCard(QStringLiteral("PL Allow"), QStringLiteral("占位"), QStringLiteral("等待 PL 允许结果寄存器 map"), content), 0, 0);
    grid->addWidget(createMetricCard(QStringLiteral("Result JSON"), QStringLiteral("待生成"), QStringLiteral("报告生成后读取 report.json 结论"), content), 0, 1);
    grid->addWidget(createMetricCard(QStringLiteral("Program Evidence"), QStringLiteral("待执行"), QStringLiteral("烧写、回读、运行证据齐全后可通过"), content), 0, 2);
    grid->addWidget(createMetricCard(QStringLiteral("Conclusion"), QStringLiteral("未生成"), QStringLiteral("点击生成报告后更新任务报告文件"), content), 1, 0);
    layout->addLayout(grid);
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createDiagnosticsPanel(QWidget* const parent)
{
    QWidget* const panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("diagnostics_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
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
    logLayout->setContentsMargins(0, 6, 0, 0);
    logLayout->setSpacing(0);
    logEdit_ = new QPlainTextEdit(logPage);
    logEdit_->setObjectName(QStringLiteral("workbench_log_edit"));
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(4000);
    logEdit_->setMinimumHeight(42);
    logEdit_->setMaximumHeight(72);
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
    portCombo->addItem(QStringLiteral("COM 占位"));
    QComboBox* const baudCombo = new WorkbenchComboBox(group);
    baudCombo->addItems({QStringLiteral("9600"), QStringLiteral("115200"), QStringLiteral("921600")});
    baudCombo->setCurrentText(QStringLiteral("115200"));
    QComboBox* const displayModeCombo = new WorkbenchComboBox(group);
    displayModeCombo->addItems({QStringLiteral("文本"), QStringLiteral("HEX")});
    QPushButton* const refreshButton = createActionButton(UiAction::RefreshSerialPorts, NavigationPage::Connection, group, false);
    QPushButton* const openButton = createActionButton(UiAction::ToggleSerialMonitor, NavigationPage::Connection, group, false);
    QPushButton* const clearButton = createActionButton(UiAction::ClearSerialOutput, NavigationPage::Connection, group, false);

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

    layout->addRow(QStringLiteral("串口"), portRow);
    layout->addRow(QStringLiteral("波特率"), baudCombo);
    layout->addRow(QStringLiteral("显示"), displayModeCombo);
    layout->addRow(QStringLiteral("操作"), actionRow);
    layout->addRow(QStringLiteral("状态"), mutedLabel(QStringLiteral("串口尚未打开。当前为 UI 占位，不访问真实串口。"), group));
    return group;
}

QWidget* MainWindowShell::createSerialMonitorPanel()
{
    QWidget* const panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("serial_monitor_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);
    serialOutputEdit_ = new QPlainTextEdit(panel);
    serialOutputEdit_->setObjectName(QStringLiteral("serial_output_edit"));
    serialOutputEdit_->setReadOnly(true);
    serialOutputEdit_->setMinimumHeight(42);
    serialOutputEdit_->setMaximumHeight(72);
    serialOutputEdit_->setMaximumBlockCount(4000);
    serialOutputEdit_->setPlaceholderText(QStringLiteral("串口打印信息会显示在这里。当前为只读占位，无输入框。"));
    layout->addWidget(serialOutputEdit_, 1);
    return panel;
}

QWidget* MainWindowShell::createImagePanel(const NavigationPage page)
{
    QGroupBox* const group = panelBox(QStringLiteral("程序镜像"), this);
    group->setObjectName(QStringLiteral("image_load"));
    QFormLayout* const layout = new QFormLayout(group);
    QLineEdit* const programImageEdit = new QLineEdit(group);
    programImageEdit->setObjectName(QStringLiteral("program_image_path_edit"));
    layout->addRow(QStringLiteral("文件"),
                   createPathInputRow(group,
                                      programImageEdit,
                                      createActionButton(UiAction::BrowseProgramImage, page, group, false)));
    return group;
}

QWidget* MainWindowShell::createRunImagePanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("程序镜像"), this);
    group->setObjectName(QStringLiteral("run_image_info"));
    QFormLayout* const layout = new QFormLayout(group);

    QLineEdit* const imageNameEdit = new QLineEdit(group);
    imageNameEdit->setObjectName(QStringLiteral("program_run_image_name_edit"));
    imageNameEdit->setReadOnly(true);
    imageNameEdit->setPlaceholderText(QStringLiteral("等待程序烧录确定镜像"));
    QLineEdit* const imagePathEdit = new QLineEdit(group);
    imagePathEdit->setObjectName(QStringLiteral("program_run_image_path_edit"));
    imagePathEdit->setReadOnly(true);
    imagePathEdit->setPlaceholderText(QStringLiteral("等待程序烧录确定镜像路径"));

    layout->addRow(QStringLiteral("名称"), imageNameEdit);
    layout->addRow(QStringLiteral("路径"), imagePathEdit);
    return group;
}

QWidget* MainWindowShell::createControlPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("研发调试控制"), this);
    group->setObjectName(QStringLiteral("run_control"));
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QHBoxLayout* const debugRow = new QHBoxLayout();
    debugRow->addWidget(createActionButton(UiAction::StartDebugService, NavigationPage::Connection, group, false));
    debugRow->addWidget(createActionButton(UiAction::StopDebugService, NavigationPage::Connection, group, false));
    layout->addLayout(debugRow);
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

void MainWindowShell::applyWaveformTraceToDisplay(QWidget* const widget) const
{
    if (widget == nullptr) {
        return;
    }

    WaveformDisplayWidget* const display = static_cast<WaveformDisplayWidget*>(widget);
    display->setTrace(waveformStatusText_, waveformPathText_, waveformTimeRangeText_, waveformGroups_);
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
    if (detachedLogEdit_ != nullptr) {
        detachedLogEdit_->setPlainText(currentLogText());
        detachedLogEdit_->moveCursor(QTextCursor::End);
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
        return NavigationPage::ProgramRun;
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
