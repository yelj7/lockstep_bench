/**********************************************************
* 文件名: waveform_trace_viewer.h
* 日期: 2026-07-08
* 版本: v1.0
* 更新记录: 初版创建固定 trace analysis 读取与展示模型接口
* 描述: 声明 M11 波形显示模块的任务级加载结果和 UI 展示模型
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_WAVEFORM_VIEWER_WAVEFORM_TRACE_VIEWER_H_
#define LOCKSTEP_HOST_SRC_WAVEFORM_VIEWER_WAVEFORM_TRACE_VIEWER_H_

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace lockstep::waveform_viewer {

struct WaveformGroupView final {
    QString id;
    QString displayName;
    QString status;
    QString reason;
    QStringList fields;
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
