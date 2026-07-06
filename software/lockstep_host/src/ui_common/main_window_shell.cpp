/**********************************************************
* 文件名: main_window_shell.cpp
* 日期: 2026-07-06
* 版本: v3.0
* 更新记录: v1.0 初版创建主窗口 UI 骨架；v2.0 创建解耦接口；v3.0 从零复刻源工程工作台结构
* 描述: 按源工程 MainWindow 结构创建浅色工作台，仅保留占位与动作接口
**********************************************************/

#include "main_window_shell.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "ui_theme.h"

namespace lockstep::ui {

namespace {

constexpr int kMinimumWidth = 1280;
constexpr int kMinimumHeight = 720;
constexpr int kSidebarWidth = 224;
constexpr int kDiagnosticsMinHeight = 128;
constexpr int kDiagnosticsMaxHeight = 220;
constexpr int kDetachedLogWidth = 900;
constexpr int kDetachedLogHeight = 420;

QLabel* pageTitle(const QString& text, QWidget* const parent)
{
    QLabel* const label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("page_title"));
    return label;
}

QLabel* mutedLabel(const QString& text, QWidget* const parent)
{
    QLabel* const label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted_label"));
    label->setProperty("mutedLabel", true);
    label->setWordWrap(true);
    return label;
}

QGroupBox* panelBox(const QString& title, QWidget* const parent)
{
    QGroupBox* const group = new QGroupBox(title, parent);
    group->setProperty("panelBox", true);
    group->setAttribute(Qt::WA_StyledBackground, true);
    return group;
}

QScrollArea* scrollPage(QWidget* const content)
{
    QScrollArea* const scroll = new QScrollArea(content->parentWidget());
    scroll->setObjectName(QStringLiteral("workbench_scroll_page"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

void setLayoutItemsVisible(QLayout* const layout, const bool visible)
{
    if (layout == nullptr) {
        return;
    }

    for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem* const item = layout->itemAt(index);
        if (item == nullptr) {
            continue;
        }
        QWidget* const widget = item->widget();
        if (widget != nullptr) {
            widget->setVisible(visible);
        }
        setLayoutItemsVisible(item->layout(), visible);
    }
}

QString pageIdForPage(const NavigationPage page)
{
    QString pageId;

    switch (page) {
    case NavigationPage::Project:
        pageId = QStringLiteral("project");
        break;
    case NavigationPage::Connection:
        pageId = QStringLiteral("connection");
        break;
    case NavigationPage::Mode:
        pageId = QStringLiteral("mode");
        break;
    case NavigationPage::RamProgram:
        pageId = QStringLiteral("ram_program");
        break;
    case NavigationPage::Waveform:
        pageId = QStringLiteral("waveform");
        break;
    case NavigationPage::Protocol:
        pageId = QStringLiteral("protocol");
        break;
    case NavigationPage::Stats:
        pageId = QStringLiteral("stats");
        break;
    default:
        pageId = QStringLiteral("project");
        break;
    }

    return pageId;
}

}  // namespace

MainWindowShell::MainWindowShell(QWidget* const parent)
    : QMainWindow(parent),
      topStatusBar_(nullptr),
      pageStack_(nullptr),
      navButtons_(),
      pageIds_(),
      logTabs_(nullptr),
      logDetachButton_(nullptr),
      logEdit_(nullptr),
      serialOutputEdit_(nullptr),
      detachedLogDialog_(nullptr),
      detachedLogEdit_(nullptr)
{
    qRegisterMetaType<UiActionRequest>("lockstep::ui::UiActionRequest");
    qRegisterMetaType<UiWorkbenchState>("lockstep::ui::UiWorkbenchState");

    setCentralWidget(createWorkbenchShell());
    setMinimumSize(kMinimumWidth, kMinimumHeight);
    setWindowTitle(QStringLiteral("锁步研发测试系统上位机"));
    setWorkbenchState(makeDefaultWorkbenchState(UiMode::Research));
    setActivePage(QStringLiteral("project"));
}

void MainWindowShell::setWorkbenchState(const UiWorkbenchState& state)
{
    setTopStatus(state.topStatus);

    if ((logEdit_ != nullptr) && !state.logText.isEmpty()) {
        logEdit_->setPlainText(state.logText);
        logEdit_->moveCursor(QTextCursor::End);
    }
    if ((serialOutputEdit_ != nullptr) && !state.serialText.isEmpty()) {
        serialOutputEdit_->setPlainText(state.serialText);
        serialOutputEdit_->moveCursor(QTextCursor::End);
    }
}

void MainWindowShell::setTopStatus(const GlobalStatus& status)
{
    if (topStatusBar_ != nullptr) {
        topStatusBar_->setStatus(status);
    }
}

void MainWindowShell::appendLog(
    const LogChannel channel,
    const LogLevel level,
    const QString& source,
    const QString& message)
{
    QPlainTextEdit* const view = (channel == LogChannel::Serial) ? serialOutputEdit_ : logEdit_;
    appendFormattedLog(view, level, source, message);
    if ((level == LogLevel::Error) && (topStatusBar_ != nullptr)) {
        GlobalStatus status = makeDefaultGlobalStatus(UiMode::Research);
        status.taskStatusText = QStringLiteral("任务: 阻断");
        status.targetStatusText = QStringLiteral("目标: 待检查");
        status.programStatusText = QStringLiteral("最近错误: %1").arg(message);
        topStatusBar_->setStatus(status);
    }
}

void MainWindowShell::setSerialPlaceholderText(const QString& text)
{
    if (serialOutputEdit_ != nullptr) {
        serialOutputEdit_->setPlainText(text);
    }
}

void MainWindowShell::switchWorkbenchPage()
{
    const QPushButton* const button = qobject_cast<QPushButton*>(sender());
    if (button != nullptr) {
        setActivePage(button->property("pageId").toString());
    }
}

void MainWindowShell::clearVisibleLog()
{
    QPlainTextEdit* const view = currentLogView();
    if (view != nullptr) {
        view->clear();
    }
}

void MainWindowShell::toggleLogDetached()
{
    emitAction(UiAction::DetachLogWindow, pageForId(QStringLiteral("project")), QStringLiteral("log_detach_button"));

    if (detachedLogDialog_ != nullptr) {
        detachedLogDialog_->raise();
        detachedLogDialog_->activateWindow();
        return;
    }

    detachedLogDialog_ = new QDialog(this);
    detachedLogDialog_->setAttribute(Qt::WA_DeleteOnClose, true);
    detachedLogDialog_->setWindowTitle(QStringLiteral("Log"));
    detachedLogDialog_->resize(kDetachedLogWidth, kDetachedLogHeight);
    UiTheme::applyWorkbenchStyle(detachedLogDialog_);

    detachedLogEdit_ = new QPlainTextEdit(detachedLogDialog_);
    detachedLogEdit_->setReadOnly(true);
    detachedLogEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    detachedLogEdit_->setPlainText(currentLogText());

    QVBoxLayout* const layout = new QVBoxLayout(detachedLogDialog_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(detachedLogEdit_);

    connect(detachedLogDialog_, &QDialog::finished, this, [this]() {
        detachedLogDialog_ = nullptr;
        detachedLogEdit_ = nullptr;
    });
    detachedLogDialog_->show();
}

QWidget* MainWindowShell::createWorkbenchShell()
{
    QWidget* const root = new QWidget(this);
    root->setObjectName(QStringLiteral("workbench_shell"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    UiTheme::applyWorkbenchStyle(root);

    QVBoxLayout* const layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createTopBar(root));

    QWidget* const body = new QWidget(root);
    body->setObjectName(QStringLiteral("workbench_body"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* const bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(createSidebar(body));
    bodyLayout->addWidget(createPageContainer(body), 1);
    layout->addWidget(body, 1);

    return root;
}

QWidget* MainWindowShell::createTopBar(QWidget* const parent)
{
    topStatusBar_ = new TopStatusBar(parent);
    return topStatusBar_;
}

QWidget* MainWindowShell::createSidebar(QWidget* const parent)
{
    QFrame* const sidebar = new QFrame(parent);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    sidebar->setFixedWidth(kSidebarWidth);

    QVBoxLayout* const layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(8, 14, 8, 14);
    layout->setSpacing(3);

    QLabel* const caption = new QLabel(QStringLiteral("WORKSPACE"), sidebar);
    caption->setObjectName(QStringLiteral("sidebar_caption"));
    layout->addWidget(caption);

    const QList<QPair<QString, QString>> nav = {
        {QStringLiteral("project"), QStringLiteral("项目与任务")},
        {QStringLiteral("connection"), QStringLiteral("目标连接")},
        {QStringLiteral("mode"), QStringLiteral("工作模式")},
        {QStringLiteral("ram_program"), QStringLiteral("RAM程序烧录")},
        {QStringLiteral("waveform"), QStringLiteral("波形分析仪")},
        {QStringLiteral("protocol"), QStringLiteral("协议解析")},
        {QStringLiteral("stats"), QStringLiteral("性能统计")},
    };

    for (const QPair<QString, QString>& item : nav) {
        layout->addWidget(createNavButton(item.first, item.second, sidebar));
    }

    layout->addStretch(1);
    return sidebar;
}

QWidget* MainWindowShell::createPageContainer(QWidget* const parent)
{
    QWidget* const container = new QWidget(parent);
    container->setObjectName(QStringLiteral("page_container"));
    container->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* const layout = new QVBoxLayout(container);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    pageStack_ = new QStackedWidget(container);
    pageStack_->setObjectName(QStringLiteral("page_stack"));
    pageStack_->setAttribute(Qt::WA_StyledBackground, true);
    pageStack_->setMinimumHeight(72);
    pageStack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    addWorkbenchPage(QStringLiteral("project"), NavigationPage::Project, createProjectPage());
    addWorkbenchPage(QStringLiteral("connection"), NavigationPage::Connection, createConnectionPage());
    addWorkbenchPage(QStringLiteral("mode"), NavigationPage::Mode, createModePage());
    addWorkbenchPage(QStringLiteral("ram_program"), NavigationPage::RamProgram, createRamProgramPage());
    addWorkbenchPage(QStringLiteral("waveform"), NavigationPage::Waveform, createWaveformPage());
    addWorkbenchPage(QStringLiteral("protocol"), NavigationPage::Protocol, createProtocolPage());
    addWorkbenchPage(QStringLiteral("stats"), NavigationPage::Stats, createStatsPage());
    layout->addWidget(pageStack_, 12);

    layout->addWidget(createDiagnosticsPanel(container));
    return container;
}

QWidget* MainWindowShell::createProjectPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_project"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setSpacing(12);

    QHBoxLayout* const header = new QHBoxLayout();
    header->addWidget(pageTitle(QStringLiteral("项目与任务管理"), content));
    header->addStretch(1);
    header->addWidget(createActionButton(UiAction::NewTask, NavigationPage::Project, content, false));
    header->addWidget(createActionButton(UiAction::SaveTask, NavigationPage::Project, content, true));
    QPushButton* const loadButton = createActionButton(UiAction::LoadTaskToWorkbench, NavigationPage::Project, content, false);
    loadButton->setEnabled(false);
    header->addWidget(loadButton);
    layout->addLayout(header);

    QGridLayout* const mainGrid = new QGridLayout();
    mainGrid->setSpacing(14);

    QGroupBox* const browser = panelBox(QStringLiteral("项目浏览器"), content);
    browser->setMinimumWidth(420);
    browser->setMinimumHeight(180);
    QVBoxLayout* const browserLayout = new QVBoxLayout(browser);
    browserLayout->addWidget(mutedLabel(QStringLiteral("工作区未加载"), browser));
    QTreeWidget* const projectTree = new QTreeWidget(browser);
    projectTree->setObjectName(QStringLiteral("project_browser_tree"));
    projectTree->setHeaderLabel(QStringLiteral("工作区内容"));
    projectTree->setSelectionMode(QAbstractItemView::SingleSelection);
    projectTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QTreeWidgetItem* const rootItem = new QTreeWidgetItem(projectTree, {QStringLiteral("UI 占位工作区")});
    QTreeWidgetItem* const tasksItem = new QTreeWidgetItem(rootItem, {QStringLiteral("tasks")});
    new QTreeWidgetItem(tasksItem, {QStringLiteral("验证任务占位")});
    new QTreeWidgetItem(rootItem, {QStringLiteral("reports")});
    projectTree->expandAll();
    browserLayout->addWidget(projectTree, 1);
    mainGrid->addWidget(browser, 0, 0);

    QGroupBox* const detail = panelBox(QStringLiteral("任务详情"), content);
    QVBoxLayout* const detailLayout = new QVBoxLayout(detail);
    QFormLayout* const detailForm = new QFormLayout();
    detailForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    QLineEdit* const taskNameEdit = new QLineEdit(detail);
    taskNameEdit->setReadOnly(true);
    taskNameEdit->setText(QStringLiteral("验证任务占位"));
    QPlainTextEdit* const taskDescriptionEdit = new QPlainTextEdit(detail);
    taskDescriptionEdit->setReadOnly(true);
    taskDescriptionEdit->setFixedHeight(55);
    taskDescriptionEdit->setPlainText(QStringLiteral("这里仅展示 UI 占位，不读取真实工作区。"));
    QPlainTextEdit* const taskBasicInfoEdit = new QPlainTextEdit(detail);
    taskBasicInfoEdit->setReadOnly(true);
    taskBasicInfoEdit->setFixedHeight(48);
    taskBasicInfoEdit->setPlainText(QStringLiteral("模式: 占位\n状态: 未加载"));
    detailForm->addRow(QStringLiteral("任务名称"), taskNameEdit);
    detailForm->addRow(QStringLiteral("描述"), taskDescriptionEdit);
    detailForm->addRow(QStringLiteral("基本信息"), taskBasicInfoEdit);
    detailLayout->addLayout(detailForm);
    QHBoxLayout* const editButtons = new QHBoxLayout();
    editButtons->addStretch(1);
    editButtons->addWidget(createActionButton(UiAction::EditTask, NavigationPage::Project, detail, false));
    editButtons->addWidget(createActionButton(UiAction::SaveTaskEdit, NavigationPage::Project, detail, true));
    editButtons->addWidget(createActionButton(UiAction::CancelTaskEdit, NavigationPage::Project, detail, false));
    detailLayout->addLayout(editButtons);
    mainGrid->addWidget(detail, 0, 1);

    mainGrid->setColumnStretch(0, 1);
    mainGrid->setColumnStretch(1, 2);
    layout->addLayout(mainGrid);
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createConnectionPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_connection"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("目标连接"), content));
    layout->addWidget(createControlPanel());
    layout->addWidget(createConnectionPanel());
    layout->addWidget(createSerialConfigPanel());
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createModePage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_mode"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("工作模式"), content));

    QGroupBox* const group = panelBox(QStringLiteral("处理器工作模式期望"), content);
    QFormLayout* const form = new QFormLayout(group);
    QComboBox* const modeCombo = new QComboBox(group);
    modeCombo->addItem(QStringLiteral("双核锁步模式"));
    modeCombo->addItem(QStringLiteral("双核独立模式"));
    form->addRow(QStringLiteral("模式"), modeCombo);
    form->addRow(QStringLiteral("控制状态"), mutedLabel(QStringLiteral("仅记录期望模式，控制通道未接入。"), group));
    layout->addWidget(group);
    layout->addWidget(createTodoCard(QStringLiteral("TODO: 工作模式控制后端"),
                                     QStringLiteral("目标配置(profile)尚未声明寄存器写入、程序约定或外部开关；当前不得声称目标已切换成功。"),
                                     content));
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createRamProgramPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_ram_program"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("RAM程序烧录"), content));

    QSplitter* const split = new QSplitter(content);
    QWidget* const left = new QWidget(split);
    QVBoxLayout* const leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(createImagePanel(), 1);

    QGroupBox* const actionPanel = panelBox(QStringLiteral("RAM 操作"), left);
    QGridLayout* const actionLayout = new QGridLayout(actionPanel);
    actionLayout->setSpacing(8);
    QPushButton* const programButton = createActionButton(UiAction::ProgramImage, NavigationPage::RamProgram, actionPanel, true);
    QPushButton* const verifyButton = createActionButton(UiAction::VerifyReadback, NavigationPage::RamProgram, actionPanel, false);
    QPushButton* const runButton = createActionButton(UiAction::RunProgram, NavigationPage::RamProgram, actionPanel, false);
    QPushButton* const stopButton = createActionButton(UiAction::StopProgram, NavigationPage::RamProgram, actionPanel, false);
    stopButton->setProperty("danger_button", true);
    for (QPushButton* const button : {programButton, verifyButton, runButton, stopButton}) {
        button->setMinimumHeight(42);
    }
    actionLayout->addWidget(programButton, 0, 0);
    actionLayout->addWidget(verifyButton, 0, 1);
    actionLayout->addWidget(runButton, 1, 0);
    actionLayout->addWidget(stopButton, 1, 1);
    leftLayout->addWidget(actionPanel);
    split->addWidget(left);

    QGroupBox* const right = panelBox(QStringLiteral("摘要"), split);
    QVBoxLayout* const rightLayout = new QVBoxLayout(right);
    QHBoxLayout* const summaryTabs = new QHBoxLayout();
    QPushButton* const verifySummary = createActionButton(UiAction::ShowVerifySummary, NavigationPage::RamProgram, right, false);
    QPushButton* const runSummary = createActionButton(UiAction::ShowRunSummary, NavigationPage::RamProgram, right, false);
    for (QPushButton* const button : {verifySummary, runSummary}) {
        button->setCheckable(true);
        button->setProperty("summaryTab", true);
        summaryTabs->addWidget(button);
    }
    verifySummary->setChecked(true);
    summaryTabs->addStretch(1);
    rightLayout->addLayout(summaryTabs);
    rightLayout->addWidget(mutedLabel(QStringLiteral("尚未执行回读校验。"), right));
    QProgressBar* const progress = new QProgressBar(right);
    progress->setRange(0, 100);
    progress->setValue(0);
    rightLayout->addWidget(progress);
    QPlainTextEdit* const readbackView = new QPlainTextEdit(right);
    readbackView->setReadOnly(true);
    readbackView->setMinimumHeight(120);
    readbackView->setPlainText(QStringLiteral("回读原文和运行摘要将在这里占位显示。"));
    rightLayout->addWidget(readbackView, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    return content;
}

QWidget* MainWindowShell::createWaveformPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_waveform"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(8);

    QHBoxLayout* const header = new QHBoxLayout();
    header->setSpacing(8);
    QWidget* const titleControls = new QWidget(content);
    titleControls->setObjectName(QStringLiteral("waveform_title_controls"));
    titleControls->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const titleControlsLayout = new QVBoxLayout(titleControls);
    titleControlsLayout->setContentsMargins(0, 0, 0, 0);
    titleControlsLayout->setSpacing(8);
    QHBoxLayout* const titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(pageTitle(QStringLiteral("波形分析仪"), titleControls));
    titleRow->addWidget(createActionButton(UiAction::ShowWaveformEmbedded, NavigationPage::Waveform, titleControls, false));
    titleRow->addWidget(createActionButton(UiAction::ShowWaveformDetached, NavigationPage::Waveform, titleControls, false));
    QLabel* const statusLabel = mutedLabel(QStringLiteral("等待导入波形文件。"), titleControls);
    statusLabel->setWordWrap(false);
    statusLabel->setMinimumWidth(120);
    titleRow->addWidget(statusLabel);
    titleRow->addStretch(1);
    titleControlsLayout->addLayout(titleRow);
    header->addWidget(titleControls, 0, Qt::AlignTop);

    QWidget* const inputPanel = new QWidget(content);
    inputPanel->setObjectName(QStringLiteral("waveform_inputs_header_panel"));
    QGridLayout* const inputGrid = new QGridLayout(inputPanel);
    inputGrid->setContentsMargins(0, 0, 0, 0);
    inputGrid->setHorizontalSpacing(6);
    inputGrid->setVerticalSpacing(4);
    QLabel* const label = new QLabel(QStringLiteral("波形文件"), inputPanel);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QLineEdit* const edit = new QLineEdit(inputPanel);
    edit->setPlaceholderText(QStringLiteral("VCD/采集文件路径；默认保存到当前任务 logic_analyzier_file/capture.vcd"));
    QPushButton* const browse = createActionButton(UiAction::BrowseWaveform, NavigationPage::Waveform, inputPanel, false);
    QPushButton* const importButton = createActionButton(UiAction::ImportWaveform, NavigationPage::Waveform, inputPanel, true);
    QPushButton* const clearButton = createActionButton(UiAction::ClearWaveform, NavigationPage::Waveform, inputPanel, false);
    inputGrid->addWidget(label, 0, 0);
    inputGrid->addWidget(edit, 0, 1);
    inputGrid->addWidget(browse, 0, 2);
    inputGrid->addWidget(importButton, 0, 3);
    inputGrid->addWidget(clearButton, 0, 4);
    inputGrid->setColumnStretch(1, 1);
    header->addWidget(inputPanel, 1);
    layout->addLayout(header);

    QWidget* const analyzerPanel = new QWidget(content);
    analyzerPanel->setObjectName(QStringLiteral("waveform_analyzer_panel"));
    analyzerPanel->setAttribute(Qt::WA_StyledBackground, true);
    QGridLayout* const analyzerLayout = new QGridLayout(analyzerPanel);
    analyzerLayout->setContentsMargins(1, 1, 1, 1);
    analyzerLayout->setSpacing(0);
    QWidget* const embedHost = new QWidget(analyzerPanel);
    embedHost->setObjectName(QStringLiteral("waveform_embed_host"));
    embedHost->setMinimumHeight(220);
    embedHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* const embedLayout = new QVBoxLayout(embedHost);
    embedLayout->setContentsMargins(0, 0, 0, 0);
    embedLayout->setSpacing(0);
    embedLayout->addWidget(mutedLabel(QStringLiteral("未加载采集文件"), embedHost), 0, Qt::AlignCenter);
    analyzerLayout->addWidget(embedHost, 0, 0);
    layout->addWidget(analyzerPanel, 8);
    return content;
}

QWidget* MainWindowShell::createProtocolPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_protocol"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("协议解析"), content));

    QGroupBox* const inputPanel = panelBox(QStringLiteral("波形文件协议解析"), content);
    QFormLayout* const form = new QFormLayout(inputPanel);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    QLineEdit* const vcdEdit = new QLineEdit(inputPanel);
    vcdEdit->setPlaceholderText(QStringLiteral("VCD/采集文件路径；会同步到当前任务采集输入"));
    form->addRow(QStringLiteral("波形文件"),
                 createPathInputRow(inputPanel,
                                    vcdEdit,
                                    createActionButton(UiAction::BrowseProtocolWaveform, NavigationPage::Protocol, inputPanel, false)));
    QLineEdit* const outputEdit = new QLineEdit(inputPanel);
    outputEdit->setPlaceholderText(QStringLiteral("空表示写入当前任务报告目录下的 bus_analysis.json"));
    form->addRow(QStringLiteral("输出文件"),
                 createPathInputRow(inputPanel,
                                    outputEdit,
                                    createActionButton(UiAction::BrowseProtocolOutput, NavigationPage::Protocol, inputPanel, false)));
    QWidget* const buttonRow = new QWidget(inputPanel);
    QHBoxLayout* const buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(createActionButton(UiAction::AnalyzeProtocol, NavigationPage::Protocol, inputPanel, true));
    buttonLayout->addStretch(1);
    form->addRow(QString(), buttonRow);
    layout->addWidget(inputPanel);

    layout->addWidget(createTodoCard(QStringLiteral("TODO: expected / rule set 判定"),
                                     QStringLiteral("当前仅保留协议解析 UI 占位；缺少期望结果或规则集合时，不输出通过结论。"),
                                     content));

    QGroupBox* const resultPanel = panelBox(QStringLiteral("解析结果"), content);
    QVBoxLayout* const resultLayout = new QVBoxLayout(resultPanel);
    QPlainTextEdit* const resultEdit = new QPlainTextEdit(resultPanel);
    resultEdit->setReadOnly(true);
    resultEdit->setPlaceholderText(QStringLiteral("解析日志、事务数量、输出文件和缺证说明会显示在这里。"));
    resultLayout->addWidget(resultEdit);
    layout->addWidget(resultPanel, 1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createStatsPage()
{
    QWidget* const content = new QWidget(this);
    content->setObjectName(QStringLiteral("page_stats"));
    QVBoxLayout* const layout = new QVBoxLayout(content);
    layout->addWidget(pageTitle(QStringLiteral("性能统计"), content));
    layout->addWidget(createTodoCard(QStringLiteral("TODO: 板上寄存器读取"),
                                     QStringLiteral("性能统计数据将从板子上的 reg/寄存器读取；当前未接入时只显示缺少依据，不显示假数据。"),
                                     content));
    QGridLayout* const grid = new QGridLayout();
    grid->addWidget(createMetricCard(QStringLiteral("Inst"), QStringLiteral("未接入"), QStringLiteral("等待板上寄存器 map"), content), 0, 0);
    grid->addWidget(createMetricCard(QStringLiteral("Cycle"), QStringLiteral("未接入"), QStringLiteral("等待板上寄存器 map"), content), 0, 1);
    grid->addWidget(createMetricCard(QStringLiteral("IPC"), QStringLiteral("未接入"), QStringLiteral("Inst/Cycle 均来自真实 reg 后才计算"), content), 0, 2);
    grid->addWidget(createMetricCard(QStringLiteral("Lockstep Compare"), QStringLiteral("未接入"), QStringLiteral("等待比较计数寄存器定义"), content), 1, 0);
    layout->addLayout(grid);
    layout->addStretch(1);
    return scrollPage(content);
}

QWidget* MainWindowShell::createDiagnosticsPanel(QWidget* const parent)
{
    QWidget* const panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("diagnostics_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(createLogPanel(), 1);
    panel->setMinimumHeight(kDiagnosticsMinHeight);
    panel->setMaximumHeight(kDiagnosticsMaxHeight);
    return panel;
}

QWidget* MainWindowShell::createLogPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("Log"), this);
    group->setObjectName(QStringLiteral("raw_log"));
    QVBoxLayout* const layout = new QVBoxLayout(group);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QWidget* const contentWidget = new QWidget(group);
    contentWidget->setObjectName(QStringLiteral("log_content_widget"));
    contentWidget->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    logTabs_ = new QTabWidget(contentWidget);
    logTabs_->setObjectName(QStringLiteral("diagnostics_output_tabs"));
    logTabs_->setDocumentMode(true);
    QWidget* const corner = new QWidget(logTabs_);
    QHBoxLayout* const cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(4);
    QPushButton* const clearButton = new QPushButton(QStringLiteral("清空窗口"), corner);
    clearButton->setObjectName(QStringLiteral("log_clear_button"));
    logDetachButton_ = new QToolButton(corner);
    logDetachButton_->setObjectName(QStringLiteral("log_detach_button"));
    logDetachButton_->setToolTip(QStringLiteral("弹出独立窗口"));
    logDetachButton_->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    logDetachButton_->setIconSize(QSize(18, 18));
    logDetachButton_->setAutoRaise(true);
    logDetachButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    logDetachButton_->setCursor(Qt::PointingHandCursor);
    logDetachButton_->setFixedSize(26, 22);
    cornerLayout->addWidget(clearButton);
    cornerLayout->addWidget(logDetachButton_);
    logTabs_->setCornerWidget(corner, Qt::TopRightCorner);

    QWidget* const logPage = new QWidget(logTabs_);
    logPage->setObjectName(QStringLiteral("diagnostics_log_page"));
    logPage->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 6, 0, 0);
    logLayout->setSpacing(0);
    logEdit_ = new QPlainTextEdit(logPage);
    logEdit_->setObjectName(QStringLiteral("workbench_log_edit"));
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(4000);
    logEdit_->setMinimumHeight(56);
    logEdit_->setPlaceholderText(QStringLiteral("关键流程日志和失败原文显示在这里。"));
    logLayout->addWidget(logEdit_, 1);
    logTabs_->addTab(logPage, QStringLiteral("Log"));
    logTabs_->addTab(createSerialMonitorPanel(), QStringLiteral("串口监控"));
    contentLayout->addWidget(logTabs_, 1);
    layout->addWidget(contentWidget, 1);

    connect(clearButton, &QPushButton::clicked, this, &MainWindowShell::clearVisibleLog);
    connect(logDetachButton_, &QToolButton::clicked, this, &MainWindowShell::toggleLogDetached);
    return group;
}

QWidget* MainWindowShell::createSerialConfigPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("串口配置"), this);
    group->setObjectName(QStringLiteral("serial_config_panel"));
    QFormLayout* const layout = new QFormLayout(group);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QComboBox* const portCombo = new QComboBox(group);
    portCombo->addItem(QStringLiteral("COM 占位"));
    QComboBox* const baudCombo = new QComboBox(group);
    baudCombo->addItems({QStringLiteral("9600"), QStringLiteral("115200"), QStringLiteral("921600")});
    baudCombo->setCurrentText(QStringLiteral("115200"));
    QComboBox* const displayModeCombo = new QComboBox(group);
    displayModeCombo->addItems({QStringLiteral("文本"), QStringLiteral("HEX")});
    QPushButton* const refreshButton = createActionButton(UiAction::RefreshSerialPorts, NavigationPage::Connection, group, false);
    QPushButton* const openButton = createActionButton(UiAction::ToggleSerialMonitor, NavigationPage::Connection, group, false);
    QPushButton* const clearButton = createActionButton(UiAction::ClearSerialOutput, NavigationPage::Connection, group, false);

    QWidget* const portRow = new QWidget(group);
    QHBoxLayout* const portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->setSpacing(6);
    portLayout->addWidget(portCombo, 1);
    portLayout->addWidget(refreshButton);

    QWidget* const actionRow = new QWidget(group);
    QHBoxLayout* const actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(6);
    actionLayout->addWidget(openButton);
    actionLayout->addWidget(clearButton);
    actionLayout->addStretch(1);

    layout->addRow(QStringLiteral("串口"), portRow);
    layout->addRow(QStringLiteral("波特率"), baudCombo);
    layout->addRow(QStringLiteral("显示"), displayModeCombo);
    layout->addRow(QStringLiteral("操作"), actionRow);
    layout->addRow(QStringLiteral("状态"), mutedLabel(QStringLiteral("串口尚未打开。当前为 UI 占位，不访问真实串口。"), group));
    return group;
}

QWidget* MainWindowShell::createSerialMonitorPanel()
{
    QWidget* const panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("serial_monitor_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* const layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);
    serialOutputEdit_ = new QPlainTextEdit(panel);
    serialOutputEdit_->setObjectName(QStringLiteral("serial_output_edit"));
    serialOutputEdit_->setReadOnly(true);
    serialOutputEdit_->setMinimumHeight(56);
    serialOutputEdit_->setMaximumBlockCount(4000);
    serialOutputEdit_->setPlaceholderText(QStringLiteral("串口打印信息会显示在这里。当前为只读占位，无输入框。"));
    layout->addWidget(serialOutputEdit_, 1);
    return panel;
}

QWidget* MainWindowShell::createConnectionPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("高级连接设置"), this);
    group->setObjectName(QStringLiteral("connection"));
    group->setCheckable(true);
    group->setChecked(false);
    QFormLayout* const layout = new QFormLayout(group);
    QLineEdit* const profileName = new QLineEdit(group);
    profileName->setText(QStringLiteral("ui-placeholder-profile"));
    layout->addRow(QStringLiteral("目标配置(profile)"), profileName);
    layout->addRow(QStringLiteral("片上调试器可执行文件"),
                   createPathInputRow(group,
                                      new QLineEdit(group),
                                      createActionButton(UiAction::BrowseTargetDebugTool, NavigationPage::Connection, group, false)));
    layout->addRow(QStringLiteral("interface.cfg"),
                   createPathInputRow(group,
                                      new QLineEdit(group),
                                      createActionButton(UiAction::BrowseInterfaceConfig, NavigationPage::Connection, group, false)));
    layout->addRow(QStringLiteral("target.cfg"),
                   createPathInputRow(group,
                                      new QLineEdit(group),
                                      createActionButton(UiAction::BrowseTargetConfig, NavigationPage::Connection, group, false)));
    QLineEdit* const hostEdit = new QLineEdit(group);
    hostEdit->setText(QStringLiteral("127.0.0.1"));
    layout->addRow(QStringLiteral("Tcl Host"), hostEdit);
    QWidget* const portsRow = new QWidget(group);
    QHBoxLayout* const portsLayout = new QHBoxLayout(portsRow);
    portsLayout->setContentsMargins(0, 0, 0, 0);
    QSpinBox* const tclPort = new QSpinBox(group);
    tclPort->setRange(1, 65535);
    tclPort->setValue(6666);
    QSpinBox* const gdbPort = new QSpinBox(group);
    gdbPort->setRange(1, 65535);
    gdbPort->setValue(3333);
    portsLayout->addWidget(new QLabel(QStringLiteral("Tcl"), group));
    portsLayout->addWidget(tclPort);
    portsLayout->addWidget(new QLabel(QStringLiteral("GDB"), group));
    portsLayout->addWidget(gdbPort);
    layout->addRow(QStringLiteral("端口"), portsRow);
    QSpinBox* const khz = new QSpinBox(group);
    khz->setRange(1, 50000);
    khz->setValue(10000);
    khz->setSuffix(QStringLiteral(" kHz"));
    layout->addRow(QStringLiteral("JTAG 速率"), khz);
    layout->addRow(QStringLiteral("RAM Base"), new QLineEdit(QStringLiteral("0x80000000"), group));
    QComboBox* const reset = new QComboBox(group);
    reset->addItems({QStringLiteral("halt"), QStringLiteral("reset halt"), QStringLiteral("reset init")});
    layout->addRow(QStringLiteral("复位策略"), reset);
    connect(group, &QGroupBox::toggled, group, [group](const bool checked) {
        setLayoutItemsVisible(group->layout(), checked);
    });
    setLayoutItemsVisible(group->layout(), group->isChecked());
    return group;
}

QWidget* MainWindowShell::createImagePanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("程序镜像"), this);
    group->setObjectName(QStringLiteral("image_load"));
    QFormLayout* const layout = new QFormLayout(group);
    layout->addRow(QStringLiteral("文件"),
                   createPathInputRow(group,
                                      new QLineEdit(group),
                                      createActionButton(UiAction::BrowseProgramImage, NavigationPage::RamProgram, group, false)));
    QComboBox* const formatCombo = new QComboBox(group);
    formatCombo->addItems({QStringLiteral("Auto"), QStringLiteral("ELF"), QStringLiteral("BIN"), QStringLiteral("IHEX"), QStringLiteral("S19")});
    layout->addRow(QStringLiteral("格式"), formatCombo);
    layout->addRow(QStringLiteral("BIN 地址"), new QLineEdit(QStringLiteral("0x80000000"), group));
    layout->addRow(QStringLiteral("Offset"), new QLineEdit(QStringLiteral("0x0"), group));
    layout->addRow(QStringLiteral("运行地址覆盖"), new QLineEdit(group));
    return group;
}

