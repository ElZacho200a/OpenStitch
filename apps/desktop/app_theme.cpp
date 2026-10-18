// SPDX-License-Identifier: Apache-2.0
#include "app_theme.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QWidget>

namespace openstitch::desktop {

namespace {

QSettings settings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenStitch"),
                     QStringLiteral("OpenStitch Studio"));
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
    // Teinte dédiée, distincte du texte d'aide (textSecondary) : un contrôle grisé ne doit
    // pas se confondre avec une légende ordinaire.
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::Text, t.textDisabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.textDisabled);
    return p;
}

// Image de case cochée / indéterminée (QSS ne sait pas dessiner une coche) : PNG 1x et 2x
// générés dans le dossier temporaire, nommés d'après leurs couleurs (donc jamais périmés).
QString indicator_image(const QColor& fill, const QColor& mark, bool dash) {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("openstitch-theme"));
    QDir().mkpath(dir);
    const QString base = QStringLiteral("%1/ind-%2-%3-%4")
                             .arg(dir, dash ? QStringLiteral("dash") : QStringLiteral("check"),
                                  fill.name().mid(1), mark.name().mid(1));
    for (const int scale : {1, 2}) {
        const QString file =
            scale == 1 ? base + QStringLiteral(".png") : base + QStringLiteral("@2x.png");
        if (QFile::exists(file)) {
            continue;
        }
        QPixmap pm(16 * scale, 16 * scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(scale, scale);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 4, 4);
        QPen pen(mark, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        if (dash) {
            p.drawLine(QPointF(4.5, 8), QPointF(11.5, 8));
        } else {
            p.drawPolyline(QPolygonF({{4.2, 8.4}, {7, 11.2}, {11.8, 5}}));
        }
        p.end();
        pm.save(file);
    }
    return QString(base).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

// Feuille de style CIBLÉE (widgets nommés), pas monolithique : elle raffine
// bordures, focus, sélection et espacements ; la palette fait le reste.
QString build_stylesheet(const Tokens& t) {
    const auto c = [](const QColor& col) { return col.name(QColor::HexRgb); };
    QString qss;
    qss += QStringLiteral("QMainWindow, QDialog { background: %1; }\n").arg(c(t.window));
    qss += QStringLiteral("QToolTip { background: %1; color: %2; border: 1px solid %3; "
                          "border-radius: %5px; padding: %4px; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.borderStrong))
               .arg(t.space2)
               .arg(t.radiusMd);

    qss += QStringLiteral("QMenuBar { background: %1; }\n").arg(c(t.surface));
    qss += QStringLiteral("QMenuBar::item { padding: %1px %2px; }\n").arg(t.space1).arg(t.space3);
    qss += QStringLiteral("QMenuBar::item:selected { background: %1; color: %2; "
                          "border-radius: %3px; }\n")
               .arg(c(t.accentSoft), c(t.text))
               .arg(t.radiusSm);
    qss += QStringLiteral("QMenu { background: %1; border: 1px solid %2; border-radius: %3px; "
                          "padding: %4px; }\n")
               .arg(c(t.surfaceRaised), c(t.border))
               .arg(t.radiusLg)
               .arg(t.space2);
    qss += QStringLiteral("QMenu::item { padding: %1px %2px; border-radius: %3px; }\n")
               .arg(t.space2)
               .arg(t.space4)
               .arg(t.radiusSm);
    qss += QStringLiteral("QMenu::item:selected { background: %1; color: %2; }\n")
               .arg(c(t.accentSoft), c(t.text));
    qss += QStringLiteral("QMenu::separator { height: 1px; background: %1; margin: %2px 0; }\n")
               .arg(c(t.border))
               .arg(t.space1);

    qss +=
        QStringLiteral("QToolBar { background: %1; border: none; spacing: %2px; padding: %3px; }\n")
            .arg(c(t.surface))
            .arg(t.space2)
            .arg(t.space1);
    qss += QStringLiteral("QStatusBar { background: %1; }\n").arg(c(t.surface));
    qss += QStringLiteral("QStatusBar::item { border: none; }\n");

    qss += QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                          "border-radius: %4px; padding: %5px %6px; min-height: %7px; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.borderStrong))
               .arg(t.radiusSm)
               .arg(t.space1)
               .arg(t.space3)
               .arg(t.controlHeight);
    qss += QStringLiteral("QPushButton:hover { border-color: %1; background: %2; }\n")
               .arg(c(t.accent), c(t.accentSoft));
    qss += QStringLiteral("QPushButton:pressed { background: %1; }\n").arg(c(t.window));
    qss += QStringLiteral("QPushButton:focus { border: 1px solid %1; }\n").arg(c(t.focus));
    qss += QStringLiteral("QPushButton:disabled { color: %1; background: %2; border-color: %3; }\n")
               .arg(c(t.textDisabled), c(t.window), c(t.surfaceRaised));
    // Anneau de focus clavier sur les contrôles que le style natif ne dessine pas assez.
    qss += QStringLiteral("QToolButton:focus, QTabBar::tab:focus { border: 1px solid %1; }\n")
               .arg(c(t.focus));

    qss += QStringLiteral(
               "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %1; color: %2; "
               "border: 1px solid %3; border-radius: %4px; padding: %5px %6px; min-height: %7px; "
               "selection-background-color: %8; selection-color: %9; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.borderStrong))
               .arg(t.radiusSm)
               .arg(t.space1)
               .arg(t.space2)
               .arg(t.controlHeight)
               .arg(c(t.accent), c(t.selectionText));
    qss +=
        QStringLiteral("QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { "
                       "border: 1px solid %1; }\n")
            .arg(c(t.focus));

    qss +=
        QStringLiteral("QListView, QListWidget, QTreeView { background: %1; border: 1px solid %2; "
                       "border-radius: %3px; }\n")
            .arg(c(t.surfaceRaised), c(t.borderStrong))
            .arg(t.radiusSm);
    qss += QStringLiteral("QListView::item:selected, QListWidget::item:selected, "
                          "QTreeView::item:selected { background: %1; color: %2; }\n")
               .arg(c(t.selection), c(t.selectionText));
    qss += QStringLiteral("QListView::item:hover:!selected, QListWidget::item:hover:!selected { "
                          "background: %1; }\n")
               .arg(c(t.accentSoft));

    qss += QStringLiteral("QLabel { color: %1; }\n").arg(c(t.text));
    qss += QStringLiteral("QLabel[secondary=\"true\"] { color: %1; }\n").arg(c(t.textSecondary));
    qss += QStringLiteral(
               "QGroupBox { border: 1px solid %1; border-radius: %2px; margin-top: %3px; }\n")
               .arg(c(t.border))
               .arg(t.radiusMd)
               .arg(t.space3);
    qss += QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: %1px; padding: 0 "
                          "%2px; color: %3; }\n")
               .arg(t.space3)
               .arg(t.space1)
               .arg(c(t.textSecondary));

    qss += QStringLiteral("QScrollBar:vertical { background: %1; width: 14px; margin: 0; }\n")
               .arg(c(t.surface));
    qss += QStringLiteral("QScrollBar::handle:vertical { background: %1; border-radius: %2px; "
                          "min-height: 24px; }\n")
               .arg(c(t.borderStrong))
               .arg(t.radiusSm);
    qss += QStringLiteral("QScrollBar:horizontal { background: %1; height: 14px; margin: 0; }\n")
               .arg(c(t.surface));
    qss += QStringLiteral("QScrollBar::handle:horizontal { background: %1; border-radius: %2px; "
                          "min-width: 24px; }\n")
               .arg(c(t.borderStrong))
               .arg(t.radiusSm);
    qss += QStringLiteral("QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }\n");

    // --- Composants « refonte » : boutons d'icônes, onglets, cartes, pastilles d'état ---
    const auto tint = [](const QColor& col, int alpha) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(col.red())
            .arg(col.green())
            .arg(col.blue())
            .arg(alpha);
    };
    qss +=
        QStringLiteral("QDockWidget { font-weight: 600; }\n"
                       "QDockWidget::title { background: %1; padding: %2px %3px; text-align: left; "
                       "border-bottom: 1px solid %4; }\n")
            .arg(c(t.surface))
            .arg(t.space3)
            .arg(t.space4)
            .arg(c(t.border));
    qss += QStringLiteral(
               "QToolBar QToolButton { background: transparent; border: 1px solid transparent; "
               "border-radius: %1px; padding: %2px; color: %3; }\n"
               "QToolBar QToolButton:hover { background: %4; }\n"
               "QToolBar QToolButton:pressed { background: %4; border-color: %5; }\n"
               "QToolBar QToolButton:checked { background: %4; border: 1px solid %5; }\n"
               "QToolBar::separator { background: %6; width: 1px; margin: %7px %7px; }\n")
               .arg(t.radiusMd)
               .arg(t.space2 + 1)
               .arg(c(t.text), c(t.accentSoft), c(t.accent), c(t.border))
               .arg(t.space3);
    // Cases à cocher (formulaires et colonnes « Vis. »/« Figé » des listes) : couleurs du thème
    // au lieu de l'accent système de Windows.
    {
        const QString check = indicator_image(t.accent, t.selectionText, false);
        const QString dash = indicator_image(t.accent, t.selectionText, true);
        qss += QStringLiteral(
                   "QCheckBox::indicator, QAbstractItemView::indicator { width: 16px; height: "
                   "16px; border: 1px solid %1; border-radius: 4px; background: %2; }\n"
                   "QCheckBox::indicator:hover, QAbstractItemView::indicator:hover { border-color: "
                   "%3; }\n"
                   "QCheckBox::indicator:checked, QAbstractItemView::indicator:checked { "
                   "background: %3; border-color: %3; image: url(%4.png); }\n"
                   "QCheckBox::indicator:indeterminate, "
                   "QAbstractItemView::indicator:indeterminate { background: %3; border-color: "
                   "%3; image: url(%5.png); }\n"
                   "QCheckBox::indicator:disabled, QAbstractItemView::indicator:disabled { "
                   "border-color: %6; background: %7; }\n")
                   .arg(c(t.borderStrong), c(t.surfaceRaised), c(t.accent), check, dash)
                   .arg(c(t.border), c(t.window));
    }
    // Onglets de docks regroupés (Document / Workflow / Ordre de couture…) : plus compacts,
    // jamais tronqués ; défilement si le dock est vraiment trop étroit.
    qss += QStringLiteral("QMainWindow > QTabBar { qproperty-elideMode: ElideNone; }\n"
                          "QMainWindow > QTabBar::tab { padding: %1px %2px; margin-right: 1px; }\n")
               .arg(t.space2)
               .arg(t.space3);
    // Barre des fils (bas de fenêtre) : même surface que la barre d'état, séparée par un filet.
    qss += QStringLiteral("QToolBar#threadStripBar { background: %1; border-top: 1px solid %2; "
                          "padding: 0px; spacing: 0px; }\n")
               .arg(c(t.surface), c(t.border));
    // Bouton principal d'un écran ou d'une boîte de dialogue : plein, couleur d'accent.
    qss +=
        QStringLiteral(
            "QPushButton[primary=\"true\"] { background: %1; color: %2; border: 1px solid %1; "
            "font-weight: 700; }\n"
            "QPushButton[primary=\"true\"]:hover { background: %3; border-color: %3; }\n"
            "QPushButton[primary=\"true\"]:disabled { background: %4; color: %5; "
            "border-color: %4; }\n")
            .arg(c(t.accent), c(t.selectionText), c(t.accentHover), c(t.border), c(t.textDisabled));
    // Le focus du bouton principal ne doit pas être écrasé par sa bordure
    // d'accent. Le texte de sélection contraste déjà avec ce fond dans les deux thèmes.
    qss += QStringLiteral("QPushButton[primary=\"true\"]:focus { border: 2px solid %1; }\n")
               .arg(c(t.selectionText));
    // Barre contextuelle (message d'état + actions de l'objet sélectionné) : bandeau discret,
    // texte secondaire, actions en boutons « secondaires » à bord marqué.
    qss +=
        QStringLiteral(
            "QToolBar#contextToolbar { background: %1; border-bottom: 1px solid %2; "
            "padding: %3px %4px; spacing: %5px; }\n"
            "QToolBar#contextToolbar QLabel { color: %6; background: transparent; "
            "padding: 0 %3px; }\n"
            "QToolBar#contextToolbar QToolButton { background: %7; color: %8; border: 1px solid "
            "%9; border-radius: %10px; padding: %3px %11px; font-weight: 600; }\n"
            "QToolBar#contextToolbar QToolButton:hover { background: %12; border-color: %13; }\n"
            "QToolBar#contextToolbar QToolButton:disabled { background: transparent; color: %14; "
            "border-color: %2; }\n")
            .arg(c(t.surface), c(t.border))
            .arg(t.space2)
            .arg(t.space4)
            .arg(t.space3)
            .arg(c(t.textSecondary), c(t.surfaceRaised), c(t.text), c(t.borderStrong))
            .arg(t.radiusMd)
            .arg(t.space4 - 2)
            .arg(c(t.accentSoft), c(t.accent), c(t.textDisabled));
    // Nombre de côtés du polygone régulier (palette d'outils verticale) : pastille numérique
    // compacte alignée sur les boutons d'outils.
    qss += QStringLiteral("QToolBar#toolPalette QSpinBox { background: %1; color: %2; border: 1px "
                          "solid %3; border-radius: %4px; padding: %5px 0px; font-weight: 700; "
                          "min-height: 0px; }\n"
                          "QToolBar#toolPalette QSpinBox:focus { border-color: %6; }\n")
               .arg(c(t.surfaceRaised), c(t.text), c(t.borderStrong))
               .arg(t.radiusMd)
               .arg(t.space1)
               .arg(c(t.focus));
    qss += QStringLiteral(
               "QToolButton[primary=\"true\"] { background: %1; color: %2; border: 1px solid %1; "
               "border-radius: %3px; padding: %4px %5px; font-weight: 700; }\n"
               "QToolButton[primary=\"true\"]:hover { background: %6; border-color: %6; }\n"
               "QToolButton[primary=\"true\"]:disabled { background: %7; color: %8; "
               "border-color: %7; }\n")
               .arg(c(t.accent), c(t.selectionText))
               .arg(t.radiusMd)
               .arg(t.space2 + 1)
               .arg(t.space4 + 2)
               .arg(c(t.accentHover), c(t.border), c(t.textDisabled));
    qss += QStringLiteral(
               "QTabWidget::pane { border: none; border-top: 1px solid %1; }\n"
               "QTabBar::tab { background: transparent; color: %2; padding: %3px %4px; "
               "margin-right: %5px; border: none; border-bottom: 2px solid transparent; }\n"
               "QTabBar::tab:selected { color: %6; font-weight: 700; border-bottom: 2px solid "
               "%7; }\n"
               "QTabBar::tab:hover:!selected { color: %6; background: %8; border-top-left-radius: "
               "%9px; border-top-right-radius: %9px; }\n")
               .arg(c(t.border), c(t.textSecondary))
               .arg(t.space3 - 1)
               .arg(t.space4)
               .arg(t.space1)
               .arg(c(t.text), c(t.accent), c(t.accentSoft))
               .arg(t.radiusSm);
    qss += QStringLiteral("QStatusBar { border-top: 1px solid %1; color: %2; }\n")
               .arg(c(t.border), c(t.textSecondary));
    // Cartes (étapes du workflow) : surface surélevée, bord discret, coins arrondis.
    qss += QStringLiteral(
               "QFrame[card=\"true\"] { background: %1; border: 1px solid %2; border-radius: %3px; "
               "}\n"
               "QFrame[card=\"true\"]:hover { border-color: %4; }\n"
               "QFrame[card=\"true\"] QToolButton { background: transparent; border: none; "
               "color: %5; font-weight: 600; padding: %6px %7px; text-align: left; }\n"
               "QFrame[card=\"true\"] QLabel { background: transparent; border: none; }\n")
               .arg(c(t.surfaceRaised), c(t.border))
               .arg(t.radiusMd)
               .arg(c(t.accent), c(t.text))
               .arg(t.space2)
               .arg(t.space3);
    // Badge numéroté + pastille de mot d'état. L'état passe par la propriété dynamique
    // « state » (todo / available / progress / done / attention) : jamais la couleur seule,
    // le mot reste affiché.
    qss += QStringLiteral(
               "QLabel[badge=\"true\"] { min-width: 24px; max-width: 24px; min-height: 24px; "
               "max-height: 24px; border-radius: 12px; background: %1; color: %2; "
               "border: 1px solid %3; font-weight: 700; qproperty-alignment: AlignCenter; }\n"
               "QLabel[pill=\"true\"] { background: %1; color: %2; border-radius: 9px; "
               "padding: 1px 9px; font-size: 11px; font-weight: 600; }\n")
               .arg(c(t.window), c(t.textSecondary), c(t.border));
    const auto stateRule = [&](const char* key, const QColor& fill, const QColor& badgeText) {
        qss +=
            QStringLiteral("QLabel[badge=\"true\"][state=\"%1\"] { background: %2; color: %3; "
                           "border-color: %2; }\n"
                           "QLabel[pill=\"true\"][state=\"%1\"] { background: %4; color: %2; }\n")
                .arg(QString::fromLatin1(key), c(fill), c(badgeText), tint(fill, 38));
    };
    stateRule("available", t.info, t.window);
    stateRule("progress", t.accent, t.selectionText);
    stateRule("done", t.success, t.window);
    stateRule("attention", t.warning, t.window);

    return qss;
}

} // namespace

