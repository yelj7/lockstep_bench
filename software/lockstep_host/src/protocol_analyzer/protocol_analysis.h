/**********************************************************
* 文件名: protocol_analysis.h
* 日期: 2026-07-14
* 版本: v1.1
* 更新记录: 增加九协议束字段目录和聚合总线定义
* 描述: 声明固定 trace 协议目录、任务级输入、解析结果和分析入口。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_PROTOCOL_ANALYZER_PROTOCOL_ANALYSIS_H_
#define LOCKSTEP_HOST_SRC_PROTOCOL_ANALYZER_PROTOCOL_ANALYSIS_H_

#include <QJsonObject>
#include <QList>
#include <QString>

#include "error_registry.h"

namespace lockstep::protocol_analyzer {

struct ProtocolDiagnostic final {
    QString code;
    QString severity;
    QString message;
    QString detail;
    QString errorId;
    QJsonObject context;
};

struct ProtocolFieldDefinition final {
    QString name;
    QString displayName;
    int lsb = -1;
    int width = 1;
    bool errorSignal = false;
};

struct ProtocolGroupDefinition final {
    QString id;
    QString displayName;
    QList<ProtocolFieldDefinition> fields;
};

struct ProtocolAnalysisRequest final {
    QString taskRootPath;
    QString taskId;
    lockstep::error_handling::ErrorRegistry* errorRegistry = nullptr;
    bool reportDiagnosticsToErrorRegistry = true;
};

struct ProtocolAnalysisResult final {
    bool success = false;
    bool wroteAnalysis = false;
    QString status;
    QString analysisPath;
    QString errorMessage;
    QList<ProtocolDiagnostic> diagnostics;
    QJsonObject analysis;
};

class ProtocolAnalyzer final {
public:
    ProtocolAnalysisResult analyzeTask(const ProtocolAnalysisRequest& request) const;
};

QString fixedWaveformRelativePath();
QString fixedTraceSchemaRelativePath();
QString fixedTraceAnalysisRelativePath();
QList<ProtocolGroupDefinition> fixedProtocolGroups();

}  // namespace lockstep::protocol_analyzer

#endif  // LOCKSTEP_HOST_SRC_PROTOCOL_ANALYZER_PROTOCOL_ANALYSIS_H_
