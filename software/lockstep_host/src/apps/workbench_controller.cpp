/*****************************************************************************
* 文件名: workbench_controller.cpp
* 日期: 2026-07-13
* 版本: v1.7
* 更新记录: 硬件打开后再提交运行配置，异步回调统一使用 QPointer 守卫。
* 描述: 实现 UI 动作到工作区、资源、流程、目标控制、报告和错误日志模块的适配
*****************************************************************************/

#include "workbench_controller.h"
#include "workbench_dialogs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSemaphore>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QSysInfo>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

#include "ui_theme.h"
#include "sampling_capture.h"

namespace lockstep::apps {
namespace {

template <typename Callback>
void postToController(const QPointer<WorkbenchController>& controller, const Callback& callback)
{
    QCoreApplication* const application = QCoreApplication::instance();
    if (application == nullptr) return;
    QMetaObject::invokeMethod(
        application,
        [controller, callback]() {
            if (!controller.isNull()) callback(controller.data());
        },
        Qt::QueuedConnection);
}

constexpr quint64 kDebugMemorySizeBytes = 64ULL * 1024ULL * 1024ULL;
constexpr char kProgramWriteRecordName[] = "program_write_record.json";
constexpr char kReadbackVerifyRecordName[] = "readback_verify_record.json";
constexpr char kRunControlRecordName[] = "run_control_record.json";
constexpr char kHaltControlRecordName[] = "halt_control_record.json";
constexpr char kProgramOperationProgressName[] = "program_operation_progress.json";
constexpr int kSamplingSampleCount = 4096;
constexpr int kSamplingPretrigger = 2047;
// 硬件窗口为 2047 pre + 1 trigger + 2048 post；协议字段 posttrigger
// 表示触发后的总深度，因此包含触发样本本身。
constexpr int kSamplingPosttrigger = 2049;
constexpr int kSamplingTriggerCount = 1;
constexpr int kSamplingPostAfterTrigger = 2048;
constexpr int kSamplingSampleWordBits = 1024;
constexpr int kSamplingSampleRateHz = 120000000;
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

bool samplingConfigChanged(const QString& path, const QJsonObject& proposed)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return true;
    return lockstep::apps::dialogs::samplingConfigHasMeaningfulChanges(file.readAll(), proposed);
}

bool writePayloadFile(const QString& path, const QByteArray& payload, QString* const error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) *error = QStringLiteral("无法写入文件: %1").arg(path);
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (error != nullptr) *error = QStringLiteral("文件写入不完整: %1").arg(path);
        return false;
    }
    if (!file.commit()) {
        if (error != nullptr) *error = QStringLiteral("文件提交失败: %1").arg(path);
        return false;
    }
    return true;
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

int serialPortNumber(const QString& portName)
{
    const QString normalized = portName.trimmed().toUpper();
    if (!normalized.startsWith(QStringLiteral("COM"))) {
        return 0;
    }

    bool ok = false;
    const int value = normalized.mid(3).toInt(&ok);
    return ok ? value : 0;
}

int serialPreferredScore(const QSerialPortInfo& info)
{
    const QString text = QStringLiteral("%1 %2 %3 %4")
        .arg(info.portName(), info.description(), info.manufacturer(), info.serialNumber())
        .toLower();

    int score = serialPortNumber(info.portName());
    if (text.contains(QStringLiteral("usb2uart")) || text.contains(QStringLiteral("usb to uart")) ||
        text.contains(QStringLiteral("cp210")) || text.contains(QStringLiteral("uart"))) {
        score += 1000;
    }
    if (text.contains(QStringLiteral("interface 2")) || text.contains(QStringLiteral("interface_2")) ||
        text.contains(QStringLiteral("mi_02"))) {
        score += 2000;
    }
    if (text.contains(QStringLiteral("cmsis")) || text.contains(QStringLiteral("dap"))) {
        score -= 500;
    }
    return score;
}

