/**********************************************************
* 文件名: waveform_ui_test.cpp
* 日期: 2026-07-14
* 版本: v1.4
* 更新记录: 增加关键行为摘要树与 Mismatch 正常/异常文案回归测试
* 描述: 离屏验证九协议波形、关键行为摘要和红色异常波形。
**********************************************************/

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTextStream>
#include <QTreeWidget>
#include <QWidget>

#include "main_window_shell.h"

namespace {

bool expect(const bool condition, const QString& message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
    }
    return condition;
}

bool treeContainsText(const QTreeWidget* const tree, const QString& text)
{
    if (tree == nullptr) return false;
    QList<QTreeWidgetItem*> pending;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        pending.append(tree->topLevelItem(index));
    }
    while (!pending.isEmpty()) {
        QTreeWidgetItem* const item = pending.takeFirst();
        for (int column = 0; column < item->columnCount(); ++column) {
            if (item->text(column).contains(text)) return true;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.append(item->child(index));
        }
    }
    return false;
}

QTreeWidgetItem* findTopLevelItem(const QTreeWidget* const tree, const QString& text)
{
    if (tree == nullptr) return nullptr;
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
        QTreeWidgetItem* const item = tree->topLevelItem(index);
        if (item->text(0) == text) return item;
    }
    return nullptr;
}

QString packedValueWithBit(const int bit)
{
    QString value(128, QLatin1Char('0'));
    const int nibbleIndex = value.size() - 1 - bit / 4;
    const int nibbleValue = 1 << (bit % 4);
    value[nibbleIndex] = QString::number(nibbleValue, 16).toUpper().at(0);
    return value;
}

int redPixelCount(QWidget* const widget)
{
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    painter.end();

    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 125 && color.blue() < 125) {
                ++count;
            }
        }
    }
    return count;
}

int exactColorCount(QWidget* const widget, const QColor& expected, const QRect& area)
{
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    painter.end();
    int count = 0;
    const QRect bounded = area.intersected(image.rect());
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            if (image.pixelColor(x, y).rgb() == expected.rgb()) ++count;
        }
    }
    return count;
}

