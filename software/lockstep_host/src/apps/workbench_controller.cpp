/*****************************************************************************
*  @file      workbench_controller.cpp
*  @brief     UI与底层模块适配控制器实现
*  Details.   实现UI动作到工作区、资源、流程、目标控制、报告和错误日志模块的适配流程。
*
*  @version   1.0.0.1
*
*----------------------------------------------------------------------------*
*  Change History :
*  <Version> | <Description>
*----------------------------------------------------------------------------*
*   1.0.0.1   | Create file
*----------------------------------------------------------------------------*
*
*****************************************************************************/

#include "workbench_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

#include "ui_theme.h"

namespace lockstep::apps {
namespace {

constexpr quint64 kDebugMemorySizeBytes = 64ULL * 1024ULL * 1024ULL;
constexpr char kProgramWriteRecordName[] = "program_write_record.json";
constexpr char kReadbackVerifyRecordName[] = "readback_verify_record.json";
constexpr char kRunControlRecordName[] = "run_control_record.json";
constexpr char kHaltControlRecordName[] = "halt_control_record.json";
constexpr char kProgramOperationProgressName[] = "program_operation_progress.json";

QString currentTimeText()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString compactTimeText()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

bool sameCleanPath(const QString& lhs, const QString& rhs)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(lhs.trimmed())) ==
        QDir::cleanPath(QDir::fromNativeSeparators(rhs.trimmed()));
}

bool askYesNo(QWidget* const parent, const QString& title, const QString& message)
{
    return QMessageBox::question(parent, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
        QMessageBox::Yes;
}

bool confirmTaskDeletion(QWidget* const parent, const QString& taskLabel)
{
    QMessageBox dialog(parent);
    dialog.setWindowTitle(QStringLiteral("删除验证任务"));
    dialog.setIcon(QMessageBox::Warning);
    dialog.setText(QStringLiteral("确认删除验证任务“%1”？").arg(taskLabel));
    dialog.setInformativeText(QStringLiteral("任务目录中的输入、证据、日志和报告会全部删除，且无法撤销。"));

    QPushButton* const noButton = dialog.addButton(QStringLiteral("否"), QMessageBox::RejectRole);
    QPushButton* const yesButton = dialog.addButton(QStringLiteral("是"), QMessageBox::AcceptRole);
    noButton->setProperty("primary_button", true);
    noButton->setProperty("primaryButton", true);
    noButton->setDefault(true);
    noButton->setAutoDefault(true);
    yesButton->setAutoDefault(false);
    dialog.setDefaultButton(noButton);
    dialog.setEscapeButton(noButton);
    lockstep::ui::UiTheme::applyWorkbenchStyle(&dialog);

    dialog.exec();
    return dialog.clickedButton() == yesButton;
}

bool promptTaskMetadata(
    QWidget* const parent,
    const QString& title,
    const QString& defaultName,
    const QString& defaultDescription,
    QString* const taskName,
    QString* const description)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    lockstep::ui::UiTheme::applyWorkbenchStyle(&dialog);

    QVBoxLayout* const layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);

    QLabel* const titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName(QStringLiteral("page_title"));
    layout->addWidget(titleLabel);

    QFormLayout* const form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    QLineEdit* const nameEdit = new QLineEdit(&dialog);
    nameEdit->setText(defaultName);
    nameEdit->setMinimumWidth(380);
    QPlainTextEdit* const descriptionEdit = new QPlainTextEdit(&dialog);
    descriptionEdit->setPlainText(defaultDescription);
    descriptionEdit->setFixedHeight(86);
    form->addRow(QStringLiteral("任务名称"), nameEdit);
    form->addRow(QStringLiteral("描述"), descriptionEdit);
    layout->addLayout(form);

    QLabel* const errorLabel = new QLabel(&dialog);
    errorLabel->setObjectName(QStringLiteral("workspace_select_error"));
    errorLabel->setStyleSheet(QStringLiteral("color: #b42318; font-weight: 600;"));
    errorLabel->setVisible(false);
    layout->addWidget(errorLabel);

    QHBoxLayout* const buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton* const cancelButton = new QPushButton(QStringLiteral("取消"), &dialog);
    QPushButton* const okButton = new QPushButton(QStringLiteral("确定"), &dialog);
    okButton->setProperty("primary_button", true);
    okButton->setProperty("primaryButton", true);
    okButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton);
    layout->addLayout(buttons);

    QObject::connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, [&dialog, nameEdit, descriptionEdit, errorLabel, taskName, description]() {
        const QString normalizedName = nameEdit->text().trimmed();
        if (normalizedName.isEmpty()) {
            errorLabel->setText(QStringLiteral("任务名称不能为空。"));
            errorLabel->setVisible(true);
            return;
        }
        if (taskName != nullptr) {
            *taskName = normalizedName;
        }
        if (description != nullptr) {
            *description = descriptionEdit->toPlainText();
        }
        dialog.accept();
    });

    return dialog.exec() == QDialog::Accepted;
}

QString taskBasicInfoText(const workspace::TaskSummary& summary)
{
    return QStringLiteral("模式: %1\n状态: %2\n任务ID: %3\n更新时间: %4")
        .arg(workspace::toString(summary.mode),
             workspace::toString(summary.status),
             summary.taskId,
             summary.updatedAt);
}

QString toModeText(const ui::UiMode mode)
{
    return (mode == ui::UiMode::Test) ? QStringLiteral("test") : QStringLiteral("research");
}

QString connectionStateText(const target_control::ConnectionState state)
{
    QString text;
    switch (state) {
    case target_control::ConnectionState::Connected:
        text = QStringLiteral("connected");
        break;
    case target_control::ConnectionState::Failed:
        text = QStringLiteral("failed");
        break;
    case target_control::ConnectionState::NotConnected:
    default:
        text = QStringLiteral("not_connected");
        break;
    }
    return text;
}

QString precheckStateText(const target_control::PrecheckState state)
{
    QString text;
    switch (state) {
    case target_control::PrecheckState::Passed:
        text = QStringLiteral("passed");
        break;
    case target_control::PrecheckState::Failed:
        text = QStringLiteral("failed");
        break;
    case target_control::PrecheckState::Blocked:
        text = QStringLiteral("blocked");
        break;
    case target_control::PrecheckState::NotRun:
    default:
        text = QStringLiteral("not_run");
        break;
    }
    return text;
}

QString addressText(const quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

QJsonArray segmentsToJson(const QList<target_control::ImageSegment>& segments)
{
    QJsonArray array;
    for (const target_control::ImageSegment& segment : segments) {
        QJsonObject object;
        object.insert(QStringLiteral("address"), addressText(segment.address));
        object.insert(QStringLiteral("length"), QString::number(segment.data.size()));
        array.append(object);
    }
    return array;
}

QJsonObject imageToJson(const target_control::ProgramImageInfo& image)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), target_control::toString(image.type));
    object.insert(QStringLiteral("file_name"), image.fileName);
    object.insert(QStringLiteral("sha256"), image.sha256);
    object.insert(QStringLiteral("size_bytes"), QString::number(image.sizeBytes));
    object.insert(QStringLiteral("entry_address"), addressText(image.entryAddress));
    object.insert(QStringLiteral("segments"), segmentsToJson(image.segments));
    object.insert(QStringLiteral("error_message"), image.errorMessage);
    return object;
}

QJsonObject writeRecordToJson(const target_control::WriteRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("success"), record.success);
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("segments"), segmentsToJson(record.segments));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), currentTimeText());
    return object;
}