QString diagnosticCodeFromMessage(const QString& message)
{
    const QStringList codes = {
        QStringLiteral("DAP_NOT_ENUMERATED"), QStringLiteral("DAP_OPEN_FAILED"),
        QStringLiteral("JTAG_CHAIN_EMPTY"), QStringLiteral("JTAG_IDCODE_FAILED"),
        QStringLiteral("DMI_NO_RESPONSE"), QStringLiteral("CAPTURE_TRIGGER_TIMEOUT"),
        QStringLiteral("CAPTURE_STREAM_TIMEOUT"), QStringLiteral("CAPTURE_RECOVERY_FAILED")};
    for (const QString& code : codes) {
        if (message.contains(QStringLiteral("[") + code + QLatin1Char(']')) || message.contains(code)) {
            return code;
        }
    }
    return QString();
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

QString resolveDebugServicePath(
    const resources::BoardProfile&,
    const QString&)
{
    return QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
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
        for (const waveform_viewer::WaveformFieldView& field : group.fields) {
            ui::TraceFieldViewItem fieldItem;
            fieldItem.name = field.name;
            fieldItem.displayName = field.displayName;
            fieldItem.lsb = field.lsb;
            fieldItem.width = field.width;
            fieldItem.errorSignal = field.errorSignal;
            item.fields.append(fieldItem);
        }
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

QVector<ui::TraceSampleViewItem> traceSamplesToUi(
    const QList<waveform_viewer::WaveformSampleView>& samples)
{
    QVector<ui::TraceSampleViewItem> items;
    items.reserve(samples.size());
    for (const waveform_viewer::WaveformSampleView& sample : samples) {
        ui::TraceSampleViewItem item;
        item.time = sample.time;
        item.valueHex = sample.valueHex;
        item.unknown = sample.unknown;
        items.append(item);
    }
    return items;
}

QString evidenceStateText(const reporting::EvidenceState state)
{
    switch (state) {
    case reporting::EvidenceState::Passed: return QStringLiteral("通过");
    case reporting::EvidenceState::Failed: return QStringLiteral("失败");
    case reporting::EvidenceState::Missing: return QStringLiteral("缺失");
    case reporting::EvidenceState::NotRun:
    default: return QStringLiteral("未执行");
    }
}

QString optionalStateText(const reporting::OptionalRecordState state)
{
    switch (state) {
    case reporting::OptionalRecordState::Available: return QStringLiteral("可用");
    case reporting::OptionalRecordState::Failed: return QStringLiteral("失败");
    case reporting::OptionalRecordState::Skipped: return QStringLiteral("已跳过");
    case reporting::OptionalRecordState::NotAvailable:
    default: return QStringLiteral("不可用");
    }
}

QString conclusionText(const reporting::ReportConclusion conclusion)
{
    switch (conclusion) {
    case reporting::ReportConclusion::Pass: return QStringLiteral("通过");
    case reporting::ReportConclusion::Fail: return QStringLiteral("失败");
    case reporting::ReportConclusion::Blocked: return QStringLiteral("已阻断");
    case reporting::ReportConclusion::Incomplete:
    default: return QStringLiteral("未完成");
    }
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
      runButtonResetMode_(false),
      reportGenerationBusy_(false),
      samplingCaptureBusy_(false),
      samplingCaptureThread_(nullptr),
      workerThreads_(),
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
            } else if (page == ui::NavigationPage::Stats) {
                refreshReportView();
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

WorkbenchController::~WorkbenchController()
{
    const QList<QThread*> threads = workerThreads_.values();
    for (QThread* const thread : threads) {
        if (thread != nullptr && thread->isRunning()) thread->requestInterruption();
    }
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    for (QThread* const thread : threads) {
        if (thread == nullptr) continue;
        const int remainingMs = qMax(0, 10'000 - static_cast<int>(shutdownTimer.elapsed()));
        bool finished = !thread->isRunning() || thread->wait(static_cast<unsigned long>(remainingMs));
        if (!finished) {
            thread->terminate();
            finished = thread->wait(2'000);
        }
        if (finished) delete thread;
    }
    workerThreads_.clear();
    samplingCaptureThread_ = nullptr;
}

void WorkbenchController::trackWorkerThread(QThread* const thread)
{
    if (thread == nullptr) return;
    workerThreads_.insert(thread);
    connect(thread, &QObject::destroyed, this, [this, thread]() {
        workerThreads_.remove(thread);
        if (samplingCaptureThread_ == thread) samplingCaptureThread_ = nullptr;
    });
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
    refreshReportView();
    return true;
}

void WorkbenchController::handleAction(const ui::UiActionRequest& request)
{
    if (samplingCaptureBusy_ && request.action != ui::UiAction::StopProgram) {
        logWarning(QStringLiteral("Sampling"), QStringLiteral("采集正在进行，请等待 CAPTURE_END。"));
        return;
    }
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
    case ui::UiAction::SendSamplingConfig:
        sendSamplingConfig(request.parameters);
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
        runProgram(request.parameters);
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
    case ui::UiAction::OpenReportHtml:
        openReportHtml();
        break;
    case ui::UiAction::OpenReportDirectory:
        openReportDirectory();
        break;
    case ui::UiAction::CopyReportPath:
        copyReportPath();
        break;
    case ui::UiAction::OpenReportArtifact:
        openReportArtifact(request.parameters.value(QStringLiteral("relativePath")).toString());
        break;
    case ui::UiAction::NavigateToReportSource:
        if (window_ != nullptr) {
            const QString pageId = request.parameters.value(QStringLiteral("targetPage")).toString();
            if (pageId == QStringLiteral("protocol")) {
                window_->showPage(ui::NavigationPage::Protocol);
            } else if (pageId == QStringLiteral("waveform")) {
                window_->showPage(ui::NavigationPage::Waveform);
            } else {
                window_->showPage(ui::NavigationPage::RamProgram);
            }
        }
        break;
    case ui::UiAction::BrowseWaveform:
        importWaveformFile();
        break;
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
    refreshReportView();
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
    refreshReportView();
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

    const QString targetLabel = context.summary.taskName.trimmed().isEmpty()
        ? normalizedTaskId
        : QStringLiteral("%1 (%2)").arg(context.summary.taskName, normalizedTaskId);
    const QString currentSummary = hasTask_
        ? QStringLiteral("当前任务：%1 (%2)")
              .arg(currentTask_.summary.taskName.trimmed().isEmpty()
                       ? currentTask_.summary.taskId : currentTask_.summary.taskName,
                   currentTask_.summary.taskId)
        : QStringLiteral("当前工作台没有已加载任务。");
    if (window_ != nullptr && !dialogs::confirmTaskLoad(window_, targetLabel, currentSummary)) {
        logInfo(QStringLiteral("Workspace"), QStringLiteral("已取消加载验证任务: %1").arg(normalizedTaskId));
        return;
    }

    currentTask_ = context;
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
    refreshReportView();
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

    if ((window_ != nullptr) && !dialogs::confirmTaskDeletion(window_, taskLabel)) {
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
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../resources")),
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
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, debugConfig, profile]() {
        target_control::DebugServiceAccess workerAccess(debugConfig);
        target_control::TargetConnectionService service;
        const target_control::ConnectionRecord connection = service.connectTarget(workerAccess, profile);
        target_control::PrecheckRecord precheck;
        if (connection.state == target_control::ConnectionState::Connected) {
            precheck = service.runPrecheck(workerAccess, profile);
        }

        postToController(self, [profile, connection, precheck](WorkbenchController* const controller) {
                controller->targetConnectionBusy_ = false;
                controller->connectionRecord_ = connection;
                controller->precheckRecord_ = precheck;
                controller->hasConnection_ =
                    (controller->connectionRecord_.state == target_control::ConnectionState::Connected);
                controller->hasPrecheck_ = controller->hasConnection_ &&
                    (controller->precheckRecord_.state == target_control::PrecheckState::Passed);

                if (controller->hasConnection_) {
                    if (target_control::DebugServiceAccess* const serviceAccess =
                            dynamic_cast<target_control::DebugServiceAccess*>(controller->debugAccess_.get())) {
                        serviceAccess->assumeConnected(profile);
                    }
                } else {
                    controller->precheckRecord_ = target_control::PrecheckRecord();
                }

                const QString statusText = QStringLiteral("调试器: %1 / 预检: %2")
                    .arg(connectionStateText(controller->connectionRecord_.state),
                         precheckStateText(controller->precheckRecord_.state));
                if (controller->window_ != nullptr) {
                    controller->window_->setConnectionSummary(controller->debugProfile_.profileName, statusText);
                }
                controller->updateConnectionDiagnostics(
                    controller->hasConnection_ ? QStringLiteral("已连接") : QStringLiteral("未连接"));

                if (controller->hasConnection_) {
                    controller->logInfo(QStringLiteral("Target"), statusText);
                    controller->refreshSerialPorts();
                } else {
                    controller->logError(
                        QStringLiteral("Target"),
                        QStringLiteral("目标连接失败: %1").arg(controller->connectionRecord_.errorMessage));
                }
                controller->updateProgramActionAvailability();
                controller->updateTopStatus();
            });
    });
    trackWorkerThread(thread);
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
        updateProgramActionAvailability();
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
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, debugConfig, profile]() {
        target_control::DebugServiceAccess workerAccess(debugConfig);
        workerAccess.assumeConnected(profile);
        target_control::TargetConnectionService service;
        const target_control::ConnectionRecord disconnectRecord = service.disconnectTarget(workerAccess);

        postToController(self, [disconnectRecord](WorkbenchController* const controller) {
                controller->targetConnectionBusy_ = false;
                controller->connectionRecord_ = disconnectRecord;
                controller->hasConnection_ = false;
                controller->hasPrecheck_ = false;
                controller->precheckRecord_ = target_control::PrecheckRecord();
                if (target_control::DebugServiceAccess* const serviceAccess =
                        dynamic_cast<target_control::DebugServiceAccess*>(controller->debugAccess_.get())) {
                    serviceAccess->assumeDisconnected();
                }

                controller->updateConnectionDiagnostics(
                    controller->connectionRecord_.state == target_control::ConnectionState::Failed
                    ? QStringLiteral("未连接")
                    : QStringLiteral("已断开"));
                if (controller->window_ != nullptr) {
                    controller->window_->setConnectionSummary(
                        controller->debugProfile_.profileName, QStringLiteral("调试器: 已停止"));
                }
                if (controller->connectionRecord_.state == target_control::ConnectionState::Failed) {
                    controller->logError(
                        QStringLiteral("Target"),
                        QStringLiteral("调试服务断开失败: %1")
                            .arg(controller->connectionRecord_.errorMessage));
                } else {
                    controller->logInfo(QStringLiteral("Target"), QStringLiteral("调试服务已停止"));
                }
                controller->updateProgramActionAvailability();
                controller->updateTopStatus();
            });
    });
    trackWorkerThread(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    updateTopStatus();
}

void WorkbenchController::refreshSerialPorts()
{
    serialPorts_.clear();
    QStringList displayNames;
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    std::sort(ports.begin(), ports.end(), [](const QSerialPortInfo& lhs, const QSerialPortInfo& rhs) {
        const int leftScore = serialPreferredScore(lhs);
        const int rightScore = serialPreferredScore(rhs);
        if (leftScore != rightScore) {
            return leftScore > rightScore;
        }
        return lhs.portName() < rhs.portName();
    });
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

bool WorkbenchController::openSelectedSerialPort()
{
    if (serialPort_ == nullptr || window_ == nullptr) {
        return false;
    }

    QString portName = window_->selectedSerialPortName();
    if (portName.isEmpty() || !serialPorts_.contains(portName, Qt::CaseInsensitive)) {
        window_->setSerialStatus(QStringLiteral("请先刷新串口列表。"), false);
        logWarning(QStringLiteral("Serial"), QStringLiteral("串口列表未刷新或没有可打开串口。"));
        return false;
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
        return false;
    }

    serialOpen_ = true;
    const QString message = QStringLiteral("串口已打开: %1 @ %2")
        .arg(portName)
        .arg(serialPort_->baudRate());
    window_->setSerialStatus(message, true);
    logInfo(QStringLiteral("Serial"), message);
    return true;
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

    static_cast<void>(openSelectedSerialPort());
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

bool WorkbenchController::persistSamplingConfig(
    const QVariantMap& parameters,
    acquisition::SamplingCaptureConfig* const hardwareConfig,
    const bool saveToTask)
{
    if (hardwareConfig == nullptr || !ensureTask()) {
        return false;
    }

    const int sampleCount = parameters.value(QStringLiteral("sample_count")).toInt();
    const int pretrigger = parameters.value(QStringLiteral("pretrigger")).toInt();
    const int posttrigger = parameters.value(QStringLiteral("posttrigger")).toInt();
    const int triggerCount = parameters.value(QStringLiteral("trigger_count")).toInt();
    const int postAfterTrigger = parameters.value(QStringLiteral("post_after_trigger")).toInt();
    const int sampleWordBits = parameters.value(QStringLiteral("sample_word_bits")).toInt();
    const int sampleRateHz = parameters.value(QStringLiteral("sample_rate_hz")).toInt();
    if (sampleCount != kSamplingSampleCount ||
        pretrigger != kSamplingPretrigger ||
        posttrigger != kSamplingPosttrigger ||
        triggerCount != kSamplingTriggerCount ||
        postAfterTrigger != kSamplingPostAfterTrigger ||
        sampleWordBits != kSamplingSampleWordBits || sampleRateHz != kSamplingSampleRateHz) {
        logError(
            QStringLiteral("Sampling"),
            QStringLiteral("采样窗口参数与当前硬件不匹配: sample_count=%1 pretrigger=%2 posttrigger=%3 trigger_count=%4 post_after_trigger=%5 sample_word_bits=%6 sample_rate_hz=%7")
                .arg(sampleCount)
                .arg(pretrigger)
                .arg(posttrigger)
                .arg(triggerCount)
                .arg(postAfterTrigger)
                .arg(sampleWordBits)
                .arg(sampleRateHz));
        return false;
    }

    quint64 triggerAddress = 0U;
    const QString triggerAddressText = parameters.value(QStringLiteral("trigger_addr")).toString().trimmed();
    if (!parseAddressText(triggerAddressText, &triggerAddress) ||
        triggerAddress > std::numeric_limits<quint32>::max()) {
        logError(QStringLiteral("Sampling"), QStringLiteral("触发地址无效: %1").arg(triggerAddressText));
        return false;
    }

    const bool mismatchEnable = parameters.value(QStringLiteral("mismatch_enable")).toBool();
    const int mismatchMask = parameters.value(QStringLiteral("mismatch_mask")).toInt() & kSamplingMismatchMask;
    const int triggerMask = mismatchEnable ? mismatchMask : 0;

    hardwareConfig->sampleRateHz = static_cast<quint32>(sampleRateHz);
    hardwareConfig->sampleCount = static_cast<quint32>(sampleCount);
    hardwareConfig->pretriggerCount = static_cast<quint32>(pretrigger);
    hardwareConfig->posttriggerCount = static_cast<quint32>(posttrigger);
    hardwareConfig->protocolGroupMask = 0x1ffU;
    hardwareConfig->triggerMask = static_cast<quint32>(triggerMask);
    hardwareConfig->triggerValue = static_cast<quint32>(triggerAddress & 0xffffffffULL);
    hardwareConfig->triggerEdgeRise = mismatchEnable ? 1U : 0U;
    QString validationError;
    if (!hardwareConfig->validate(&validationError)) {
        logError(QStringLiteral("Sampling"), validationError);
        return false;
    }

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

    QJsonObject root = hardwareConfig->toJson();
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    root.insert(QStringLiteral("trigger_count"), triggerCount);
    root.insert(QStringLiteral("post_after_trigger"), postAfterTrigger);
    root.insert(QStringLiteral("trigger_logic"), QStringLiteral("valid_ready_addr_or_mismatch_rise"));
    root.insert(QStringLiteral("addr_trigger"), addressTrigger);
    root.insert(QStringLiteral("mismatch_trigger"), mismatchTrigger);
    root.insert(QStringLiteral("hardware_protocol"), hardwareProtocol);
    root.insert(QStringLiteral("created_at"), currentTimeText());
    if (!saveToTask) return true;

    const QJsonDocument document(root);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    workspace::TaskContext targetTask = currentTask_;
    QString error;
    const bool meaningfulConfigChange = QFileInfo::exists(currentTask_.paths.samplingConfigPath) &&
        samplingConfigChanged(currentTask_.paths.samplingConfigPath, root);

    const QString configPath = targetTask.paths.samplingConfigPath;
    if (!QDir().mkpath(QFileInfo(configPath).absolutePath())) {
        logError(QStringLiteral("Sampling"), QStringLiteral("无法创建采样配置目录: %1").arg(QFileInfo(configPath).absolutePath()));
        return false;
    }

    QString writeError;
    if (!writePayloadFile(configPath, payload, &writeError)) {
        logError(QStringLiteral("Sampling"), writeError);
        return false;
    }

    workspace::TaskInputSet inputs = targetTask.inputs;
    inputs.resourceSnapshot = resourceSnapshotJson();
    inputs.samplingConfig.id = QStringLiteral("input_%1").arg(currentTimeText());
    inputs.samplingConfig.relativePath = QDir(targetTask.paths.taskRootPath).relativeFilePath(configPath);
    inputs.samplingConfig.originalFileName = QFileInfo(configPath).fileName();
    inputs.samplingConfig.sha256 = fileSha256Text(configPath);
    inputs.samplingConfig.sizeBytes = QFileInfo(configPath).size();
    inputs.samplingConfig.importedAt = currentTimeText();

    workspace::TaskContext updated;
    bool inputSaved = false;
    if (meaningfulConfigChange) {
        workspace::InputChangeRequest request;
        request.mode = workspaceMode();
        request.taskId = targetTask.summary.taskId;
        request.changedInputs = inputs;
        request.action = workspace::InputChangeAction::OverwriteCurrentTask;
        workspace::InputChangeResult result;
        inputSaved = workspace_.handleInputChange(request, &result, &error);
        if (inputSaved) updated = result.task;
    } else {
        inputSaved = workspace_.saveTaskInputs(
            workspaceMode(), targetTask.summary.taskId, inputs, &updated, &error);
    }
    if (!inputSaved) {
        logError(QStringLiteral("Sampling"), QStringLiteral("保存采样配置索引失败: %1").arg(error));
        return false;
    }

    currentTask_ = updated;
    selectedTaskId_ = currentTask_.summary.taskId;
    logInfo(
        QStringLiteral("Sampling"),
        QStringLiteral("采样配置已保存: addr=%1 mismatch_enable=%2 mismatch_mask=0x%3")
            .arg(addressText(triggerAddress),
                 mismatchEnable ? QStringLiteral("true") : QStringLiteral("false"),
                 QString::number(mismatchMask, 16)));
    updateProjectView();
    updateTaskDetail();
    updateTopStatus();
    refreshReportView();
    return true;
}

bool WorkbenchController::sendSamplingConfig(const QVariantMap& parameters)
{
    acquisition::SamplingCaptureConfig hardwareConfig;
    if (!persistSamplingConfig(parameters, &hardwareConfig, true)) {
        return false;
    }

    samplingCaptureBusy_ = true;
    if (window_ != nullptr) {
        window_->setActionButtonsEnabled(ui::UiAction::SendSamplingConfig, false);
    }
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, hardwareConfig]() {
        acquisition::D3xxRuntime transport;
        QString transportError;
        bool accepted = transport.load(&transportError);
        if (accepted) {
            const QList<acquisition::D3xxDeviceInfo> devices = transport.enumerate(&transportError);
            accepted = !devices.isEmpty();
            if (!accepted && transportError.isEmpty()) transportError = QStringLiteral("未枚举到 FT601 设备");
            if (accepted) accepted = transport.open(devices.first().index, &transportError);
        }
        if (accepted) {
            acquisition::SamplingCaptureSession session;
            accepted = session.configure(&transport, hardwareConfig, &transportError);
        }
        transport.close();
        postToController(self, [accepted, transportError](WorkbenchController* const controller) {
                controller->samplingCaptureBusy_ = false;
                if (controller->window_ != nullptr) {
                    controller->window_->setActionButtonsEnabled(ui::UiAction::SendSamplingConfig, true);
                }
                if (accepted) {
                    controller->logInfo(
                        QStringLiteral("Sampling"),
                        QStringLiteral("采样配置已保存到任务并由 FT601 确认。"));
                } else {
                    controller->logError(
                        QStringLiteral("Sampling"),
                        transportError.isEmpty()
                            ? QStringLiteral("CONFIG_CAPTURE 未获得有效确认") : transportError);
                }
                controller->updateTopStatus();
            });
    });
    samplingCaptureThread_ = thread;
    trackWorkerThread(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (samplingCaptureThread_ == thread) samplingCaptureThread_ = nullptr;
    });
    thread->start();
    return true;
}

void WorkbenchController::runProgram(const QVariantMap& parameters)
{
    if (samplingCaptureBusy_) {
        return;
    }

    if (!hasConnection_ || !hasPrecheck_ || !hasVerifyRecord_ ||
        verifyRecord_.state != target_control::VerifyState::Passed) {
        logError(QStringLiteral("Sampling"),
                 QStringLiteral("采集启动前必须完成目标连接、预检、烧写和回读校验"));
        return;
    }
    if (!serialOpen_ && !openSelectedSerialPort()) {
        logWarning(
            QStringLiteral("Sampling"),
            QStringLiteral("程序打印 UART 未打开，本次运行继续；缺少串口结束证据，不能据此判定通过"));
    }
    acquisition::SamplingCaptureConfig config;
    if (!persistSamplingConfig(parameters, &config, false)) {
        return;
    }

    const QString taskRoot = currentTask_.paths.taskRootPath;
    const QString taskId = currentTask_.summary.taskId;
    const bool resetBeforeRun = runButtonResetMode_;
    const target_control::DebugServiceConfig debugConfig = debugConfig_;
    const target_control::DebugProfile profile = debugProfile_;
    const target_control::ProgramImageInfo image = imageInfo_;
    const target_control::ReadbackVerifyRecord verifyRecord = verifyRecord_;
    hasHaltRecord_ = false;
    haltRecord_ = target_control::RunControlRecord();
    runSerialOutput_.clear();
    latestRunInstruction_ = resetBeforeRun ? QStringLiteral("程序复位并运行") : QStringLiteral("程序运行");
    runOutputConfirmed_ = false;
    runSerialCaptureActive_ = true;
    currentRunProgress_ = 10;
    currentStopProgress_ = 0;
    samplingCaptureBusy_ = true;
    if (window_ != nullptr) {
        window_->setActionButtonsEnabled(ui::UiAction::SendSamplingConfig, false);
    }
    setRunSummaryFromCurrentState(
        QStringLiteral("采样已启动，等待程序运行"), currentRunProgress_, currentStopProgress_);
    logInfo(
        QStringLiteral("Sampling"),
        QStringLiteral("程序运行前启动 FT601 采集: 120 MHz / 4096 点 / 1024 bit"));

    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create(
        [self, parameters, config, taskRoot, taskId, resetBeforeRun,
         debugConfig, profile, image, verifyRecord]() {
        QString captureError;
        fault_injection::FaultInjectionResult faultResult;
        const QString faultConfigPath = QDir(taskRoot).filePath(QStringLiteral("inputs/fault_injection_config.json"));
        fault_injection::FaultInjectionRequest faultRequest;
        faultRequest.taskRootPath = taskRoot;
        faultRequest.configured = QFileInfo::exists(faultConfigPath);
        const QString resourceScriptDir = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("resources/error_injection"));
        faultRequest.allowedScriptDirectory = resourceScriptDir;
        if (faultRequest.configured) {
            QFile configFile(faultConfigPath);
            if (!configFile.open(QIODevice::ReadOnly)) {
                captureError = QStringLiteral("无法读取错误注入配置: %1").arg(configFile.errorString());
            } else {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &parseError);
                const QJsonObject object = document.object();
                const QString scriptName = object.value(QStringLiteral("script")).toString().trimmed();
                if (parseError.error != QJsonParseError::NoError || scriptName.isEmpty() ||
                    QFileInfo(scriptName).fileName() != scriptName) {
                    captureError = QStringLiteral("错误注入配置无效，必须指定资源目录内脚本文件名");
                } else {
                    faultRequest.scriptPath = QDir(resourceScriptDir).filePath(scriptName);
                    faultRequest.workingDirectory = resourceScriptDir;
                    faultRequest.timeoutMs = object.value(QStringLiteral("timeout_ms")).toInt(30'000);
                    const QJsonArray arguments = object.value(QStringLiteral("arguments")).toArray();
                    for (const QJsonValue& argument : arguments) {
                        faultRequest.arguments.append(argument.toString());
                    }
                }
            }
        }
        acquisition::D3xxRuntime transport;
        bool success = captureError.isEmpty() && transport.load(&captureError);
        if (success) {
            const QList<acquisition::D3xxDeviceInfo> devices = transport.enumerate(&captureError);
            success = !devices.isEmpty();
            if (!success && captureError.isEmpty()) captureError = QStringLiteral("未枚举到 FT601 设备");
            if (success) success = transport.open(devices.first().index, &captureError);
        }
        if (success) {
            const auto persisted = std::make_shared<bool>(false);
            const auto persistState = std::make_shared<std::atomic<int>>(0);
            const auto completed = std::make_shared<QSemaphore>();
            QCoreApplication* const application = QCoreApplication::instance();
            const bool posted = application != nullptr && QMetaObject::invokeMethod(
                application,
                [self, parameters, persisted, persistState, completed]() {
                    int expected = 0;
                    if (persistState->compare_exchange_strong(expected, 1) && !self.isNull()) {
                        acquisition::SamplingCaptureConfig persistedConfig;
                        *persisted = self->persistSamplingConfig(parameters, &persistedConfig, true);
                    }
                    persistState->store(3);
                    completed->release();
                },
                Qt::QueuedConnection);
            bool commitCompleted = posted && completed->tryAcquire(1, 5'000);
            if (posted && !commitCompleted) {
                int expected = 0;
                if (!persistState->compare_exchange_strong(expected, 2)) {
                    completed->acquire();
                    commitCompleted = true;
                }
            }
            if (!commitCompleted || !*persisted) {
                success = false;
                if (captureError.isEmpty()) {
                    captureError = posted
                        ? QStringLiteral("FT601 已打开，但采样配置未能提交到当前任务")
                        : QStringLiteral("无法调度采样配置任务提交");
                }
            }
        }
        acquisition::SamplingCaptureRecord record;
        target_control::RunControlRecord captureRunRecord;
        if (success) {
            acquisition::SamplingCaptureSession session;
            const auto afterArm = [&](const quint32 captureId, QString* const callbackError) {
                target_control::DebugServiceAccess runAccess(debugConfig);
                const target_control::DebugResult connectResult = runAccess.connectTarget(profile);
                if (!connectResult.success) {
                    if (callbackError != nullptr) {
                        *callbackError = QStringLiteral(
                            "ARM 后目标连接失败: capture_id=%1 error=%2")
                                             .arg(captureId).arg(connectResult.errorMessage);
                    }
                    return false;
                }
                target_control::ProgramController controller;
                captureRunRecord = controller.runTarget(
                    runAccess, taskId, image, verifyRecord, resetBeforeRun, profile.resetStrategy);
                runAccess.disconnectTarget();
                if (captureRunRecord.state != target_control::RunState::Running) {
                    if (callbackError != nullptr) {
                        *callbackError = QStringLiteral(
                            "ARM 后程序启动失败: capture_id=%1 error=%2")
                                             .arg(captureId).arg(captureRunRecord.errorMessage);
                    }
                    return false;
                }
                faultResult = fault_injection::FaultInjectionOrchestrator().execute(faultRequest);
                if (faultResult.status == QStringLiteral("failed")) {
                    if (callbackError != nullptr) {
                        *callbackError = QStringLiteral(
                            "程序启动后错误注入失败: capture_id=%1 error=%2")
                                             .arg(captureId).arg(faultResult.error);
                    }
                    return false;
                }
                return true;
            };
            const auto cancelled = []() {
                return QThread::currentThread()->isInterruptionRequested();
            };
            const acquisition::CaptureSessionResult captureResult =
                session.runDetailed(
                    &transport, config, taskRoot, 120'000, &record, afterArm, cancelled);
            success = captureResult.success;
            captureError = captureResult.message;
            if (!success) {
                QString code = QStringLiteral("CAPTURE_STREAM_ERROR");
                const QStringList endCodes = {
                    QStringLiteral("CAPTURE_END_HOST_TERMINATE"),
                    QStringLiteral("CAPTURE_END_WATCHDOG"),
                    QStringLiteral("CAPTURE_END_OVERFLOW"),
                    QStringLiteral("CAPTURE_END_HARD_TIMEOUT"),
                    QStringLiteral("CAPTURE_END_FATAL_ERROR")};
                for (const QString& candidate : endCodes) {
                    if (captureError.contains(candidate)) {
                        code = candidate;
                        break;
                    }
                }
                if (captureError.contains(QStringLiteral("CAPTURE_RECOVERY_FAILED"))) {
                    code = QStringLiteral("CAPTURE_RECOVERY_FAILED");
                } else if (captureError.contains(QStringLiteral("等待 CAPTURE_END 超时"))) {
                    code = QStringLiteral("CAPTURE_STREAM_TIMEOUT");
                }
                captureError = QStringLiteral("[%1] %2").arg(code, captureError);
            }
        }
        transport.close();

        postToController(
            self,
            [success, captureError, taskRoot, taskId, captureRunRecord](WorkbenchController* const controller) {
                controller->samplingCaptureBusy_ = false;
                if (controller->window_ != nullptr) {
                    controller->window_->setActionButtonsEnabled(ui::UiAction::SendSamplingConfig, true);
                }
                if (!controller->hasTask_ || controller->currentTask_.summary.taskId != taskId ||
                    controller->currentTask_.paths.taskRootPath != taskRoot) {
                    controller->logWarning(
                        QStringLiteral("Sampling"), QStringLiteral("采集已完成，但当前任务已变化。"));
                    return;
                }
                if (!captureRunRecord.taskId.isEmpty()) {
                    controller->runRecord_ = captureRunRecord;
                    controller->hasRunRecord_ = true;
                    if (controller->runRecord_.state == target_control::RunState::Running) {
                        controller->currentRunProgress_ = qMax(controller->currentRunProgress_, 85);
                        controller->logInfo(
                            QStringLiteral("Run"),
                            QStringLiteral("采样 ARM 后程序运行成功: %1")
                                .arg(debugReturnSummary(controller->runRecord_.rawReturn)));
                    }
                    QString evidenceError;
                    if (!controller->writeEvidenceJson(
                            QString::fromLatin1(kRunControlRecordName),
                            runRecordToJson(controller->runRecord_), nullptr, &evidenceError)) {
                        controller->logWarning(
                            QStringLiteral("Evidence"),
                            QStringLiteral("ARM 后运行证据写入失败: %1").arg(evidenceError));
                    }
                }
                if (!success) {
                    controller->runSerialCaptureActive_ = false;
                    controller->logError(QStringLiteral("Sampling"), captureError.isEmpty()
                        ? QStringLiteral("FT601 采集失败") : captureError);
                    const bool programStarted = captureRunRecord.state == target_control::RunState::Running;
                    controller->setRunSummaryFromCurrentState(
                        programStarted ? QStringLiteral("程序已运行，采集结束失败")
                                       : QStringLiteral("采集启动失败，程序未运行"),
                        controller->currentRunProgress_, controller->currentStopProgress_);
                    controller->updateTopStatus();
                    controller->refreshReportView();
                    return;
                }

                QJsonObject schema;
                schema.insert(QStringLiteral("schema_version"), QStringLiteral("2.0"));
                schema.insert(QStringLiteral("task_id"), taskId);
                schema.insert(QStringLiteral("sample_signal"), QStringLiteral("CH0..CH1023"));
                schema.insert(QStringLiteral("sample_width"), 1024);
                schema.insert(QStringLiteral("physical_channels"), 1024);
                schema.insert(QStringLiteral("trace_profile_id"), QStringLiteral("trace.noelv.lockstep_1024"));
                const QString schemaPath = QDir(taskRoot).filePath(QStringLiteral("waveform/capture_schema.json"));
                QSaveFile schemaFile(schemaPath);
                const QByteArray schemaBytes = QJsonDocument(schema).toJson(QJsonDocument::Indented);
                if (!schemaFile.open(QIODevice::WriteOnly) ||
                    schemaFile.write(schemaBytes) != schemaBytes.size() || !schemaFile.commit()) {
                    controller->logError(
                        QStringLiteral("Sampling"), QStringLiteral("采集完成，但通道 schema 写入失败"));
                    return;
                }

                if (!controller->analyzeCurrentTrace(false)) {
                    controller->logError(
                        QStringLiteral("Sampling"),
                        QStringLiteral("采集完成，但协议解析失败，未生成通过报告"));
                    controller->refreshReportView();
                    return;
                }
                QJsonArray artifacts;
                const auto appendArtifact = [&artifacts](const QString& name, const QString& relativePath) {
                    QJsonObject artifact;
                    artifact.insert(QStringLiteral("name"), name);
                    artifact.insert(QStringLiteral("relative_path"), relativePath);
                    artifacts.append(artifact);
                };
                appendArtifact(QStringLiteral("acquisition_raw"), QStringLiteral("evidence/raw_capture.dat"));
                appendArtifact(QStringLiteral("capture_sidecar"), QStringLiteral("evidence/capture_sidecar.json"));
                appendArtifact(QStringLiteral("capture_vcd"), QStringLiteral("waveform/capture.vcd"));
                appendArtifact(QStringLiteral("protocol_analysis"), QStringLiteral("evidence/protocol_analysis.json"));
                if (QFileInfo::exists(QDir(taskRoot).filePath(QStringLiteral("evidence/capture_status.json")))) {
                    appendArtifact(QStringLiteral("capture_status"), QStringLiteral("evidence/capture_status.json"));
                }
                if (QFileInfo::exists(QDir(taskRoot).filePath(QStringLiteral("evidence/capture_error.json")))) {
                    appendArtifact(QStringLiteral("capture_error"), QStringLiteral("evidence/capture_error.json"));
                }
                if (QFileInfo::exists(QDir(taskRoot).filePath(QStringLiteral("evidence/fault_injection.json")))) {
                    appendArtifact(QStringLiteral("fault_injection"), QStringLiteral("evidence/fault_injection.json"));
                }
                QJsonObject artifactIndex;
                artifactIndex.insert(QStringLiteral("schema"), QStringLiteral("lockstep-artifacts-v1"));
                artifactIndex.insert(QStringLiteral("artifacts"), artifacts);
                QString artifactError;
                if (!controller->writeEvidenceJson(
                        QStringLiteral("artifacts.json"), artifactIndex, nullptr, &artifactError)) {
                    controller->logError(QStringLiteral("Sampling"), artifactError);
                    return;
                }
                controller->refreshWaveformView();
                controller->logInfo(QStringLiteral("Sampling"), QStringLiteral("采集、VCD 与协议解析已完成。"));
                controller->generateReport();
            });
    });
    samplingCaptureThread_ = thread;
    trackWorkerThread(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (samplingCaptureThread_ == thread) samplingCaptureThread_ = nullptr;
    });
    thread->start();
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
    updateProgramActionAvailability();
}

void WorkbenchController::endHardwareOperation()
{
    hardwareOperationBusy_ = false;
    hardwareOperationName_.clear();
    updateProgramActionAvailability();
}

void WorkbenchController::updateProgramActionAvailability()
{
    if (window_ == nullptr) {
        return;
    }

    const bool operationIdle = !hardwareOperationBusy_;
    window_->setActionButtonsEnabled(ui::UiAction::ProgramImage, operationIdle);
    window_->setActionButtonsEnabled(ui::UiAction::VerifyReadback, operationIdle);
    window_->setActionButtonsEnabled(ui::UiAction::RunProgram, operationIdle);
    window_->setActionButtonsEnabled(ui::UiAction::StopProgram, operationIdle && hasConnection_);
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
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, debugConfig, profile, image, taskId]() {
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
                [self](const quint64 completedBytes, const quint64 totalBytes, const QString& message) {
                    postToController(
                        self,
                        [completedBytes, totalBytes, message](WorkbenchController* const workbench) {
                            workbench->updateWriteOperationProgress(completedBytes, totalBytes, message);
                        });
                };
            result.record = controller.programTarget(workerAccess, taskId, image, callback);
        }
        result.elapsedMs = timer.elapsed();
        postToController(self, [result](WorkbenchController* const workbench) {
                workbench->writeRecord_ = result.record;
                workbench->hasWriteRecord_ = workbench->writeRecord_.success ||
                    !workbench->writeRecord_.rawReturn.isEmpty() ||
                    !workbench->writeRecord_.errorMessage.isEmpty();
                workbench->currentWriteProgress_ = 100;

                QString error;
                QString relativePath;
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kProgramWriteRecordName),
                        writeRecordToJson(workbench->writeRecord_), &relativePath, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"),
                        QStringLiteral("鐑у啓璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(
                        target_control::ProgramOperation::Write,
                        workbench->writeRecord_.success
                            ? target_control::OperationStage::PersistWriteRecord
                            : target_control::OperationStage::Failed);
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kProgramOperationProgressName),
                        progressToJson(persistedProgress), nullptr, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"),
                        QStringLiteral("鐑у綍杩涘害璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }

                if (workbench->writeRecord_.success) {
                    workbench->logInfo(QStringLiteral("Program"), QStringLiteral("烧录成功。"));
                } else {
                    workbench->logError(
                        QStringLiteral("Program"),
                        QStringLiteral("烧录失败: %1").arg(workbench->writeRecord_.errorMessage));
                }
                workbench->logInfo(
                    QStringLiteral("Program"), QStringLiteral("烧录开销: %1 ms").arg(result.elapsedMs));
                workbench->endHardwareOperation();
                workbench->setRamSummaryFromCurrentState(
                    workbench->writeRecord_.success ? QStringLiteral("烧录完成") : QStringLiteral("烧录失败"));
                workbench->updateTopStatus();
            });
    });
    trackWorkerThread(thread);
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
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, debugConfig, profile, image, taskId]() {
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
                [self](const quint64 completedBytes, const quint64 totalBytes, const QString& message) {
                    postToController(
                        self,
                        [completedBytes, totalBytes, message](WorkbenchController* const workbench) {
                            workbench->updateReadbackOperationProgress(completedBytes, totalBytes, message);
                        });
                };
            result.record = controller.verifyReadback(workerAccess, taskId, image, callback);
        }
        result.elapsedMs = timer.elapsed();
        postToController(self, [result](WorkbenchController* const workbench) {
                workbench->verifyRecord_ = result.record;
                workbench->hasVerifyRecord_ = true;
                workbench->currentReadbackProgress_ = 100;

                QString error;
                QString relativePath;
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kReadbackVerifyRecordName),
                        verifyRecordToJson(workbench->verifyRecord_), &relativePath, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"),
                        QStringLiteral("鍥炶璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(target_control::ProgramOperation::Readback, target_control::OperationStage::PersistVerifyRecord);
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kProgramOperationProgressName),
                        progressToJson(persistedProgress), nullptr, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"),
                        QStringLiteral("鍥炶杩涘害璇佹嵁鍐欏叆澶辫触: %1").arg(error));
                }

                const bool passed = workbench->verifyRecord_.state == target_control::VerifyState::Passed;
                if (passed) {
                    workbench->logInfo(QStringLiteral("Readback"), QStringLiteral("回读校验通过。"));
                } else {
                    workbench->logError(
                        QStringLiteral("Readback"),
                        QStringLiteral("回读校验失败: %1").arg(workbench->verifyRecord_.errorMessage));
                }
                workbench->logInfo(
                    QStringLiteral("Readback"), QStringLiteral("回读开销: %1 ms").arg(result.elapsedMs));
                workbench->endHardwareOperation();
                workbench->setRamSummaryFromCurrentState(
                    passed ? QStringLiteral("回读校验完成") : QStringLiteral("回读校验失败"));
                workbench->updateTopStatus();
            });
    });
    trackWorkerThread(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    const QPointer<WorkbenchController> self(this);
    QThread* const thread = QThread::create([self, debugConfig, profile, taskId]() {
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

        postToController(self, [result](WorkbenchController* const workbench) {
                target_control::OperationProgress progress =
                    target_control::makeOperationProgress(target_control::ProgramOperation::Halt, target_control::OperationStage::CaptureHaltStatus);
                workbench->currentStopProgress_ = qMax(workbench->currentStopProgress_, progress.percent);
                workbench->setRunSummaryFromCurrentState(
                    QStringLiteral("终止返回确认中 - ") + progress.message,
                    workbench->currentRunProgress_, workbench->currentStopProgress_);

                workbench->haltRecord_ = result.record;
                workbench->hasHaltRecord_ = true;

                QString error;
                QString relativePath;
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kHaltControlRecordName),
                        runRecordToJson(workbench->haltRecord_), &relativePath, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"), QStringLiteral("终止证据写入失败: %1").arg(error));
                }
                const bool halted = workbench->haltRecord_.state == target_control::RunState::Halted;
                const target_control::OperationProgress persistedProgress =
                    target_control::makeOperationProgress(
                        target_control::ProgramOperation::Halt,
                        halted ? target_control::OperationStage::PersistHaltRecord : target_control::OperationStage::Failed);
                if (!workbench->writeEvidenceJson(
                        QString::fromLatin1(kProgramOperationProgressName),
                        progressToJson(persistedProgress), nullptr, &error)) {
                    workbench->logWarning(
                        QStringLiteral("Evidence"), QStringLiteral("终止进度证据写入失败: %1").arg(error));
                }

                if (halted) {
                    workbench->currentStopProgress_ = 100;
                    workbench->runSerialCaptureActive_ = false;
                    workbench->runButtonResetMode_ = true;
                    workbench->logInfo(
                        QStringLiteral("Run"),
                        QStringLiteral("程序已终止: %1")
                            .arg(debugReturnSummary(workbench->haltRecord_.rawReturn)));
                    workbench->updateRunButtonText();
                } else {
                    workbench->logWarning(
                        QStringLiteral("Run"),
                        QStringLiteral("终止请求失败: %1").arg(workbench->haltRecord_.errorMessage));
                }
                workbench->logInfo(
                    QStringLiteral("Run"), QStringLiteral("终止控制开销: %1 ms").arg(result.elapsedMs));
                workbench->endHardwareOperation();
                workbench->setRunSummaryFromCurrentState(
                    halted ? QStringLiteral("终止控制完成") : QStringLiteral("终止控制失败"),
                    workbench->currentRunProgress_, workbench->currentStopProgress_);
                workbench->updateTopStatus();
            });
    });
    trackWorkerThread(thread);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    updateTopStatus();
}