QWidget* MainWindowShell::createControlPanel()
{
    QGroupBox* const group = panelBox(QStringLiteral("研发调试控制"), this);
    group->setObjectName(QStringLiteral("run_control"));
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QHBoxLayout* const profileRow = new QHBoxLayout();
    profileRow->addWidget(createActionButton(UiAction::LoadProfile, NavigationPage::Connection, group, false));
    profileRow->addWidget(createActionButton(UiAction::SaveProfile, NavigationPage::Connection, group, false));
    layout->addLayout(profileRow);
    QHBoxLayout* const debugRow = new QHBoxLayout();
    debugRow->addWidget(createActionButton(UiAction::StartDebugService, NavigationPage::Connection, group, false));
    debugRow->addWidget(createActionButton(UiAction::StopDebugService, NavigationPage::Connection, group, false));
    layout->addLayout(debugRow);
    QLabel* const status = new QLabel(QStringLiteral("研发调试就绪"), group);
    status->setObjectName(QStringLiteral("workflow_status_label"));
    layout->addWidget(status);
    return group;
}

QWidget* MainWindowShell::createTodoCard(const QString& title, const QString& body, QWidget* const parent)
{
    QGroupBox* const group = panelBox(title, parent);
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QLabel* const label = mutedLabel(body, group);
    label->setObjectName(QStringLiteral("todo_card_label"));
    layout->addWidget(label);
    return group;
}