QJsonObject verifyRecordToJson(const target_control::ReadbackVerifyRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("state"), target_control::toString(record.state));
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("expected_length"), QString::number(record.expectedLength));
    object.insert(QStringLiteral("actual_length"), QString::number(record.actualLength));
    object.insert(QStringLiteral("diff_count"), QString::number(record.diffCount));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), currentTimeText());
    return object;
}

QJsonObject runRecordToJson(const target_control::RunControlRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("operation"), target_control::toString(record.operation));
    object.insert(QStringLiteral("state"), target_control::toString(record.state));
    object.insert(QStringLiteral("task_id"), record.taskId);
    object.insert(QStringLiteral("entry_address"), addressText(record.entryAddress));
    object.insert(QStringLiteral("raw_return"), record.rawReturn);
    object.insert(QStringLiteral("snapshot"), record.snapshot);
    object.insert(QStringLiteral("error_message"), record.errorMessage);
    object.insert(QStringLiteral("created_at"), currentTimeText());
    return object;
}

QJsonObject progressToJson(const target_control::OperationProgress& progress)
{
    QJsonObject object;
    object.insert(QStringLiteral("operation"), target_control::toString(progress.operation));
    object.insert(QStringLiteral("stage"), target_control::toString(progress.stage));
    object.insert(QStringLiteral("percent"), progress.percent);
    object.insert(QStringLiteral("message"), progress.message);
    object.insert(QStringLiteral("can_cancel"), progress.canCancel);
    object.insert(QStringLiteral("created_at"), currentTimeText());
    return object;
}

QString progressTitle(
    const QString& title,
    const target_control::OperationProgress& progress,
    const QString& detail)
{
    QString text = QStringLiteral("%1\n阶段: %2\n%3")
        .arg(title, progress.message, detail);
    return text;
}

bool readEvidenceObject(
    const QString& evidencePath,
    const QString& fileName,
    QJsonObject* const object)
{
    if (object == nullptr) {
        return false;
    }

    QFile file(QDir(evidencePath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    *object = document.object();
    return true;
}

bool parseBoolText(const QJsonValue& value)
{
    if (value.isBool()) {
        return value.toBool();
    }
    const QString text = value.toString().trimmed().toLower();
    return text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("passed");
}

quint64 parseU64Text(const QJsonValue& value)
{
    bool ok = false;
    const quint64 parsed = value.toString().toULongLong(&ok);
    return ok ? parsed : 0U;
}

target_control::VerifyState parseVerifyStateText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("passed")) {
        return target_control::VerifyState::Passed;
    }
    if (normalized == QStringLiteral("mismatch")) {
        return target_control::VerifyState::Mismatch;
    }
    if (normalized == QStringLiteral("failed")) {
        return target_control::VerifyState::Failed;
    }
    return target_control::VerifyState::NotRun;
}

target_control::RunState parseRunStateText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("running")) {
        return target_control::RunState::Running;
    }
    if (normalized == QStringLiteral("halted")) {
        return target_control::RunState::Halted;
    }
    if (normalized == QStringLiteral("failed")) {
        return target_control::RunState::Failed;
    }
    if (normalized == QStringLiteral("ready")) {
        return target_control::RunState::Ready;
    }
    return target_control::RunState::NotAllowed;
}

target_control::WriteRecord writeRecordFromJson(const QJsonObject& object)
{
    target_control::WriteRecord record;
    record.success = parseBoolText(object.value(QStringLiteral("success")));
    record.taskId = object.value(QStringLiteral("task_id")).toString();
    record.rawReturn = object.value(QStringLiteral("raw_return")).toString();
    record.errorMessage = object.value(QStringLiteral("error_message")).toString();
    return record;
}

target_control::ReadbackVerifyRecord verifyRecordFromJson(const QJsonObject& object)
{
    target_control::ReadbackVerifyRecord record;
    record.state = parseVerifyStateText(object.value(QStringLiteral("state")).toString());
    record.taskId = object.value(QStringLiteral("task_id")).toString();
    record.expectedLength = parseU64Text(object.value(QStringLiteral("expected_length")));
    record.actualLength = parseU64Text(object.value(QStringLiteral("actual_length")));
    record.diffCount = parseU64Text(object.value(QStringLiteral("diff_count")));
    record.rawReturn = object.value(QStringLiteral("raw_return")).toString();
    record.errorMessage = object.value(QStringLiteral("error_message")).toString();
    return record;
}

target_control::RunControlRecord runRecordFromJson(
    const QJsonObject& object,
    const target_control::ProgramOperation operation)
{
    target_control::RunControlRecord record;
    record.operation = operation;
    record.state = parseRunStateText(object.value(QStringLiteral("state")).toString());
    record.taskId = object.value(QStringLiteral("task_id")).toString();
    record.entryAddress = 0U;
    record.rawReturn = object.value(QStringLiteral("raw_return")).toString();
    record.snapshot = object.value(QStringLiteral("snapshot")).toString();
    record.errorMessage = object.value(QStringLiteral("error_message")).toString();
    return record;
}

}  // namespace

WorkbenchController::WorkbenchController(
    ui::MainWindowShell* const window,
    const ui::UiMode mode,
    QObject* const parent)
    : QObject(parent),
      window_(window),
      mode_(mode),
      workspace_(),
      resources_(),
      workflow_(),
      connectionService_(),
      programController_(),
      reportGenerator_(),
      errorRegistry_(),
      debugAccess_(kDebugMemorySizeBytes),
      workspaceRootPath_(),
      selectedTaskId_(),
      workspaceReady_(false),
      resourcePackReady_(false),
      hasTask_(false),
      hasImage_(false),
      hasConnection_(false),
      hasPrecheck_(false),
      hasWriteRecord_(false),
      hasVerifyRecord_(false),
      hasRunRecord_(false),
      hasHaltRecord_(false),
      currentTask_(),
      flowState_(),
      debugProfile_(),
      connectionRecord_(),
      precheckRecord_(),
      imageInfo_(),
      writeRecord_(),
      verifyRecord_(),
      runRecord_(),
      haltRecord_()
{
    debugProfile_.profileId = QStringLiteral("ui_in_memory_profile");
    debugProfile_.profileName = QStringLiteral("UI内存调试后端");
    debugProfile_.ramBaseAddress = 0x80000000ULL;
    debugProfile_.defaultRunAddress = debugProfile_.ramBaseAddress;
    debugProfile_.maxWritableAddress = debugProfile_.ramBaseAddress + kDebugMemorySizeBytes;
    debugProfile_.resetStrategy = QStringLiteral("halt");

    if (window_ != nullptr) {
        connect(window_, &ui::MainWindowShell::actionRequested, this, [this](const ui::UiActionRequest& request) {
            handleAction(request);
        });
    }
}

bool WorkbenchController::initialize(const QString& workspaceRootPath)
{
    const QString trimmedRoot = workspaceRootPath.trimmed();
    if (trimmedRoot.isEmpty()) {
        logError(QStringLiteral("Workspace"), QStringLiteral("工作区路径为空。"));
        return false;
    }

    const QString normalizedRoot = QDir::cleanPath(QDir::fromNativeSeparators(trimmedRoot));
    workspaceRootPath_ = normalizedRoot;
    workspaceReady_ = false;
    if (!ensureWorkspace()) {
        return false;
    }
    loadResourcePackIfAvailable();
    updateProjectView();
    updateTopStatus();
    if (window_ != nullptr) {
        window_->setConnectionProfileDetails(
            debugProfile_.profileName,
            QStringLiteral("127.0.0.1"),
            6666,
            3333,
            14,
            addressText(debugProfile_.ramBaseAddress),
            debugProfile_.resetStrategy,
            QStringLiteral("调试器: 未连接"));
        window_->setRamSummary(QStringLiteral("尚未选择程序镜像。"), 0, 0);
    }
    return true;
}