reporting::ReportDocumentModel WorkbenchController::buildReportModel() const
{
    reporting::ReportDocumentModel model;
    if (!hasTask_) {
        return model;
    }
    model.taskId = currentTask_.summary.taskId;
    model.taskName = currentTask_.summary.taskName;
    model.taskDescription = currentTask_.summary.description;
    model.mode = toModeText(mode_);
    model.programFileName = !currentTask_.inputs.programFile.originalFileName.isEmpty()
        ? currentTask_.inputs.programFile.originalFileName
        : QFileInfo(currentTask_.inputs.programFile.relativePath).fileName();
    model.programRelativePath = currentTask_.inputs.programFile.relativePath;
    model.targetSummary = debugProfile_.profileName;
    model.environment.insert(QStringLiteral("application_version"), QString::fromLatin1(LOCKSTEP_APP_VERSION));
    model.environment.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
    model.environment.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    model.environment.insert(QStringLiteral("cpu_architecture"), QSysInfo::currentCpuArchitecture());
    model.resourceSnapshot = resourceSnapshotJson();

    QList<error_handling::ErrorRecord> errors;
    errorRegistry_.loadTaskErrors(currentTask_.paths.taskRootPath, &errors, nullptr);
    for (const error_handling::ErrorRecord& error : errors) {
        if (error.status == error_handling::ErrorStatus::Resolved) {
            continue;
        }
        reporting::ReportDiagnostic diagnostic;
        diagnostic.id = error.errorId;
        diagnostic.code = error.code;
        diagnostic.severity = error_handling::toString(error.severity);
        diagnostic.source = error.source;
        diagnostic.message = error.message;
        diagnostic.suggestion = error.resolution.isEmpty()
            ? error.context.value(QStringLiteral("suggestion")).toString(
                  QStringLiteral("查看来源记录并处理后重新生成报告"))
            : error.resolution;
        diagnostic.occurredAt = error.createdAt;
        if (error.severity == error_handling::ErrorSeverity::Warning) {
            model.warnings.append(diagnostic);
        } else if (error.severity == error_handling::ErrorSeverity::Blocking ||
                   error.severity == error_handling::ErrorSeverity::Critical) {
            model.blockingErrors.append(diagnostic);
        }
    }
    return reportGenerator_.buildModelFromTask(currentTask_.paths.taskRootPath, model);
}

