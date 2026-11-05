// SPDX-License-Identifier: Apache-2.0
#include "studio_theme.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace openstitch::desktop_v2 {

StudioColors studioColors(StudioScheme scheme) {
    if (scheme == StudioScheme::Dark) {
        return {"#191B21", "#242731", "#2D313D", "#414653", "#ECECF2", "#B6BBC8",
                "#F08CB7", "#483141"};
    }
    return {"#F7F6F3", "#FFFFFF", "#F1EFEB", "#D8D4CF", "#22252D", "#575D69",
            "#A92D66", "#F8E2EB"};
}

QString studioStyleSheet(StudioScheme scheme) {
    const auto c = studioColors(scheme);
    // A single style for native Qt widgets. The stitched canvas and real thread RGB swatches
    // remain untouched; all interactions continue through their existing widgets and actions.
    QString css = QStringLiteral(R"QSS(
QMainWindow#ShellWindow, QDialog, QDockWidget {
    background: %BG%;
    color: %FG%;
}
QMenuBar, QToolBar, QStatusBar {
    background: %BG%;
    border: 0;
    color: %FG%;
}
QToolBar {
    spacing: 4px;
    padding: 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: 5px;
}
QMenuBar::item:selected, QMenu::item:selected {
    background: %RAISED%;
    color: %FG%;
}
QMenu, QAbstractItemView, QTreeWidget, QListWidget {
    background: %SURFACE%;
    color: %FG%;
    border: 1px solid %BORDER%;
    outline: none;
}
QAbstractItemView::item {
    padding: 3px;
}
QAbstractItemView::item:selected {
    background: %ACCENT_SOFT%;
    color: %FG%;
}
QDockWidget::title {
    background: %RAISED%;
    color: %FG%;
    padding: 6px 10px;
    text-align: left;
}
QTabWidget::pane {
    border: 1px solid %BORDER%;
    background: %SURFACE%;
}
QTabBar::tab {
    background: %BG%;
    color: %MUTED%;
    padding: 7px 13px;
    border: 1px solid transparent;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected {
    color: %FG%;
    background: %SURFACE%;
    border-bottom-color: %ACCENT%;
}
QPushButton, QToolButton {
    background: %SURFACE%;
    color: %FG%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    padding: 5px 9px;
}
QPushButton:hover, QToolButton:hover {
    background: %RAISED%;
    border-color: %ACCENT%;
}
QToolButton:checked, QPushButton:checked {
    background: %ACCENT_SOFT%;
    color: %FG%;
    border-color: %ACCENT%;
}
QPushButton:disabled, QToolButton:disabled {
    color: %MUTED%;
    background: %BG%;
    border-color: %BORDER%;
}
QLineEdit, QComboBox, QAbstractSpinBox, QTextEdit, QPlainTextEdit {
    background: %SURFACE%;
    color: %FG%;
    border: 1px solid %BORDER%;
    border-radius: 5px;
    padding: 4px 6px;
    selection-background-color: %ACCENT_SOFT%;
    selection-color: %FG%;
}
QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus,
QPushButton:focus, QToolButton:focus, QTreeWidget:focus, QListWidget:focus {
    border: 2px solid %ACCENT%;
}
QProgressBar {
    background: %RAISED%;
    color: %FG%;
    border: 1px solid %BORDER%;
    border-radius: 4px;
    text-align: center;
}
QProgressBar::chunk {
    background: %ACCENT%;
    border-radius: 3px;
}
QLabel {
    color: %FG%;
}
)QSS");
    css.replace(QStringLiteral("%BG%"), c.background);
    css.replace(QStringLiteral("%SURFACE%"), c.surface);
    css.replace(QStringLiteral("%RAISED%"), c.surfaceRaised);
    css.replace(QStringLiteral("%BORDER%"), c.border);
    css.replace(QStringLiteral("%FG%"), c.text);
    css.replace(QStringLiteral("%MUTED%"), c.muted);
    css.replace(QStringLiteral("%ACCENT%"), c.accent);
    css.replace(QStringLiteral("%ACCENT_SOFT%"), c.accentSoft);
    return css;
}

void applyStudioTheme(QApplication& app, const QString& mode) {
    StudioScheme scheme = StudioScheme::Light;
    if (mode == QLatin1String("dark")) {
        scheme = StudioScheme::Dark;
    } else if (mode != QLatin1String("light")) {
        const auto system = QGuiApplication::styleHints()->colorScheme();
        if (system == Qt::ColorScheme::Dark ||
            (system == Qt::ColorScheme::Unknown &&
             app.palette().color(QPalette::Window).lightness() < 128)) {
            scheme = StudioScheme::Dark;
        }
    }
    app.setStyleSheet(studioStyleSheet(scheme));
}

} // namespace openstitch::desktop_v2
