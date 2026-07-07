/*****************************************************************************
*  @file      workspace_selection_dialog.cpp
*  @brief     工作区选择对话框实现
*  Details.   实现启动阶段工作区选择、自定义样式、默认路径解析和工作区路径持久化流程。
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

#include "workspace_selection_dialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "ui_theme.h"

namespace lockstep::ui {
namespace {

constexpr int kPathEditMinimumWidth = 520;
constexpr int kDialogMinimumWidth = 680;

QString normalizedPathText(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(trimmedPath));
}

QString workspaceDialogStyleSheet()
{
    return UiTheme::workbenchStyleSheet() + QStringLiteral(R"(
        QDialog#workspace_select_dialog {
            background-color: #eceff3;
            color: #1f2937;
        }
        QFrame#workspace_select_panel {
            background-color: #f7f8fa;
            border: 1px solid #d7dde5;
            border-radius: 8px;
        }
        QLabel#workspace_select_title {
            color: #111827;
            font-size: 21px;
            font-weight: 700;
        }
        QLabel#workspace_select_path_label {
            color: #374151;
            font-weight: 700;
        }
        QLabel#workspace_select_error {
            color: #b42318;
            font-weight: 600;
        }
        QPushButton#workspace_select_open_button {
            background-color: #1677ff;
            border-color: #1677ff;
            color: #ffffff;
            font-weight: 700;
            min-width: 104px;
        }
        QPushButton#workspace_select_open_button:hover {
            background-color: #4096ff;
            border-color: #4096ff;
            color: #ffffff;
        }
        QPushButton#workspace_select_cancel_button,
        QPushButton#workspace_select_browse_button {
            min-width: 82px;
        }
    )");
}

}  // namespace

WorkspaceSelectionDialog::WorkspaceSelectionDialog(const QString& defaultRoot, QWidget* const parent)
    : QDialog(parent),
      pathEdit_(nullptr),
      errorLabel_(nullptr)
{
    setObjectName(QStringLiteral("workspace_select_dialog"));
    setWindowTitle(QStringLiteral("选择工作区"));
    setModal(true);
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(kDialogMinimumWidth);
    setStyleSheet(workspaceDialogStyleSheet());

    QVBoxLayout* const rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 18, 20, 16);
    rootLayout->setSpacing(12);

    QFrame* const panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("workspace_select_panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* const panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 16, 18, 16);
    panelLayout->setSpacing(12);

    QHBoxLayout* const titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);
    QLabel* const titleLabel = new QLabel(QStringLiteral("选择工作区"), panel);
    titleLabel->setObjectName(QStringLiteral("workspace_select_title"));
    titleLayout->addWidget(titleLabel, 1);
    panelLayout->addLayout(titleLayout);

    QLabel* const pathLabel = new QLabel(QStringLiteral("工作区路径"), panel);
    pathLabel->setObjectName(QStringLiteral("workspace_select_path_label"));
    panelLayout->addWidget(pathLabel);

    QHBoxLayout* const pathLayout = new QHBoxLayout();
    pathLayout->setSpacing(8);
    pathEdit_ = new QLineEdit(panel);
    pathEdit_->setObjectName(QStringLiteral("workspace_select_path_edit"));
    pathEdit_->setText(QDir::toNativeSeparators(normalizedPathText(defaultRoot)));
    pathEdit_->setMinimumWidth(kPathEditMinimumWidth);
    QPushButton* const browseButton = new QPushButton(QStringLiteral("浏览"), panel);
    browseButton->setObjectName(QStringLiteral("workspace_select_browse_button"));
    browseButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    pathLayout->addWidget(pathEdit_, 1);
    pathLayout->addWidget(browseButton, 0);
    panelLayout->addLayout(pathLayout);

    errorLabel_ = new QLabel(panel);
    errorLabel_->setObjectName(QStringLiteral("workspace_select_error"));
    errorLabel_->setVisible(false);
    errorLabel_->setWordWrap(true);
    panelLayout->addWidget(errorLabel_);

    rootLayout->addWidget(panel);

    QHBoxLayout* const buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch(1);
    QPushButton* const cancelButton = new QPushButton(QStringLiteral("取消"), this);
    cancelButton->setObjectName(QStringLiteral("workspace_select_cancel_button"));
    QPushButton* const openButton = new QPushButton(QStringLiteral("新建/打开工作区"), this);
    openButton->setObjectName(QStringLiteral("workspace_select_open_button"));
    openButton->setDefault(true);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(openButton);
    rootLayout->addLayout(buttonLayout);

    connect(browseButton, &QPushButton::clicked, this, [this]() {
        browseWorkspace();
    });
    connect(openButton, &QPushButton::clicked, this, [this]() {
        acceptSelection();
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(pathEdit_, &QLineEdit::textChanged, this, [this]() {
        setErrorText(QString());
    });
}

QString WorkspaceSelectionDialog::selectedRoot() const
{
    return normalizedPathText(pathEdit_ == nullptr ? QString() : pathEdit_->text());
}

QString WorkspaceSelectionDialog::defaultWorkspaceRoot()
{
    QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.trimmed().isEmpty()) {
        appDir = QDir::homePath();
    }

    return QDir(QDir::fromNativeSeparators(appDir)).filePath(QStringLiteral("workspace"));
}

bool WorkspaceSelectionDialog::selectWorkspaceRoot(QWidget* const parent, QString* const selectedRoot)
{
    WorkspaceSelectionDialog dialog(defaultWorkspaceRoot(), parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    if (selectedRoot != nullptr) {
        *selectedRoot = dialog.selectedRoot();
    }
    return true;
}

void WorkspaceSelectionDialog::browseWorkspace()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择工作区"),
        pathEdit_ == nullptr ? QString() : pathEdit_->text().trimmed(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!chosen.trimmed().isEmpty() && pathEdit_ != nullptr) {
        pathEdit_->setText(QDir::toNativeSeparators(normalizedPathText(chosen)));
    }
}

void WorkspaceSelectionDialog::acceptSelection()
{
    if (selectedRoot().isEmpty()) {
        setErrorText(QStringLiteral("工作区路径不能为空。"));
        return;
    }
    accept();
}

void WorkspaceSelectionDialog::setErrorText(const QString& text)
{
    if (errorLabel_ == nullptr) {
        return;
    }

    const QString normalized = text.trimmed();
    errorLabel_->setText(normalized);
    errorLabel_->setVisible(!normalized.isEmpty());
}

}  // namespace lockstep::ui