void saveScreenshotIfRequested(QWidget* const widget)
{
    const QString path = qEnvironmentVariable("LOCKSTEP_UI_TEST_SCREENSHOT");
    if (path.isEmpty()) {
        return;
    }
    QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget->render(&painter);
    painter.end();
    if (image.save(path)) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "WARN: unable to save waveform screenshot: " << path << '\n';
        return;
    }
    file.write(QStringLiteral("P6\n%1 %2\n255\n").arg(image.width()).arg(image.height()).toLatin1());
    for (int y = 0; y < image.height(); ++y) {
        QByteArray row;
        row.reserve(image.width() * 3);
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            row.append(static_cast<char>(color.red()));
            row.append(static_cast<char>(color.green()));
            row.append(static_cast<char>(color.blue()));
        }
        file.write(row);
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    lockstep::ui::MainWindowShell window;
    window.resize(1280, 720);
    window.showPage(lockstep::ui::NavigationPage::SamplingConfig);
    QPushButton* captureButton = nullptr;
    for (QPushButton* const button : window.findChildren<QPushButton*>()) {
        if (button->property("uiAction").toInt() ==
            static_cast<int>(lockstep::ui::UiAction::StartSamplingCapture)) {
            captureButton = button;
            break;
        }
    }
    bool captureRequested = false;
    lockstep::ui::UiActionRequest captureRequest;
    QObject::connect(&window, &lockstep::ui::MainWindowShell::actionRequested,
                     [&captureRequested, &captureRequest](const lockstep::ui::UiActionRequest& request) {
        if (request.action == lockstep::ui::UiAction::StartSamplingCapture) {
            captureRequested = true;
            captureRequest = request;
        }
    });
    if (!expect(captureButton != nullptr, QStringLiteral("sampling page exposes the C++ capture action"))) return 1;
    captureButton->click();
    QCoreApplication::processEvents();
    if (!expect(captureRequested &&
                    captureRequest.parameters.value(QStringLiteral("sample_word_bits")).toInt() == 1024 &&
                    captureRequest.parameters.value(QStringLiteral("sample_rate_hz")).toInt() == 120000000,
                QStringLiteral("capture action sends the fixed 1024-bit 120 MHz contract"))) return 1;

    window.showPage(lockstep::ui::NavigationPage::Waveform);

    const QStringList ids = {
        QStringLiteral("ahb"), QStringLiteral("uart"), QStringLiteral("spi"),
        QStringLiteral("can"), QStringLiteral("i2c"), QStringLiteral("eth"),
        QStringLiteral("usb"), QStringLiteral("jtag"), QStringLiteral("mismatch")
    };
    QVector<lockstep::ui::TraceGroupViewItem> groups;
    for (const QString& id : ids) {
        lockstep::ui::TraceGroupViewItem group;
        group.id = id;
        group.displayName = id.toUpper();
        group.status = id == QStringLiteral("mismatch")
            ? QStringLiteral("event_detected") : QStringLiteral("signals_captured");
        lockstep::ui::TraceFieldViewItem field;
        if (id == QStringLiteral("mismatch")) {
            field.name = QStringLiteral("mismatch[4]");
            field.displayName = QStringLiteral("mismatch[4] ahb_master_output");
            field.lsb = 506;
            field.width = 1;
            field.errorSignal = true;
            group.fields.append(field);
            group.transactions.append(QStringLiteral("[10..20] mismatch_event: ahb_master_output"));
        } else {
            field.name = QStringLiteral("data");
            field.displayName = QStringLiteral("data[7:0]");
            field.lsb = 0;
            field.width = 8;
            group.fields.append(field);
            if (id == QStringLiteral("ahb")) {
                for (int event = 0; event < 8; ++event) {
                    group.transactions.append(QStringLiteral(
                        "[%1..%2] ahb_transfer: AHB READ 0x%3 DATA=0xCCCCCCCC RESP=0")
                        .arg(event * 2 + 1)
                        .arg(event * 2 + 2)
                        .arg(0x10 + event * 4, 8, 16, QLatin1Char('0')));
                }
            } else if (id == QStringLiteral("jtag")) {
                group.transactions.append(QStringLiteral("[200..201] jtag_cycle: TMS=1 TDI=1 TDO=0"));
            }
        }
        groups.append(group);
    }

    QVector<lockstep::ui::TraceSampleViewItem> samples;
    for (const auto& value : QList<QPair<qint64, QString>>{
             {0, QString(128, QLatin1Char('0'))},
             {10, packedValueWithBit(506)},
             {200, QString(128, QLatin1Char('0'))}}) {
        lockstep::ui::TraceSampleViewItem sample;
        sample.time = value.first;
        sample.valueHex = value.second;
        samples.append(sample);
    }

    window.setWaveformTraceView(
        QStringLiteral("complete"), QStringLiteral("waveform/lockstep_trace.vcd"),
        QStringLiteral("0 .. 20 ns"), groups, samples, {}, {});
    window.setProtocolAnalysisView(
        QStringLiteral("complete"), QStringLiteral("evidence/protocol_analysis.json"),
        {QStringLiteral("[1..2] ahb_transfer: AHB READ 0x00000010")}, {});
    window.show();
    QCoreApplication::processEvents();

    QWidget* const display = window.findChild<QWidget*>(QStringLiteral("waveform_display_widget"));
    QTreeWidget* const protocolEvents =
        window.findChild<QTreeWidget*>(QStringLiteral("protocol_key_behaviors_tree"));
    QWidget* const analyzerPanel = window.findChild<QWidget*>(QStringLiteral("waveform_analyzer_panel"));
    QScrollBar* const timeScrollBar = display == nullptr ? nullptr
        : display->findChild<QScrollBar*>(QStringLiteral("waveform_time_scrollbar"));
    if (!expect(display != nullptr, QStringLiteral("waveform display exists")) ||
        !expect(protocolEvents != nullptr &&
                    protocolEvents->property("protocolEventCount").toInt() == 10 &&
                    protocolEvents->property("activeProtocolCount").toInt() == 2 &&
                    protocolEvents->property("mismatchDetected").toBool() &&
                    treeContainsText(protocolEvents, QStringLiteral("AHB READ")) &&
                    treeContainsText(protocolEvents, QStringLiteral("检测到处理器内 Mismatch")) &&
                    findTopLevelItem(protocolEvents, QStringLiteral("AHB")) != nullptr &&
                    findTopLevelItem(protocolEvents, QStringLiteral("AHB"))->childCount() <= 4,
                QStringLiteral("protocol page prioritizes grouped activity and mismatch status")) ||
        !expect(analyzerPanel != nullptr && timeScrollBar != nullptr,
                QStringLiteral("embedded waveform owns a horizontal time scrollbar")) ||
        !expect(timeScrollBar->height() == 9,
                QStringLiteral("embedded horizontal scrollbar uses the compact 9px height")) ||
        !expect(display->height() <= analyzerPanel->height() &&
                    timeScrollBar->mapToGlobal(timeScrollBar->rect().bottomRight()).y() <=
                        analyzerPanel->mapToGlobal(analyzerPanel->rect().bottomRight()).y(),
                QStringLiteral("embedded horizontal scrollbar is not clipped by its panel")) ||
        !expect(display->accessibleDescription().contains(QStringLiteral("9 个协议组，0 个已展开")),
                QStringLiteral("all protocol child signals are collapsed by default"))) {
        return 1;
    }
    if (!expect(exactColorCount(display, QColor(QStringLiteral("#edf3f7")),
                                QRect(display->width() / 4, 28, display->width() * 3 / 4, 27)) > 20,
                QStringLiteral("one-unit protocol event remains visible on a wide time axis"))) {
        return 1;
    }
    if (!expect(display->property("waveformRenderedProtocolEvents").toInt() >= 2,
                QStringLiteral("short and right-edge protocol events are counted as rendered"))) return 1;
    const QRect ahbEventRect =
        display->property("waveformFirstProtocolEventRect").toRect();
    if (!expect(!ahbEventRect.isEmpty(), QStringLiteral("AHB event hit rectangle is available"))) return 1;
    const QPointF ahbEventPoint(ahbEventRect.center());
    QMouseEvent ahbHoverEvent(
        QEvent::MouseMove, ahbEventPoint, display->mapToGlobal(ahbEventPoint.toPoint()),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(display, &ahbHoverEvent);
    QCoreApplication::processEvents();
    const QString eventTooltip = display->property("waveformCursorText").toString();
    if (!expect(eventTooltip.contains(QStringLiteral("AHB READ")) &&
                    eventTooltip.contains(QStringLiteral("ADDR=0x00000010")) &&
                    eventTooltip.contains(QStringLiteral("DATA=0xCCCCCCCC")) &&
                    eventTooltip.contains(QStringLiteral("RESP=OKAY")),
                QStringLiteral("AHB event hover exposes structured details: %1").arg(eventTooltip))) {
        return 1;
    }

    if (!expect(QMetaObject::invokeMethod(&window, "showWaveformDetached", Qt::DirectConnection),
                QStringLiteral("detached waveform action is invokable"))) {
        return 1;
    }
    QDialog* const detachedDialog =
        window.findChild<QDialog*>(QStringLiteral("waveform_detached_dialog"));
    if (!expect(detachedDialog != nullptr, QStringLiteral("detached waveform dialog exists")) ||
        !expect(detachedDialog->windowTitle().contains(QStringLiteral("lockstep_ui_preview")),
                QStringLiteral("detached waveform title identifies the only product executable"))) {
        return 1;
    }

    const int collapsedRedPixels = redPixelCount(display);
    const QPointF mismatchRowPoint(15.0, 28.0 + 8.0 * 27.0 + 13.0);
    QMouseEvent pressEvent(
        QEvent::MouseButtonPress,
        mismatchRowPoint,
        display->mapToGlobal(mismatchRowPoint.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(display, &pressEvent);
    QCoreApplication::processEvents();

    if (!expect(display->accessibleDescription().contains(QStringLiteral("9 个协议组，1 个已展开")),
                QStringLiteral("mismatch child signal expands from the keyboard: %1")
                    .arg(display->accessibleDescription())) ||
        !expect(redPixelCount(display) > collapsedRedPixels,
                QStringLiteral("mismatch=1 adds a visible red child waveform"))) {
        return 1;
    }
    saveScreenshotIfRequested(display);

    samples[1].time = 11;
    groups.last().transactions.clear();
    groups.last().status = QStringLiteral("complete");
    window.setWaveformTraceView(
        QStringLiteral("complete"), QStringLiteral("waveform/lockstep_trace.vcd"),
        QStringLiteral("0 .. 20 ns"), groups, samples, {}, {});
    window.setProtocolAnalysisView(
        QStringLiteral("complete"), QStringLiteral("evidence/protocol_analysis.json"),
        {QStringLiteral("[1..2] ahb_transfer: AHB READ 0x00000010")}, {});
    if (!expect(display->accessibleDescription().contains(QStringLiteral("9 个协议组，0 个已展开")),
                QStringLiteral("a newly imported VCD collapses all child signals again")) ||
        !expect(!protocolEvents->property("mismatchDetected").toBool() &&
                    treeContainsText(protocolEvents, QStringLiteral("处理器内无Mismatch")) &&
                    !treeContainsText(protocolEvents, QStringLiteral("已解析，未检测到协议事件")),
                QStringLiteral("idle peripherals stay hidden and zero mismatch has a clear conclusion"))) {
        return 1;
    }

    return 0;
}
