// SPDX-License-Identifier: Apache-2.0
#include "app_style.hpp"

#include <QRegularExpression>

#include <cmath>

namespace openstitch::desktop {

namespace {

// ---------------------------------------------------------------------------
// Gabarit QSS. Placeholders : `@token@` (valeur de style_token_map) et
// `@asset:nom.png@` (url("<racine>/nom.png") ; la déclaration entière est retirée
// quand aucune racine n'est fournie). Interdits : box-shadow, outline-offset,
// transition, font-variant, règle `QWidget {}` large. Le gabarit est découpé en
// sections < 16 Ko (limite des littéraux de chaîne MSVC).
//
// Spécificité : les règles `[variant=…]` viennent APRÈS les règles de base et
// redéclarent leurs états (:hover/:pressed/:disabled) car repos et pseudo-état de
// base ont la même spécificité.
// ---------------------------------------------------------------------------

constexpr const char* kSectionBase = R"qss(
QMainWindow, QDialog { background: @window@; }
QMainWindow::separator { background: @window@; width: @space2@px; height: @space2@px; }
QToolTip { background: @surfaceRaised@; color: @text@; border: 1px solid @border@; padding: @space2@px @space3@px; }

QMenuBar { background: @surface@; color: @text@; spacing: @space1@px; }
QMenuBar::item { background: transparent; padding: @space2@px @space3@px; border-radius: @radiusSm@px; }
QMenuBar::item:selected { background: @tonalHover@; color: @text@; }
QMenuBar::item:pressed { background: @tonalPressed@; color: @text@; }
QMenuBar::item:disabled { color: @textDisabled@; }

QMenu { background: @surfaceRaised@; color: @text@; border: 1px solid @border@; padding: @space2@px; }
QMenu::item { background: transparent; color: @text@; padding: @space2@px @space6@px @space2@px @space4@px; border-radius: @radiusSm@px; }
QMenu::item:selected { background: @tonalHover@; color: @text@; }
QMenu::item:disabled { background: transparent; color: @textDisabled@; }
QMenu::icon { padding-left: @space3@px; }
QMenu::separator { height: 1px; background: @border@; margin: @space2@px @space3@px; }
QMenu::right-arrow { image: @asset:chevron-right.png@; width: @glyph@px; height: @glyph@px; margin-right: @space3@px; }

QToolBar { background: @surface@; border: none; spacing: @space2@px; padding: @space1@px; }
QToolBar::separator:horizontal { background: @border@; width: 1px; margin: @space2@px @space2@px; }
QToolBar::separator:vertical { background: @border@; height: 1px; margin: @space2@px @space2@px; }
QToolBar::handle { background: @border@; margin: @space2@px; width: @space1@px; }

QToolButton { background: transparent; color: @text@; border: @focusRing@px solid transparent; border-radius: @radiusSm@px; padding: @space1@px; }
QToolButton:hover { background: @tonal@; }
QToolButton:pressed { background: @tonalPressed@; }
QToolButton:checked { background: @tonalPressed@; }
QToolButton:checked:hover { background: @tonalHover@; }
QToolButton:disabled { background: transparent; color: @textDisabled@; }
QToolButton:focus { border-color: @focus@; }
QToolButton[popupMode="1"] { padding-right: @space5@px; }
QToolButton::menu-indicator { image: @asset:chevron-down.png@; width: @glyph@px; height: @glyph@px; subcontrol-origin: padding; subcontrol-position: center right; right: @space2@px; }
QToolButton::menu-indicator:disabled { image: @asset:chevron-down-disabled.png@; width: @glyph@px; height: @glyph@px; }

QStatusBar { background: @surface@; color: @textSecondary@; }
QStatusBar::item { border: none; }

QDockWidget { background: @surface@; color: @text@; }
QDockWidget::title { background: @surface@; color: @text@; padding: @space2@px @space3@px; border: none; text-align: left; font-weight: @weightSemibold@; }
QDockWidget::close-button, QDockWidget::float-button { background: transparent; border: none; border-radius: @radiusSm@px; padding: @space1@px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @tonal@; }
QDockWidget::close-button:pressed, QDockWidget::float-button:pressed { background: @tonalPressed@; }
QSplitter::handle { background: @window@; }
QSplitter::handle:horizontal { width: @space2@px; }
QSplitter::handle:vertical { height: @space2@px; }
)qss";

constexpr const char* kSectionButtons = R"qss(
QPushButton { background: @tonal@; color: @text@; border: @focusRing@px solid transparent; border-radius: @radiusSm@px; padding: 0 @buttonPadX@px; min-height: @buttonInnerH@px; }
QPushButton:hover { background: @tonalHover@; }
QPushButton:pressed { background: @tonalPressed@; }
QPushButton:checked { background: @tonalPressed@; }
QPushButton:disabled { background: @surfaceSunken@; color: @textDisabled@; }
QPushButton:focus { border-color: @focus@; }
QPushButton::menu-indicator { image: @asset:chevron-down.png@; width: @glyph@px; height: @glyph@px; subcontrol-origin: padding; subcontrol-position: center right; right: @space3@px; }

QPushButton[variant="tonal"] { background: @tonal@; color: @text@; }
QPushButton[variant="tonal"]:hover { background: @tonalHover@; }
QPushButton[variant="tonal"]:pressed { background: @tonalPressed@; }
QPushButton[variant="tonal"]:disabled { background: @surfaceSunken@; color: @textDisabled@; }

QPushButton[variant="primary"], QDialogButtonBox QPushButton:default { background: @accent@; color: @onAccent@; }
QPushButton[variant="primary"]:hover, QDialogButtonBox QPushButton:default:hover { background: @accentHover@; color: @onAccent@; }
QPushButton[variant="primary"]:pressed, QDialogButtonBox QPushButton:default:pressed { background: @accentPressed@; color: @onAccent@; }
QPushButton[variant="primary"]:disabled, QDialogButtonBox QPushButton:default:disabled { background: @surfaceSunken@; color: @textDisabled@; }

QPushButton[variant="ghost"] { background: transparent; color: @text@; }
QPushButton[variant="ghost"]:hover { background: @tonal@; }
QPushButton[variant="ghost"]:pressed { background: @tonalPressed@; }
QPushButton[variant="ghost"]:disabled { background: transparent; color: @textDisabled@; }

QPushButton[variant="danger"] { background: transparent; color: @error@; }
QPushButton[variant="danger"]:hover { background: @surfaceSunken@; color: @error@; }
QPushButton[variant="danger"]:pressed { background: @surfaceSunken@; color: @error@; border-color: @error@; }
QPushButton[variant="danger"]:disabled { background: transparent; color: @textDisabled@; }
QPushButton[variant="danger"]:focus { border-color: @focus@; }

QLineEdit, QAbstractSpinBox, QComboBox { background: @surfaceRaised@; color: @text@; border: 1px solid @borderStrong@; border-radius: @radiusSm@px; padding: 1px @fieldPadRest@px; min-height: @fieldInnerH@px; selection-background-color: @textSelection@; selection-color: @textSelectionText@; }
QPlainTextEdit, QTextEdit { background: @surfaceRaised@; color: @text@; border: 1px solid @borderStrong@; border-radius: @radiusSm@px; padding: @textPadRest@px; selection-background-color: @textSelection@; selection-color: @textSelectionText@; }
QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover, QPlainTextEdit:hover, QTextEdit:hover { border-color: @textSecondary@; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border: @focusRing@px solid @focus@; padding: 0 @space3@px; }
QPlainTextEdit:focus, QTextEdit:focus { border: @focusRing@px solid @focus@; padding: @space2@px; }
QLineEdit:read-only, QPlainTextEdit:read-only, QTextEdit:read-only { background: @surfaceSunken@; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled, QPlainTextEdit:disabled, QTextEdit:disabled { background: @surfaceSunken@; color: @textDisabled@; border: 1px solid @border@; }

QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: @spinButtonW@px; border: none; background: transparent; }
QAbstractSpinBox::up-button { subcontrol-position: top right; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @tonal@; }
QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed { background: @tonalPressed@; }
QAbstractSpinBox::up-arrow { image: @asset:chevron-up.png@; width: @glyph@px; height: @glyph@px; }
QAbstractSpinBox::down-arrow { image: @asset:chevron-down.png@; width: @glyph@px; height: @glyph@px; }
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off { image: @asset:chevron-up-disabled.png@; width: @glyph@px; height: @glyph@px; }
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off { image: @asset:chevron-down-disabled.png@; width: @glyph@px; height: @glyph@px; }

QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: @dropDownW@px; border: none; background: transparent; }
QComboBox::down-arrow { image: @asset:chevron-down.png@; width: @glyph@px; height: @glyph@px; }
QComboBox::down-arrow:disabled { image: @asset:chevron-down-disabled.png@; width: @glyph@px; height: @glyph@px; }
QComboBox QAbstractItemView { background: @surfaceRaised@; color: @text@; border: 1px solid @border@; selection-background-color: @selection@; selection-color: @selectionText@; outline: 0; padding: @space1@px; }
QComboBox QAbstractItemView::item { min-height: @rowHeight@px; padding: 0 @space3@px; }
QComboBox QAbstractItemView::item:hover { background: @tonal@; color: @text@; }
QComboBox QAbstractItemView::item:selected { background: @selection@; color: @selectionText@; }
)qss";

