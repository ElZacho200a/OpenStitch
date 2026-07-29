// SPDX-License-Identifier: Apache-2.0
#include "app_theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QSettings>

namespace openstitch::desktop {

namespace {

QSettings settings() {
    return QSettings(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
}

QPalette build_palette(const Tokens& t) {
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surfaceRaised);
    p.setColor(QPalette::AlternateBase, t.surface);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.surface);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::ToolTipBase, t.surfaceRaised);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::Highlight, t.selection);
    p.setColor(QPalette::HighlightedText, t.selectionText);
    p.setColor(QPalette::PlaceholderText, t.textSecondary);
    p.setColor(QPalette::Link, t.info);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Dark, t.border);
    // États désactivés reconnaissables (contraste réduit mais lisible).
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.textSecondary);
    p.setColor(QPalette::Disabled, QPalette::Text, t.textSecondary);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.textSecondary);
    return p;
}

// Feuille de style CIBLÉE (widgets nommés), pas monolithique : elle raffine
// bordures, focus, sélection et espacements ; la palette fait le reste.
QString build_stylesheet(const Tokens& t) {
    const auto c = [](const QColor& col) { return col.name(QColor::HexRgb); };
    QString qss;
    qss += QStringLiteral("QMainWindow, QDialog { background: %1; }\n").arg(c(t.window));
    qss += QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; padding: %4px; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.border))
               .arg(t.space2);

    qss += QStringLiteral("QMenuBar { background: %1; }\n").arg(c(t.surface));
    qss += QStringLiteral("QMenuBar::item { padding: %1px %2px; }\n").arg(t.space1).arg(t.space3);
    qss += QStringLiteral("QMenuBar::item:selected { background: %1; color: %2; }\n")
               .arg(c(t.accent), c(t.selectionText));
    qss += QStringLiteral("QMenu { background: %1; border: 1px solid %2; }\n")
               .arg(c(t.surfaceRaised), c(t.border));
    qss += QStringLiteral("QMenu::item { padding: %1px %2px; }\n").arg(t.space2).arg(t.space4);
    qss += QStringLiteral("QMenu::item:selected { background: %1; color: %2; }\n")
               .arg(c(t.accent), c(t.selectionText));
    qss += QStringLiteral("QMenu::separator { height: 1px; background: %1; margin: %2px 0; }\n")
               .arg(c(t.border))
               .arg(t.space1);

    qss += QStringLiteral("QToolBar { background: %1; border: none; spacing: %2px; padding: %3px; }\n")
               .arg(c(t.surface))
               .arg(t.space2)
               .arg(t.space1);
    qss += QStringLiteral("QStatusBar { background: %1; }\n").arg(c(t.surface));
    qss += QStringLiteral("QStatusBar::item { border: none; }\n");

    qss += QStringLiteral("QDockWidget::title { background: %1; padding: %2px; border-bottom: 1px solid %3; }\n")
               .arg(c(t.surface))
               .arg(t.space2)
               .arg(c(t.border));

    qss += QStringLiteral(
               "QPushButton { background: %1; color: %2; border: 1px solid %3; "
               "border-radius: %4px; padding: %5px %6px; min-height: %7px; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.border))
               .arg(t.radiusSm)
               .arg(t.space1)
               .arg(t.space3)
               .arg(t.controlHeight);
    qss += QStringLiteral("QPushButton:hover { border-color: %1; }\n").arg(c(t.accent));
    qss += QStringLiteral("QPushButton:pressed { background: %1; }\n").arg(c(t.window));
    qss += QStringLiteral("QPushButton:focus { border: 1px solid %1; }\n").arg(c(t.focus));
    qss += QStringLiteral("QPushButton:disabled { color: %1; border-color: %2; }\n")
               .arg(c(t.textSecondary), c(t.border));

    qss += QStringLiteral(
               "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %1; color: %2; "
               "border: 1px solid %3; border-radius: %4px; padding: %5px %6px; min-height: %7px; "
               "selection-background-color: %8; selection-color: %9; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.border))
               .arg(t.radiusSm)
               .arg(t.space1)
               .arg(t.space2)
               .arg(t.controlHeight)
               .arg(c(t.accent), c(t.selectionText));
    qss += QStringLiteral(
               "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { "
               "border: 1px solid %1; }\n")
               .arg(c(t.focus));

    qss += QStringLiteral(
               "QListView, QListWidget, QTreeView { background: %1; border: 1px solid %2; }\n")
               .arg(c(t.surfaceRaised), c(t.border));
    qss += QStringLiteral(
               "QListView::item:selected, QListWidget::item:selected, "
               "QTreeView::item:selected { background: %1; color: %2; }\n")
               .arg(c(t.selection), c(t.selectionText));
    qss += QStringLiteral(
               "QListView::item:hover, QListWidget::item:hover { background: %1; }\n")
               .arg(c(t.surface));

    qss += QStringLiteral("QLabel { color: %1; }\n").arg(c(t.text));
    qss += QStringLiteral("QGroupBox { border: 1px solid %1; border-radius: %2px; margin-top: %3px; }\n")
               .arg(c(t.border))
               .arg(t.radiusSm)
               .arg(t.space3);
    qss += QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: %1px; padding: 0 %2px; color: %3; }\n")
               .arg(t.space3)
               .arg(t.space1)
               .arg(c(t.textSecondary));

    qss += QStringLiteral("QScrollBar:vertical { background: %1; width: 12px; margin: 0; }\n")
               .arg(c(t.surface));
    qss += QStringLiteral("QScrollBar::handle:vertical { background: %1; border-radius: %2px; min-height: 24px; }\n")
               .arg(c(t.border))
               .arg(t.radiusSm);
    qss += QStringLiteral("QScrollBar:horizontal { background: %1; height: 12px; margin: 0; }\n")
               .arg(c(t.surface));
    qss += QStringLiteral("QScrollBar::handle:horizontal { background: %1; border-radius: %2px; min-width: 24px; }\n")
               .arg(c(t.border))
               .arg(t.radiusSm);
    qss += QStringLiteral(
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }\n");

    return qss;
}

}  // namespace

AppTheme& AppTheme::instance() {
    static AppTheme theme;
    return theme;
}

void AppTheme::applyToApp(QApplication& app) {
    load();
    tokens_ = tokens_for(mode_, density_);
    app.setPalette(build_palette(tokens_));
    app.setStyleSheet(build_stylesheet(tokens_));
    emit changed();
}

void AppTheme::reapply() {
    tokens_ = tokens_for(mode_, density_);
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance())) {
        app->setPalette(build_palette(tokens_));
        app->setStyleSheet(build_stylesheet(tokens_));
    }
    save();
    emit changed();
}

void AppTheme::setMode(ThemeMode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    reapply();
}

void AppTheme::setDensity(Density density) {
    if (density_ == density) {
        return;
    }
    density_ = density;
    reapply();
}

void AppTheme::load() {
    auto s = settings();
    mode_ = s.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString() ==
                    QStringLiteral("dark")
                ? ThemeMode::Dark
                : ThemeMode::Light;
    density_ = s.value(QStringLiteral("ui/density"), QStringLiteral("comfortable")).toString() ==
                       QStringLiteral("compact")
                   ? Density::Compact
                   : Density::Comfortable;
}

void AppTheme::save() const {
    auto s = settings();
    s.setValue(QStringLiteral("ui/theme"),
               mode_ == ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    s.setValue(QStringLiteral("ui/density"), density_ == Density::Compact
                                                 ? QStringLiteral("compact")
                                                 : QStringLiteral("comfortable"));
}

}  // namespace openstitch::desktop
