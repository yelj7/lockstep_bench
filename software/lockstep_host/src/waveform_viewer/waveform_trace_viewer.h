/**********************************************************
* 文件名: waveform_trace_viewer.h
* 日期: 2026-07-14
* 版本: v1.2
* 更新记录: 汇总全部协议组事务供协议页和波形页共同显示
* 描述: 声明波形显示模块的九协议束、采样点和任务级加载模型。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_WAVEFORM_VIEWER_WAVEFORM_TRACE_VIEWER_H_
#define LOCKSTEP_HOST_SRC_WAVEFORM_VIEWER_WAVEFORM_TRACE_VIEWER_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::waveform_viewer {

struct WaveformFieldView final {
    QString name;
    QString displayName;
    int lsb = -1;
    int width = 1;
    bool errorSignal = false;
};

struct WaveformSampleView final {
    qint64 time = 0;
    QString valueHex;
    bool unknown = false;
};

struct WaveformGroupView final {
    QString id;
    QString displayName;
    QString status;
    QString reason;
    QList<WaveformFieldView> fields;
    QStringList transactions;
};

struct WaveformViewModel final {
    bool hasVcd = false;
    bool hasSchema = false;
    bool hasAnalysis = false;
    bool analysisStale = false;
    QString status;
    QString vcdPath;
    QString schemaPath;
    QString analysisPath;
    QString timeRangeText;
    QStringList keyBehaviors;
    QStringList diagnostics;
    QList<WaveformGroupView> groups;
    QList<WaveformSampleView> samples;
    QJsonObject analysis;
};

class WaveformTraceViewer final {
public:
    WaveformViewModel loadTask(const QString& taskRootPath) const;
};

QString fixedWaveformRelativePath();
QString fixedTraceSchemaRelativePath();
QString fixedTraceAnalysisRelativePath();

}  // namespace lockstep::waveform_viewer

#endif  // LOCKSTEP_HOST_SRC_WAVEFORM_VIEWER_WAVEFORM_TRACE_VIEWER_H_