constexpr const char* kSectionControls = R"qss(
QCheckBox, QRadioButton { background: transparent; color: @text@; spacing: @space3@px; }
QCheckBox:disabled, QRadioButton:disabled { color: @textDisabled@; }
QCheckBox::indicator, QRadioButton::indicator { width: @indicatorContent@px; height: @indicatorContent@px; border: @focusRing@px solid @borderStrong@; background: @surfaceRaised@; }
QCheckBox::indicator { border-radius: @indicatorRadius@px; }
QRadioButton::indicator { border-radius: @radioRadius@px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @text@; }
QCheckBox::indicator:focus, QRadioButton::indicator:focus { border-color: @focus@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; image: @asset:check.png@; }
QCheckBox::indicator:indeterminate { background: @accent@; border-color: @accent@; image: @asset:dash.png@; }
QRadioButton::indicator:checked { background: @accent@; border-color: @accent@; image: @asset:dot.png@; }
QCheckBox::indicator:checked:hover, QCheckBox::indicator:indeterminate:hover, QRadioButton::indicator:checked:hover { background: @accentHover@; border-color: @accentHover@; }
QCheckBox::indicator:checked:focus, QCheckBox::indicator:indeterminate:focus, QRadioButton::indicator:checked:focus { border-color: @focus@; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background: @surfaceSunken@; border-color: @border@; }
QCheckBox::indicator:checked:disabled { image: @asset:check-disabled.png@; background: @surfaceSunken@; border-color: @border@; }
QCheckBox::indicator:indeterminate:disabled { image: @asset:dash-disabled.png@; background: @surfaceSunken@; border-color: @border@; }
QRadioButton::indicator:checked:disabled { image: @asset:dot-disabled.png@; background: @surfaceSunken@; border-color: @border@; }

