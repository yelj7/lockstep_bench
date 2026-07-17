/**********************************************************
* 文件名: scalar_1024_vcd_import_test.cpp
* 日期: 2026-07-15
* 版本: v1.2
* 更新记录: 验证标准时间滚动条和纯协议波形信号列表
* 描述: 验证协议解析、波形模型和 Qt 画布能够显示实采 1024 路 VCD
**********************************************************/

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWheelEvent>

#include "protocol_analysis.h"
#include "main_window_shell.h"
#include "waveform_trace_viewer.h"

namespace {

bool expect(const bool condition, const QString& message)
{
    if (!condition) QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

bool writeSchema(const QString& path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray document = QByteArrayLiteral(
        "{\n"
        "  \"schema_version\": \"2.0\",\n"
        "  \"task_id\": \"trace_1024_test\",\n"
        "  \"sample_signal\": \"CH0..CH1023\",\n"
        "  \"sample_width\": 1024,\n"
        "  \"physical_channels\": 1024,\n"
        "  \"trace_profile_id\": \"trace.noelv.lockstep_1024\"\n"
        "}\n");
    return file.write(document) == document.size() && file.commit();
}

bool writeSemanticVcd(const QString& path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QByteArray document;
    document.append("$timescale 1 ns $end\n$scope module capture $end\n");
    for (int bit = 1023; bit >= 0; --bit) {
        document.append(QStringLiteral("$var wire 1 v%1 signal[%1] $end\n").arg(bit).toLatin1());
    }
    document.append("$upscope $end\n$enddefinitions $end\n#0\n");
    for (int bit = 0; bit < 1024; ++bit) {
        document.append("0v");
        document.append(QByteArray::number(bit));
        document.append('\n');
    }
    document.append("#1\nxv512\n#2\n0v512\n#3\nxv900\n#4096\n1v506\n");
    return file.write(document) == document.size() && file.commit();
}

QVector<lockstep::ui::TraceGroupViewItem> uiGroups(
    const QList<lockstep::waveform_viewer::WaveformGroupView>& groups)
{
    QVector<lockstep::ui::TraceGroupViewItem> result;
    for (const auto& source : groups) {
        lockstep::ui::TraceGroupViewItem group;
        group.id = source.id;
        group.displayName = source.displayName;
        group.status = source.status;
        group.reason = source.reason;
        group.transactions = source.transactions;
        for (const auto& sourceField : source.fields) {
            lockstep::ui::TraceFieldViewItem field;
            field.name = sourceField.name;
            field.displayName = sourceField.displayName;
            field.lsb = sourceField.lsb;
            field.width = sourceField.width;
            field.errorSignal = sourceField.errorSignal;
            group.fields.append(field);
        }
        result.append(group);
    }
    return result;
}

QVector<lockstep::ui::TraceSampleViewItem> uiSamples(
    const QList<lockstep::waveform_viewer::WaveformSampleView>& samples)
{
    QVector<lockstep::ui::TraceSampleViewItem> result;
    for (const auto& source : samples) {
        lockstep::ui::TraceSampleViewItem sample;
        sample.time = source.time;
        sample.valueHex = source.valueHex;
        sample.unknown = source.unknown;
        result.append(sample);
    }
    return result;
}

int waveformPixelCount(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = image.width() / 4; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const bool signal = color.red() >= 35 && color.red() <= 100 &&
                color.green() >= 65 && color.green() <= 135 &&
                color.blue() >= 80 && color.blue() <= 155;
            if (signal) ++count;
        }
    }
    return count;
}