void WorkbenchController::handleAction(const ui::UiActionRequest& request)
{
    switch (request.action) {
    case ui::UiAction::NewTask:
        createTask();
        break;
    case ui::UiAction::SaveTask:
        saveCurrentTask();
        break;
    case ui::UiAction::LoadTaskToWorkbench:
        loadTaskToWorkbench(request.parameters.value(QStringLiteral("taskId")).toString());
        break;
    case ui::UiAction::DeleteTask:
        deleteTask(request.parameters.value(QStringLiteral("taskId")).toString());
        break;
    case ui::UiAction::EditTask:
        startEditTask(request.parameters.value(QStringLiteral("taskId")).toString());
        break;
    case ui::UiAction::SaveTaskEdit:
        saveTaskMetadataEdit(
            request.parameters.value(QStringLiteral("taskId")).toString(),
            request.parameters.value(QStringLiteral("taskName")).toString(),
            request.parameters.value(QStringLiteral("description")).toString());
        break;
    case ui::UiAction::CancelTaskEdit:
        cancelTaskMetadataEdit(request.parameters.value(QStringLiteral("taskId")).toString());
        break;
    case ui::UiAction::LoadProfile:
        loadResourcePackIfAvailable();
        break;
    case ui::UiAction::SaveProfile:
        logWarning(QStringLiteral("UI"), QStringLiteral("底层固定资源管理不保存 UI 录入的本机 profile 路径，请使用 resources/manifest.json 固化资源。"));
        break;
    case ui::UiAction::StartDebugService:
        startDebugService();
        break;
    case ui::UiAction::StopDebugService:
        stopDebugService();
        break;
    case ui::UiAction::BrowseProgramImage:
        browseProgramImage();
        break;
    case ui::UiAction::ProgramImage:
        programImage();
        break;
    case ui::UiAction::VerifyReadback:
        verifyReadback();
        break;
    case ui::UiAction::RunProgram:
        runProgram();
        break;
    case ui::UiAction::StopProgram:
        stopProgram();
        break;
    case ui::UiAction::ShowVerifySummary:
        showVerifySummary();
        break;
    case ui::UiAction::ShowRunSummary:
        showRunSummary();
        break;
    case ui::UiAction::GenerateReport:
        generateReport();
        break;
    case ui::UiAction::BrowseWaveform:
    case ui::UiAction::ImportWaveform:
    case ui::UiAction::ClearWaveform:
    case ui::UiAction::ShowWaveformEmbedded:
    case ui::UiAction::ShowWaveformDetached:
    case ui::UiAction::BrowseProtocolWaveform:
    case ui::UiAction::BrowseProtocolOutput:
    case ui::UiAction::AnalyzeProtocol:
        logWarning(QStringLiteral("UI"), QStringLiteral("波形/协议模块本轮仅保留接口占位，不作为 pass 阻塞条件。"));
        break;
    default:
        logInfo(QStringLiteral("UI"), QStringLiteral("动作已接收: %1").arg(ui::toDisplayText(request.action)));
        break;
    }
}

bool WorkbenchController::ensureWorkspace()
{
    if (workspaceReady_) {
        return true;
    }

    QString error;
    if (!workspace_.openWorkspace(workspaceRootPath_, &error) ||
        !workspace_.switchMode(workspaceMode(), &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("打开工作区失败: %1").arg(error));
        return false;
    }

    workspaceReady_ = true;
    logInfo(QStringLiteral("Workspace"), QStringLiteral("工作区已打开: %1").arg(workspaceRootPath_));
    return true;
}

bool WorkbenchController::ensureTask()
{
    if (hasTask_) {
        return true;
    }

    return createTask();
}

bool WorkbenchController::createTask()
{
    if (!ensureWorkspace()) {
        return false;
    }

    if (hasTask_ && window_ != nullptr &&
        !askYesNo(window_, QStringLiteral("新建验证任务"), QStringLiteral("新建任务会切换当前工作台，是否继续？"))) {
        return false;
    }

    QString taskName = QStringLiteral("验证任务_%1").arg(compactTimeText());
    QString description;
    if (window_ != nullptr &&
        !promptTaskMetadata(window_, QStringLiteral("新建验证任务"), taskName, description, &taskName, &description)) {
        return false;
    }

    workspace::TaskCreateOptions options;
    options.taskName = taskName;
    options.description = description;
    options.inputs.resourceSnapshot = resourceSnapshotJson();
    options.stageStatus = QStringLiteral("created");

    workspace::TaskContext context;
    QString error;
    if (!workspace_.createTask(workspaceMode(), options, &context, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("创建任务失败: %1").arg(error));
        return false;
    }

    currentTask_ = context;
    flowState_ = workflow_.startFlow(currentTask_.summary.taskId, flowMode());
    selectedTaskId_ = currentTask_.summary.taskId;
    hasTask_ = true;
    resetExecutionState();
    if (window_ != nullptr) {
        window_->setProgramImagePath(QString());
        window_->setTaskDetailEditing(false);
    }
    logInfo(QStringLiteral("Workspace"), QStringLiteral("任务已创建: %1").arg(currentTask_.summary.taskId));
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
    return true;
}

bool WorkbenchController::saveCurrentTask()
{
    const QString pendingProgramPath = (window_ == nullptr) ? QString() : window_->programImagePath();
    if (!ensureTask()) {
        return false;
    }
    if (window_ != nullptr && !pendingProgramPath.isEmpty() && window_->programImagePath().isEmpty()) {
        window_->setProgramImagePath(pendingProgramPath);
    }

    workspace::TaskInputSet inputs = currentTask_.inputs;
    inputs.resourceSnapshot = resourceSnapshotJson();
    if (!saveProgramInputForCurrentTask(&inputs)) {
        return false;
    }

    workspace::TaskContext updated;
    QString error;
    if (!workspace_.saveTaskInputs(workspaceMode(), currentTask_.summary.taskId, inputs, &updated, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("保存验证任务失败: %1").arg(error));
        return false;
    }

    currentTask_ = updated;
    selectedTaskId_ = currentTask_.summary.taskId;
    logInfo(QStringLiteral("Workspace"), QStringLiteral("验证任务已保存: %1").arg(currentTask_.summary.taskName));
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
    return true;
}