QSlider::groove:horizontal { height: @grooveH@px; background: @surfaceSunken@; border-radius: @grooveRadius@px; }
QSlider::groove:vertical { width: @grooveH@px; background: @surfaceSunken@; border-radius: @grooveRadius@px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: @grooveRadius@px; }
QSlider::add-page:vertical { background: @accent@; border-radius: @grooveRadius@px; }
QSlider::handle:horizontal { background: @surfaceRaised@; border: @focusRing@px solid @accent@; width: @handleContent@px; height: @handleContent@px; margin: @handleMargin@px 0; border-radius: @handleRadius@px; }
QSlider::handle:vertical { background: @surfaceRaised@; border: @focusRing@px solid @accent@; width: @handleContent@px; height: @handleContent@px; margin: 0 @handleMargin@px; border-radius: @handleRadius@px; }
QSlider::handle:hover { border-color: @accentHover@; }
QSlider::handle:pressed { border-color: @accentPressed@; }
QSlider::handle:focus { border-color: @focus@; }
QSlider::groove:disabled { background: @border@; }
QSlider::sub-page:horizontal:disabled, QSlider::add-page:vertical:disabled { background: @borderStrong@; }
QSlider::handle:disabled { background: @surfaceSunken@; border-color: @borderStrong@; }

QProgressBar { background: @surfaceSunken@; color: @text@; border: none; border-radius: @radiusSm@px; text-align: center; min-height: @space3@px; max-height: @space3@px; }
QProgressBar::chunk { background: @accent@; border-radius: @radiusSm@px; }

QTabWidget::pane { background: @surface@; border: none; border-top: 1px solid @border@; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @textSecondary@; padding: @space2@px @space4@px; border: none; border-bottom: @focusRing@px solid transparent; min-height: @rowHeight@px; }
QTabBar::tab:hover { background: @tonal@; color: @text@; }
QTabBar::tab:selected { background: transparent; color: @text@; border-bottom: @focusRing@px solid @accent@; }
QTabBar::tab:selected:hover { background: @tonal@; }
QTabBar::tab:disabled { color: @textDisabled@; }
QTabBar::tab:focus { border-bottom-color: @focus@; }