int exactColorCount(const QImage& image, const QColor& expected)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = image.width() / 4; x < image.width(); ++x) {
            if (image.pixelColor(x, y).rgb() == expected.rgb()) ++count;
        }
    }
    return count;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir taskRoot(QDir::tempPath() + QStringLiteral("/lockstep_scalar_1024_XXXXXX"));
    if (!expect(taskRoot.isValid(), QStringLiteral("temporary task directory is available"))) return 1;
    const QString waveformPath = QDir(taskRoot.path()).filePath(QStringLiteral("waveform"));
    if (!expect(QDir().mkpath(waveformPath), QStringLiteral("waveform directory is available"))) return 1;
    const QString sourcePath = QDir(waveformPath).filePath(QStringLiteral("source.vcd"));
    const bool sourceReady = argc >= 2
        ? QFile::copy(QString::fromLocal8Bit(argv[1]), sourcePath)
        : writeSemanticVcd(sourcePath);
    if (!expect(sourceReady, QStringLiteral("1024-bit VCD fixture is available")) ||
        !expect(QFile::copy(sourcePath, QDir(waveformPath).filePath(QStringLiteral("capture.vcd"))),
                QStringLiteral("1024-bit VCD is imported")) ||
        !expect(writeSchema(QDir(waveformPath).filePath(QStringLiteral("capture_schema.json"))),
                QStringLiteral("1024-bit schema is written"))) {
        return 1;
    }

    lockstep::protocol_analyzer::ProtocolAnalyzer analyzer;
    lockstep::protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = taskRoot.path();
    request.taskId = QStringLiteral("trace_1024_test");
    request.reportDiagnosticsToErrorRegistry = false;
    const auto analysis = analyzer.analyzeTask(request);
    if (!expect(analysis.success,
                QStringLiteral("1024-bit VCD analysis succeeds: %1").arg(analysis.errorMessage))) {
        return 1;
    }

    const lockstep::waveform_viewer::WaveformViewModel model =
        lockstep::waveform_viewer::WaveformTraceViewer().loadTask(taskRoot.path());
    if (!expect(model.hasVcd && model.hasAnalysis,
                QStringLiteral("imported 1024-bit VCD is visible")) ||
        !expect(!model.samples.isEmpty(), QStringLiteral("waveform contains visible samples")) ||
        !expect(model.groups.size() == 9, QStringLiteral("waveform contains nine protocol groups")) ||
        !expect(model.samples.first().valueHex.size() == 256,
                QStringLiteral("sample preserves all 1024 bits"))) {
        QTextStream(stderr) << "status=" << model.status
                            << " diagnostics=" << model.diagnostics.join(QStringLiteral(" | ")) << '\n';
        return 1;
    }
    const auto& ahbFields = model.groups.first().fields;
    if (!expect(ahbFields.size() >= 2 && ahbFields.at(0).lsb == 32 && ahbFields.at(1).lsb == 417,
                QStringLiteral("1024-bit AHB display mapping matches the hardware trace map"))) return 1;
    int unknownSample = -1;
    int recoveredSample = -1;
    QStringList unknownPattern;
    for (int index = 0; index < model.samples.size(); ++index) {
        unknownPattern.append(model.samples.at(index).unknown ? QStringLiteral("X") : QStringLiteral("K"));
        if (model.samples.at(index).unknown && unknownSample < 0) unknownSample = index;
        if (unknownSample >= 0 && index > unknownSample && !model.samples.at(index).unknown) {
            recoveredSample = index;
            break;
        }
    }
    if (argc < 2 &&
        (!expect(unknownSample >= 0 && recoveredSample > unknownSample,
                 QStringLiteral("scalar x/z state recovers after the channel returns to 0/1: %1")
                     .arg(unknownPattern.join(QLatin1Char(',')))) ||
         !expect(model.keyBehaviors.join(QLatin1Char('\n')).contains(QStringLiteral("mismatch_event")),
                 QStringLiteral("out-of-order scalar declarations preserve channel mapping")))) return 1;
    const QStringList auxiliaryFragments = {
        QStringLiteral("index"), QStringLiteral("activity"), QStringLiteral("event"),
        QStringLiteral("tick"), QStringLiteral("toggle"), QStringLiteral("fall")
    };
    for (const auto& protocolGroup : model.groups) {
        for (const auto& protocolField : protocolGroup.fields) {
            for (const QString& fragment : auxiliaryFragments) {
                if (!expect(!protocolField.name.contains(fragment, Qt::CaseInsensitive),
                            QStringLiteral("waveform only exposes protocol signals: %1")
                                .arg(protocolField.name))) return 1;
            }
        }
    }

    lockstep::ui::MainWindowShell window;
    window.resize(1440, 900);
    window.showPage(lockstep::ui::NavigationPage::Waveform);
    window.setWaveformTraceView(model.status, model.vcdPath, model.timeRangeText,
                                uiGroups(model.groups), uiSamples(model.samples),
                                model.keyBehaviors, model.diagnostics);
    window.show();
    QCoreApplication::processEvents();
    QWidget* const display = window.findChild<QWidget*>(QStringLiteral("waveform_display_widget"));
    if (!expect(display != nullptr, QStringLiteral("waveform display widget exists"))) return 1;

    const QPointF ahbGroupPoint(15.0, 28.0 + 13.0);
    QMouseEvent pressEvent(QEvent::MouseButtonPress, ahbGroupPoint,
                           display->mapToGlobal(ahbGroupPoint.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(display, &pressEvent);
    QCoreApplication::processEvents();

    const int oldRowHeight = display->property("waveformRowHeight").toInt();
    QWheelEvent rowZoomEvent(QPointF(display->width() * 0.6, 120.0),
                             display->mapToGlobal(QPoint(display->width() * 0.6, 120)),
                             QPoint(), QPoint(0, 120), Qt::NoButton,
                             Qt::AltModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(display, &rowZoomEvent);
    QCoreApplication::processEvents();
    if (!expect(display->property("waveformRowHeight").toInt() > oldRowHeight,
                QStringLiteral("Alt+wheel increases waveform row height"))) return 1;

    const QPointF zoomPoint(display->width() * 0.65, display->height() * 0.35);
    for (int step = 0; step < 10; ++step) {
        QWheelEvent zoomEvent(zoomPoint, display->mapToGlobal(zoomPoint.toPoint()),
                              QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(display, &zoomEvent);
    }
    QCoreApplication::processEvents();
    const double zoom = display->property("waveformZoom").toDouble();
    if (!expect(zoom > 16.0,
                QStringLiteral("horizontal zoom is no longer limited to 16x: %1").arg(zoom))) return 1;

    const qint64 visibleStartBeforeResize = display->property("waveformVisibleStart").toLongLong();
    const qint64 visibleEndBeforeResize = display->property("waveformVisibleEnd").toLongLong();
    const qint64 visibleSpanBeforeResize = visibleEndBeforeResize - visibleStartBeforeResize;
    window.resize(1680, 900);
    QCoreApplication::processEvents();
    const int rowHeight = display->property("waveformRowHeight").toInt();
    const QPointF hoverPoint(display->width() * 0.68, 28.0 + rowHeight + rowHeight / 2.0);
    QMouseEvent hoverEvent(QEvent::MouseMove, hoverPoint,
                           display->mapToGlobal(hoverPoint.toPoint()),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(display, &hoverEvent);
    QCoreApplication::processEvents();
    const qint64 visibleSpanAfterResize =
        display->property("waveformVisibleEnd").toLongLong() -
        display->property("waveformVisibleStart").toLongLong();
    if (!expect(visibleSpanAfterResize > visibleSpanBeforeResize,
                QStringLiteral("time-per-pixel scale remains linear when viewport width changes"))) return 1;

    QScrollBar* const timeScrollBar =
        display->findChild<QScrollBar*>(QStringLiteral("waveform_time_scrollbar"));
    if (!expect(timeScrollBar != nullptr && timeScrollBar->isVisible() &&
                    timeScrollBar->maximum() > 0,
                QStringLiteral("standard horizontal time scrollbar is visible and scrollable"))) return 1;
    const qint64 startBeforeScroll = display->property("waveformVisibleStart").toLongLong();
    timeScrollBar->setValue(qRound(timeScrollBar->maximum() * 0.503));
    QCoreApplication::processEvents();
    if (!expect(display->property("waveformVisibleStart").toLongLong() != startBeforeScroll &&
                    display->property("waveformScrollMaximum").toInt() > 0,
                QStringLiteral("bottom scrollbar moves the visible time region"))) return 1;
    const QString cursorText = display->property("waveformCursorText").toString();
    if (!expect(cursorText.contains(QStringLiteral("时间:")) &&
                    cursorText.contains(QStringLiteral("信号:")) &&
                    cursorText.contains(QStringLiteral("数值:")),
                QStringLiteral("mouse cursor exposes time, signal and value: %1").arg(cursorText))) {
        return 1;
    }

    window.resize(1280, 720);
    QCoreApplication::processEvents();
    QWidget* const analyzerPanel =
        window.findChild<QWidget*>(QStringLiteral("waveform_analyzer_panel"));
    if (!expect(analyzerPanel != nullptr && display->height() <= analyzerPanel->height() &&
                    timeScrollBar->mapToGlobal(timeScrollBar->rect().bottomRight()).y() <=
                        analyzerPanel->mapToGlobal(analyzerPanel->rect().bottomRight()).y(),
                QStringLiteral("real VCD horizontal scrollbar remains visible in embedded 720p layout"))) {
        return 1;
    }
    const QRect protocolEventRect =
        display->property("waveformFirstProtocolEventRect").toRect();
    if (!protocolEventRect.isEmpty()) {
        const QPointF protocolHoverPoint(protocolEventRect.center());
        QMouseEvent protocolHoverEvent(
            QEvent::MouseMove, protocolHoverPoint,
            display->mapToGlobal(protocolHoverPoint.toPoint()),
            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(display, &protocolHoverEvent);
        QCoreApplication::processEvents();
        if (!model.groups.first().transactions.isEmpty() &&
            !expect(display->property("waveformCursorText").toString().contains(QStringLiteral("RESP=OKAY")),
                    QStringLiteral("real AHB event hover exposes semantic response"))) return 1;
    }
    QImage image(display->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    display->render(&painter);
    painter.end();
    if (!expect(waveformPixelCount(image) > 100,
                QStringLiteral("1024-bit samples produce visible waveform pixels"))) return 1;
    if (!expect(exactColorCount(image, QColor(QStringLiteral("#eef4f8"))) > 100,
                QStringLiteral("stable bus segments retain their background fill"))) return 1;
    const QString screenshotPath = argc >= 3
        ? QString::fromLocal8Bit(argv[2]) : qEnvironmentVariable("LOCKSTEP_1024_SCREENSHOT");
    if (!screenshotPath.isEmpty() &&
        !expect(image.save(screenshotPath), QStringLiteral("waveform screenshot is saved"))) {
        return 1;
    }
    window.setProtocolAnalysisView(
        model.status, model.analysisPath, model.keyBehaviors, model.diagnostics);
    window.showPage(lockstep::ui::NavigationPage::Protocol);
    QCoreApplication::processEvents();
    QPlainTextEdit* const eventList =
        window.findChild<QPlainTextEdit*>(QStringLiteral("protocol_key_behaviors_edit"));
    if (!expect(eventList != nullptr &&
                    eventList->property("protocolEventCount").toInt() == model.keyBehaviors.size() &&
                    !eventList->toPlainText().trimmed().isEmpty(),
                QStringLiteral("protocol page renders the decoded event list"))) return 1;
    if (!screenshotPath.isEmpty()) {
        QImage protocolPage(window.size(), QImage::Format_ARGB32_Premultiplied);
        protocolPage.fill(Qt::white);
        QPainter pagePainter(&protocolPage);
        window.render(&pagePainter);
        pagePainter.end();
        const QFileInfo screenshotInfo(screenshotPath);
        const QString protocolScreenshot = screenshotInfo.dir().filePath(
            screenshotInfo.completeBaseName() + QStringLiteral("_protocol_page.png"));
        if (!expect(protocolPage.save(protocolScreenshot),
                    QStringLiteral("protocol page screenshot is saved"))) return 1;
    }
    return 0;
}
