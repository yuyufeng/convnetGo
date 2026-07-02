#pragma once

// Theme.h —— 全局深色主题（参考 Radmin LAN 风格）：深底、圆角、青色强调。
// 通过 qApp->setStyleSheet(kDarkTheme) 应用到全部窗口。

#include <QString>

inline QString darkThemeQss()
{
    return QStringLiteral(R"QSS(
* { font-size: 13px; }

QMainWindow, QDialog, QWidget#central, ChatWindow { background: #1b1f24; }

QMenuBar { background: #1b1f24; color: #c8ccd2; }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected { background: #2b3138; }
QMenu { background: #23282e; color: #d7dbe0; border: 1px solid #33383f; }
QMenu::item:selected { background: #0e7fbf; color: #ffffff; }

QToolBar { background: #161a1e; border: none; spacing: 2px; padding: 3px; }
QToolButton { color: #c8ccd2; padding: 5px 10px; border-radius: 5px; }
QToolButton:hover { background: #2b3138; }
QToolButton:pressed { background: #0e7fbf; color: #fff; }

QStatusBar { background: #161a1e; color: #8b929b; }
QStatusBar::item { border: none; }

QTreeWidget, QListWidget, QTableWidget {
  background: #1b1f24; color: #d7dbe0; border: none; outline: none;
  alternate-background-color: #1f242a;
}
QTreeWidget::item, QListWidget::item { padding: 5px 3px; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #2b3138; color: #ffffff; }
QTreeWidget::branch { background: #1b1f24; }
QHeaderView::section { background: #1b1f24; color: #8b929b; border: none; padding: 4px; }

QLabel { color: #d7dbe0; background: transparent; }

QLineEdit, QSpinBox, QComboBox, QTextEdit, QTextBrowser, QPlainTextEdit {
  background: #23282e; color: #e6e9ee; border: 1px solid #333a42;
  border-radius: 5px; padding: 3px 6px; selection-background-color: #0e7fbf;
}
QComboBox QAbstractItemView { background: #23282e; color: #e6e9ee; selection-background-color: #0e7fbf; }

QPushButton {
  background: #2b3138; color: #e6e9ee; border: 1px solid #3a424b;
  border-radius: 5px; padding: 5px 14px;
}
QPushButton:hover { background: #343b44; }
QPushButton:pressed { background: #0e7fbf; color: #fff; }
QPushButton:disabled { color: #6b7178; border-color: #2b3138; }

QCheckBox, QRadioButton { color: #d7dbe0; spacing: 6px; }
QGroupBox { color: #a9b0b8; border: 1px solid #2f353c; border-radius: 6px; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }

QScrollBar:vertical { background: #1b1f24; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #3a424b; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #4a535d; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }

/* 顶部“我”的卡片（Radmin 风格） */
QFrame#selfCard { background: #242b33; border-radius: 10px; }
QLabel#selfNick { color: #29b6f6; font-size: 16px; font-weight: bold; }
QLabel#selfIp   { color: #e6e9ee; font-size: 14px; }
)QSS");
}