QHeaderView::section { background: @surface@; color: @textSecondary@; border: none; border-bottom: 1px solid @border@; padding: 0 @space3@px; min-height: @rowHeight@px; font-weight: @weightSemibold@; }
QTableView QTableCornerButton::section { background: @surface@; border: none; }

QListView, QTreeView, QTableView { background: @surfaceSunken@; alternate-background-color: @surface@; color: @text@; border: 1px solid @border@; border-radius: @radiusSm@px; outline: 0; gridline-color: @border@; selection-background-color: @selection@; selection-color: @selectionText@; }
QListView:focus, QTreeView:focus, QTableView:focus { border: 1px solid @focus@; }
QListView::item, QTreeView::item, QTableView::item { min-height: @rowHeight@px; padding: 0 @space2@px; border: none; border-left: @focusRing@px solid transparent; }
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover { background: @tonal@; color: @text@; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected { background: @selection@; color: @selectionText@; border-left: @focusRing@px solid @accent@; }
QListView::item:disabled, QTreeView::item:disabled, QTableView::item:disabled { color: @textDisabled@; }
QTreeView::branch { background: transparent; }
QTreeView::branch:closed:has-children { background: transparent; image: @asset:chevron-right.png@; }
QTreeView::branch:open:has-children { background: transparent; image: @asset:chevron-down.png@; }

QScrollBar:vertical { background: transparent; width: @scrollbarWidth@px; margin: 0; border: none; }
QScrollBar:horizontal { background: transparent; height: @scrollbarWidth@px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: @borderStrong@; border-radius: @scrollbarRadius@px; min-height: @space6@px; }
QScrollBar::handle:horizontal { background: @borderStrong@; border-radius: @scrollbarRadius@px; min-width: @space6@px; }
QScrollBar::handle:hover { background: @textSecondary@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QGroupBox { background: transparent; border: none; margin-top: @groupTop@px; padding-top: @space2@px; color: @text@; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 0; padding: 0 0 @space1@px 0; color: @textSecondary@; border-bottom: 1px solid @border@; }

QFrame[frameShape="4"] { background: @border@; border: none; max-height: 1px; }
QFrame[frameShape="5"] { background: @border@; border: none; max-width: 1px; }
QFrame#emptyState { background: @surfaceRaised@; border: 1px solid @border@; border-radius: @radiusMd@px; }
)qss";

constexpr const char* kSectionRoles = R"qss(
QLabel[role="title"] { color: @text@; font-size: @fontTitle@pt; font-weight: @weightSemibold@; }
QLabel[role="heading"] { color: @text@; font-size: @fontHeading@pt; font-weight: @weightSemibold@; }
QLabel[role="caption"] { color: @textSecondary@; font-size: @fontSmall@pt; }
QLabel[role="section"] { color: @textSecondary@; font-size: @fontSmall@pt; font-weight: @weightSemibold@; }
QLabel[role="warning"] { color: @warning@; }
QLabel[role="error"] { color: @error@; }
QLabel[role="success"] { color: @success@; }
QLabel[role="mono"], QPlainTextEdit[role="mono"], QTextEdit[role="mono"], QLineEdit[role="mono"] { font-family: @monoFamily@; font-size: @fontMono@pt; }
)qss";

QString template_text() {
    return QString::fromUtf8(kSectionBase) + QString::fromUtf8(kSectionButtons) +
           QString::fromUtf8(kSectionControls) + QString::fromUtf8(kSectionRoles);
}

QString hex(const QColor& c) {
    if (c.alpha() == 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}

QString num(int v) {
    return QString::number(v);
}

QString num(double v) {
    return QString::number(v, 'g', 4);
}

const QRegularExpression& placeholder_re() {
    static const QRegularExpression re(QStringLiteral("@([A-Za-z0-9_]+)@"));
    return re;
}

const QRegularExpression& asset_re() {
    static const QRegularExpression re(QStringLiteral("@asset:([A-Za-z0-9_.\\-]+)@"));
    return re;
}

} // namespace

