/**********************************************************
* 文件名: fault_injection_orchestrator.h
* 日期: 2026-07-14
* 版本: v1.0
* 更新记录: 初版创建错误注入脚本编排接口
* 描述: 在回读后、采样 armed 前执行固化 SEM/ICAP 脚本并归档证据。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_FAULT_INJECTION_FAULT_INJECTION_ORCHESTRATOR_H_
#define LOCKSTEP_HOST_SRC_FAULT_INJECTION_FAULT_INJECTION_ORCHESTRATOR_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace lockstep::fault_injection {

struct FaultInjectionRequest final {
    QString taskRootPath;
    QString scriptPath;
    QString allowedScriptDirectory;
    QStringList arguments;
    QString workingDirectory;
    int timeoutMs = 30'000;
    bool configured = false;
};

struct FaultInjectionResult final {
    QString status;
    int exitCode = -1;
    bool timedOut = false;
    QString stdoutText;
    QString stderrText;
    QString evidencePath;
    QString error;

    QJsonObject toJson() const;
};

class FaultInjectionOrchestrator final {
public:
    FaultInjectionResult execute(const FaultInjectionRequest& request) const;
};

}  // namespace lockstep::fault_injection

#endif  // LOCKSTEP_HOST_SRC_FAULT_INJECTION_FAULT_INJECTION_ORCHESTRATOR_H_