void WorkbenchController::refreshReportView(const QString& generationError)
{
    if (window_ == nullptr) {
        return;
    }
    ui::ReportPageViewModel view;
    if (!hasTask_) {
        view.lifecycle = ui::ReportLifecycleState::NoTask;
        view.lifecycleText = QStringLiteral("未选择任务");
        view.conclusionText = QStringLiteral("无可评估任务");
        view.primaryReason = QStringLiteral("请先创建或加载验证任务。");
        window_->setReportPageState(view);
        return;
    }

    const reporting::ReportDocumentModel live = buildReportModel();
    QStringList liveReasons;
    const reporting::ReportConclusion liveConclusion = reportGenerator_.calculateConclusion(live, &liveReasons);
    const QString liveDigest = reportGenerator_.calculateInputDigest(live);
    reporting::ReportDocumentModel persisted;
    reporting::ReportConclusion persistedConclusion = reporting::ReportConclusion::Incomplete;
    QStringList persistedReasons;
    QString loadError;
    const bool reportFileExists = QFileInfo::exists(
        QDir(currentTask_.paths.reportsPath).filePath(QStringLiteral("report.json")));
    const bool hasPersisted = reportGenerator_.loadLatestReport(
        currentTask_.paths.taskRootPath, &persisted, &persistedConclusion, &persistedReasons, &loadError);

    view.hasTask = true;
    view.taskName = currentTask_.summary.taskName;
    view.taskId = currentTask_.summary.taskId;
    view.modeText = toModeText(mode_);
    view.hasPersistedReport = hasPersisted;
    view.reportRelativePath = QStringLiteral("reports/report.json");
    view.htmlRelativePath = QStringLiteral("reports/report.html");
    view.warningCount = live.warnings.size();
    view.blockingCount = live.blockingErrors.size();
    view.generating = reportGenerationBusy_;
    view.inputDigest = liveDigest;
    view.stale = hasPersisted && persisted.inputDigest != liveDigest;

    view.conclusion = reporting::toString(liveConclusion);
    view.conclusionText = conclusionText(liveConclusion);
    view.primaryReason = liveReasons.isEmpty() ? QStringLiteral("暂无结论依据") : liveReasons.first();
    if (hasPersisted) {
        view.persistedConclusion = reporting::toString(persistedConclusion);
        view.persistedConclusionText = conclusionText(persistedConclusion);
    }
    if (reportGenerationBusy_) {
        view.lifecycle = ui::ReportLifecycleState::Generating;
        view.lifecycleText = QStringLiteral("正在生成");
    } else if (!generationError.isEmpty()) {
        view.lifecycle = ui::ReportLifecycleState::GenerationError;
        view.lifecycleText = QStringLiteral("生成失败");
        view.errorMessage = generationError;
    } else if (!hasPersisted && reportFileExists) {
        view.lifecycle = ui::ReportLifecycleState::LoadError;
        view.lifecycleText = QStringLiteral("读取失败");
        view.errorMessage = loadError;
    } else if (!hasPersisted) {
        view.lifecycle = ui::ReportLifecycleState::NotGenerated;
        view.lifecycleText = QStringLiteral("当前证据预检");
    } else if (view.stale) {
        view.lifecycle = ui::ReportLifecycleState::Stale;
        view.lifecycleText = QStringLiteral("报告已过期");
    } else {
        view.lifecycle = ui::ReportLifecycleState::Current;
        view.lifecycleText = QStringLiteral("报告为最新");
    }
    if (hasPersisted) {
        view.reportId = persisted.reportId;
        view.generatedAt = persisted.generatedAt;
        view.schemaVersion = persisted.schemaVersion;
        view.revision = persisted.revision;
        view.reportSha256 = fileSha256Text(
            QDir(currentTask_.paths.reportsPath).filePath(QStringLiteral("report.json")));
    }

    const auto addEvidence = [&view](
                                 const QString& id,
                                 const QString& name,
                                 const reporting::ReportEvidence& evidence) {
        ui::ReportEvidenceViewItem item;
        item.id = id;
        item.displayName = name;
        item.state = reporting::toString(evidence.state);
        item.stateText = evidenceStateText(evidence.state);
        item.summary = evidence.summary;
        item.recordedAt = evidence.recordedAt;
        item.relativePath = evidence.recordPath;
        item.details = QString::fromUtf8(QJsonDocument(evidence.metrics).toJson(QJsonDocument::Compact));
        item.errorIds = evidence.errorIds;
        view.requiredEvidence.append(item);
    };
    addEvidence(QStringLiteral("program_write"), QStringLiteral("程序烧写"), live.requiredEvidence.programWrite);
    addEvidence(QStringLiteral("readback_verify"), QStringLiteral("回读校验"), live.requiredEvidence.readbackVerify);
    addEvidence(QStringLiteral("run_control"), QStringLiteral("程序运行"), live.requiredEvidence.runControl);

    const auto addOptional = [&view](
                                 const QString& id,
                                 const QString& name,
                                 const reporting::ReportOptionalRecord& record) {
        ui::ReportOptionalViewItem item;
        item.id = id;
        item.displayName = name;
        item.state = reporting::toString(record.state);
        item.stateText = optionalStateText(record.state);
        item.summary = record.summary;
        item.recordedAt = record.recordedAt;
        item.relativePath = record.path;
        view.optionalRecords.append(item);
    };
    addOptional(QStringLiteral("vcd_waveform"), QStringLiteral("VCD 波形"), live.optionalRecords.vcdWaveform);
    addOptional(QStringLiteral("protocol_analysis"), QStringLiteral("协议分析"), live.optionalRecords.protocolAnalysis);
    addOptional(QStringLiteral("acquisition"), QStringLiteral("采集记录"), live.optionalRecords.acquisition);
    addOptional(QStringLiteral("fault_injection"), QStringLiteral("故障注入"), live.optionalRecords.faultInjection);

    const auto addDiagnostics = [&view](const QList<reporting::ReportDiagnostic>& values) {
        for (const reporting::ReportDiagnostic& diagnostic : values) {
            ui::ReportDiagnosticViewItem item;
            item.id = diagnostic.id;
            item.code = diagnostic.code;
            item.severity = diagnostic.severity;
            item.source = diagnostic.source;
            item.message = diagnostic.message;
            item.suggestion = diagnostic.suggestion;
            item.occurredAt = diagnostic.occurredAt;
            const QString source = diagnostic.source.toLower();
            item.targetPage = source.contains(QStringLiteral("protocol")) ||
                    source.contains(QStringLiteral("trace")) || source.contains(QStringLiteral("m12"))
                ? QStringLiteral("protocol")
                : (source.contains(QStringLiteral("waveform")) || source.contains(QStringLiteral("vcd"))
                    ? QStringLiteral("waveform") : QStringLiteral("ram_program"));
            view.diagnostics.append(item);
        }
    };
    addDiagnostics(live.blockingErrors);
    const auto addEvidenceProblems = [&view](const QStringList& states) {
        for (const ui::ReportEvidenceViewItem& evidence : view.requiredEvidence) {
            if (!states.contains(evidence.state)) {
                continue;
            }
            ui::ReportDiagnosticViewItem item;
            item.id = evidence.id;
            item.code = QStringLiteral("REQUIRED_EVIDENCE");
            item.severity = evidence.state == QStringLiteral("failed")
                ? QStringLiteral("error") : QStringLiteral("incomplete");
            item.source = evidence.displayName;
            item.message = evidence.summary.isEmpty() ? evidence.stateText : evidence.summary;
            item.suggestion = QStringLiteral("转到程序烧录与运行页面完成或检查该步骤");
            item.targetPage = QStringLiteral("ram_program");
            view.diagnostics.append(item);
        }
    };
    addEvidenceProblems({QStringLiteral("failed")});
    addEvidenceProblems({QStringLiteral("missing"), QStringLiteral("not_run")});
    addDiagnostics(live.warnings);
    view.archiveDetails = QStringLiteral("schema: %1\nrevision: %2\ninput_digest: %3\nreport_sha256: %4\n资源快照: %5\n产物索引: %6 项")
        .arg(view.schemaVersion.isEmpty() ? QStringLiteral("2.0") : view.schemaVersion)
        .arg(view.revision)
        .arg(view.inputDigest)
        .arg(view.reportSha256)
        .arg(QString::fromUtf8(QJsonDocument(live.resourceSnapshot).toJson(QJsonDocument::Compact)))
        .arg(live.artifacts.size());
    window_->setReportPageState(view);
}

