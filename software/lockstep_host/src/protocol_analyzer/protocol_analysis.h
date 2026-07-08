/**********************************************************
* 文件名: protocol_analysis.h
* 日期: 2026-07-08
* 版本: v1.0
* 更新记录: 初版创建固定 trace VCD 协议解析接口
* 描述: 声明 M12 协议解析模块的任务级输入、解析结果和分析入口
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

}  // namespace lockstep::protocol_analyzer

#endif  // LOCKSTEP_HOST_SRC_PROTOCOL_ANALYZER_PROTOCOL_ANALYSIS_H_
