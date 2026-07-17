/**********************************************************
* 文件名: fault_injection_orchestrator.cpp
* 日期: 2026-07-14
* 版本: v1.0
* 更新记录: 初版实现脚本启动、超时、退出码和证据归档
* 描述: C++ 负责边界与失败传播，脚本负责硬件相关 SEM/ICAP 操作。
**********************************************************/

#include "fault_injection_orchestrator.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QDateTime>

namespace lockstep::fault_injection {

QJsonObject FaultInjectionResult::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), QStringLiteral("lockstep-fault-injection-v1"));
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("exit_code"), exitCode);
    object.insert(QStringLiteral("timed_out"), timedOut);
    object.insert(QStringLiteral("stdout"), stdoutText);
    object.insert(QStringLiteral("stderr"), stderrText);
    object.insert(QStringLiteral("evidence"), QStringLiteral("evidence/fault_injection.json"));
    object.insert(QStringLiteral("generated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!error.isEmpty()) object.insert(QStringLiteral("error"), error);
    return object;
}

FaultInjectionResult FaultInjectionOrchestrator::execute(const FaultInjectionRequest& request) const
{
    FaultInjectionResult result;
    if (!request.configured) {
        result.status = QStringLiteral("skipped");
    } else if (request.taskRootPath.trimmed().isEmpty() ||
               !QDir(request.taskRootPath).exists() || request.scriptPath.trimmed().isEmpty() ||
               !QFileInfo::exists(request.scriptPath) || request.timeoutMs <= 0) {
        result.status = QStringLiteral("failed");
        result.error = QStringLiteral("错误注入脚本、任务目录或超时参数无效");
    } else {
        const QString scriptPath = QFileInfo(request.scriptPath).canonicalFilePath();
        const QString allowedDirectory = request.allowedScriptDirectory.trimmed().isEmpty()
            ? QString() : QFileInfo(request.allowedScriptDirectory).canonicalFilePath();
        if (!allowedDirectory.isEmpty() &&
            !scriptPath.startsWith(QDir(allowedDirectory).absolutePath() + QDir::separator(), Qt::CaseInsensitive)) {
            result.status = QStringLiteral("failed");
            result.error = QStringLiteral("错误注入脚本不在产品资源目录内");
        } else {
            const QString evidenceDir = QDir(request.taskRootPath).filePath(QStringLiteral("evidence"));
            QDir().mkpath(evidenceDir);
            const QString taskScript = QDir(evidenceDir).filePath(QFileInfo(scriptPath).fileName());
            if (!QFile::exists(taskScript) && !QFile::copy(scriptPath, taskScript)) {
                result.status = QStringLiteral("failed");
                result.error = QStringLiteral("无法复制错误注入脚本到任务证据目录");
            } else {
        QProcess process;
        process.setProgram(taskScript);
        process.setArguments(request.arguments);
        process.setWorkingDirectory(request.workingDirectory.isEmpty()
            ? QFileInfo(taskScript).absolutePath() : request.workingDirectory);
        process.start();
        if (!process.waitForStarted(request.timeoutMs)) {
            result.status = QStringLiteral("failed");
            result.error = process.errorString();
        } else if (!process.waitForFinished(request.timeoutMs)) {
            process.kill();
            process.waitForFinished();
            result.status = QStringLiteral("failed");
            result.timedOut = true;
            result.error = QStringLiteral("错误注入脚本超时");
        } else {
            result.exitCode = process.exitCode();
            result.stdoutText = QString::fromUtf8(process.readAllStandardOutput());
            result.stderrText = QString::fromUtf8(process.readAllStandardError());
            result.status = result.exitCode == 0 ? QStringLiteral("passed") : QStringLiteral("failed");
            if (result.exitCode != 0) result.error = QStringLiteral("错误注入脚本退出码非零");
        }
            }
        }
    }
    const QString evidenceDir = QDir(request.taskRootPath).filePath(QStringLiteral("evidence"));
    QDir().mkpath(evidenceDir);
    result.evidencePath = QDir(evidenceDir).filePath(QStringLiteral("fault_injection.json"));
    QSaveFile file(result.evidencePath);
    if (file.open(QIODevice::WriteOnly)) {
        const QByteArray payload = QJsonDocument(result.toJson()).toJson(QJsonDocument::Indented);
        if (file.write(payload) == payload.size()) file.commit();
    }
    return result;
}

}  // namespace lockstep::fault_injection