void WorkbenchController::loadTaskToWorkbench(const QString& taskId)
{
    const QString normalizedTaskId = taskId.trimmed();
    if (normalizedTaskId.isEmpty()) {
        logWarning(QStringLiteral("Workspace"), QStringLiteral("请选择一个验证任务后再加载到工作台。"));
        return;
    }

    workspace::TaskContext context;
    QString error;
    if (!workspace_.loadTask(workspaceMode(), normalizedTaskId, &context, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("加载验证任务失败: %1").arg(error));
        return;
    }

    currentTask_ = context;
    flowState_ = workflow_.startFlow(currentTask_.summary.taskId, flowMode());
    selectedTaskId_ = currentTask_.summary.taskId;
    hasTask_ = true;
    resetExecutionState();

    const QString programRelativePath = currentTask_.inputs.programFile.relativePath.trimmed();
    if (window_ != nullptr) {
        if (programRelativePath.isEmpty()) {
            window_->setProgramImagePath(QString());
        } else {
            const QString taskProgramPath = QDir(currentTask_.paths.taskRootPath).filePath(programRelativePath);
            window_->setProgramImagePath(taskProgramPath);
            imageInfo_ = programController_.detectImage(taskProgramPath, debugProfile_);
            hasImage_ = (imageInfo_.type != target_control::ImageType::Unknown) && imageInfo_.errorMessage.isEmpty();
            if (!hasImage_) {
                logWarning(QStringLiteral("Program"), QStringLiteral("已加载任务，但程序镜像无法识别: %1").arg(imageInfo_.errorMessage));
            }
        }
        window_->setTaskDetailEditing(false);
    }
    restoreExecutionEvidenceForCurrentTask();

    logInfo(QStringLiteral("Workspace"), QStringLiteral("验证任务已加载到工作台: %1").arg(currentTask_.summary.taskName));
    setRamSummaryFromCurrentState(QStringLiteral("任务证据已加载"));
    setRunSummaryFromCurrentState(QStringLiteral("运行摘要"), hasRunRecord_ ? 100 : 0, hasHaltRecord_ ? 100 : 0);
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
}

void WorkbenchController::startEditTask(const QString& taskId)
{
    if (taskId.trimmed().isEmpty() || window_ == nullptr) {
        return;
    }
    selectedTaskId_ = taskId.trimmed();
    window_->setTaskDetailEditing(true);
}

void WorkbenchController::saveTaskMetadataEdit(
    const QString& taskId,
    const QString& taskName,
    const QString& description)
{
    const QString normalizedTaskId = taskId.trimmed();
    if (normalizedTaskId.isEmpty()) {
        logWarning(QStringLiteral("Workspace"), QStringLiteral("请选择任务后再保存修改。"));
        return;
    }

    QString error;
    if (!workspace_.updateTaskMetadata(workspaceMode(), normalizedTaskId, taskName, description, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("保存任务元信息失败: %1").arg(error));
        return;
    }

    if (hasTask_ && currentTask_.summary.taskId == normalizedTaskId) {
        currentTask_.summary.taskName = taskName.trimmed();
        currentTask_.summary.description = description;
    }
    selectedTaskId_ = normalizedTaskId;
    if (window_ != nullptr) {
        window_->setTaskDetailEditing(false);
    }
    logInfo(QStringLiteral("Workspace"), QStringLiteral("任务元信息已更新: %1").arg(taskName.trimmed()));
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
}

void WorkbenchController::cancelTaskMetadataEdit(const QString& taskId)
{
    selectedTaskId_ = taskId.trimmed();
    if (window_ != nullptr) {
        window_->setTaskDetailEditing(false);
    }
    updateProjectView();
}

void WorkbenchController::deleteTask(const QString& taskId)
{
    if (!ensureWorkspace()) {
        return;
    }

    const QString normalizedTaskId = taskId.trimmed();
    if (normalizedTaskId.isEmpty()) {
        logWarning(QStringLiteral("Workspace"), QStringLiteral("请选择一个验证任务后再删除。"));
        return;
    }

    QString taskLabel = normalizedTaskId;
    QList<workspace::TaskSummary> tasks;
    QString listError;
    if (workspace_.listTasks(workspaceMode(), &tasks, &listError)) {
        for (const workspace::TaskSummary& task : tasks) {
            if (task.taskId == normalizedTaskId) {
                taskLabel = task.taskName.isEmpty()
                    ? normalizedTaskId
                    : QStringLiteral("%1 (%2)").arg(task.taskName, normalizedTaskId);
                break;
            }
        }
    }

    if ((window_ != nullptr) && !confirmTaskDeletion(window_, taskLabel)) {
        logInfo(QStringLiteral("Workspace"), QStringLiteral("已取消删除验证任务: %1").arg(normalizedTaskId));
        return;
    }

    QString error;
    if (!workspace_.deleteTask(workspaceMode(), normalizedTaskId, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("删除验证任务失败: %1").arg(error));
        return;
    }

    const bool deletedCurrentTask = hasTask_ && (currentTask_.summary.taskId == normalizedTaskId);
    if (deletedCurrentTask) {
        currentTask_ = workspace::TaskContext();
        flowState_ = workflow::FlowState();
        selectedTaskId_.clear();
        hasTask_ = false;
        resetExecutionState();
        if (window_ != nullptr) {
            window_->setProgramImagePath(QString());
            window_->setTaskDetailEditing(false);
        }
    } else if (selectedTaskId_ == normalizedTaskId) {
        selectedTaskId_.clear();
    }

    logInfo(QStringLiteral("Workspace"), QStringLiteral("验证任务已删除: %1").arg(normalizedTaskId));
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
}

void WorkbenchController::loadResourcePackIfAvailable()
{
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("resources")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("software/lockstep_host/resources"))
    };

    for (const QString& candidate : candidates) {
        if (!QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("manifest.json")))) {
            continue;
        }

        const resources::ResourceValidationResult result = resources_.validateResourcePack(candidate);
        if (!result.success) {
            logWarning(QStringLiteral("Resources"), QStringLiteral("资源包校验失败: %1").arg(result.errors.join(QStringLiteral("; "))));
            return;
        }

        const QString modeText = toModeText(mode_);
        const resources::ManifestDefaults defaults = resources_.defaults();
        const QString profileId = (mode_ == ui::UiMode::Test) ? defaults.testProfileId : defaults.researchProfileId;
        resources::BoardProfile profile;
        QString error;
        if (resources_.resolveBoardProfile(profileId, &profile, &error)) {
            debugProfile_.profileId = profile.profileId;
            debugProfile_.profileName = profile.profileName;
            debugProfile_.ramBaseAddress = profile.ramBaseAddress;
            debugProfile_.defaultRunAddress = profile.defaultRunAddress;
            debugProfile_.maxWritableAddress = profile.ramBaseAddress + kDebugMemorySizeBytes;
            debugProfile_.resetStrategy = profile.resetStrategy;
            resourcePackReady_ = true;
            if (window_ != nullptr) {
                window_->setConnectionProfileDetails(
                    debugProfile_.profileName,
                    profile.host,
                    profile.tclPort,
                    profile.gdbPort,
                    profile.jtagKhz,
                    addressText(profile.ramBaseAddress),
                    profile.resetStrategy,
                    QStringLiteral("调试器: 已加载 profile"));
            }
            logInfo(QStringLiteral("Resources"), QStringLiteral("已加载 %1 profile: %2").arg(modeText, profile.profileId));
            return;
        }

        logWarning(QStringLiteral("Resources"), QStringLiteral("默认 profile 无法解析: %1").arg(error));
        return;
    }

    resourcePackReady_ = false;
    logWarning(QStringLiteral("Resources"), QStringLiteral("未找到 resources/manifest.json，UI 使用内存调试后端占位，不写入真实外部路径。"));
}

void WorkbenchController::startDebugService()
{
    if (!ensureWorkspace()) {
        return;
    }

    connectionRecord_ = connectionService_.connectTarget(debugAccess_, debugProfile_);
    hasConnection_ = (connectionRecord_.state == target_control::ConnectionState::Connected);
    if (!hasConnection_) {
        logError(QStringLiteral("Target"), QStringLiteral("目标连接失败: %1").arg(connectionRecord_.errorMessage));
        return;
    }

    precheckRecord_ = connectionService_.runPrecheck(debugAccess_, debugProfile_);
    hasPrecheck_ = (precheckRecord_.state == target_control::PrecheckState::Passed);
    const QString statusText = QStringLiteral("调试器: %1 / 预检: %2")
        .arg(connectionStateText(connectionRecord_.state), precheckStateText(precheckRecord_.state));
    if (window_ != nullptr) {
        window_->setConnectionSummary(debugProfile_.profileName, statusText);
    }

    if (hasTask_) {
        flowState_ = workflow_.recordStageResult(
            flowState_,
            workflow::Stage::Connection,
            hasPrecheck_ ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
            statusText,
            QJsonObject());
    }
    logInfo(QStringLiteral("Target"), statusText);
    updateTopStatus();
}