QWidget* MainWindowShell::createMetricCard(
    const QString& title,
    const QString& value,
    const QString& detail,
    QWidget* const parent)
{
    QGroupBox* const group = panelBox(title, parent);
    QVBoxLayout* const layout = new QVBoxLayout(group);
    QLabel* const valueLabel = new QLabel(value, group);
    valueLabel->setProperty("metricValue", true);
    layout->addWidget(valueLabel);
    layout->addWidget(mutedLabel(detail, group));
    return group;
}

QPushButton* MainWindowShell::createNavButton(const QString& pageId, const QString& title, QWidget* const parent)
{
    QPushButton* const button = new QPushButton(title, parent);
    button->setObjectName(QStringLiteral("nav_%1").arg(pageId));
    button->setProperty("pageId", pageId);
    button->setCheckable(true);
    button->setProperty("navButton", true);
    navButtons_.insert(pageId, button);
    connect(button, &QPushButton::clicked, this, &MainWindowShell::switchWorkbenchPage);
    return button;
}

QPushButton* MainWindowShell::createActionButton(
    const UiAction action,
    const NavigationPage page,
    QWidget* const parent,
    const bool primary)
{
    QPushButton* const button = new QPushButton(toDisplayText(action), parent);
    button->setProperty("uiAction", static_cast<int>(action));
    button->setProperty("uiPage", static_cast<int>(page));
    if (primary) {
        button->setProperty("primary_button", true);
        button->setProperty("primaryButton", true);
    }
    connect(button, &QPushButton::clicked, this, [this, action, page, button]() {
        emitAction(action, page, button->objectName());
    });
    return button;
}

