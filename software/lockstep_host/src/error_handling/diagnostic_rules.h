/**********************************************************
* 文件名: diagnostic_rules.h
* 日期: 2026-07-17
* 版本: 1.0
* 更新记录: 新增机器可读诊断规则目录接口。
* 描述: 为界面、错误登记、报告和板级诊断提供统一建议动作。
**********************************************************/

#ifndef LOCKSTEP_HOST_SRC_ERROR_HANDLING_DIAGNOSTIC_RULES_H_
#define LOCKSTEP_HOST_SRC_ERROR_HANDLING_DIAGNOSTIC_RULES_H_

#include <QList>
#include <QString>

namespace lockstep::error_handling {

struct DiagnosticRule final {
    QString code;
    QString severity;
    QString message;
    QString suggestion;
};

class DiagnosticRuleCatalog final {
public:
    bool load(const QString& path, QString* errorMessage = nullptr);
    bool loadDefault(QString* errorMessage = nullptr);
    bool find(const QString& code, DiagnosticRule* rule) const;

private:
    QList<DiagnosticRule> rules_;
};

QString diagnosticSuggestion(const QString& code);

}  // namespace lockstep::error_handling

#endif  // LOCKSTEP_HOST_SRC_ERROR_HANDLING_DIAGNOSTIC_RULES_H_