void WorkbenchController::stopDebugService()
{
    connectionRecord_ = connectionService_.disconnectTarget(debugAccess_);
    hasConnection_ = false;
    hasPrecheck_ = false;
    if (window_ != nullptr) {
        window_->setConnectionSummary(debugProfile_.profileName, QStringLiteral("调试器: 已停止"));
    }
    logInfo(QStringLiteral("Target"), QStringLiteral("调试服务已停止"));
    updateTopStatus();
}

void WorkbenchController::browseProgramImage()
{
    if (window_ == nullptr) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        window_,
        QStringLiteral("选择程序镜像"),
        QString(),
        QStringLiteral("Program Images (*.elf *.bin *.srec *.s19 *.s28 *.s37 *.mot *.hex *.ihex *.ihx);;All Files (*.*)"));
    if (!path.isEmpty()) {
        window_->setProgramImagePath(path);
        logInfo(QStringLiteral("Program"), QStringLiteral("已选择程序镜像: %1").arg(path));
    }
}

void WorkbenchController::programImage()
{
    if (!ensureTask()) {
        return;
    }
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Program"), QStringLiteral("目标尚未连接或预检未通过，不能烧写。"));
        return;
    }
    if (window_ == nullptr || window_->programImagePath().isEmpty()) {
        logError(QStringLiteral("Program"), QStringLiteral("尚未选择程序镜像。"));
        return;
    }

    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Write, target_control::OperationStage::CheckDebugAccess);
    window_->setRamSummary(
        progressTitle(QStringLiteral("烧写准备中"), progress, QStringLiteral("正在确认目标连接、预检状态、片上调试器和内存写入可用性。")),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    workspace::TaskInputSet inputs = currentTask_.inputs;
    inputs.resourceSnapshot = resourceSnapshotJson();
    QString error;
    if (!saveProgramInputForCurrentTask(&inputs)) {
        return;
    }
    workspace::TaskContext updated;
    if (!workspace_.saveTaskInputs(workspaceMode(), currentTask_.summary.taskId, inputs, &updated, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("保存程序输入失败: %1").arg(error));
        return;
    }
    currentTask_ = updated;

    const QString taskImagePath =
        QDir(currentTask_.paths.taskRootPath).filePath(currentTask_.inputs.programFile.relativePath);
    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Write,
        target_control::OperationStage::DetectImage);
    window_->setRamSummary(
        progressTitle(QStringLiteral("烧写准备中"), progress, QStringLiteral("正在识别任务内程序镜像。")),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    imageInfo_ = programController_.detectImage(taskImagePath, debugProfile_);
    hasImage_ = (imageInfo_.type != target_control::ImageType::Unknown) && imageInfo_.errorMessage.isEmpty();
    if (!hasImage_) {
        logError(QStringLiteral("Program"), QStringLiteral("镜像识别失败: %1").arg(imageInfo_.errorMessage));
        window_->setRamSummary(
            QStringLiteral("烧写准备失败\n镜像格式、地址范围或资源配置确认失败: %1").arg(imageInfo_.errorMessage),
            30,
            0);
        return;
    }

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Write,
        target_control::OperationStage::ParseWritePlan);
    window_->setRamSummary(
        progressTitle(QStringLiteral("烧写准备中"), progress, QStringLiteral("镜像段和写入范围已形成，准备发送写入命令。")),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Write,
        target_control::OperationStage::WriteSegments);
    window_->setRamSummary(
        progressTitle(QStringLiteral("烧写执行中"), progress, QStringLiteral("烧写可用性已确认，正在等待片上调试器返回写入结果。")),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    writeRecord_ = programController_.programTarget(debugAccess_, currentTask_.summary.taskId, imageInfo_);
    hasWriteRecord_ = writeRecord_.success;

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Write,
        writeRecord_.success ? target_control::OperationStage::ConfirmWriteResult : target_control::OperationStage::Failed);
    window_->setRamSummary(
        progressTitle(
            writeRecord_.success ? QStringLiteral("烧写确认中") : QStringLiteral("烧写失败"),
            progress,
            writeRecord_.success ? QStringLiteral("片上调试器已返回写入成功记录。") : writeRecord_.errorMessage),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString relativePath;
    if (!writeEvidenceJson(QString::fromLatin1(kProgramWriteRecordName), writeRecordToJson(writeRecord_), &relativePath, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("烧写证据写入失败: %1").arg(error));
    }
    const target_control::OperationProgress persistedProgress =
        target_control::makeOperationProgress(
            target_control::ProgramOperation::Write,
            writeRecord_.success ? target_control::OperationStage::PersistWriteRecord : target_control::OperationStage::Failed);
    if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("烧写进度证据写入失败: %1").arg(error));
    }

    flowState_ = workflow_.recordStageResult(
        flowState_,
        workflow::Stage::ProgramWrite,
        writeRecord_.success ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
        writeRecord_.success ? QStringLiteral("烧写成功") : writeRecord_.errorMessage,
        imageToJson(imageInfo_));

    if (writeRecord_.success) {
        logInfo(QStringLiteral("Program"), QStringLiteral("烧写成功: %1").arg(writeRecord_.rawReturn));
    } else {
        logError(QStringLiteral("Program"), QStringLiteral("烧写失败: %1").arg(writeRecord_.errorMessage));
    }
    setRamSummaryFromCurrentState(QStringLiteral("烧写完成"));
    updateTopStatus();
}

void WorkbenchController::verifyReadback()
{
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Readback"), QStringLiteral("目标尚未连接或预检未通过，不能回读校验。"));
        return;
    }
    if (!hasImage_ || !hasWriteRecord_) {
        logError(QStringLiteral("Readback"), QStringLiteral("尚未完成有效烧写，不能回读校验。"));
        return;
    }

    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Readback, target_control::OperationStage::CheckReadbackAccess);
    window_->setRamSummary(
        progressTitle(QStringLiteral("回读准备中"), progress, QStringLiteral("正在确认烧写记录、片上调试器和目标内存回读可用性。")),
        100,
        progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Readback,
        target_control::OperationStage::PrepareReadRanges);
    window_->setRamSummary(
        progressTitle(QStringLiteral("回读准备中"), progress, QStringLiteral("正在准备与烧写一致的回读范围。")),
        100,
        progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Readback,
        target_control::OperationStage::ReadSegments);
    window_->setRamSummary(
        progressTitle(QStringLiteral("回读执行中"), progress, QStringLiteral("回读可用性已确认，正在等待片上调试器返回回读数据。")),
        100,
        progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    verifyRecord_ = programController_.verifyReadback(debugAccess_, currentTask_.summary.taskId, imageInfo_);
    hasVerifyRecord_ = true;

    progress = target_control::makeOperationProgress(
        target_control::ProgramOperation::Readback,
        target_control::OperationStage::CompareData);
    window_->setRamSummary(
        progressTitle(QStringLiteral("回读比较中"), progress, QStringLiteral("正在确认回读数据长度和差异数量。")),
        100,
        progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString error;
    QString relativePath;
    if (!writeEvidenceJson(QString::fromLatin1(kReadbackVerifyRecordName), verifyRecordToJson(verifyRecord_), &relativePath, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("回读证据写入失败: %1").arg(error));
    }
    const target_control::OperationProgress persistedProgress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Readback, target_control::OperationStage::PersistVerifyRecord);
    if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("回读进度证据写入失败: %1").arg(error));
    }

    const bool passed = verifyRecord_.state == target_control::VerifyState::Passed;
    flowState_ = workflow_.recordStageResult(
        flowState_,
        workflow::Stage::ReadbackVerify,
        passed ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
        passed ? QStringLiteral("回读校验通过") : verifyRecord_.errorMessage,
        verifyRecordToJson(verifyRecord_));
    if (passed) {
        logInfo(QStringLiteral("Readback"), QStringLiteral("回读校验通过。"));
    } else {
        logError(QStringLiteral("Readback"), QStringLiteral("回读校验失败: %1").arg(verifyRecord_.errorMessage));
    }
    setRamSummaryFromCurrentState(QStringLiteral("回读校验完成"));
    updateTopStatus();
}