QMap<QString, QString> style_token_map(const Tokens& t) {
    QMap<QString, QString> m;
#define OS_COLOR(field) m.insert(QStringLiteral(#field), hex(t.field))
    OS_COLOR(window);
    OS_COLOR(surface);
    OS_COLOR(surfaceRaised);
    OS_COLOR(surfaceSunken);
    OS_COLOR(border);
    OS_COLOR(borderStrong);
    OS_COLOR(text);
    OS_COLOR(textSecondary);
    OS_COLOR(textDisabled);
    OS_COLOR(accent);
    OS_COLOR(accentHover);
    OS_COLOR(accentPressed);
    OS_COLOR(onAccent);
    OS_COLOR(tonal);
    OS_COLOR(tonalHover);
    OS_COLOR(tonalPressed);
    OS_COLOR(selection);
    OS_COLOR(selectionText);
    OS_COLOR(textSelection);
    OS_COLOR(textSelectionText);
    OS_COLOR(focus);
    OS_COLOR(icon);
    OS_COLOR(success);
    OS_COLOR(warning);
    OS_COLOR(error);
    OS_COLOR(info);
    OS_COLOR(canvasBackground);
    OS_COLOR(canvasPaper);
#undef OS_COLOR
#define OS_INT(field) m.insert(QStringLiteral(#field), num(t.field))
    OS_INT(space1);
    OS_INT(space2);
    OS_INT(space3);
    OS_INT(space4);
    OS_INT(space5);
    OS_INT(space6);
    OS_INT(space7);
    OS_INT(controlHeight);
    OS_INT(controlPadX);
    OS_INT(rowHeight);
    OS_INT(radiusSm);
    OS_INT(radiusMd);
    OS_INT(radiusLg);
    OS_INT(radiusPill);
    OS_INT(iconSize);
    OS_INT(toolIconSize);
    OS_INT(scrollbarWidth);
    OS_INT(focusRingWidth);
    OS_INT(weightRegular);
    OS_INT(weightMedium);
    OS_INT(weightSemibold);
    OS_INT(motionShortMs);
#undef OS_INT
    m.insert(QStringLiteral("fontSmall"), num(t.fontSmall));
    m.insert(QStringLiteral("fontBase"), num(t.fontBase));
    m.insert(QStringLiteral("fontTitle"), num(t.fontTitle));
    m.insert(QStringLiteral("fontHeading"), num(t.fontHeading));
    m.insert(QStringLiteral("fontMono"), num(t.fontMono));

    // Métriques dérivées. Un contrôle = bordure 2 px (anneau de focus, transparente
    // au repos pour les boutons) + contenu : le total vaut `controlHeight`.
    m.insert(QStringLiteral("focusRing"), num(t.focusRingWidth));
    m.insert(QStringLiteral("buttonInnerH"), num(t.controlHeight - 2 * t.focusRingWidth));
    m.insert(QStringLiteral("buttonPadX"), num(t.controlPadX - t.focusRingWidth));
    m.insert(QStringLiteral("fieldInnerH"), num(t.controlHeight - 2 * t.focusRingWidth));
    // Champ : bordure 1 px + 1 px de padding au repos == anneau de 2 px au focus.
    m.insert(QStringLiteral("fieldPadRest"), num(t.space3 + 1));
    m.insert(QStringLiteral("textPadRest"), num(t.space2 + 1));
    m.insert(QStringLiteral("spinButtonW"), num(t.space5 + t.space2));
    m.insert(QStringLiteral("dropDownW"), num(t.space6));
    m.insert(QStringLiteral("glyph"), QStringLiteral("10"));
    m.insert(QStringLiteral("indicatorContent"), QStringLiteral("12")); // + 2x2 de bordure = 16
    m.insert(QStringLiteral("indicatorRadius"), num(t.space2));
    m.insert(QStringLiteral("radioRadius"), num(t.space3));
    m.insert(QStringLiteral("grooveH"), num(t.space2));
    m.insert(QStringLiteral("grooveRadius"), num(t.space1));
    m.insert(QStringLiteral("handleContent"), QStringLiteral("12"));
    m.insert(QStringLiteral("handleRadius"), num(t.space3));
    m.insert(QStringLiteral("handleMargin"), num(-(16 - t.space2) / 2));
    m.insert(QStringLiteral("scrollbarRadius"), num(t.scrollbarWidth / 2));
    m.insert(QStringLiteral("groupTop"), num(t.space5 + t.space2));
    // Pile monospace : Qt accepte une liste de familles dans `font-family`.
    QStringList quoted;
    for (const QString& f : mono_font_families()) {
        quoted << QStringLiteral("\"%1\"").arg(f);
    }
    m.insert(QStringLiteral("monoFamily"), quoted.join(QStringLiteral(", ")));
    return m;
}