void WorkbenchController::generateReport()
{
    if (!ensureTask() || reportGenerationBusy_) {
        return;
    }
    reporting::ReportDocumentModel model = buildReportModel();
    reporting::ReportDocumentModel previous;
    reporting::ReportConclusion previousConclusion;
    QStringList previousReasons;
    model.revision = reportGenerator_.loadLatestReport(
        currentTask_.paths.taskRootPath, &previous, &previousConclusion, &previousReasons, nullptr)
        ? previous.revision + 1 : 1;
    model.reportId = QStringLiteral("report_%1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    reportGenerationBusy_ = true;
    refreshReportView();
    const reporting::ReportResult result = reportGenerator_.generateReport(
        currentTask_.paths.taskRootPath, model);
    reportGenerationBusy_ = false;
    if (!result.success) {
        logError(QStringLiteral("Report"), QStringLiteral("报告生成失败: %1").arg(result.errorMessage));
        refreshReportView(result.errorMessage);
        return;
    }

    const auto attachReport = [this, &model, &result](
                                  const QString& suffix,
                                  const QString& name,
                                  const QString& absolutePath) {
        workspace::ArtifactRecord artifact;
        artifact.artifactId = QStringLiteral("%1_%2").arg(model.reportId, suffix);
        artifact.kind = workspace::ArtifactKind::Report;
        artifact.relativePath = QStringLiteral("reports/versions/%1/%2").arg(model.reportId, name);
        artifact.name = name;
        artifact.sha256 = fileSha256Text(absolutePath);
        artifact.sizeBytes = QFileInfo(absolutePath).size();
        artifact.createdAt = currentTimeText();
        artifact.metadata.insert(QStringLiteral("report_id"), model.reportId);
        artifact.metadata.insert(QStringLiteral("revision"), model.revision);
        artifact.metadata.insert(QStringLiteral("conclusion"), reporting::toString(result.conclusion));
        workspace_.attachArtifact(workspaceMode(), currentTask_.summary.taskId, artifact, nullptr);
    };
    attachReport(QStringLiteral("json"), QStringLiteral("report.json"),
                 QDir(result.versionPath).filePath(QStringLiteral("report.json")));
    attachReport(QStringLiteral("html"), QStringLiteral("report.html"),
                 QDir(result.versionPath).filePath(QStringLiteral("report.html")));
    logInfo(QStringLiteral("Report"), QStringLiteral("报告已生成: %1 / 结论: %2")
        .arg(result.reportPath, reporting::toString(result.conclusion)));
    refreshReportView();
}

void WorkbenchController::openReportHtml()
{
    if (!ensureTask()) {
        return;
    }
    const QString path = QDir(currentTask_.paths.reportsPath).filePath(QStringLiteral("report.html"));
    if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        logWarning(QStringLiteral("Report"), QStringLiteral("无法打开 HTML 报告: %1").arg(path));
    }
}

