/*****************************************************************************
* 文件名: workbench_controller.cpp
* 日期: 2026-07-13
* 版本: v1.1
* 更新记录: 增加安装布局资源路径并统一中文文件头
* 描述: 实现 UI 动作到工作区、资源、流程、目标控制、报告和错误日志模块的适配
*****************************************************************************/

#include "workbench_controller.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
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
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

#include "ui_theme.h"

namespace lockstep::apps {
namespace {

constexpr quint64 kDebugMemorySizeBytes = 64ULL * 1024ULL * 1024ULL;
constexpr char kProgramWriteRecordName[] = "program_write_record.json";
constexpr char kReadbackVerifyRecordName[] = "readback_verify_record.json";
constexpr char kRunControlRecordName[] = "run_control_record.json";
constexpr char kHaltControlRecordName[] = "halt_control_record.json";
constexpr char kProgramOperationProgressName[] = "program_operation_progress.json";
constexpr int kSamplingSampleCount = 4096;
constexpr int kSamplingPretrigger = 2047;
constexpr int kSamplingPosttrigger = 2049;
constexpr int kSamplingTriggerCount = 1;
constexpr int kSamplingPostAfterTrigger = 2048;
constexpr int kSamplingSampleWordBits = 512;
constexpr int kSamplingMismatchMask = 0x1F;

struct WriteWorkerResult final {
    target_control::WriteRecord record;
    QString connectionError;
    qint64 elapsedMs = 0;
};

struct ReadbackWorkerResult final {
    target_control::ReadbackVerifyRecord record;
    QString connectionError;
    qint64 elapsedMs = 0;
};

struct RunWorkerResult final {
    target_control::RunControlRecord record;
    QString connectionError;
    qint64 elapsedMs = 0;
};

bool isBlockedByHardwareOperation(const ui::UiAction action)
{
    bool blocked = true;
    switch (action) {
    case ui::UiAction::RefreshSerialPorts:
    case ui::UiAction::ToggleSerialMonitor:
    case ui::UiAction::ClearSerialOutput:
    case ui::UiAction::SendSerialData:
    case ui::UiAction::ShowVerifySummary:
    case ui::UiAction::ShowRunSummary:
        blocked = false;
        break;
    default:
        blocked = true;
        break;
    }
    return blocked;
}

int dataProgressPercent(const quint64 completedBytes, const quint64 totalBytes)
{
    if (totalBytes == 0U) {
        return 20;
    }
    const quint64 boundedCompleted = qMin(completedBytes, totalBytes);
    const quint64 scaled = (boundedCompleted * 70U) / totalBytes;
    return static_cast<int>(20U + scaled);
}

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

QString shortRawText(const QString& rawText)
{
    constexpr qsizetype kMaxRawTextLength = 3500;
    if (rawText.size() <= kMaxRawTextLength) {
        return rawText;
    }
    return rawText.left(kMaxRawTextLength) + QStringLiteral("\n... 原始返回过长，已截断显示。");
}

QString debugReturnSummary(const QString& rawText)
{
    const QString trimmed = rawText.trimmed();
    if (trimmed.isEmpty()) {
        return QStringLiteral("无返回");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return shortRawText(trimmed);
    }

    const QJsonObject object = document.object();
    QStringList lines;
    lines.append(QStringLiteral("success=%1")
        .arg(object.value(QStringLiteral("success")).toBool(false) ? QStringLiteral("true") : QStringLiteral("false")));
    const QString operation = object.value(QStringLiteral("operation")).toString();
    if (!operation.isEmpty()) {
        lines.append(QStringLiteral("operation=%1").arg(operation));
    }
    const QString errorCode = object.value(QStringLiteral("error_code")).toString();
    if (!errorCode.isEmpty()) {
        lines.append(QStringLiteral("error_code=%1").arg(errorCode));
    }
    const QString errorMessage = object.value(QStringLiteral("error_message")).toString();
    if (!errorMessage.isEmpty()) {
        lines.append(QStringLiteral("error=%1").arg(errorMessage));
    }

    const QJsonObject snapshot = object.value(QStringLiteral("snapshot")).toObject();
    const QString targetState = snapshot.value(QStringLiteral("target_state")).toString();
    if (!targetState.isEmpty()) {
        lines.append(QStringLiteral("target_state=%1").arg(targetState));
    }
    const QString dmStatus = snapshot.value(QStringLiteral("dmstatus")).toString();
    if (!dmStatus.isEmpty()) {
        lines.append(QStringLiteral("dmstatus=%1").arg(dmStatus));
    }
    const QString exchangeCount = snapshot.value(QStringLiteral("exchange_count")).toString();
    if (!exchangeCount.isEmpty()) {
        lines.append(QStringLiteral("exchange_count=%1").arg(exchangeCount));
    }
    return lines.join(QStringLiteral(", "));
}

QString serialDisplayName(const QSerialPortInfo& info)
{
    const QString portName = info.portName().trimmed();
    const QString description = info.description().trimmed();
    if (!description.isEmpty()) {
        return QStringLiteral("%1 - %2").arg(portName, description);
    }
    const QString manufacturer = info.manufacturer().trimmed();
    if (!manufacturer.isEmpty()) {
        return QStringLiteral("%1 - %2").arg(portName, manufacturer);
    }
    return portName;
}

bool hasExpectedRunOutput(const QString& text)
{
    const QString normalized = text.toLower();
    if (normalized.contains(QStringLiteral("program_run_done"))) {
        return true;
    }
    if (normalized.contains(QStringLiteral("lockstep_run_done"))) {
        return true;
    }
    int matchedFields = 0;
    if (normalized.contains(QStringLiteral("rx total execution time"))) {
        ++matchedFields;
    }
    if (normalized.contains(QStringLiteral("microseconds for one run"))) {
        ++matchedFields;
    }
    if (normalized.contains(QStringLiteral("dhrystones per second"))) {
        ++matchedFields;
    }
    if (normalized.contains(QStringLiteral("dhrystones mips"))) {
        ++matchedFields;
    }
    return normalized.contains(QStringLiteral("dhrystone")) && matchedFields >= 2;
}

QString runUiStateText(
    const bool operationBusy,
    const target_control::ProgramOperation operation,
    const bool hasRunRecord,
    const target_control::RunState runState,
    const bool hasHaltRecord,
    const target_control::RunState haltState)
{
    if (operationBusy && operation == target_control::ProgramOperation::Halt) {
        return QStringLiteral("终止中");
    }
    if (hasHaltRecord && haltState == target_control::RunState::Halted) {
        return QStringLiteral("已终止");
    }
    if (operationBusy && operation == target_control::ProgramOperation::Run) {
        return QStringLiteral("运行中");
    }
    if (hasRunRecord && runState == target_control::RunState::Running) {
        return QStringLiteral("运行中");
    }
    return QStringLiteral("未运行");
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

bool parseAddressText(const QString& text, quint64* const value)
{
    if (value == nullptr) {
        return false;
    }

    const QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    bool ok = false;
    const quint64 parsed = normalized.toULongLong(&ok, 0);
    if (!ok) {
        return false;
    }

    *value = parsed;
    return true;
}

QString executablePathForBaseName(const QString& directoryPath, const QString& baseName)
{
    const QDir directory(directoryPath);
    const QString windowsPath = directory.filePath(baseName + QStringLiteral(".exe"));
    if (QFileInfo(windowsPath).isExecutable()) {
        return QFileInfo(windowsPath).absoluteFilePath();
    }

    const QString plainPath = directory.filePath(baseName);
    if (QFileInfo(plainPath).isExecutable()) {
        return QFileInfo(plainPath).absoluteFilePath();
    }
    return QString();
}

QString resolveDebugServicePath(
    const resources::BoardProfile& profile,
    const QString& resourceRootPath)
{
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QString siblingPath = executablePathForBaseName(appDirPath, QStringLiteral("lockstep_debug_service"));
    if (!siblingPath.isEmpty()) {
        return siblingPath;
    }

    const QString resourcePath = QDir(resourceRootPath).filePath(profile.targetDebugToolPath);
    if (QFileInfo(resourcePath).isExecutable()) {
        return QFileInfo(resourcePath).absoluteFilePath();
    }
    return QString();
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

QVector<ui::TraceGroupViewItem> traceGroupsToUi(const QList<waveform_viewer::WaveformGroupView>& groups)
{
    QVector<ui::TraceGroupViewItem> items;
    items.reserve(groups.size());
    for (const waveform_viewer::WaveformGroupView& group : groups) {
        ui::TraceGroupViewItem item;
        item.id = group.id;
        item.displayName = group.displayName;
        item.status = group.status;
        item.reason = group.reason;
        item.fields = group.fields;
        item.transactions = group.transactions;
        items.append(item);
    }
    return items;
}

QString fileSha256Text(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

workspace::ArtifactRecord waveformArtifact(
    const QString& artifactId,
    const QString& absolutePath,
    const QString& relativePath,
    const QJsonObject& metadata)
{
    workspace::ArtifactRecord artifact;
    artifact.artifactId = artifactId;
    artifact.kind = workspace::ArtifactKind::Waveform;
    artifact.relativePath = relativePath;
    artifact.name = QFileInfo(absolutePath).fileName();
    artifact.sha256 = fileSha256Text(absolutePath);
    artifact.sizeBytes = QFileInfo(absolutePath).size();
    artifact.createdAt = currentTimeText();
    artifact.metadata = metadata;
    return artifact;
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
      protocolAnalyzer_(),
      waveformViewer_(),
      debugAccess_(),
      debugConfig_(),
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
      hardwareOperationBusy_(false),
      targetConnectionBusy_(false),
      currentTask_(),
      flowState_(),
      debugProfile_(),
      connectionRecord_(),
      precheckRecord_(),
      imageInfo_(),
      writeRecord_(),
      verifyRecord_(),
      runRecord_(),
      haltRecord_(),
      serialPort_(std::make_unique<QSerialPort>(this)),
      serialPorts_(),
      runSerialOutput_(),
      latestRunInstruction_(),
      runOutputConfirmed_(false),
      serialOpen_(false),
      runSerialCaptureActive_(false),
      hardwareOperation_(target_control::ProgramOperation::Write),
      hardwareOperationName_(),
      currentWriteProgress_(0),
      currentReadbackProgress_(0),
      currentRunProgress_(0),
      currentStopProgress_(0)
{
    debugProfile_.profileId = QStringLiteral("debug_service_unconfigured");
    debugProfile_.profileName = QStringLiteral("自研片上调试服务未配置");
    debugProfile_.ramBaseAddress = 0x80000000ULL;
    debugProfile_.defaultRunAddress = debugProfile_.ramBaseAddress;
    debugProfile_.maxWritableAddress = debugProfile_.ramBaseAddress + kDebugMemorySizeBytes;
    debugProfile_.resetStrategy = QStringLiteral("halt");

    if (window_ != nullptr) {
        connect(window_, &ui::MainWindowShell::actionRequested, this, [this](const ui::UiActionRequest& request) {
            handleAction(request);
        });
        connect(window_, &ui::MainWindowShell::pageChanged, this, [this](const ui::NavigationPage page) {
            if (page == ui::NavigationPage::Waveform || page == ui::NavigationPage::Protocol) {
                refreshWaveformViewWithAutoAnalysis();
            }
        });
        connect(serialPort_.get(), &QSerialPort::readyRead, this, [this]() {
            const QByteArray data = serialPort_->readAll();
            if ((window_ != nullptr) && !data.isEmpty()) {
                const QString text = QString::fromUtf8(data);
                window_->appendLog(
                    ui::LogChannel::Serial,
                    ui::LogLevel::Info,
                    QStringLiteral("Serial"),
                    text);
                appendProgramSerialOutput(text);
            }
        });
        connect(serialPort_.get(), &QSerialPort::errorOccurred, this, [this](const QSerialPort::SerialPortError error) {
            if (error != QSerialPort::NoError) {
                const QString message = serialPort_->errorString();
                if (window_ != nullptr) {
                    window_->setSerialStatus(QStringLiteral("串口错误: %1").arg(message), serialPort_->isOpen());
                }
                logWarning(QStringLiteral("Serial"), message);
            }
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
    if (window_ != nullptr && !resourcePackReady_) {
        window_->setConnectionProfileDetails(
            debugProfile_.profileName,
            QString(),
            0,
            0,
            0,
            addressText(debugProfile_.ramBaseAddress),
            debugProfile_.resetStrategy,
            QStringLiteral("调试器: 未配置"));
        window_->setRamSummary(QStringLiteral("尚未选择程序镜像。"), 0, 0);
    }
    return true;
}

void WorkbenchController::handleAction(const ui::UiActionRequest& request)
{
    if (hardwareOperationBusy_ && isBlockedByHardwareOperation(request.action)) {
        logWarning(
            QStringLiteral("UI"),
            QStringLiteral("%1 正在执行，已阻止动作: %2")
                .arg(hardwareOperationName_, ui::toDisplayText(request.action)));
        return;
    }

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
    case ui::UiAction::RefreshSerialPorts:
        refreshSerialPorts();
        break;
    case ui::UiAction::ToggleSerialMonitor:
        toggleSerialMonitor();
        break;
    case ui::UiAction::ClearSerialOutput:
        clearSerialOutput();
        break;
    case ui::UiAction::SendSerialData:
        sendSerialData(request.parameters.value(QStringLiteral("serialText")).toString());
        break;
    case ui::UiAction::SaveSamplingConfig:
        saveSamplingConfig(request.parameters, false);
        break;
    case ui::UiAction::SendSamplingConfig:
        saveSamplingConfig(request.parameters, true);
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
    case ui::UiAction::ShowWaveformEmbedded:
    case ui::UiAction::ShowWaveformDetached:
    case ui::UiAction::BrowseProtocolWaveform:
    case ui::UiAction::BrowseProtocolOutput:
        refreshWaveformViewWithAutoAnalysis();
        break;
    case ui::UiAction::ImportWaveform:
    case ui::UiAction::AnalyzeProtocol:
        analyzeCurrentTrace();
        break;
    case ui::UiAction::ClearWaveform:
        logInfo(QStringLiteral("Waveform"), QStringLiteral("波形显示已切回当前任务固定文件视图。"));
        refreshWaveformView();
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
    currentRunProgress_ = 0;
    currentStopProgress_ = 0;
    setRunSummaryFromCurrentState(QStringLiteral("运行摘要"), currentRunProgress_, currentStopProgress_);
    updateRunButtonText();
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
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../resources")),
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
            resourcePackReady_ = configureDebugServiceAccess(profile, result.resourceRootPath);
            if (window_ != nullptr) {
                window_->setConnectionProfileDetails(
                    debugProfile_.profileName,
                    profile.host,
                    profile.tclPort,
                    profile.gdbPort,
                    profile.jtagKhz,
                    addressText(profile.ramBaseAddress),
                    profile.resetStrategy,
                    resourcePackReady_
                        ? QStringLiteral("调试器: 已加载自研服务")
                        : QStringLiteral("调试器: 自研服务不可用"));
            }
            logInfo(QStringLiteral("Resources"), QStringLiteral("已加载 %1 profile: %2").arg(modeText, profile.profileId));
            return;
        }

        logWarning(QStringLiteral("Resources"), QStringLiteral("默认 profile 无法解析: %1").arg(error));
        return;
    }

    resourcePackReady_ = false;
    debugAccess_.reset();
    logWarning(QStringLiteral("Resources"), QStringLiteral("未找到 resources/manifest.json，目标连接、烧写、回读和运行将被阻断。"));
}

void WorkbenchController::startDebugService()
{
    if (!ensureWorkspace()) {
        return;
    }
    if (targetConnectionBusy_) {
        logWarning(QStringLiteral("Target"), QStringLiteral("目标连接正在进行，请等待完成。"));
        return;
    }
    if (!resourcePackReady_) {
        updateConnectionDiagnostics(QStringLiteral("未连接"));
        logError(QStringLiteral("Target"), QStringLiteral("自研片上调试服务未配置或不可执行，不能连接目标。"));
        return;
    }

    targetConnectionBusy_ = true;
    updateConnectionDiagnostics(QStringLiteral("连接中"));
    if (window_ != nullptr) {
        window_->setConnectionSummary(debugProfile_.profileName, QStringLiteral("调试器: 正在连接目标并执行预检"));
    }
    logInfo(QStringLiteral("Target"), QStringLiteral("开始连接目标并执行预检。"));

    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    QThread* const thread = QThread::create([this, debugConfig, profile]() {
        target_control::DebugServiceAccess workerAccess(debugConfig);
        target_control::TargetConnectionService service;
        const target_control::ConnectionRecord connection = service.connectTarget(workerAccess, profile);
        target_control::PrecheckRecord precheck;
        if (connection.state == target_control::ConnectionState::Connected) {
            precheck = service.runPrecheck(workerAccess, profile);
        }

        QMetaObject::invokeMethod(
            this,
            [this, profile, connection, precheck]() {
                targetConnectionBusy_ = false;
                connectionRecord_ = connection;
                precheckRecord_ = precheck;
                hasConnection_ = (connectionRecord_.state == target_control::ConnectionState::Connected);
                hasPrecheck_ = hasConnection_ && (precheckRecord_.state == target_control::PrecheckState::Passed);

                if (hasConnection_) {
                    if (target_control::DebugServiceAccess* const serviceAccess =
                            dynamic_cast<target_control::DebugServiceAccess*>(debugAccess_.get())) {
                        serviceAccess->assumeConnected(profile);
                    }
                } else {
                    precheckRecord_ = target_control::PrecheckRecord();
                }

                const QString statusText = QStringLiteral("调试器: %1 / 预检: %2")
                    .arg(connectionStateText(connectionRecord_.state), precheckStateText(precheckRecord_.state));
                if (window_ != nullptr) {
                    window_->setConnectionSummary(debugProfile_.profileName, statusText);
                }
                updateConnectionDiagnostics(hasConnection_ ? QStringLiteral("已连接") : QStringLiteral("未连接"));

                if (hasTask_) {
                    flowState_ = workflow_.recordStageResult(
                        flowState_,
                        workflow::Stage::Connection,
                        hasPrecheck_ ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
                        statusText,
                        QJsonObject());
                }

                if (hasConnection_) {
                    logInfo(QStringLiteral("Target"), statusText);
                } else {
                    logError(QStringLiteral("Target"), QStringLiteral("目标连接失败: %1").arg(connectionRecord_.errorMessage));
                }
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    updateTopStatus();
}

void WorkbenchController::stopDebugService()
{
    if (targetConnectionBusy_) {
        logWarning(QStringLiteral("Target"), QStringLiteral("目标连接仍在进行，等待连接请求返回后再停止。"));
        return;
    }
    if (!resourcePackReady_) {
        connectionRecord_ = target_control::ConnectionRecord();
        hasConnection_ = false;
        hasPrecheck_ = false;
        precheckRecord_ = target_control::PrecheckRecord();
        updateConnectionDiagnostics(QStringLiteral("已断开"));
        if (window_ != nullptr) {
            window_->setConnectionSummary(debugProfile_.profileName, QStringLiteral("调试器: 已停止"));
        }
        logInfo(QStringLiteral("Target"), QStringLiteral("调试服务已停止"));
        updateTopStatus();
        return;
    }

    targetConnectionBusy_ = true;
    updateConnectionDiagnostics(QStringLiteral("断开中"));
    if (window_ != nullptr) {
        window_->setConnectionSummary(debugProfile_.profileName, QStringLiteral("调试器: 正在断开"));
    }
    logInfo(QStringLiteral("Target"), QStringLiteral("开始断开片上调试服务。"));

    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    QThread* const thread = QThread::create([this, debugConfig, profile]() {
        target_control::DebugServiceAccess workerAccess(debugConfig);
        workerAccess.assumeConnected(profile);
        target_control::TargetConnectionService service;
        const target_control::ConnectionRecord disconnectRecord = service.disconnectTarget(workerAccess);

        QMetaObject::invokeMethod(
            this,
            [this, disconnectRecord]() {
                targetConnectionBusy_ = false;
                connectionRecord_ = disconnectRecord;
                hasConnection_ = false;
                hasPrecheck_ = false;
                precheckRecord_ = target_control::PrecheckRecord();
                if (target_control::DebugServiceAccess* const serviceAccess =
                        dynamic_cast<target_control::DebugServiceAccess*>(debugAccess_.get())) {
                    serviceAccess->assumeDisconnected();
                }

                updateConnectionDiagnostics(connectionRecord_.state == target_control::ConnectionState::Failed
                    ? QStringLiteral("未连接")
                    : QStringLiteral("已断开"));
                if (window_ != nullptr) {
                    window_->setConnectionSummary(debugProfile_.profileName, QStringLiteral("调试器: 已停止"));
                }
                if (connectionRecord_.state == target_control::ConnectionState::Failed) {
                    logError(QStringLiteral("Target"), QStringLiteral("调试服务断开失败: %1").arg(connectionRecord_.errorMessage));
                } else {
                    logInfo(QStringLiteral("Target"), QStringLiteral("调试服务已停止"));
                }
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    updateTopStatus();
}

void WorkbenchController::refreshSerialPorts()
{
    serialPorts_.clear();
    QStringList displayNames;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& info : ports) {
        const QString portName = info.portName().trimmed();
        if (!portName.isEmpty()) {
            serialPorts_.append(portName);
            displayNames.append(serialDisplayName(info));
        }
    }

    const QString statusText = serialPorts_.isEmpty()
        ? QStringLiteral("未发现可用串口。")
        : QStringLiteral("发现 %1 个串口。").arg(serialPorts_.size());
    if (window_ != nullptr) {
        window_->setSerialPorts(displayNames, serialPorts_, statusText);
    }
    logInfo(QStringLiteral("Serial"), statusText);
}

void WorkbenchController::toggleSerialMonitor()
{
    if (serialPort_ == nullptr || window_ == nullptr) {
        return;
    }

    if (serialPort_->isOpen()) {
        const QString portName = serialPort_->portName();
        serialPort_->close();
        serialOpen_ = false;
        window_->setSerialStatus(QStringLiteral("串口已关闭: %1").arg(portName), false);
        logInfo(QStringLiteral("Serial"), QStringLiteral("串口已关闭: %1").arg(portName));
        return;
    }

    QString portName = window_->selectedSerialPortName();
    if (portName.isEmpty() || !serialPorts_.contains(portName, Qt::CaseInsensitive)) {
        window_->setSerialStatus(QStringLiteral("请先刷新串口列表。"), false);
        logWarning(QStringLiteral("Serial"), QStringLiteral("串口列表未刷新或没有可打开串口。"));
        return;
    }

    serialPort_->setPortName(portName);
    serialPort_->setBaudRate(window_->selectedSerialBaudRate());
    serialPort_->setDataBits(QSerialPort::Data8);
    serialPort_->setParity(QSerialPort::NoParity);
    serialPort_->setStopBits(QSerialPort::OneStop);
    serialPort_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serialPort_->open(QIODevice::ReadWrite)) {
        const QString message = serialPort_->errorString();
        serialOpen_ = false;
        window_->setSerialStatus(QStringLiteral("串口打开失败: %1").arg(message), false);
        logError(QStringLiteral("Serial"), QStringLiteral("串口打开失败: %1").arg(message));
        return;
    }

    serialOpen_ = true;
    const QString message = QStringLiteral("串口已打开: %1 @ %2")
        .arg(portName)
        .arg(serialPort_->baudRate());
    window_->setSerialStatus(message, true);
    logInfo(QStringLiteral("Serial"), message);
}

void WorkbenchController::clearSerialOutput()
{
    if (window_ != nullptr) {
        window_->setSerialPlaceholderText(QString());
    }
    logInfo(QStringLiteral("Serial"), QStringLiteral("串口输出窗口已清空。"));
}

void WorkbenchController::sendSerialData(const QString& text)
{
    const QString trimmedText = text;
    if (trimmedText.isEmpty()) {
        logWarning(QStringLiteral("Serial"), QStringLiteral("串口发送内容为空。"));
        return;
    }
    if (serialPort_ == nullptr || !serialPort_->isOpen()) {
        logError(QStringLiteral("Serial"), QStringLiteral("串口未打开，无法发送。"));
        return;
    }

    const QByteArray payload = trimmedText.toUtf8();
    const qint64 written = serialPort_->write(payload);
    if (written != payload.size()) {
        logError(QStringLiteral("Serial"), QStringLiteral("串口发送失败: %1").arg(serialPort_->errorString()));
        return;
    }
    if (!serialPort_->flush()) {
        logWarning(QStringLiteral("Serial"), QStringLiteral("串口发送已排队，等待驱动刷新。"));
    }
    if (window_ != nullptr) {
        window_->appendLog(
            ui::LogChannel::Serial,
            ui::LogLevel::Info,
            QStringLiteral("TX"),
            trimmedText);
    }
}

void WorkbenchController::saveSamplingConfig(const QVariantMap& parameters, const bool requestHardwareSend)
{
    if (!ensureTask()) {
        return;
    }

    const int sampleCount = parameters.value(QStringLiteral("sample_count")).toInt();
    const int pretrigger = parameters.value(QStringLiteral("pretrigger")).toInt();
    const int posttrigger = parameters.value(QStringLiteral("posttrigger")).toInt();
    const int triggerCount = parameters.value(QStringLiteral("trigger_count")).toInt();
    const int postAfterTrigger = parameters.value(QStringLiteral("post_after_trigger")).toInt();
    const int sampleWordBits = parameters.value(QStringLiteral("sample_word_bits")).toInt();
    if (sampleCount != kSamplingSampleCount ||
        pretrigger != kSamplingPretrigger ||
        posttrigger != kSamplingPosttrigger ||
        triggerCount != kSamplingTriggerCount ||
        postAfterTrigger != kSamplingPostAfterTrigger ||
        sampleWordBits != kSamplingSampleWordBits) {
        logError(
            QStringLiteral("Sampling"),
            QStringLiteral("采样窗口参数与当前硬件不匹配: sample_count=%1 pretrigger=%2 posttrigger=%3 trigger_count=%4 post_after_trigger=%5 sample_word_bits=%6")
                .arg(sampleCount)
                .arg(pretrigger)
                .arg(posttrigger)
                .arg(triggerCount)
                .arg(postAfterTrigger)
                .arg(sampleWordBits));
        return;
    }

    quint64 triggerAddress = 0U;
    const QString triggerAddressText = parameters.value(QStringLiteral("trigger_addr")).toString().trimmed();
    if (!parseAddressText(triggerAddressText, &triggerAddress)) {
        logError(QStringLiteral("Sampling"), QStringLiteral("触发地址无效: %1").arg(triggerAddressText));
        return;
    }

    const bool mismatchEnable = parameters.value(QStringLiteral("mismatch_enable")).toBool();
    const int mismatchMask = parameters.value(QStringLiteral("mismatch_mask")).toInt() & kSamplingMismatchMask;
    const int triggerMask = mismatchEnable ? mismatchMask : 0;

    QJsonObject addressTrigger;
    addressTrigger.insert(QStringLiteral("valid"), true);
    addressTrigger.insert(QStringLiteral("ready"), true);
    addressTrigger.insert(QStringLiteral("addr"), addressText(triggerAddress));

    QJsonObject mismatchTrigger;
    mismatchTrigger.insert(QStringLiteral("enable"), mismatchEnable);
    mismatchTrigger.insert(QStringLiteral("mask"), QStringLiteral("0x%1").arg(mismatchMask, 0, 16));
    mismatchTrigger.insert(QStringLiteral("source"), QStringLiteral("mismatch[4:0]"));
    mismatchTrigger.insert(QStringLiteral("edge"), QStringLiteral("rise"));

    QJsonObject hardwareProtocol;
    hardwareProtocol.insert(QStringLiteral("trigger_mask"), QStringLiteral("0x%1").arg(triggerMask, 0, 16));
    hardwareProtocol.insert(QStringLiteral("trigger_value"), addressText(triggerAddress));
    hardwareProtocol.insert(QStringLiteral("trigger_edge_rise"), mismatchEnable ? 1 : 0);
    hardwareProtocol.insert(QStringLiteral("trigger_edge_fall"), 0);

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("sample_count"), sampleCount);
    root.insert(QStringLiteral("pretrigger"), pretrigger);
    root.insert(QStringLiteral("posttrigger"), posttrigger);
    root.insert(QStringLiteral("trigger_count"), triggerCount);
    root.insert(QStringLiteral("post_after_trigger"), postAfterTrigger);
    root.insert(QStringLiteral("sample_word_bits"), sampleWordBits);
    root.insert(QStringLiteral("trigger_logic"), QStringLiteral("valid_ready_addr_or_mismatch_rise"));
    root.insert(QStringLiteral("addr_trigger"), addressTrigger);
    root.insert(QStringLiteral("mismatch_trigger"), mismatchTrigger);
    root.insert(QStringLiteral("hardware_protocol"), hardwareProtocol);
    root.insert(QStringLiteral("created_at"), currentTimeText());

    const QString configPath = currentTask_.paths.samplingConfigPath;
    if (!QDir().mkpath(QFileInfo(configPath).absolutePath())) {
        logError(QStringLiteral("Sampling"), QStringLiteral("无法创建采样配置目录: %1").arg(QFileInfo(configPath).absolutePath()));
        return;
    }

    QSaveFile file(configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        logError(QStringLiteral("Sampling"), QStringLiteral("无法写入采样配置: %1").arg(configPath));
        return;
    }
    const QJsonDocument document(root);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        logError(QStringLiteral("Sampling"), QStringLiteral("采样配置写入不完整: %1").arg(configPath));
        return;
    }
    if (!file.commit()) {
        logError(QStringLiteral("Sampling"), QStringLiteral("采样配置提交失败: %1").arg(configPath));
        return;
    }

    workspace::TaskInputSet inputs = currentTask_.inputs;
    inputs.resourceSnapshot = resourceSnapshotJson();
    inputs.samplingConfig.id = QStringLiteral("input_%1").arg(currentTimeText());
    inputs.samplingConfig.relativePath = QDir(currentTask_.paths.taskRootPath).relativeFilePath(configPath);
    inputs.samplingConfig.originalFileName = QFileInfo(configPath).fileName();
    inputs.samplingConfig.sha256 = fileSha256Text(configPath);
    inputs.samplingConfig.sizeBytes = QFileInfo(configPath).size();
    inputs.samplingConfig.importedAt = currentTimeText();

    workspace::TaskContext updated;
    QString error;
    if (!workspace_.saveTaskInputs(workspaceMode(), currentTask_.summary.taskId, inputs, &updated, &error)) {
        logError(QStringLiteral("Sampling"), QStringLiteral("保存采样配置索引失败: %1").arg(error));
        return;
    }

    currentTask_ = updated;
    selectedTaskId_ = currentTask_.summary.taskId;
    logInfo(
        QStringLiteral("Sampling"),
        QStringLiteral("采样配置已保存: addr=%1 mismatch_enable=%2 mismatch_mask=0x%3")
            .arg(addressText(triggerAddress),
                 mismatchEnable ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(mismatchMask, 16)));
    if (requestHardwareSend) {
        logWarning(QStringLiteral("Sampling"), QStringLiteral("采样硬件下发通道尚未接入，已保存配置但未写入硬件寄存器。"));
    }
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
}

void WorkbenchController::updateConnectionDiagnostics(const QString& serviceState)
{
    if (window_ == nullptr) {
        return;
    }

    const QString normalizedServiceState = serviceState;
    const QString precheckText = QStringLiteral("%1 / reset=%2 write=%3 read=%4 run=%5")
        .arg(precheckStateText(precheckRecord_.state),
             precheckRecord_.resetSupported ? QStringLiteral("OK") : QStringLiteral("NO"),
             precheckRecord_.writeSupported ? QStringLiteral("OK") : QStringLiteral("NO"),
             precheckRecord_.readSupported ? QStringLiteral("OK") : QStringLiteral("NO"),
             precheckRecord_.runSupported ? QStringLiteral("OK") : QStringLiteral("NO"));
    const QString errorText = connectionRecord_.errorMessage.isEmpty()
        ? precheckRecord_.errorMessage
        : connectionRecord_.errorMessage;
    const QString rawText = shortRawText(QStringList{
        QStringLiteral("[connection]"),
        connectionRecord_.rawReturn,
        QStringLiteral("[precheck]"),
        precheckRecord_.rawReturn,
    }.join(QLatin1Char('\n')));

    window_->setConnectionDiagnostics(
        normalizedServiceState,
        QString(),
        precheckText,
        QString(),
        QString(),
        QString(),
        errorText,
        rawText);
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
        currentWriteProgress_ = 0;
        currentReadbackProgress_ = 0;
        window_->setRamSummary(QStringLiteral("已选择程序镜像，等待烧录。"), currentWriteProgress_, currentReadbackProgress_);
        logInfo(QStringLiteral("Program"), QStringLiteral("已选择程序镜像: %1").arg(path));
    }
}

void WorkbenchController::beginHardwareOperation(
    const target_control::ProgramOperation operation,
    const QString& name)
{
    hardwareOperationBusy_ = true;
    hardwareOperation_ = operation;
    hardwareOperationName_ = name;
    if (operation == target_control::ProgramOperation::Write) {
        currentWriteProgress_ = 0;
        currentReadbackProgress_ = 0;
    } else if (operation == target_control::ProgramOperation::Readback) {
        currentReadbackProgress_ = 0;
    } else if (operation == target_control::ProgramOperation::Run) {
        currentRunProgress_ = 0;
        runOutputConfirmed_ = false;
    } else if (operation == target_control::ProgramOperation::Halt) {
        currentStopProgress_ = 0;
    }
}

void WorkbenchController::endHardwareOperation()
{
    hardwareOperationBusy_ = false;
    hardwareOperationName_.clear();
}

void WorkbenchController::updateWriteOperationProgress(
    const quint64 completedBytes,
    const quint64 totalBytes,
    const QString& message)
{
    const int mappedProgress = dataProgressPercent(completedBytes, totalBytes);
    currentWriteProgress_ = qMax(currentWriteProgress_, mappedProgress);
    setRamSummaryFromCurrentState(
        QStringLiteral("烧录执行中: %1/%2 bytes, %3")
            .arg(QString::number(completedBytes), QString::number(totalBytes), message));
}

void WorkbenchController::updateReadbackOperationProgress(
    const quint64 completedBytes,
    const quint64 totalBytes,
    const QString& message)
{
    const int mappedProgress = dataProgressPercent(completedBytes, totalBytes);
    currentReadbackProgress_ = qMax(currentReadbackProgress_, mappedProgress);
    setRamSummaryFromCurrentState(
        QStringLiteral("回读执行中: %1/%2 bytes, %3")
            .arg(QString::number(completedBytes), QString::number(totalBytes), message));
}

void WorkbenchController::programImage()
{
    if (hardwareOperationBusy_) {
        logWarning(QStringLiteral("Program"), QStringLiteral("%1 正在执行，请等待完成。").arg(hardwareOperationName_));
        return;
    }
    if (!ensureTask()) {
        return;
    }
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Program"), QStringLiteral("目标尚未连接或预检未通过，不能烧写。"));
        return;
    }
    target_control::DebugAccess* const access = debugAccess();
    if (access == nullptr) {
        logError(QStringLiteral("Program"), QStringLiteral("自研片上调试服务未配置，不能烧写。"));
        return;
    }
    if (window_ == nullptr || window_->programImagePath().isEmpty()) {
        logError(QStringLiteral("Program"), QStringLiteral("尚未选择程序镜像。"));
        return;
    }

    QElapsedTimer writeTimer;
    writeTimer.start();
    logInfo(QStringLiteral("Program"), QStringLiteral("烧录开始。"));

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
    quint64 writeBytes = 0U;
    for (const target_control::ImageSegment& segment : imageInfo_.segments) {
        writeBytes += static_cast<quint64>(segment.data.size());
    }
    window_->setRamSummary(
        progressTitle(
            QStringLiteral("烧写执行中"),
            progress,
            QStringLiteral("正在写入 %1 个镜像段，共 %2 字节；若底层调试服务无返回，将按超时失败处理。")
                .arg(imageInfo_.segments.size())
                .arg(writeBytes)),
        progress.percent,
        0);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    hasVerifyRecord_ = false;
    verifyRecord_ = target_control::ReadbackVerifyRecord();
    beginHardwareOperation(target_control::ProgramOperation::Write, QStringLiteral("程序烧录"));
    currentWriteProgress_ = qMax(currentWriteProgress_, progress.percent);
    currentReadbackProgress_ = 0;
    const target_control::ProgramImageInfo image = imageInfo_;
    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    const QString taskId = currentTask_.summary.taskId;
    QThread* const thread = QThread::create([this, debugConfig, profile, image, taskId]() {
        WriteWorkerResult result;
        QElapsedTimer timer;
        timer.start();
        target_control::DebugServiceAccess workerAccess(debugConfig);
        const target_control::DebugResult connectResult = workerAccess.connectTarget(profile);
        if (!connectResult.success) {
            result.connectionError = connectResult.errorMessage;
            result.record.taskId = taskId;
            result.record.errorMessage = QStringLiteral("目标连接确认失败: %1").arg(connectResult.errorMessage);
        } else {
            target_control::ProgramController controller;
            const target_control::OperationProgressCallback callback =
                [this](const quint64 completedBytes, const quint64 totalBytes, const QString& message) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, completedBytes, totalBytes, message]() {
                            updateWriteOperationProgress(completedBytes, totalBytes, message);
                        },
                        Qt::QueuedConnection);
                };
            result.record = controller.programTarget(workerAccess, taskId, image, callback);
        }
        result.elapsedMs = timer.elapsed();
        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                writeRecord_ = result.record;
                hasWriteRecord_ = writeRecord_.success ||
                    !writeRecord_.rawReturn.isEmpty() ||
                    !writeRecord_.errorMessage.isEmpty();
                currentWriteProgress_ = writeRecord_.success ? 100 : 100;

                QString error;
                QString relativePath;
                if (!writeEvidenceJson(QString::fromLatin1(kProgramWriteRecordName), writeRecordToJson(writeRecord_), &relativePath, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("鐑у啓璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(
                        target_control::ProgramOperation::Write,
                        writeRecord_.success ? target_control::OperationStage::PersistWriteRecord : target_control::OperationStage::Failed);
                if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("鐑у綍杩涘害璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }

                flowState_ = workflow_.recordStageResult(
                    flowState_,
                    workflow::Stage::ProgramWrite,
                    writeRecord_.success ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
                    writeRecord_.success ? QStringLiteral("鐑у啓鎴愬姛") : writeRecord_.errorMessage,
                    imageToJson(imageInfo_));

                if (writeRecord_.success) {
                    logInfo(QStringLiteral("Program"), QStringLiteral("烧录成功。"));
                } else {
                    logError(QStringLiteral("Program"), QStringLiteral("烧录失败: %1").arg(writeRecord_.errorMessage));
                }
                logInfo(QStringLiteral("Program"), QStringLiteral("烧录开销: %1 ms").arg(result.elapsedMs));
                endHardwareOperation();
                setRamSummaryFromCurrentState(writeRecord_.success ? QStringLiteral("烧录完成") : QStringLiteral("烧录失败"));
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void WorkbenchController::verifyReadback()
{
    if (hardwareOperationBusy_) {
        logWarning(QStringLiteral("Readback"), QStringLiteral("%1 正在执行，请等待完成。").arg(hardwareOperationName_));
        return;
    }
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Readback"), QStringLiteral("目标尚未连接或预检未通过，不能回读校验。"));
        return;
    }
    if (!hasImage_ || !hasWriteRecord_ || !writeRecord_.success) {
        logError(QStringLiteral("Readback"), QStringLiteral("尚未完成有效烧写，不能回读校验。"));
        return;
    }
    target_control::DebugAccess* const access = debugAccess();
    if (access == nullptr) {
        logError(QStringLiteral("Readback"), QStringLiteral("自研片上调试服务未配置，不能回读校验。"));
        return;
    }

    logInfo(QStringLiteral("Readback"), QStringLiteral("回读开始。"));

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

    beginHardwareOperation(target_control::ProgramOperation::Readback, QStringLiteral("回读校验"));
    currentReadbackProgress_ = qMax(currentReadbackProgress_, progress.percent);
    const target_control::ProgramImageInfo image = imageInfo_;
    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    const QString taskId = currentTask_.summary.taskId;
    QThread* const thread = QThread::create([this, debugConfig, profile, image, taskId]() {
        ReadbackWorkerResult result;
        QElapsedTimer timer;
        timer.start();
        target_control::DebugServiceAccess workerAccess(debugConfig);
        const target_control::DebugResult connectResult = workerAccess.connectTarget(profile);
        if (!connectResult.success) {
            result.connectionError = connectResult.errorMessage;
            result.record.taskId = taskId;
            result.record.state = target_control::VerifyState::Failed;
            result.record.errorMessage = QStringLiteral("目标连接确认失败: %1").arg(connectResult.errorMessage);
        } else {
            target_control::ProgramController controller;
            const target_control::OperationProgressCallback callback =
                [this](const quint64 completedBytes, const quint64 totalBytes, const QString& message) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, completedBytes, totalBytes, message]() {
                            updateReadbackOperationProgress(completedBytes, totalBytes, message);
                        },
                        Qt::QueuedConnection);
                };
            result.record = controller.verifyReadback(workerAccess, taskId, image, callback);
        }
        result.elapsedMs = timer.elapsed();
        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                verifyRecord_ = result.record;
                hasVerifyRecord_ = true;
                currentReadbackProgress_ = 100;

                QString error;
                QString relativePath;
                if (!writeEvidenceJson(QString::fromLatin1(kReadbackVerifyRecordName), verifyRecordToJson(verifyRecord_), &relativePath, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("鍥炶璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(target_control::ProgramOperation::Readback, target_control::OperationStage::PersistVerifyRecord);
                if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("鍥炶杩涘害璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }

                const bool passed = verifyRecord_.state == target_control::VerifyState::Passed;
                flowState_ = workflow_.recordStageResult(
                    flowState_,
                    workflow::Stage::ReadbackVerify,
                    passed ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
                    passed ? QStringLiteral("鍥炶鏍￠獙閫氳繃") : verifyRecord_.errorMessage,
                    verifyRecordToJson(verifyRecord_));
                if (passed) {
                    logInfo(QStringLiteral("Readback"), QStringLiteral("回读校验通过。"));
                } else {
                    logError(QStringLiteral("Readback"), QStringLiteral("回读校验失败: %1").arg(verifyRecord_.errorMessage));
                }
                logInfo(QStringLiteral("Readback"), QStringLiteral("回读开销: %1 ms").arg(result.elapsedMs));
                endHardwareOperation();
                setRamSummaryFromCurrentState(passed ? QStringLiteral("回读校验完成") : QStringLiteral("回读校验失败"));
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void WorkbenchController::runProgram()
{
    if (hardwareOperationBusy_) {
        logWarning(QStringLiteral("Run"), QStringLiteral("%1 正在执行，请等待完成。").arg(hardwareOperationName_));
        return;
    }
    if (!hasConnection_ || !hasPrecheck_) {
        logError(QStringLiteral("Run"), QStringLiteral("目标尚未连接或预检未通过，不能运行程序。"));
        return;
    }
    if (!hasVerifyRecord_ || verifyRecord_.state != target_control::VerifyState::Passed) {
        logError(QStringLiteral("Run"), QStringLiteral("回读校验未通过，禁止运行。"));
        return;
    }
    if (!serialOpen_) {
        logError(QStringLiteral("Run"), QStringLiteral("串口未打开，无法采集程序输出；请先在串口监控中打开板卡 UART 串口。"));
        return;
    }
    target_control::DebugAccess* const access = debugAccess();
    if (access == nullptr) {
        logError(QStringLiteral("Run"), QStringLiteral("自研片上调试服务未配置，不能运行程序。"));
        return;
    }

    beginHardwareOperation(target_control::ProgramOperation::Run, QStringLiteral("程序运行"));
    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::CheckRunGate);
    const bool resetBeforeRun = hasHaltRecord_ && haltRecord_.state == target_control::RunState::Halted;
    hasHaltRecord_ = false;
    haltRecord_ = target_control::RunControlRecord();
    runSerialOutput_.clear();
    latestRunInstruction_ = resetBeforeRun ? QStringLiteral("复位并运行") : QStringLiteral("运行");
    runOutputConfirmed_ = false;
    runSerialCaptureActive_ = true;
    currentStopProgress_ = 0;
    updateRunButtonText();
    currentRunProgress_ = progress.percent;
    setRunSummaryFromCurrentState(QStringLiteral("运行准备中 - ") + progress.message, currentRunProgress_, currentStopProgress_);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::DispatchRun);
    currentRunProgress_ = progress.percent;
    setRunSummaryFromCurrentState(QStringLiteral("运行命令已发送 - ") + progress.message, currentRunProgress_, currentStopProgress_);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    const target_control::ProgramImageInfo image = imageInfo_;
    const target_control::ReadbackVerifyRecord verifyRecord = verifyRecord_;
    const QString taskId = currentTask_.summary.taskId;
    QThread* const thread = QThread::create([this, debugConfig, profile, image, verifyRecord, taskId, resetBeforeRun]() {
        RunWorkerResult result;
        QElapsedTimer timer;
        timer.start();
        target_control::DebugServiceAccess workerAccess(debugConfig);
        const target_control::DebugResult connectResult = workerAccess.connectTarget(profile);
        if (!connectResult.success) {
            result.connectionError = connectResult.errorMessage;
            result.record.operation = target_control::ProgramOperation::Run;
            result.record.taskId = taskId;
            result.record.entryAddress = image.entryAddress;
            result.record.state = target_control::RunState::Failed;
            result.record.errorMessage = QStringLiteral("目标连接确认失败: %1").arg(connectResult.errorMessage);
        } else {
            target_control::ProgramController controller;
            if (resetBeforeRun) {
                const target_control::DebugResult resetResult = workerAccess.reset(profile.resetStrategy);
                if (!resetResult.success) {
                    result.record.operation = target_control::ProgramOperation::Run;
                    result.record.taskId = taskId;
                    result.record.entryAddress = image.entryAddress;
                    result.record.state = target_control::RunState::Failed;
                    result.record.rawReturn = resetResult.rawReturn;
                    result.record.errorMessage = QStringLiteral("目标复位失败: %1").arg(resetResult.errorMessage);
                } else {
                    result.record = controller.runTarget(workerAccess, taskId, image, verifyRecord);
                }
            } else {
                result.record = controller.runTarget(workerAccess, taskId, image, verifyRecord);
            }
        }
        result.elapsedMs = timer.elapsed();

        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                target_control::OperationProgress progress =
                    target_control::makeOperationProgress(target_control::ProgramOperation::Run, target_control::OperationStage::CaptureRunStatus);
                currentRunProgress_ = qMax(currentRunProgress_, progress.percent);
                setRunSummaryFromCurrentState(QStringLiteral("运行返回确认中 - ") + progress.message, currentRunProgress_, currentStopProgress_);

                runRecord_ = result.record;
                hasRunRecord_ = true;

                QString error;
                QString relativePath;
                if (!writeEvidenceJson(QString::fromLatin1(kRunControlRecordName), runRecordToJson(runRecord_), &relativePath, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("运行证据写入失败: %1").arg(error));
                }
                const bool running = runRecord_.state == target_control::RunState::Running;
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(
                        target_control::ProgramOperation::Run,
                        running ? target_control::OperationStage::PersistRunRecord : target_control::OperationStage::Failed);
                if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("运行进度证据写入失败: %1").arg(error));
                }

                flowState_ = workflow_.recordStageResult(
                    flowState_,
                    workflow::Stage::RunControl,
                    running ? workflow::StageStatus::Passed : workflow::StageStatus::Failed,
                    running ? QStringLiteral("运行命令已返回，等待串口输出确认") : runRecord_.errorMessage,
                    runRecordToJson(runRecord_));
                if (running) {
                    currentRunProgress_ = qMin(currentRunProgress_, 85);
                    logInfo(QStringLiteral("Run"), QStringLiteral("运行命令返回成功: %1").arg(debugReturnSummary(runRecord_.rawReturn)));
                    if (!serialOpen_) {
                        logWarning(QStringLiteral("Run"), QStringLiteral("串口未打开，无法采集程序输出，不能确认程序是否正确运行。"));
                    } else {
                        logInfo(QStringLiteral("Run"), QStringLiteral("等待串口输出确认程序运行结果。"));
                    }
                } else {
                    runSerialCaptureActive_ = false;
                    logError(QStringLiteral("Run"), QStringLiteral("程序运行失败: %1").arg(runRecord_.errorMessage));
                }
                logInfo(QStringLiteral("Run"), QStringLiteral("运行控制开销: %1 ms").arg(result.elapsedMs));
                endHardwareOperation();
                setRunSummaryFromCurrentState(running ? QStringLiteral("运行控制完成") : QStringLiteral("运行控制失败"), currentRunProgress_, currentStopProgress_);
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    updateTopStatus();
}

void WorkbenchController::stopProgram()
{
    if (hardwareOperationBusy_) {
        logWarning(QStringLiteral("Run"), QStringLiteral("%1 正在执行，请等待完成。").arg(hardwareOperationName_));
        return;
    }
    if (!ensureTask()) {
        return;
    }
    if (!hasConnection_) {
        logError(QStringLiteral("Run"), QStringLiteral("目标尚未连接，不能发送终止命令。"));
        return;
    }
    target_control::DebugAccess* const access = debugAccess();
    if (access == nullptr) {
        logError(QStringLiteral("Run"), QStringLiteral("自研片上调试服务未配置，不能发送终止命令。"));
        return;
    }

    beginHardwareOperation(target_control::ProgramOperation::Halt, QStringLiteral("程序终止"));
    latestRunInstruction_ = QStringLiteral("终止");
    target_control::OperationProgress progress =
        target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::CheckHaltAccess);
    currentStopProgress_ = progress.percent;
    setRunSummaryFromCurrentState(QStringLiteral("终止准备中 - ") + progress.message, currentRunProgress_, currentStopProgress_);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    progress = target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::DispatchHalt);
    currentStopProgress_ = progress.percent;
    setRunSummaryFromCurrentState(QStringLiteral("终止命令已发送 - ") + progress.message, currentRunProgress_, currentStopProgress_);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    const QString taskId = currentTask_.summary.taskId;
    QThread* const thread = QThread::create([this, debugConfig, profile, taskId]() {
        RunWorkerResult result;
        QElapsedTimer timer;
        timer.start();
        target_control::DebugServiceAccess workerAccess(debugConfig);
        const target_control::DebugResult connectResult = workerAccess.connectTarget(profile);
        if (!connectResult.success) {
            result.connectionError = connectResult.errorMessage;
            result.record.operation = target_control::ProgramOperation::Halt;
            result.record.taskId = taskId;
            result.record.state = target_control::RunState::Failed;
            result.record.errorMessage = QStringLiteral("目标连接确认失败: %1").arg(connectResult.errorMessage);
        } else {
            target_control::ProgramController controller;
            result.record = controller.haltTarget(workerAccess, taskId);
        }
        result.elapsedMs = timer.elapsed();

        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                target_control::OperationProgress progress =
                    target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::CaptureHaltStatus);
                currentStopProgress_ = qMax(currentStopProgress_, progress.percent);
                setRunSummaryFromCurrentState(QStringLiteral("终止返回确认中 - ") + progress.message, currentRunProgress_, currentStopProgress_);

                haltRecord_ = result.record;
                hasHaltRecord_ = true;

                QString error;
                QString relativePath;
                if (!writeEvidenceJson(QString::fromLatin1(kHaltControlRecordName), runRecordToJson(haltRecord_), &relativePath, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("终止证据写入失败: %1").arg(error));
                }
                const bool halted = haltRecord_.state == target_control::RunState::Halted;
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(
                        target_control::ProgramOperation::Halt,
                        halted ? target_control::OperationStage::PersistHaltRecord : target_control::OperationStage::Failed);
                if (!writeEvidenceJson(QString::fromLatin1(kProgramOperationProgressName), progressToJson(persistedProgress), nullptr, &error)) {
                    logWarning(QStringLiteral("Evidence"), QStringLiteral("终止进度证据写入失败: %1").arg(error));
                }

                if (halted) {
                    currentStopProgress_ = 100;
                    runSerialCaptureActive_ = false;
                    logInfo(QStringLiteral("Run"), QStringLiteral("程序已终止: %1").arg(debugReturnSummary(haltRecord_.rawReturn)));
                    updateRunButtonText();
                } else {
                    logWarning(QStringLiteral("Run"), QStringLiteral("终止请求失败: %1").arg(haltRecord_.errorMessage));
                }
                logInfo(QStringLiteral("Run"), QStringLiteral("终止控制开销: %1 ms").arg(result.elapsedMs));
                endHardwareOperation();
                setRunSummaryFromCurrentState(halted ? QStringLiteral("终止控制完成") : QStringLiteral("终止控制失败"), currentRunProgress_, currentStopProgress_);
                updateTopStatus();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    input.optionalRecords.vcdWaveform =
        QFileInfo::exists(QDir(currentTask_.paths.taskRootPath).filePath(protocol_analyzer::fixedWaveformRelativePath()))
        ? reporting::OptionalRecordState::Available
        : reporting::OptionalRecordState::NotAvailable;
    input.optionalRecords.protocolAnalysis =
        QFileInfo::exists(QDir(currentTask_.paths.taskRootPath).filePath(protocol_analyzer::fixedTraceAnalysisRelativePath()))
        ? reporting::OptionalRecordState::Available
        : reporting::OptionalRecordState::NotAvailable;
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
    setRunSummaryFromCurrentState(QStringLiteral("运行摘要"), currentRunProgress_, currentStopProgress_);
}

void WorkbenchController::refreshWaveformView()
{
    if (!ensureTask()) {
        return;
    }

    const waveform_viewer::WaveformViewModel model = waveformViewer_.loadTask(currentTask_.paths.taskRootPath);
    const QString statusText = model.analysisStale
        ? QStringLiteral("%1, analysis_stale").arg(model.status)
        : model.status;
    if (window_ != nullptr) {
        window_->setWaveformTraceView(
            statusText,
            model.vcdPath,
            model.timeRangeText,
            traceGroupsToUi(model.groups),
            model.keyBehaviors,
            model.diagnostics);
        window_->setProtocolAnalysisView(
            statusText,
            model.analysisPath,
            model.keyBehaviors,
            model.diagnostics);
    }

    if (!model.hasVcd) {
        logWarning(QStringLiteral("Waveform"), QStringLiteral("当前任务未生成固定 VCD: %1").arg(model.vcdPath));
    } else if (!model.hasAnalysis || model.analysisStale) {
        logWarning(QStringLiteral("Waveform"), QStringLiteral("协议解析结果缺失或过期，可点击“重新解析”。"));
    } else {
        logInfo(QStringLiteral("Waveform"), QStringLiteral("已读取当前任务波形解析结果: %1").arg(model.analysisPath));
    }
}

void WorkbenchController::refreshWaveformViewWithAutoAnalysis()
{
    if (!ensureTask()) {
        return;
    }

    const waveform_viewer::WaveformViewModel model = waveformViewer_.loadTask(currentTask_.paths.taskRootPath);
    if (model.hasVcd && (!model.hasAnalysis || model.analysisStale)) {
        const QString message = model.hasAnalysis
            ? QStringLiteral("analysis 与当前 VCD 不一致，自动重新解析。")
            : QStringLiteral("检测到当前任务固定 VCD，自动生成协议解析结果。");
        logInfo(QStringLiteral("Waveform"), message);
        analyzeCurrentTrace(false);
    }

    refreshWaveformView();
}

void WorkbenchController::analyzeCurrentTrace(const bool refreshAfterAnalysis)
{
    if (!ensureTask()) {
        return;
    }

    protocol_analyzer::ProtocolAnalysisRequest request;
    request.taskRootPath = currentTask_.paths.taskRootPath;
    request.taskId = currentTask_.summary.taskId;
    request.errorRegistry = &errorRegistry_;
    request.reportDiagnosticsToErrorRegistry = true;

    const protocol_analyzer::ProtocolAnalysisResult result = protocolAnalyzer_.analyzeTask(request);
    if (!result.success) {
        logWarning(
            QStringLiteral("Protocol"),
            QStringLiteral("协议解析未完整完成: %1").arg(result.errorMessage.isEmpty() ? result.status : result.errorMessage));
    } else {
        logInfo(QStringLiteral("Protocol"), QStringLiteral("协议解析完成: %1").arg(result.analysisPath));
    }

    const QString vcdPath = QDir(currentTask_.paths.taskRootPath).filePath(protocol_analyzer::fixedWaveformRelativePath());
    const QString schemaPath = QDir(currentTask_.paths.taskRootPath).filePath(protocol_analyzer::fixedTraceSchemaRelativePath());
    const QString analysisPath = QDir(currentTask_.paths.taskRootPath).filePath(protocol_analyzer::fixedTraceAnalysisRelativePath());
    if (QFileInfo::exists(vcdPath)) {
        QJsonObject metadata;
        metadata.insert(QStringLiteral("source_module"), QStringLiteral("M10_ACQUISITION"));
        workspace_.attachArtifact(
            workspaceMode(),
            currentTask_.summary.taskId,
            waveformArtifact(QStringLiteral("lockstep_trace_vcd"), vcdPath, protocol_analyzer::fixedWaveformRelativePath(), metadata),
            nullptr);
    }
    if (QFileInfo::exists(schemaPath)) {
        QJsonObject metadata;
        metadata.insert(QStringLiteral("source_module"), QStringLiteral("M10_ACQUISITION"));
        workspace_.attachArtifact(
            workspaceMode(),
            currentTask_.summary.taskId,
            waveformArtifact(QStringLiteral("lockstep_trace_schema"), schemaPath, protocol_analyzer::fixedTraceSchemaRelativePath(), metadata),
            nullptr);
    }
    if (QFileInfo::exists(analysisPath)) {
        QJsonObject metadata;
        metadata.insert(QStringLiteral("source_module"), QStringLiteral("M12_PROTOCOL_ANALYZER"));
        workspace_.attachArtifact(
            workspaceMode(),
            currentTask_.summary.taskId,
            waveformArtifact(QStringLiteral("lockstep_trace_analysis"), analysisPath, protocol_analyzer::fixedTraceAnalysisRelativePath(), metadata),
            nullptr);
    }

    if (refreshAfterAnalysis) {
        refreshWaveformView();
    }
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
    if (hasRunRecord_ && runRecord_.state == target_control::RunState::Running && runOutputConfirmed_) {
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

    QStringList lines;
    lines.append(title);
    lines.append(QStringLiteral("镜像: %1 / %2 bytes / %3")
                     .arg(imageInfo_.fileName.isEmpty() ? QStringLiteral("未选择") : imageInfo_.fileName,
                          QString::number(imageInfo_.sizeBytes),
                          target_control::toString(imageInfo_.type)));
    if (hasWriteRecord_) {
        lines.append(writeRecord_.success
            ? QStringLiteral("烧录: 成功")
            : QStringLiteral("烧录: 失败，原因: %1").arg(writeRecord_.errorMessage));
    } else {
        lines.append(QStringLiteral("烧录: 未执行"));
    }
    if (hasVerifyRecord_) {
        lines.append(QStringLiteral("回读: %1, diff=%2%3")
                         .arg(target_control::toString(verifyRecord_.state),
                              QString::number(verifyRecord_.diffCount),
                              verifyRecord_.errorMessage.isEmpty()
                                  ? QString()
                                  : QStringLiteral(", 原因: %1").arg(verifyRecord_.errorMessage)));
    } else {
        lines.append(QStringLiteral("回读: 未执行"));
    }
    const QString text = lines.join(QLatin1Char('\n'));

    int writeProgress = qBound(0, currentWriteProgress_, 100);
    int readbackProgress = qBound(0, currentReadbackProgress_, 100);
    window_->setRamSummary(text, writeProgress, readbackProgress);
}

void WorkbenchController::setRunSummaryFromCurrentState(
    const QString& title,
    const int runProgressPercent,
    const int stopProgressPercent)
{
    refreshRunSummary(title, runProgressPercent, stopProgressPercent);
}

void WorkbenchController::appendProgramSerialOutput(const QString& text)
{
    if (!runSerialCaptureActive_ || text.isEmpty()) {
        return;
    }

    runSerialOutput_.append(text);
    constexpr qsizetype kMaxSerialCaptureLength = 8000;
    if (runSerialOutput_.size() > kMaxSerialCaptureLength) {
        runSerialOutput_ = runSerialOutput_.right(kMaxSerialCaptureLength);
        runSerialOutput_.prepend(QStringLiteral("... 串口输出过长，仅显示最新内容\n"));
    }
    if (!runOutputConfirmed_ && hasExpectedRunOutput(runSerialOutput_)) {
        runOutputConfirmed_ = true;
        currentRunProgress_ = 100;
        logInfo(QStringLiteral("Run"), QStringLiteral("已从串口输出确认 Dhrystone 运行结果。"));
        updateTopStatus();
    }
    refreshRunSummary(QStringLiteral("运行摘要更新"), currentRunProgress_, currentStopProgress_);
}

void WorkbenchController::updateRunButtonText()
{
    if (window_ == nullptr) {
        return;
    }

    const bool stopped = hasHaltRecord_ && haltRecord_.state == target_control::RunState::Halted;
    window_->setActionButtonText(
        ui::UiAction::RunProgram,
        stopped ? QStringLiteral("程序复位并运行") : QStringLiteral("程序运行"));
}

void WorkbenchController::refreshRunSummary(
    const QString& title,
    const int runProgressPercent,
    const int stopProgressPercent)
{
    if (window_ == nullptr) {
        return;
    }

    Q_UNUSED(title);

    QStringList lines;
    lines.append(QStringLiteral("镜像: %1").arg(imageInfo_.fileName.isEmpty() ? QStringLiteral("未确定") : imageInfo_.fileName));
    lines.append(QStringLiteral("运行状态: %1")
                     .arg(runUiStateText(
                         hardwareOperationBusy_,
                         hardwareOperation_,
                         hasRunRecord_,
                         runRecord_.state,
                         hasHaltRecord_,
                         haltRecord_.state)));
    lines.append(QStringLiteral("最新指令: %1").arg(latestRunInstruction_.isEmpty() ? QStringLiteral("无") : latestRunInstruction_));
    const QString serialText = runSerialOutput_.trimmed();
    if (!serialText.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("串口输出:"));
        lines.append(serialText);
    }

    window_->setRunSummary(lines.join(QLatin1Char('\n')), runProgressPercent, stopProgressPercent);
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
    currentWriteProgress_ = 0;
    currentReadbackProgress_ = 0;
    currentRunProgress_ = 0;
    currentStopProgress_ = 0;
    runSerialOutput_.clear();
    latestRunInstruction_.clear();
    runOutputConfirmed_ = false;
    runSerialCaptureActive_ = false;
    if (window_ != nullptr) {
        window_->setRamSummary(QStringLiteral("尚未选择程序镜像。"), 0, 0);
        window_->setRunSummary(QStringLiteral("程序运行控制摘要将在这里显示。"), 0, 0);
    }
    updateRunButtonText();
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
    updateRunButtonText();
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

bool WorkbenchController::configureDebugServiceAccess(
    const resources::BoardProfile& profile,
    const QString& resourceRootPath)
{
    const QString servicePath = resolveDebugServicePath(profile, resourceRootPath);
    if (servicePath.isEmpty()) {
        debugConfig_ = target_control::DebugServiceConfig();
        debugAccess_.reset();
        logWarning(
            QStringLiteral("Resources"),
            QStringLiteral("未找到可执行的自研片上调试服务，目标连接将被阻断。"));
        return false;
    }

    debugConfig_.debugServicePath = servicePath;
    debugConfig_.interfaceConfigPath = QDir(resourceRootPath).filePath(profile.interfaceConfigPath);
    debugConfig_.targetConfigPath = QDir(resourceRootPath).filePath(profile.targetConfigPath);
    debugConfig_.temporaryDirectoryPath = QDir(workspaceRootPath_).filePath(QStringLiteral(".debug_service_tmp"));
    debugConfig_.adapterSpeedKhz = profile.jtagKhz;
    debugConfig_.timeoutMs = 300000;
    debugAccess_ = std::make_unique<target_control::DebugServiceAccess>(debugConfig_);
    logInfo(
        QStringLiteral("Resources"),
        QStringLiteral("自研片上调试服务已配置: %1").arg(QFileInfo(servicePath).fileName()));
    return true;
}

target_control::DebugAccess* WorkbenchController::debugAccess() const
{
    return debugAccess_.get();
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
