/**********************************************************
* 文件名: ui_theme.cpp
* 日期: 2026-07-06
* 版本: v3.0
* 更新记录: v1.0 初版创建 UI 主题实现；v2.0 调整为浅色工作台配色；v3.0 按源工程 MainWindow 样式结构做浅色映射
* 描述: 提供复刻源工程工作台布局的 Qt Widgets 浅色样式表
**********************************************************/

#include "ui_theme.h"

#include <QWidget>

namespace lockstep::ui {

QString UiTheme::workbenchStyleSheet()
{
    return QStringLiteral(R"(
        QWidget#workbench_shell {
            background-color: #eceff3;
            color: #1f2937;
        }
        QWidget {
            font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
            font-size: 13px;
            color: #1f2937;
        }
        QWidget#workbench_body,
        QWidget#page_container,
        QWidget[workbenchPageContent="true"],
        QWidget#workbench_scroll_viewport,
        QStackedWidget#page_stack {
            background-color: #eceff3;
            color: #1f2937;
        }
        QLabel {
            background-color: transparent;
            color: #374151;
        }
        QLabel#workbench_title {
            color: #111827;
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#page_title {
            color: #111827;
            font-size: 21px;
            font-weight: 700;
            padding-bottom: 2px;
        }
        QLabel#muted_label,
        QLabel#todo_card_label,
        QLabel[mutedLabel="true"] {
            color: #6b7280;
            background-color: transparent;
        }
        QLabel#sidebar_caption {
            color: #64748b;
            font-size: 11px;
            font-weight: 700;
            padding: 0 8px 8px 8px;
        }
        QLabel[statusPill="true"] {
            background-color: #e8f1ff;
            border: 1px solid #c8dbf7;
            border-radius: 4px;
            color: #1d39c4;
            font-weight: 600;
            padding: 4px 9px;
        }
        QLabel[metricValue="true"] {
            color: #1677ff;
            font-size: 19px;
            font-weight: 700;
        }
        QWidget#top_bar,
        QFrame#top_bar {
            background-color: #f7f8fa;
            border-bottom: 1px solid #d7dde5;
        }
        QFrame#sidebar {
            background-color: #eef2f6;
            border-right: 1px solid #d7dde5;
        }
        QPushButton[navButton="true"] {
            color: #334155;
            background-color: transparent;
            border: 0;
            border-left: 3px solid transparent;
            border-radius: 6px;
            padding: 9px 10px;
            text-align: left;
        }
        QPushButton[navButton="true"]:checked {
            color: #0958d9;
            background-color: #dcecff;
            border-left: 3px solid #1677ff;
            font-weight: 700;
        }
        QPushButton[navButton="true"]:hover {
            color: #1677ff;
            background-color: #e7f0ff;
        }
        QPushButton[navButton="true"]:focus {
            border: 1px solid #1677ff;
            border-left: 3px solid #1677ff;
        }
        QGroupBox[panelBox="true"] {
            background-color: #f7f8fa;
            border: 1px solid #d7dde5;
            border-radius: 8px;
            margin-top: 18px;
            padding: 14px;
            font-weight: 600;
            color: #374151;
        }
        QGroupBox[panelBox="true"]::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            padding: 0 6px;
            color: #64748b;
            background-color: #eceff3;
        }
        QGroupBox[panelBox="true"]:checked::title {
            color: #374151;
        }
        QGroupBox#raw_log {
            border-color: #d7dde5;
            border-radius: 6px;
            margin-top: 12px;
            padding: 10px;
        }
        QGroupBox#raw_log::title {
            left: 10px;
            padding: 0 5px;
        }
        QWidget#diagnostics_panel,
        QWidget#log_content_widget,
        QWidget#serial_monitor_panel {
            background-color: #eceff3;
        }
        QTabWidget#diagnostics_output_tabs {
            background-color: #eceff3;
            border: 0;
        }
        QTabWidget#diagnostics_output_tabs::pane {
            background-color: #f7f8fa;
            border: 1px solid #d7dde5;
            border-radius: 6px;
            top: -1px;
        }
        QTabWidget#diagnostics_output_tabs QTabBar::tab {
            background-color: #f2f4f7;
            border: 1px solid #d7dde5;
            color: #64748b;
            min-width: 84px;
            padding: 5px 12px;
        }
        QTabWidget#diagnostics_output_tabs QTabBar::tab:selected {
            background-color: #dcecff;
            border-color: #89bdf5;
            color: #0958d9;
        }
        QToolButton#log_detach_button {
            background-color: transparent;
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 1px;
            margin: 0;
        }
        QToolButton#log_detach_button:hover {
            background-color: #e7f0ff;
            border-color: #89bdf5;
        }
        QToolButton#log_detach_button:pressed {
            background-color: #dcecff;
            border-color: #1677ff;
        }
        QLabel#serial_status_label {
            color: #64748b;
        }
        QLineEdit,
        QComboBox,
        QSpinBox,
        QCheckBox,
        QRadioButton,
        QPlainTextEdit {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            border-radius: 5px;
            color: #1f2937;
            padding: 6px;
            selection-background-color: #1677ff;
            selection-color: #ffffff;
        }
        QLineEdit:focus,
        QComboBox:focus,
        QSpinBox:focus,
        QPlainTextEdit:focus {
            border-color: #1677ff;
        }
        QLineEdit:disabled,
        QComboBox:disabled,
        QSpinBox:disabled,
        QPlainTextEdit:disabled {
            background-color: #e8ecf2;
            border-color: #d7dde5;
            color: #9ca3af;
        }
        QCheckBox,
        QRadioButton {
            background-color: transparent;
            border: 0;
            color: #374151;
            spacing: 8px;
            padding: 3px 0;
        }
        QCheckBox:disabled,
        QRadioButton:disabled {
            color: #9ca3af;
        }
        QCheckBox::indicator,
        QRadioButton::indicator {
            width: 14px;
            height: 14px;
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
        }
        QCheckBox::indicator {
            border-radius: 3px;
        }
        QRadioButton::indicator {
            border-radius: 7px;
        }
        QCheckBox::indicator:checked,
        QRadioButton::indicator:checked {
            background-color: #1677ff;
            border-color: #1677ff;
        }
        QPlainTextEdit {
            font-family: "Consolas", "JetBrains Mono", monospace;
            font-size: 12px;
        }
        QComboBox::drop-down,
        QSpinBox::up-button,
        QSpinBox::down-button {
            background-color: #eef2f6;
            border-left: 1px solid #cfd6df;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            color: #1f2937;
            selection-background-color: #dcecff;
            selection-color: #0958d9;
        }
        QPushButton {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            border-radius: 5px;
            color: #1f2937;
            padding: 7px 12px;
        }
        QPushButton:hover {
            background-color: #e7f0ff;
            border-color: #4096ff;
            color: #1677ff;
        }
        QPushButton:pressed {
            background-color: #dcecff;
            border-color: #0958d9;
            color: #0958d9;
        }
        QPushButton:focus {
            border-color: #1677ff;
        }
        QPushButton:disabled {
            background-color: #e8ecf2;
            border-color: #d7dde5;
            color: #9ca3af;
        }
        QPushButton#primary_button,
        QPushButton[primary_button="true"],
        QPushButton[primaryButton="true"] {
            background-color: #1677ff;
            border-color: #1677ff;
            color: #ffffff;
            font-weight: 700;
        }
        QPushButton#primary_button:hover,
        QPushButton[primary_button="true"]:hover,
        QPushButton[primaryButton="true"]:hover {
            background-color: #4096ff;
            border-color: #4096ff;
            color: #ffffff;
        }
        QPushButton[summaryTab="true"] {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            border-radius: 5px;
            color: #64748b;
            font-weight: 600;
            padding: 7px 12px;
        }
        QPushButton[summaryTab="true"]:checked {
            background-color: #dcecff;
            border-color: #89bdf5;
            color: #0958d9;
        }
        QPushButton[summaryTab="true"]:hover {
            border-color: #1677ff;
            color: #0958d9;
        }
        QPushButton#danger_button,
        QPushButton[danger_button="true"] {
            background-color: #ff4d4f;
            border-color: #ff4d4f;
            color: #ffffff;
        }
        QPushButton#danger_button:hover,
        QPushButton[danger_button="true"]:hover {
            background-color: #ff7875;
            border-color: #ff7875;
            color: #ffffff;
        }
        QPushButton#tool_icon_button {
            padding: 4px;
            min-width: 28px;
        }
        QProgressBar {
            background-color: #eef2f6;
            border: 1px solid #cfd6df;
            border-radius: 4px;
            color: #1f2937;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #1677ff;
            border-radius: 3px;
        }
        QScrollArea,
        QScrollArea#workbench_scroll_page,
        QScrollArea > QWidget#workbench_scroll_viewport,
        QScrollArea::viewport {
            background-color: #eceff3;
            border: 0;
            color: #1f2937;
        }
        QTreeWidget,
        QTableWidget {
            background-color: #f7f8fa;
            alternate-background-color: #f1f3f6;
            border: 1px solid #d7dde5;
            border-radius: 6px;
            color: #1f2937;
            gridline-color: #d7dde5;
            outline: 0;
            selection-background-color: #dcecff;
            selection-color: #0958d9;
        }
        QTreeWidget::item,
        QTableWidget::item {
            padding: 5px;
            border-bottom: 1px solid #f1f5f9;
        }
        QTreeWidget::item:selected,
        QTableWidget::item:selected {
            border-left: 3px solid #1677ff;
            color: #0958d9;
        }
        QTreeWidget#project_browser_tree::item:selected {
            background-color: #dcecff;
            color: #0958d9;
        }
        QHeaderView::section {
            background-color: #eef2f6;
            border: 0;
            border-bottom: 1px solid #d7dde5;
            color: #64748b;
            font-weight: 700;
            padding: 6px;
        }
        QMenu {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            color: #1f2937;
        }
        QMenu::item:selected {
            background-color: #dcecff;
            color: #0958d9;
        }
        QToolTip {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            color: #1f2937;
            padding: 4px;
        }
        QWidget#waveform_title_controls,
        QWidget#waveform_header_export_row {
            background-color: #eceff3;
        }
        QWidget#waveform_analyzer_panel {
            background-color: #f7f8fa;
            border: 1px solid #cfd6df;
            border-radius: 6px;
        }
        QWidget#waveform_embed_host {
            background-color: #f7f8fa;
            border: 0;
        }
        QSplitter {
            background-color: #eceff3;
        }
        QSplitter::handle {
            background-color: #d7dde5;
        }
        QScrollBar:vertical {
            background-color: #eceff3;
            margin: 0;
            width: 10px;
        }
        QScrollBar::handle:vertical {
            background-color: #cbd5e1;
            border-radius: 4px;
            min-height: 28px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar:horizontal {
            background-color: #eceff3;
            height: 10px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background-color: #cbd5e1;
            border-radius: 4px;
            min-width: 28px;
        }
        QScrollBar::add-line:horizontal,
        QScrollBar::sub-line:horizontal {
            width: 0;
        }
    )");
}

void UiTheme::applyWorkbenchStyle(QWidget* const widget)
{
    if (widget != nullptr) {
        widget->setStyleSheet(workbenchStyleSheet());
    }
}

}  // namespace lockstep::ui