QString stylesheet_template() {
    return template_text();
}

QStringList stylesheet_placeholders() {
    QStringList out;
    const QString tpl = template_text();
    auto it = placeholder_re().globalMatch(tpl);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!out.contains(name)) {
            out << name;
        }
    }
    out.sort();
    return out;
}

QStringList stylesheet_asset_names() {
    QStringList out;
    const QString tpl = template_text();
    auto it = asset_re().globalMatch(tpl);
    while (it.hasNext()) {
        const QString name = it.next().captured(1);
        if (!out.contains(name)) {
            out << name;
        }
    }
    out.sort();
    return out;
}

QString build_stylesheet(const Tokens& tokens, const QString& assetRoot) {
    QString qss = template_text();

    // 1. Glyphes : url("<racine>/nom") entre guillemets, ou déclaration retirée.
    if (assetRoot.isEmpty()) {
        static const QRegularExpression decl(
            QStringLiteral("[ ]*image:[ ]*@asset:[A-Za-z0-9_.\\-]+@;"));
        qss.remove(decl);
    } else {
        QString out;
        qsizetype last = 0;
        auto it = asset_re().globalMatch(qss);
        while (it.hasNext()) {
            const auto match = it.next();
            out += qss.mid(last, match.capturedStart() - last);
            out += QStringLiteral("url(\"%1/%2\")").arg(assetRoot, match.captured(1));
            last = match.capturedEnd();
        }
        out += qss.mid(last);
        qss = out;
    }

    // 2. Tokens. Un nom inconnu reste `@nom@` (détecté par les tests).
    const QMap<QString, QString> map = style_token_map(tokens);
    QString out;
    qsizetype last = 0;
    auto it = placeholder_re().globalMatch(qss);
    while (it.hasNext()) {
        const auto match = it.next();
        const auto found = map.constFind(match.captured(1));
        if (found == map.constEnd()) {
            continue;
        }
        out += qss.mid(last, match.capturedStart() - last);
        out += found.value();
        last = match.capturedEnd();
    }
    out += qss.mid(last);
    return out;
}

QFont app_font(const Tokens& tokens) {
    QFont f;
    f.setFamilies(font_families());
    f.setPointSizeF(tokens.fontBase);
    f.setWeight(static_cast<QFont::Weight>(tokens.weightRegular));
    return f;
}

QPalette build_palette(const Tokens& t) {
    QPalette p;
    // Rôles actifs/inactifs (groupes Active + Inactive + Disabled par défaut).
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surfaceRaised);
    p.setColor(QPalette::AlternateBase, t.surface);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.tonal);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.onAccent);
    p.setColor(QPalette::Light, t.surfaceRaised);
    p.setColor(QPalette::Midlight, t.surface);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Dark, t.borderStrong);
    p.setColor(QPalette::Shadow, t.surfaceSunken.darker(150));
    p.setColor(QPalette::ToolTipBase, t.surfaceRaised);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::Link, t.info);
    p.setColor(QPalette::LinkVisited, t.textSecondary);
    p.setColor(QPalette::PlaceholderText, t.textSecondary);
    p.setColor(QPalette::Highlight, t.textSelection);
    p.setColor(QPalette::HighlightedText, t.textSelectionText);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setColor(QPalette::Accent, t.accent);
#endif
    // Groupe Disabled : tous les rôles texte = textDisabled (lisible, >= 3:1).
    const QPalette::ColorGroup d = QPalette::Disabled;
    p.setColor(d, QPalette::WindowText, t.textDisabled);
    p.setColor(d, QPalette::Text, t.textDisabled);
    p.setColor(d, QPalette::ButtonText, t.textDisabled);
    p.setColor(d, QPalette::BrightText, t.textDisabled);
    p.setColor(d, QPalette::ToolTipText, t.textDisabled);
    p.setColor(d, QPalette::PlaceholderText, t.textDisabled);
    p.setColor(d, QPalette::Link, t.textDisabled);
    p.setColor(d, QPalette::LinkVisited, t.textDisabled);
    p.setColor(d, QPalette::Highlight, t.border);
    p.setColor(d, QPalette::HighlightedText, t.textDisabled);
    return p;
}

} // namespace openstitch::desktop