void WorkbenchController::openReportDirectory()
{
    if (!ensureTask()) {
        return;
    }
    if (!QFileInfo::exists(currentTask_.paths.reportsPath) ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(currentTask_.paths.reportsPath))) {
        logWarning(QStringLiteral("Report"), QStringLiteral("报告目录尚不存在。"));
    }
}

void WorkbenchController::copyReportPath()
{
    if (!ensureTask()) {
        return;
    }
    const QString path = QDir(currentTask_.paths.reportsPath).filePath(QStringLiteral("report.json"));
    if (QGuiApplication::clipboard() != nullptr) {
        QGuiApplication::clipboard()->setText(path);
        logInfo(QStringLiteral("Report"), QStringLiteral("报告路径已复制: %1").arg(path));
    }
}

void WorkbenchController::openReportArtifact(const QString& relativePath)
{
    if (!ensureTask()) {
        return;
    }
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    const QString root = QDir::fromNativeSeparators(QDir(currentTask_.paths.taskRootPath).absolutePath());
    const QString absolute = QDir::fromNativeSeparators(
        QFileInfo(QDir(root).filePath(clean)).absoluteFilePath());
    const QString rootPrefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (clean.isEmpty() || QDir::isAbsolutePath(clean) || clean.startsWith(QStringLiteral("../")) ||
        !absolute.startsWith(rootPrefix, Qt::CaseInsensitive) || !QFileInfo::exists(absolute)) {
        logWarning(QStringLiteral("Report"), QStringLiteral("报告记录路径无效或文件不存在: %1").arg(relativePath));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolute))) {
        logWarning(QStringLiteral("Report"), QStringLiteral("无法打开报告记录: %1").arg(relativePath));
    }
}

