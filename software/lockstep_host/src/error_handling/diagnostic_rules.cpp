/**********************************************************
* 文件名: diagnostic_rules.cpp
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增默认资源搜索与规则解析。
* 描述: 从随唯一 EXE 发布的 JSON 读取连接和采集诊断建议。
**********************************************************/

#include "diagnostic_rules.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lockstep::error_handling {

bool DiagnosticRuleCatalog::load(const QString& path, QString* const errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("无法读取诊断规则: %1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("诊断规则 JSON 无效: %1").arg(path);
        return false;
    }
    QList<DiagnosticRule> parsed;
    for (const QJsonValue& value : document.object().value(QStringLiteral("rules")).toArray()) {
        const QJsonObject object = value.toObject();
        DiagnosticRule rule;
        rule.code = object.value(QStringLiteral("code")).toString().trimmed();
        rule.severity = object.value(QStringLiteral("severity")).toString().trimmed();
        rule.message = object.value(QStringLiteral("message")).toString().trimmed();
        rule.suggestion = object.value(QStringLiteral("suggestion")).toString().trimmed();
        if (rule.code.isEmpty() || rule.suggestion.isEmpty()) {
            if (errorMessage != nullptr) *errorMessage = QStringLiteral("诊断规则缺少 code 或 suggestion");
            return false;
        }
        parsed.append(rule);
    }
    if (parsed.isEmpty()) {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("诊断规则列表为空");
        return false;
    }
    rules_ = parsed;
    return true;
}

bool DiagnosticRuleCatalog::loadDefault(QString* const errorMessage)
{
    const QString relative = QStringLiteral("diagnostics/diagnostic_rules.json");
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(applicationDir).filePath(QStringLiteral("resources/") + relative),
        QDir(applicationDir).filePath(QStringLiteral("../packaged-resources/") + relative),
        QDir::current().filePath(QStringLiteral("resources/") + relative),
        QDir::current().filePath(QStringLiteral("software/lockstep_host/resources/") + relative)};
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) return load(candidate, errorMessage);
    }
    if (errorMessage != nullptr) *errorMessage = QStringLiteral("未找到随产品发布的诊断规则");
    return false;
}

bool DiagnosticRuleCatalog::find(const QString& code, DiagnosticRule* const rule) const
{
    for (const DiagnosticRule& candidate : rules_) {
        if (candidate.code.compare(code.trimmed(), Qt::CaseInsensitive) == 0) {
            if (rule != nullptr) *rule = candidate;
            return true;
        }
    }
    return false;
}

QString diagnosticSuggestion(const QString& code)
{
    DiagnosticRuleCatalog catalog;
    DiagnosticRule rule;
    return catalog.loadDefault(nullptr) && catalog.find(code, &rule) ? rule.suggestion : QString();
}

}  // namespace lockstep::error_handling
