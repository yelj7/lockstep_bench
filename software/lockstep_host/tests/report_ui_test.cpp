/**********************************************************
* 文件名: report_ui_test.cpp
* 日期: 2026-07-14
* 版本: v1.0
* 更新记录: 初版创建
* 描述: 离屏验证测试报告页面状态、证据表和响应式布局
**********************************************************/

#include <iostream>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>

#include "main_window_shell.h"

namespace {

bool expect(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

lockstep::ui::ReportPageViewModel readyReport()
{
    using namespace lockstep::ui;
    ReportPageViewModel model;
    model.lifecycle = ReportLifecycleState::Current;
    model.lifecycleText = QStringLiteral("报告为最新");
    model.taskName = QStringLiteral("锁步验证任务");
    model.taskId = QStringLiteral("task_0001");
    model.modeText = QStringLiteral("测试模式");
    model.conclusion = QStringLiteral("pass");
    model.conclusionText = QStringLiteral("通过");
    model.primaryReason = QStringLiteral("程序跑通所需强制证据齐全");
    model.generatedAt = QStringLiteral("2026-07-14T08:00:00.000Z");
    model.hasTask = true;
    model.hasPersistedReport = true;
    const QStringList ids = {QStringLiteral("program_write"), QStringLiteral("readback_verify"),
                             QStringLiteral("run_control")};
    const QStringList names = {QStringLiteral("程序烧写"), QStringLiteral("回读校验"),
                               QStringLiteral("程序运行")};
    for (int index = 0; index < ids.size(); ++index) {
        ReportEvidenceViewItem item;
        item.id = ids.at(index);
        item.displayName = names.at(index);
        item.state = QStringLiteral("passed");
        item.stateText = QStringLiteral("通过");
        item.summary = QStringLiteral("证据有效");
        item.relativePath = QStringLiteral("evidence/%1.json").arg(ids.at(index));
        model.requiredEvidence.append(item);
    }
    const QStringList optionalIds = {QStringLiteral("vcd_waveform"), QStringLiteral("protocol_analysis"),
                                     QStringLiteral("acquisition"), QStringLiteral("fault_injection")};
    for (const QString& id : optionalIds) {
        ReportOptionalViewItem item;
        item.id = id;
        item.displayName = id;
        item.state = QStringLiteral("not_available");
        item.stateText = QStringLiteral("不可用");
        model.optionalRecords.append(item);
    }
    return model;
}

bool reportPageRendersLifecycleAndEvidence()
{
    lockstep::ui::MainWindowShell window;
    window.resize(1280, 720);
    window.show();
    QApplication::processEvents();
    window.showPage(lockstep::ui::NavigationPage::Stats);
    window.setReportPageState(readyReport());
    QApplication::processEvents();

    const QLabel* const conclusion = window.findChild<QLabel*>(QStringLiteral("report_conclusion_label"));
    const QTableWidget* const evidence = window.findChild<QTableWidget*>(QStringLiteral("report_evidence_table"));
    const QSplitter* const splitter = window.findChild<QSplitter*>(QStringLiteral("report_main_split"));
    if (!expect(conclusion != nullptr && conclusion->text() == QStringLiteral("通过"),
                "conclusion should render") ||
        !expect(evidence != nullptr && evidence->item(1, 0) != nullptr &&
                    evidence->item(1, 0)->text() == QStringLiteral("回读校验"),
                "required evidence should render") ||
        !expect(splitter != nullptr && splitter->orientation() == Qt::Vertical,
                "compact report layout should use a vertical split")) {
        return false;
    }

    lockstep::ui::ReportPageViewModel stale = readyReport();
    stale.lifecycle = lockstep::ui::ReportLifecycleState::Stale;
    stale.lifecycleText = QStringLiteral("报告已过期");
    stale.stale = true;
    stale.conclusion = QStringLiteral("fail");
    stale.conclusionText = QStringLiteral("失败");
    stale.persistedConclusion = QStringLiteral("pass");
    stale.persistedConclusionText = QStringLiteral("通过");
    lockstep::ui::ReportDiagnosticViewItem protocolIssue;
    protocolIssue.id = QStringLiteral("diag_1");
    protocolIssue.severity = QStringLiteral("warning");
    protocolIssue.source = QStringLiteral("Protocol");
    protocolIssue.message = QStringLiteral("协议诊断");
    protocolIssue.targetPage = QStringLiteral("protocol");
    stale.diagnostics.append(protocolIssue);
    lockstep::ui::UiActionRequest capturedRequest;
    QObject::connect(&window, &lockstep::ui::MainWindowShell::actionRequested,
                     [&capturedRequest](const lockstep::ui::UiActionRequest& request) {
                         capturedRequest = request;
                     });
    window.setReportPageState(stale);
    const QLabel* const reason = window.findChild<QLabel*>(QStringLiteral("report_reason_label"));
    if (!expect(reason != nullptr && reason->text().contains(QStringLiteral("当前预检：失败")) &&
                    reason->text().contains(QStringLiteral("落盘快照：通过")),
                "stale reports should distinguish live and persisted conclusions")) {
        return false;
    }
    QTreeWidget* const diagnostics = window.findChild<QTreeWidget*>(QStringLiteral("report_diagnostics_tree"));
    if (!expect(diagnostics != nullptr && diagnostics->topLevelItemCount() == 1,
                "diagnostic issue should render")) {
        return false;
    }
    diagnostics->itemDoubleClicked(diagnostics->topLevelItem(0), 0);
    if (!expect(capturedRequest.action == lockstep::ui::UiAction::NavigateToReportSource &&
                    capturedRequest.parameters.value(QStringLiteral("targetPage")).toString() ==
                        QStringLiteral("protocol"),
                "diagnostic navigation should preserve the source page")) {
        return false;
    }

    const QList<QPushButton*> buttons = window.findChildren<QPushButton*>();
    QPushButton* generateButton = nullptr;
    for (QPushButton* const button : buttons) {
        if (button->property("uiAction").toInt() ==
            static_cast<int>(lockstep::ui::UiAction::GenerateReport)) {
            generateButton = button;
            break;
        }
    }
    if (!expect(generateButton != nullptr, "generate report button should exist")) {
        return false;
    }

    lockstep::ui::ReportPageViewModel transient = readyReport();
    const QVector<QPair<lockstep::ui::ReportLifecycleState, QString>> lifecycleCases = {
        {lockstep::ui::ReportLifecycleState::NotGenerated, QStringLiteral("当前证据预检")},
        {lockstep::ui::ReportLifecycleState::Generating, QStringLiteral("正在生成")},
        {lockstep::ui::ReportLifecycleState::GenerationError, QStringLiteral("生成失败")},
        {lockstep::ui::ReportLifecycleState::LoadError, QStringLiteral("读取失败")},
    };
    QLabel* const lifecycle = window.findChild<QLabel*>(QStringLiteral("report_lifecycle_label"));
    for (const auto& lifecycleCase : lifecycleCases) {
        transient.lifecycle = lifecycleCase.first;
        transient.lifecycleText = lifecycleCase.second;
        transient.generating = lifecycleCase.first == lockstep::ui::ReportLifecycleState::Generating;
        transient.errorMessage = lifecycleCase.first == lockstep::ui::ReportLifecycleState::GenerationError
            ? QStringLiteral("磁盘写入失败") : QString();
        window.setReportPageState(transient);
        if (!expect(lifecycle != nullptr && lifecycle->text() == lifecycleCase.second,
                    "report lifecycle label should render every state")) {
            return false;
        }
        if (transient.generating && !expect(!generateButton->isEnabled(),
                                            "generate should be disabled while generating")) {
            return false;
        }
    }

    lockstep::ui::ReportPageViewModel empty;
    empty.lifecycleText = QStringLiteral("未选择任务");
    empty.conclusionText = QStringLiteral("无可评估任务");
    empty.primaryReason = QStringLiteral("请先创建或加载验证任务。");
    window.setReportPageState(empty);
    return expect(!generateButton->isEnabled(), "generate should be disabled without a task");
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    return reportPageRendersLifecycleAndEvidence() ? 0 : 1;
}