void WorkbenchController::showVerifySummary()
{
    setRamSummaryFromCurrentState(QStringLiteral("回读校验摘要"));
}

void WorkbenchController::showRunSummary()
{
    setRunSummaryFromCurrentState(QStringLiteral("运行摘要"), currentRunProgress_, currentStopProgress_);
}

void WorkbenchController::importWaveformFile()
{
    if (!ensureTask() || window_ == nullptr) {
        return;
    }

    const QString sourcePath = QFileDialog::getOpenFileName(
        window_, QStringLiteral("导入 VCD 波形"), QString(),
        QStringLiteral("Value Change Dump (*.vcd);;All Files (*.*)"));
    if (sourcePath.isEmpty()) {
        return;
    }

    const QString targetPath = QDir(currentTask_.paths.taskRootPath)
        .filePath(QStringLiteral("waveform/capture.vcd"));
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (QFileInfo(sourcePath).absoluteFilePath().compare(
            QFileInfo(targetPath).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
        QFile source(sourcePath);
        QSaveFile target(targetPath);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
            logError(QStringLiteral("Waveform"), QStringLiteral("无法读取或写入 VCD: %1").arg(sourcePath));
            return;
        }
        while (!source.atEnd()) {
            const QByteArray block = source.read(256 * 1024);
            if (block.isEmpty() && source.error() != QFileDevice::NoError) {
                target.cancelWriting();
                logError(QStringLiteral("Waveform"), QStringLiteral("读取 VCD 失败: %1").arg(source.errorString()));
                return;
            }
            if (target.write(block) != block.size()) {
                target.cancelWriting();
                logError(QStringLiteral("Waveform"), QStringLiteral("写入任务 VCD 失败: %1").arg(target.errorString()));
                return;
            }
        }
        if (!target.commit()) {
            logError(QStringLiteral("Waveform"), QStringLiteral("提交任务 VCD 失败: %1").arg(target.errorString()));
            return;
        }
    }

    const QString schemaPath = QDir(currentTask_.paths.taskRootPath)
        .filePath(protocol_analyzer::fixedTraceSchemaRelativePath());
    QJsonObject schema;
    schema.insert(QStringLiteral("schema_version"), QStringLiteral("1.0"));
    schema.insert(QStringLiteral("task_id"), currentTask_.summary.taskId);
    schema.insert(QStringLiteral("sample_signal"), QStringLiteral("CH0..CH1023"));
    schema.insert(QStringLiteral("sample_width"), 1024);
    schema.insert(QStringLiteral("physical_channels"), 1024);
    schema.insert(QStringLiteral("trace_profile_id"), QStringLiteral("trace.noelv.lockstep_1024"));
    QSaveFile schemaFile(schemaPath);
    const QByteArray schemaPayload = QJsonDocument(schema).toJson(QJsonDocument::Indented);
    if (!schemaFile.open(QIODevice::WriteOnly) ||
        schemaFile.write(schemaPayload) != schemaPayload.size() ||
        !schemaFile.commit()) {
        logError(QStringLiteral("Waveform"), QStringLiteral("写入 VCD schema 失败: %1").arg(schemaPath));
        return;
    }

    QFile::remove(QDir(currentTask_.paths.taskRootPath)
        .filePath(protocol_analyzer::fixedTraceAnalysisRelativePath()));
    logInfo(QStringLiteral("Waveform"), QStringLiteral("已导入 VCD，开始自动解析: %1").arg(sourcePath));
    analyzeCurrentTrace();
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
            traceSamplesToUi(model.samples),
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

bool WorkbenchController::analyzeCurrentTrace(const bool refreshAfterAnalysis)
{
    if (!ensureTask()) {
        return false;
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
    return result.success;
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

    window_->setActionButtonText(
        ui::UiAction::RunProgram,
        runButtonResetMode_ ? QStringLiteral("程序复位并运行") : QStringLiteral("程序运行"));
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
    runButtonResetMode_ = false;
    if (window_ != nullptr) {
        window_->setRamSummary(QStringLiteral("尚未选择程序镜像。"), 0, 0);
        window_->setRunSummary(QStringLiteral("程序运行控制摘要将在这里显示。"), 0, 0);
    }
    updateRunButtonText();
    updateProgramActionAvailability();
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
        runButtonResetMode_ = hasHaltRecord_ && haltRecord_.state == target_control::RunState::Halted;
    }
    updateRunButtonText();
    updateProgramActionAvailability();
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
    const QString diagnosticCode = diagnosticCodeFromMessage(message);
    const QString code = diagnosticCode.isEmpty() ? QStringLiteral("UI_BACKEND_ERROR") : diagnosticCode;
    const error_handling::ErrorSeverity severity = code == QStringLiteral("CAPTURE_RECOVERY_FAILED")
        ? error_handling::ErrorSeverity::Blocking : error_handling::ErrorSeverity::Error;
    recordError(code, severity, source, message, QString());
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
    debugConfig_.temporaryDirectoryPath = QDir(workspaceRootPath_).filePath(QStringLiteral(".lockstep_ui_preview_tmp"));
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
    object.insert(QStringLiteral("protocol_rule_status"), QStringLiteral("placeholder"));
    return object;
}

workspace::WorkspaceMode WorkbenchController::workspaceMode() const
{
    return (mode_ == ui::UiMode::Test) ? workspace::WorkspaceMode::Test : workspace::WorkspaceMode::Research;
}

}  // namespace lockstep::apps