void markSecondaryText(QWidget* widget) {
    if (widget != nullptr) {
        widget->setProperty("secondary", true);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

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
    // Premier lancement (aucun choix enregistré) : suit le thème du système quand Qt le
    // connaît ; sinon clair. Un choix explicite de l'utilisateur prime toujours.
    const ThemeMode systemMode =
        QGuiApplication::styleHints() != nullptr &&
                QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
            ? ThemeMode::Dark
            : ThemeMode::Light;
    if (s.contains(QStringLiteral("ui/theme"))) {
        mode_ = s.value(QStringLiteral("ui/theme")).toString() == QStringLiteral("dark")
                    ? ThemeMode::Dark
                    : ThemeMode::Light;
    } else {
        mode_ = systemMode;
    }
    if (s.contains(QStringLiteral("ui/density"))) {
        density_ = s.value(QStringLiteral("ui/density")).toString() == QStringLiteral("compact")
                       ? Density::Compact
                       : Density::Comfortable;
    } else {
        // Premier lancement : la densité suit la hauteur utile de l'écran principal.
        const QScreen* screen = QGuiApplication::primaryScreen();
        density_ =
            densityForScreenHeight(screen != nullptr ? screen->availableGeometry().height() : 1080);
    }
}

Density AppTheme::densityForScreenHeight(int availableHeightPx) {
    constexpr int kCompactBelowPx = 800;
    return availableHeightPx < kCompactBelowPx ? Density::Compact : Density::Comfortable;
}

void AppTheme::save() const {
    auto s = settings();
    s.setValue(QStringLiteral("ui/theme"),
               mode_ == ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
    s.setValue(QStringLiteral("ui/density"), density_ == Density::Compact
                                                 ? QStringLiteral("compact")
                                                 : QStringLiteral("comfortable"));
}

} // namespace openstitch::desktop