void WorkbenchController::runProgram()
{
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Run"), QStringLiteral("目标尚未连接或预检未通过，不能运行程序。"));
        return;
    }
    if (!hasVerifyRecord_ || verifyRecord_.state != target_control::VerifyState::Passed) {
        logError(QStringLiteral("Run"), QStringLiteral("回读校验未通过，禁止运行。"));
        return;
    }

    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::CheckRunGate);
    setRunSummaryFromCurrentState(QStringLiteral("运行准备中 - ") + progress.message, progress.percent, 0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::DispatchRun);
    setRunSummaryFromCurrentState(QStringLiteral("运行命令已发送 - ") + progress.message, progress.percent, 0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    runRecord_ = programController_.runTarget(debugAccess_, currentTask_.summary.taskId, imageInfo_, verifyRecord_);
    hasRunRecord_ = true;

    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::CaptureRunStatus);
    setRunSummaryFromCurrentState(QStringLiteral("运行返回确认中 - ") + progress.message, progress.percent, 0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString error;
    QString relativePath;
    if (!writeEvidenceJson(QString::fromLatin1(kRunControlRecordName), runRecordToJson(runRecord_), &relativePath, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("运行证据写入失败: %1").arg(error));
    }
    const target_control::OperationProgress persistedProgress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::PersistRunRecord);
    if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("运行进度证据写入失败: %1").arg(error));
    }

    const bool running = runRecord_.state == target_control::RunState::Running;
    flowState_ = workflow_.recordStageResult(
        flowState_,
        workflow::Stage::RunControl,
        running ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
        running ? QStringLiteral("程序已运行") : runRecord_.errorMessage,
        runRecordToJson(runRecord_));
    if (running) {
        logInfo(QStringLiteral("Run"), QStringLiteral("程序运行成功: %1").arg(runRecord_.rawReturn));
    } else {
        logError(QStringLiteral("Run"), QStringLiteral("程序运行失败: %1").arg(runRecord_.errorMessage));
    }
    setRunSummaryFromCurrentState(QStringLiteral("运行控制完成"), 100, 0);
    updateTopStatus();
}

void WorkbenchController::stopProgram()
{
    if (!ensureTask()) {
        return;
    }
    if (!hasConnection_) {
        logError(QStringLiteral("Run"), QStringLiteral("目标尚未连接，不能发送中止命令。"));
        return;
    }

    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::CheckHaltAccess);
    setRunSummaryFromCurrentState(QStringLiteral("终止准备中 - ") + progress.message, hasRunRecord_ ? 100 : 0, progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::DispatchHalt);
    setRunSummaryFromCurrentState(QStringLiteral("终止命令已发送 - ") + progress.message, hasRunRecord_ ? 100 : 0, progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    haltRecord_ = programController_.haltTarget(debugAccess_, currentTask_.summary.taskId);
    hasHaltRecord_ = true;

    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::CaptureHaltStatus);
    setRunSummaryFromCurrentState(QStringLiteral("终止返回确认中 - ") + progress.message, hasRunRecord_ ? 100 : 0, progress.percent);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString error;
    QString relativePath;
    if (!writeEvidenceJson(QString::fromLatin1(kHaltControlRecordName), runRecordToJson(haltRecord_), &relativePath, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("中止证据写入失败: %1").arg(error));
    }
    const target_control::OperationProgress persistedProgress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::PersistHaltRecord);
    if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
        logWarning(QStringLiteral("Evidence"), QStringLiteral("中止进度证据写入失败: %1").arg(error));
    }

    if (haltRecord_.state == target_control::RunState::Halted) {
        logInfo(QStringLiteral("Run"), QStringLiteral("程序已中止: %1").arg(haltRecord_.rawReturn));
    } else {
        logWarning(QStringLiteral("Run"), QStringLiteral("中止请求失败: %1").arg(haltRecord_.errorMessage));
    }
    setRunSummaryFromCurrentState(QStringLiteral("终止控制完成"), hasRunRecord_ ? 100 : 0, 100);
    updateTopStatus();
}

void WorkbenchController::generateReport()
{
    if (!ensureTask()) {
        return;
    }

    QList<error_handling::ErrorRecord> errors;
    QString error;
    if (!errorRegistry_.loadTaskErrors(currentTask_.paths.taskRootPath, &errors, &error)) {
        logWarning(QStringLiteral("Report"), QStringLiteral("读取任务错误记录失败: %1").arg(error));
    }

    reporting::ReportInput input;
    input.reportId = QStringLiteral("report_%1").arg(compactTimeText());
    input.taskId = currentTask_.summary.taskId;
    input.mode = toModeText(mode_);
    input.requiredEvidence.programWritePassed = hasWriteRecord_ && writeRecord_.success;
    input.requiredEvidence.readbackVerifyPassed =
        hasVerifyRecord_ && (verifyRecord_.state == target_control::VerifyState::Passed);
    input.requiredEvidence.runControlReturned =
        hasRunRecord_ && (!runRecord_.rawReturn.isEmpty() || !runRecord_.snapshot.isEmpty());
    input.requiredEvidence.programWriteRecordPath = QStringLiteral("evidence/%1").arg(QString::fromLatin1(kProgramWriteRecordName));
    input.requiredEvidence.readbackVerifyRecordPath = QStringLiteral("evidence/%1").arg(QString::fromLatin1(kReadbackVerifyRecordName));
    input.requiredEvidence.runControlRecordPath = QStringLiteral("evidence/%1").arg(QString::fromLatin1(kRunControlRecordName));
    input.optionalRecords.faultInjection = reporting::OptionalRecordState::Skipped;
    input.unresolvedBlockingErrors = errorRegistry_.unresolvedBlockingErrors(errors);
    input.resourceSnapshot = resourceSnapshotJson();

    const reporting::ReportResult result = reportGenerator_.generateReport(currentTask_.paths.taskRootPath, input);
    if (!result.success) {
        logError(QStringLiteral("Report"), QStringLiteral("报告生成失败: %1").arg(result.errorMessage));
        return;
    }

    workspace::ArtifactRecord artifact;
    artifact.artifactId = input.reportId;
    artifact.kind = workspace::ArtifactKind::Report;
    artifact.relativePath = QStringLiteral("reports/report.json");
    artifact.name = QStringLiteral("report.json");
    artifact.createdAt = currentTimeText();
    workspace_.attachArtifact(workspaceMode(), currentTask_.summary.taskId, artifact, nullptr);

    logInfo(QStringLiteral("Report"), QStringLiteral("报告已生成: %1 / 结论: %2")
        .arg(result.reportPath, reporting::toString(result.conclusion)));
}

