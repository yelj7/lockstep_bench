/*****************************************************************************
*  @file      workspace_selection_dialog.h
*  @brief     工作区选择对话框接口
*  Details.   声明启动阶段工作区选择、默认路径解析和工作区路径持久化接口。
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

#ifndef LOCKSTEP_HOST_SRC_UI_COMMON_WORKSPACE_SELECTION_DIALOG_H_
#define LOCKSTEP_HOST_SRC_UI_COMMON_WORKSPACE_SELECTION_DIALOG_H_

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QWidget;

namespace lockstep::ui {

class WorkspaceSelectionDialog final : public QDialog {
public:
    explicit WorkspaceSelectionDialog(const QString& defaultRoot, QWidget* parent = nullptr);

    [[nodiscard]] QString selectedRoot() const;

    [[nodiscard]] static QString defaultWorkspaceRoot();
    [[nodiscard]] static bool selectWorkspaceRoot(QWidget* parent, QString* selectedRoot);

private:
    void browseWorkspace();
    void acceptSelection();
    void setErrorText(const QString& text);

    QLineEdit* pathEdit_;
    QLabel* errorLabel_;
};

}  // namespace lockstep::ui

#endif  // LOCKSTEP_HOST_SRC_UI_COMMON_WORKSPACE_SELECTION_DIALOG_H_