QToolButton* MainWindowShell::createToolButton(QWidget* const parent, const QIcon& icon, const QString& tooltip)
{
    QToolButton* const button = new QToolButton(parent);
    button->setIcon(icon);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QWidget* MainWindowShell::createPathInputRow(QWidget* const parent, QLineEdit* const edit, QAbstractButton* const button)
{
    QWidget* const row = new QWidget(parent);
    QHBoxLayout* const layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    return row;
}

void MainWindowShell::addWorkbenchPage(const QString& pageId, const NavigationPage page, QWidget* const pageWidget)
{
    pageIds_.insert(pageId, page);
    pageWidget->setProperty("workbenchPageId", pageId);
    pageWidget->setProperty("workbenchPageContent", true);
    pageWidget->setAttribute(Qt::WA_StyledBackground, true);
    pageStack_->addWidget(pageWidget);
}

void MainWindowShell::setActivePage(const QString& pageId)
{
    for (int index = 0; index < pageStack_->count(); ++index) {
        if (pageStack_->widget(index)->property("workbenchPageId").toString() == pageId) {
            pageStack_->setCurrentIndex(index);
            break;
        }
    }
    for (auto it = navButtons_.begin(); it != navButtons_.end(); ++it) {
        it.value()->setChecked(it.key() == pageId);
    }
    emit pageChanged(pageForId(pageId));
}

void MainWindowShell::emitAction(const UiAction action, const NavigationPage page, const QString& objectName)
{
    UiActionRequest request;
    request.action = action;
    request.page = page;
    request.objectName = objectName;
    request.parameters.insert(QStringLiteral("pageId"), pageIdForPage(page));
    request.parameters.insert(QStringLiteral("actionText"), toDisplayText(action));
    emit actionRequested(request);
}

void MainWindowShell::appendFormattedLog(
    QPlainTextEdit* const view,
    const LogLevel level,
    const QString& source,
    const QString& message)
{
    if (view == nullptr) {
        return;
    }
    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  toDisplayText(level),
                                  source,
                                  message);
    view->appendPlainText(line);
    view->moveCursor(QTextCursor::End);
    if (detachedLogEdit_ != nullptr) {
        detachedLogEdit_->setPlainText(currentLogText());
        detachedLogEdit_->moveCursor(QTextCursor::End);
    }
}

QString MainWindowShell::currentLogText() const
{
    const QPlainTextEdit* const view = currentLogView();
    return (view == nullptr) ? QString() : view->toPlainText();
}

QPlainTextEdit* MainWindowShell::currentLogView() const
{
    if (logTabs_ == nullptr) {
        return nullptr;
    }
    return (logTabs_->currentIndex() == 1) ? serialOutputEdit_ : logEdit_;
}

NavigationPage MainWindowShell::pageForId(const QString& pageId)
{
    if (pageId == QStringLiteral("connection")) {
        return NavigationPage::Connection;
    }
    if (pageId == QStringLiteral("mode")) {
        return NavigationPage::Mode;
    }
    if (pageId == QStringLiteral("ram_program")) {
        return NavigationPage::RamProgram;
    }
    if (pageId == QStringLiteral("waveform")) {
        return NavigationPage::Waveform;
    }
    if (pageId == QStringLiteral("protocol")) {
        return NavigationPage::Protocol;
    }
    if (pageId == QStringLiteral("stats")) {
        return NavigationPage::Stats;
    }
    return NavigationPage::Project;
}

}  // namespace lockstep::ui