void WorkbenchController::showVerifySummary()
{
    setRamSummaryFromCurrentState(QStringLiteral("回读校验摘要"));
}

void WorkbenchController::showRunSummary()
{
    setRunSummaryFromCurrentState(QStringLiteral("运行摘要"), hasRunRecord_ ? 100 : 0, hasHaltRecord_ ? 100 : 0);
}

void WorkbenchController::updateProjectView()
{
    if (window_ == nullptr || !workspaceReady_) {
        return;
    }

    QList<workspace::TaskSummary> tasks;
    QString error;
    QVector<ui::ProjectTaskViewItem> viewTasks;
    if (!workspace_.listTasks(workspaceMode(), &tasks, &error)) {
        logWarning(QStringLiteral("Workspace"), QStringLiteral("任务列表读取失败: %1").arg(error));
        window_->setProjectTasks(workspace_.workspaceDisplayName(), viewTasks, QString());
        return;
    }

    for (const workspace::TaskSummary& task : tasks) {
        ui::ProjectTaskViewItem item;
        item.taskId = task.taskId;
        item.taskName = task.taskName;
        item.description = task.description;
        item.statusText = workspace::toString(task.status);
        item.updatedAtText = task.updatedAt;
        item.basicInfo = taskBasicInfoText(task);
        viewTasks.append(item);
    }

    const QString selectedTaskId = !selectedTaskId_.trimmed().isEmpty()
        ? selectedTaskId_.trimmed()
        : (hasTask_ ? currentTask_.summary.taskId : QString());
    window_->setProjectTasks(workspace_.workspaceDisplayName(), viewTasks, selectedTaskId);
}

void WorkbenchController::updateTaskDetail()
{
    if (window_ == nullptr) {
        return;
    }
    if (!hasTask_) {
        window_->setTaskDetail(QStringLiteral("未创建任务"), QStringLiteral("请点击“新建验证任务”。"), QStringLiteral("状态: 未加载"));
        return;
    }

    window_->setTaskDetail(
        currentTask_.summary.taskName,
        currentTask_.summary.description,
        taskBasicInfoText(currentTask_.summary));
}

void WorkbenchController::updateTopStatus()
{
    if (window_ == nullptr) {
        return;
    }

    ui::GlobalStatus status = ui::makeDefaultGlobalStatus(mode_);
    status.taskStatusText = hasTask_
        ? QStringLiteral("任务: %1").arg(currentTask_.summary.taskName)
        : QStringLiteral("任务: 未创建");
    status.targetStatusText = hasConnection_ ? QStringLiteral("目标: 已连接") : QStringLiteral("目标: 未连接");
    if (hasRunRecord_ && runRecord_.state == target_control::RunState::Running) {
        status.programStatusText = QStringLiteral("程序: 运行中");
    } else if (hasVerifyRecord_ && verifyRecord_.state == target_control::VerifyState::Passed) {
        status.programStatusText = QStringLiteral("程序: 回读通过");
    } else if (hasWriteRecord_ && writeRecord_.success) {
        status.programStatusText = QStringLiteral("程序: 已烧写");
    } else if (hasImage_) {
        status.programStatusText = QStringLiteral("程序: 已识别");
    } else {
        status.programStatusText = QStringLiteral("程序: 未选择");
    }
    window_->setTopStatus(status);
}

void WorkbenchController::setRamSummaryFromCurrentState(const QString& title)
{
    if (window_ == nullptr) {
        return;
    }

    const QString text = QStringLiteral(
        "%1\n镜像: %2 / %3 bytes / %4\n烧写: %5\n回读: %6, diff=%7\n烧写返回: %8\n回读返回: %9")
        .arg(title,
             imageInfo_.fileName.isEmpty() ? QStringLiteral("未选择") : imageInfo_.fileName,
             QString::number(imageInfo_.sizeBytes),
             target_control::toString(imageInfo_.type),
             hasWriteRecord_ ? (writeRecord_.success ? QStringLiteral("passed") : writeRecord_.errorMessage) : QStringLiteral("not_run"),
             hasVerifyRecord_ ? target_control::toString(verifyRecord_.state) : QStringLiteral("not_run"),
             hasVerifyRecord_ ? QString::number(verifyRecord_.diffCount) : QStringLiteral("0"),
             writeRecord_.rawReturn,
             verifyRecord_.rawReturn);

    int writeProgress = 0;
    const bool writeAttempted =
        writeRecord_.success || !writeRecord_.rawReturn.isEmpty() || !writeRecord_.errorMessage.isEmpty();
    if (writeAttempted) {
        writeProgress = 100;
    } else if (hasImage_) {
        writeProgress = 30;
    }

    int readbackProgress = 0;
    if (hasVerifyRecord_) {
        readbackProgress = 100;
    }
    if (hasRunRecord_ && runRecord_.state == target_control::RunState::Running) {
        writeProgress = 100;
        readbackProgress = 100;
    }
    window_->setRamSummary(text, writeProgress, readbackProgress);
}

void WorkbenchController::setRunSummaryFromCurrentState(
    const QString& title,
    const int runProgressPercent,
    const int stopProgressPercent)
{
    if (window_ == nullptr) {
        return;
    }

    const QString text = QStringLiteral(
        "%1\n镜像: %2\n入口地址: %3\n运行状态: %4\n运行返回: %5\n运行快照: %6\n中止状态: %7\n中止返回: %8\n中止快照: %9\n错误: %10")
        .arg(title,
             imageInfo_.fileName.isEmpty() ? QStringLiteral("未确定") : imageInfo_.fileName,
             addressText(imageInfo_.entryAddress),
             hasRunRecord_ ? target_control::toString(runRecord_.state) : QStringLiteral("not_run"),
             runRecord_.rawReturn,
             runRecord_.snapshot,
             hasHaltRecord_ ? target_control::toString(haltRecord_.state) : QStringLiteral("not_run"),
             haltRecord_.rawReturn,
             haltRecord_.snapshot,
             !runRecord_.errorMessage.isEmpty() ? runRecord_.errorMessage : haltRecord_.errorMessage);
    window_->setRunSummary(text, runProgressPercent, stopProgressPercent);
}

void WorkbenchController::resetExecutionState()
{
    hasImage_ = false;
    hasWriteRecord_ = false;
    hasVerifyRecord_ = false;
    hasRunRecord_ = false;
    hasHaltRecord_ = false;
    imageInfo_ = target_control::ProgramImageInfo();
    writeRecord_ = target_control::WriteRecord();
    verifyRecord_ = target_control::ReadbackVerifyRecord();
    runRecord_ = target_control::RunControlRecord();
    haltRecord_ = target_control::RunControlRecord();
    if (window_ != nullptr) {
        window_->setRamSummary(QStringLiteral("尚未选择程序镜像。"), 0, 0);
        window_->setRunSummary(QStringLiteral("程序运行控制摘要将在这里显示。"), 0, 0);
    }
}

bool WorkbenchController::saveProgramInputForCurrentTask(workspace::TaskInputSet* const inputs)
{
    if (inputs == nullptr || window_ == nullptr) {
        return true;
    }

    const QString sourcePath = window_->programImagePath().trimmed();
    if (sourcePath.isEmpty()) {
        return true;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        logError(QStringLiteral("Workspace"), QStringLiteral("程序镜像不存在，无法保存任务输入: %1").arg(sourcePath));
        return false;
    }

    const QString existingRelativePath = inputs->programFile.relativePath.trimmed();
    if (!existingRelativePath.isEmpty()) {
        const QString existingPath = QDir(currentTask_.paths.taskRootPath).filePath(existingRelativePath);
        if (sameCleanPath(sourcePath, existingPath)) {
            return true;
        }
    }

    const QString targetPath = QDir(currentTask_.paths.programPath).filePath(sourceInfo.fileName());
    if (sameCleanPath(sourcePath, targetPath)) {
        inputs->programFile.id = QStringLiteral("input_%1").arg(currentTimeText());
        inputs->programFile.relativePath = QDir(currentTask_.paths.taskRootPath).relativeFilePath(targetPath);
        inputs->programFile.originalFileName = sourceInfo.fileName();
        inputs->programFile.sizeBytes = sourceInfo.size();
        inputs->programFile.importedAt = currentTimeText();
        return true;
    }

    workspace::TaskInputImportRequest request;
    request.mode = workspaceMode();
    request.taskId = currentTask_.summary.taskId;
    request.kind = workspace::TaskInputFileKind::ProgramFile;
    request.sourceFilePath = sourcePath;
    request.targetFileName = sourceInfo.fileName();

    workspace::TaskInputItem imported;
    QString error;
    if (!workspace_.importTaskInputFile(request, &imported, &error)) {
        logError(QStringLiteral("Workspace"), QStringLiteral("导入程序镜像失败: %1").arg(error));
        return false;
    }

    inputs->programFile = imported;
    window_->setProgramImagePath(QDir(currentTask_.paths.taskRootPath).filePath(imported.relativePath));
    return true;
}

void WorkbenchController::restoreExecutionEvidenceForCurrentTask()
{
    if (!hasTask_) {
        return;
    }

    QJsonObject object;
    if (readEvidenceObject(currentTask_.paths.evidencePath, QString::fromLatin1(kProgramWriteRecordName), &object)) {
        writeRecord_ = writeRecordFromJson(object);
        hasWriteRecord_ = writeRecord_.success || !writeRecord_.rawReturn.isEmpty() || !writeRecord_.errorMessage.isEmpty();
    }

    if (readEvidenceObject(currentTask_.paths.evidencePath, QString::fromLatin1(kReadbackVerifyRecordName), &object)) {
        verifyRecord_ = verifyRecordFromJson(object);
        hasVerifyRecord_ = verifyRecord_.state != target_control::VerifyState::NotRun ||
            !verifyRecord_.rawReturn.isEmpty() ||
            !verifyRecord_.errorMessage.isEmpty();
    }

    if (readEvidenceObject(currentTask_.paths.evidencePath, QString::fromLatin1(kRunControlRecordName), &object)) {
        runRecord_ = runRecordFromJson(object, target_control::ProgramOperation::Run);
        runRecord_.entryAddress = imageInfo_.entryAddress;
        hasRunRecord_ = runRecord_.state != target_control::RunState::NotAllowed ||
            !runRecord_.rawReturn.isEmpty() ||
            !runRecord_.snapshot.isEmpty();
    }

    if (readEvidenceObject(currentTask_.paths.evidencePath, QString::fromLatin1(kHaltControlRecordName), &object)) {
        haltRecord_ = runRecordFromJson(object, target_control::ProgramOperation::Halt);
        hasHaltRecord_ = haltRecord_.state != target_control::RunState::NotAllowed ||
            !haltRecord_.rawReturn.isEmpty() ||
            !haltRecord_.snapshot.isEmpty();
    }
}

void WorkbenchController::logInfo(const QString& source, const QString& message) const
{
    if (window_ != nullptr) {
        window_->appendLog(ui::LogChannel::Operation, ui::LogLevel::Info, source, message);
    }
}

void WorkbenchController::logWarning(const QString& source, const QString& message) const
{
    if (window_ != nullptr) {
        window_->appendLog(ui::LogChannel::Operation, ui::LogLevel::Warning, source, message);
    }
}

void WorkbenchController::logError(const QString& source, const QString& message)
{
    if (window_ != nullptr) {
        window_->appendLog(ui::LogChannel::Operation, ui::LogLevel::Error, source, message);
    }
    recordError(QStringLiteral("UI_BACKEND_ERROR"), error_handling::ErrorSeverity::Error, source, message, QString());
}

void WorkbenchController::recordError(
    const QString& code,
    const error_handling::ErrorSeverity severity,
    const QString& source,
    const QString& message,
    const QString& detail)
{
    if (!workspaceReady_) {
        return;
    }

    error_handling::ErrorEvent event;
    event.code = code;
    event.source = source;
    event.module = QStringLiteral("ui_adapter");
    event.taskId = hasTask_ ? currentTask_.summary.taskId : QString();
    event.severity = severity;
    event.message = message;
    event.detail = detail;

    error_handling::ErrorRecord record;
    QString error;
    if (hasTask_) {
        errorRegistry_.appendTaskError(currentTask_.paths.taskRootPath, event, &record, &error);
    } else {
        const QString systemLogPath = QDir(QDir(workspaceRootPath_).filePath(workspace::toStorageName(workspaceMode())))
            .filePath(QStringLiteral("system_logs"));
        errorRegistry_.appendSystemError(systemLogPath, event, &record, &error);
    }
}

bool WorkbenchController::writeEvidenceJson(
    const QString& fileName,
    const QJsonObject& object,
    QString* const relativePath,
    QString* const errorMessage)
{
    if (!hasTask_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("尚未创建任务");
        }
        return false;
    }

    const QString path = QDir(currentTask_.paths.evidencePath).filePath(fileName);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("无法写入证据: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument document(object);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("证据写入不完整: %1").arg(path);
        }
        return false;
    }
    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("证据提交失败: %1").arg(path);
        }
        return false;
    }

    if (relativePath != nullptr) {
        *relativePath = QStringLiteral("evidence/%1").arg(fileName);
    }

    workspace::ArtifactRecord artifact;
    artifact.artifactId = QFileInfo(fileName).completeBaseName();
    artifact.kind = workspace::ArtifactKind::Evidence;
    artifact.relativePath = QStringLiteral("evidence/%1").arg(fileName);
    artifact.name = fileName;
    artifact.createdAt = currentTimeText();
    workspace_.attachArtifact(workspaceMode(), currentTask_.summary.taskId, artifact, nullptr);
    return true;
}

QJsonObject WorkbenchController::resourceSnapshotJson() const
{
    if (resourcePackReady_) {
        return resources::toJson(resources_.getModeResourceSnapshot(toModeText(mode_)));
    }

    QJsonObject object;
    object.insert(QStringLiteral("resource_pack_id"), QStringLiteral("ui_default"));
    object.insert(QStringLiteral("resource_pack_version"), QStringLiteral("0"));
    object.insert(QStringLiteral("profile_id"), debugProfile_.profileId);
    object.insert(QStringLiteral("profile_sha256"), QString());
    object.insert(QStringLiteral("pl_allow_result"), QStringLiteral("placeholder"));
    object.insert(QStringLiteral("protocol_rule_status"), QStringLiteral("placeholder"));
    return object;
}

workspace::WorkspaceMode WorkbenchController::workspaceMode() const
{
    return (mode_ == ui::UiMode::Test) ? workspace::WorkspaceMode::Test : workspace::WorkspaceMode::Research;
}

workflow::FlowMode WorkbenchController::flowMode() const
{
    return (mode_ == ui::UiMode::Test) ? workflow::FlowMode::Test : workflow::FlowMode::Research;
}

}  // namespace lockstep::apps
