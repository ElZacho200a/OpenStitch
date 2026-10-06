// SPDX-License-Identifier: Apache-2.0
// Génération de la QSS et de la palette (app_style) : structure du gabarit, couverture
// des widgets, absence d'avertissement Qt au parsing, palette complète.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "app_style.hpp"
#include "design_tokens.hpp"
#include "style_assets.hpp"
#include "ui_style.hpp"

namespace openstitch::desktop {

namespace {

QStringList g_messages; // tous les messages Qt capturés
QtMessageHandler g_previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    Q_UNUSED(ctx);
    if (msg.contains(QLatin1String("This plugin does not support"))) {
        return; // bruit de la plateforme offscreen, sans rapport avec la QSS
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_messages << msg;
    }
}

struct Combo {
    ThemeMode mode;
    Density density;
};

const Combo kCombos[] = {
    {ThemeMode::Light, Density::Comfortable},
    {ThemeMode::Light, Density::Compact},
    {ThemeMode::Dark, Density::Comfortable},
    {ThemeMode::Dark, Density::Compact},
};

bool bracesBalanced(const QString& s) {
    int depth = 0;
    for (const QChar c : s) {
        if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}')) {
            if (--depth < 0) {
                return false;
            }
        }
    }
    return depth == 0;
}

} // namespace

class AppStyleTest : public QObject {
    Q_OBJECT

private slots:
    void init() { g_messages.clear(); }

    void stylesheetIsStructurallySoundForTheFourCombinations() {
        for (const Combo& c : kCombos) {
            const Tokens t = tokens_for(c.mode, c.density);
            for (const QString& root : {QString(), QStringLiteral(":/openstitch-9")}) {
                const QString qss = build_stylesheet(t, root);
                QVERIFY(qss.size() > 4000);
                static const QRegularExpression leftover(QStringLiteral("@[A-Za-z]"));
                QVERIFY2(!leftover.match(qss).hasMatch(), "placeholder non resolu");
                QVERIFY(bracesBalanced(qss));
                static const QRegularExpression emptyBody(QStringLiteral("\\{\\s*\\}"));
                QVERIFY2(!emptyBody.match(qss).hasMatch(), "regle a corps vide");
                // Pas de règle QWidget large (coût de polissage).
                static const QRegularExpression broad(
                    QStringLiteral("(^|[\\n,}])\\s*QWidget\\s*[{,]"));
                QVERIFY2(!broad.match(qss).hasMatch(), "regle QWidget {} interdite");
                // Aucune propriété ignorée par QSS.
                for (const char* bad :
                     {"box-shadow", "outline-offset", "transition", "font-variant"}) {
                    QVERIFY2(!qss.contains(QLatin1String(bad)), bad);
                }
                if (root.isEmpty()) {
                    QVERIFY2(!qss.contains(QLatin1String("image:")), "image: sans racine");
                    QVERIFY2(!qss.contains(QLatin1String("url(")), "url( sans racine");
                } else {
                    QVERIFY(qss.contains(QStringLiteral("url(\":/openstitch-9/check.png\")")));
                    static const QRegularExpression unquoted(QStringLiteral("url\\((?!\")"));
                    QVERIFY2(!unquoted.match(qss).hasMatch(), "url( non guillemete");
                }
            }
        }
    }

    // Aucun sélecteur sans corps, aucun corps sans sélecteur : chaque bloc `{...}` est
    // précédé d'un sélecteur non vide et contient au moins une déclaration `a: b;`.
    void noEmptySelectorsOrBodies() {
        const QString qss = build_stylesheet(light_tokens(), QStringLiteral(":/openstitch-1"));
        static const QRegularExpression block(QStringLiteral("([^{}]*)\\{([^{}]*)\\}"));
        auto it = block.globalMatch(qss);
        int blocks = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            ++blocks;
            QVERIFY2(!m.captured(1).trimmed().isEmpty(), "selecteur vide");
            QVERIFY2(m.captured(2).contains(QLatin1Char(':')) &&
                         m.captured(2).contains(QLatin1Char(';')),
                     qPrintable(m.captured(1)));
        }
        QVERIFY(blocks > 100);
    }

    // Qt 6.4 n'avertit PAS d'une propriété QSS inconnue : on vérifie donc nous-mêmes
    // que chaque déclaration du gabarit utilise une propriété de la référence QSS de Qt.
    void everyDeclaredPropertyIsAKnownQssProperty() {
        static const QSet<QString> known = {"background",
                                            "background-color",
                                            "color",
                                            "border",
                                            "border-top",
                                            "border-bottom",
                                            "border-left",
                                            "border-right",
                                            "border-color",
                                            "border-top-color",
                                            "border-bottom-color",
                                            "border-left-color",
                                            "border-right-color",
                                            "border-radius",
                                            "padding",
                                            "padding-left",
                                            "padding-right",
                                            "padding-top",
                                            "padding-bottom",
                                            "margin",
                                            "margin-left",
                                            "margin-right",
                                            "margin-top",
                                            "margin-bottom",
                                            "min-width",
                                            "min-height",
                                            "max-width",
                                            "max-height",
                                            "width",
                                            "height",
                                            "spacing",
                                            "image",
                                            "subcontrol-origin",
                                            "subcontrol-position",
                                            "selection-background-color",
                                            "selection-color",
                                            "alternate-background-color",
                                            "gridline-color",
                                            "outline",
                                            "font-size",
                                            "font-weight",
                                            "font-family",
                                            "text-align",
                                            "qproperty-textVisible",
                                            "left",
                                            "right",
                                            "top",
                                            "bottom"};
        const QString qss = build_stylesheet(light_tokens(), QStringLiteral(":/openstitch-1"));
        static const QRegularExpression block(QStringLiteral("\\{([^{}]*)\\}"));
        QSet<QString> used;
        auto it = block.globalMatch(qss);
        while (it.hasNext()) {
            const QStringList decls =
                it.next().captured(1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString& d : decls) {
                if (d.trimmed().isEmpty()) {
                    continue;
                }
                const QString prop = d.left(d.indexOf(QLatin1Char(':'))).trimmed();
                QVERIFY2(known.contains(prop), qPrintable(prop));
                used.insert(prop);
            }
        }
        QVERIFY(used.size() > 25);
    }

    void everySpecSelectorIsPresent() {
        const QString qss = build_stylesheet(dark_tokens(), QStringLiteral(":/openstitch-1"));
        const QStringList selectors = {
            "QMainWindow",
            "QDialog",
            "QToolTip",
            "QMenuBar::item",
            "QMenu::item",
            "QMenu::separator",
            "QMenu::icon",
            "QMenu::right-arrow",
            "QToolBar::separator",
            "QToolBar::handle",
            "QToolButton",
            "QToolButton:checked",
            "QStatusBar::item",
            "QDockWidget::title",
            "QDockWidget::close-button",
            "QDockWidget::float-button",
            "QPushButton",
            "QPushButton:hover",
            "QPushButton:pressed",
            "QPushButton:disabled",
            "QPushButton:focus",
            "QDialogButtonBox QPushButton:default",
            "QLineEdit",
            "QPlainTextEdit",
            "QTextEdit",
            "QAbstractSpinBox",
            "QAbstractSpinBox::up-button",
            "QAbstractSpinBox::down-button",
            "QComboBox::drop-down",
            "QComboBox::down-arrow",
            "QComboBox QAbstractItemView",
            "QCheckBox::indicator",
            "QRadioButton::indicator",
            "QCheckBox::indicator:checked",
            "QSlider::groove:horizontal",
            "QSlider::sub-page:horizontal",
            "QSlider::handle:horizontal",
            "QProgressBar",
            "QProgressBar::chunk",
            "QTabWidget::pane",
            "QTabBar::tab",
            "QTabBar::tab:selected",
            "QSplitter::handle",
            "QHeaderView::section",
            "QTableView",
            "QListView",
            "QTreeView",
            "QTreeView::branch",
            "QListView::item:selected",
            "QScrollBar:vertical",
            "QScrollBar:horizontal",
            "QScrollBar::handle:vertical",
            "QScrollBar::add-line",
            "QScrollBar::sub-line",
            "QGroupBox",
            "QGroupBox::title",
            "QFrame[frameShape=\"4\"]",
            "QFrame#emptyState",
            "alternate-background-color",
        };
        for (const QString& s : selectors) {
            QVERIFY2(qss.contains(s), qPrintable(s));
        }
    }

    void everyVariantAndRoleHasRules() {
        const QString qss = build_stylesheet(light_tokens());
        for (const char* v : {"primary", "tonal", "ghost", "danger"}) {
            const QString sel = QStringLiteral("QPushButton[variant=\"%1\"]").arg(QLatin1String(v));
            QVERIFY2(qss.contains(sel), qPrintable(sel));
            QVERIFY2(qss.contains(sel + QStringLiteral(":hover")), qPrintable(sel));
            QVERIFY2(qss.contains(sel + QStringLiteral(":disabled")), qPrintable(sel));
        }
        for (const char* r :
             {"title", "heading", "caption", "mono", "warning", "error", "success", "section"}) {
            const QString sel = QStringLiteral("[role=\"%1\"]").arg(QLatin1String(r));
            QVERIFY2(qss.contains(sel), qPrintable(sel));
        }
        // Les noms de l'API ui:: sont ceux de la QSS.
        for (auto v : {ui::ButtonVariant::Primary, ui::ButtonVariant::Tonal,
                       ui::ButtonVariant::Ghost, ui::ButtonVariant::Danger}) {
            QVERIFY(qss.contains(
                QStringLiteral("[variant=\"%1\"]").arg(QLatin1String(ui::variant_name(v)))));
        }
        for (auto r : {ui::LabelRole::Title, ui::LabelRole::Heading, ui::LabelRole::Caption,
                       ui::LabelRole::Mono, ui::LabelRole::Warning, ui::LabelRole::Error,
                       ui::LabelRole::Success, ui::LabelRole::Section}) {
            QVERIFY(
                qss.contains(QStringLiteral("[role=\"%1\"]").arg(QLatin1String(ui::role_name(r)))));
        }
    }

    void metricsFollowTheDensity() {
        const QString comfortable = build_stylesheet(light_tokens(Density::Comfortable));
        const QString compact = build_stylesheet(light_tokens(Density::Compact));
        QVERIFY(comfortable != compact);
        QVERIFY(comfortable.contains(QStringLiteral("min-height: 28px"))); // 32 - 2 x 2
        QVERIFY(compact.contains(QStringLiteral("min-height: 22px")));     // 26 - 2 x 2
        QVERIFY(build_stylesheet(light_tokens()) != build_stylesheet(dark_tokens()));
        // Déterministe, sans cache : deux appels identiques.
        QCOMPARE(build_stylesheet(light_tokens()), build_stylesheet(light_tokens()));
    }

    // Le QSS complet est accepté par Qt : aucun « Could not parse stylesheet » ni
    // « Unknown property », sur un échantillon de tous les widgets habillés.
    void stylesheetParsesWithoutQtWarnings() {
        g_previous = qInstallMessageHandler(capture);
        QString saved = qApp->styleSheet();
        for (const Combo& c : kCombos) {
            const Tokens t = tokens_for(c.mode, c.density);
            for (const QString& root : {QString(), QStringLiteral(":/openstitch-77")}) {
                qApp->setStyleSheet(build_stylesheet(t, root));
                QWidget host;
                auto* lay = new QVBoxLayout(&host);
                auto* tabs = new QTabWidget;
                tabs->addTab(new QLabel(QStringLiteral("a")), QStringLiteral("A"));
                auto* table = new QTableWidget(2, 2);
                auto* tree = new QTreeWidget;
                auto* child = new QTreeWidgetItem(tree, QStringList{QStringLiteral("x")});
                new QTreeWidgetItem(child, QStringList{QStringLiteral("y")});
                auto* combo = new QComboBox;
                combo->addItems({QStringLiteral("a"), QStringLiteral("b")});
                auto* box = new QGroupBox(QStringLiteral("Box"));
                ui::styleGroupBox(box);
                auto* edit = new QPlainTextEdit;
                ui::setRole(edit, ui::LabelRole::Mono);
                auto* label = new QLabel(QStringLiteral("t"));
                ui::setRole(label, ui::LabelRole::Title);
                auto* primary = new QPushButton(QStringLiteral("ok"));
                ui::setVariant(primary, ui::ButtonVariant::Primary);
                auto* toolButton = new QToolButton;
                toolButton->setCheckable(true);
                for (QWidget* w : std::initializer_list<QWidget*>{
                         tabs, table, tree, combo, box, edit, label, primary, toolButton,
                         new QCheckBox(QStringLiteral("c")), new QRadioButton(QStringLiteral("r")),
                         new QSlider(Qt::Horizontal), new QProgressBar, new QLineEdit,
                         new QDoubleSpinBox, new QScrollBar(Qt::Vertical),
                         new QPushButton(QStringLiteral("p"))}) {
                    lay->addWidget(w);
                }
                host.show();
                QApplication::processEvents();
                QMenu menu;
                menu.addAction(QStringLiteral("a"));
                menu.addSeparator();
                menu.addAction(QStringLiteral("b"))->setEnabled(false);
                menu.ensurePolished();
                menu.show();
                QApplication::processEvents();
                menu.hide();
                host.hide();
            }
        }
        qApp->setStyleSheet(saved);
        qInstallMessageHandler(g_previous);
        for (const QString& m : g_messages) {
            QVERIFY2(!m.contains(QStringLiteral("Could not parse")) &&
                         !m.contains(QStringLiteral("Unknown property")),
                     qPrintable(m));
        }
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(QLatin1Char('\n'))));
    }

    void paletteIsCompleteInBothThemes() {
        for (const Combo& c : kCombos) {
            const Tokens t = tokens_for(c.mode, c.density);
            const QPalette p = build_palette(t);
            QCOMPARE(p.color(QPalette::Window), t.window);
            QCOMPARE(p.color(QPalette::WindowText), t.text);
            QCOMPARE(p.color(QPalette::Base), t.surfaceRaised);
            QCOMPARE(p.color(QPalette::AlternateBase), t.surface);
            QCOMPARE(p.color(QPalette::Text), t.text);
            QCOMPARE(p.color(QPalette::Button), t.tonal);
            QCOMPARE(p.color(QPalette::ButtonText), t.text);
            QCOMPARE(p.color(QPalette::BrightText), t.onAccent);
            QCOMPARE(p.color(QPalette::Light), t.surfaceRaised);
            QCOMPARE(p.color(QPalette::Midlight), t.surface);
            QCOMPARE(p.color(QPalette::Mid), t.border);
            QCOMPARE(p.color(QPalette::Dark), t.borderStrong);
            QVERIFY(p.color(QPalette::Shadow).isValid());
            QCOMPARE(p.color(QPalette::ToolTipBase), t.surfaceRaised);
            QCOMPARE(p.color(QPalette::ToolTipText), t.text);
            QCOMPARE(p.color(QPalette::Link), t.info);
            QCOMPARE(p.color(QPalette::LinkVisited), t.textSecondary);
            QCOMPARE(p.color(QPalette::PlaceholderText), t.textSecondary);
            QCOMPARE(p.color(QPalette::Highlight), t.textSelection);
            QCOMPARE(p.color(QPalette::HighlightedText), t.textSelectionText);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            QCOMPARE(p.color(QPalette::Accent), t.accent);
#endif
            // Les trois groupes sont cohérents ; Disabled = textDisabled pour le texte.
            for (auto group : {QPalette::Active, QPalette::Inactive}) {
                QCOMPARE(p.color(group, QPalette::Window), t.window);
                QCOMPARE(p.color(group, QPalette::Text), t.text);
                QCOMPARE(p.color(group, QPalette::Highlight), t.textSelection);
            }
            for (auto role :
                 {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::BrightText,
                  QPalette::ToolTipText, QPalette::PlaceholderText, QPalette::Link,
                  QPalette::LinkVisited, QPalette::HighlightedText}) {
                QCOMPARE(p.color(QPalette::Disabled, role), t.textDisabled);
            }
            QCOMPARE(p.color(QPalette::Disabled, QPalette::Window), t.window);
            QCOMPARE(p.color(QPalette::Disabled, QPalette::Base), t.surfaceRaised);
            // Aucun rôle ne reste à la valeur par défaut de Qt (palette thémée).
            QVERIFY(p != QPalette());
            // Les deux thèmes produisent des palettes distinctes (fond ET texte).
            const Tokens other = tokens_for(
                c.mode == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::Light, c.density);
            QVERIFY(build_palette(other).color(QPalette::Window) != p.color(QPalette::Window));
            QVERIFY(build_palette(other).color(QPalette::Text) != p.color(QPalette::Text));
        }
    }

    void progressBarTextIsHiddenAndComboTextClearsTheArrow() {
        const QString qss = build_stylesheet(light_tokens());
        // Le texte de pourcentage sur le chunk accent échouerait à R1 : masqué.
        QVERIFY(qss.contains(QStringLiteral("qproperty-textVisible: false")));
        // Padding droit du combo = bouton (24) + écart (4) au repos, 27 au focus (bordure 2 px).
        QVERIFY(qss.contains(QStringLiteral("QComboBox { padding-right: 28px; }")));
        QVERIFY(qss.contains(QStringLiteral("QComboBox:focus { padding-right: 27px; }")));
    }

    // font-family : liste de familles acceptée par Qt (déterministe : on compare la liste
    // demandée, pas la police résolue, qui dépend des polices installées).
    void monoRoleCarriesTheWholeFamilyStack() {
        const QString saved = qApp->styleSheet();
        qApp->setStyleSheet(build_stylesheet(light_tokens()));
        QPlainTextEdit edit;
        ui::setRole(&edit, ui::LabelRole::Mono);
        edit.ensurePolished();
        const QStringList families = edit.font().families();
        QCOMPARE(families.first(), mono_font_families().first());
        for (const QString& f : mono_font_families()) {
            QVERIFY2(families.contains(f), qPrintable(f));
        }
        QVERIFY(families.contains(QStringLiteral("monospace")));
        qApp->setStyleSheet(saved);
    }

    void appFontUsesTheFamilyStackAndBaseSize() {
        for (const Combo& c : kCombos) {
            const Tokens t = tokens_for(c.mode, c.density);
            const QFont f = app_font(t);
            QCOMPARE(f.families(), font_families());
            QCOMPARE(f.pointSizeF(), t.fontBase);
            QCOMPARE(static_cast<int>(f.weight()), t.weightRegular);
        }
    }

    void setVariantAndRoleSetPropertiesAndAreIdempotent() {
        QPushButton b(QStringLiteral("x"));
        ui::setVariant(&b, ui::ButtonVariant::Danger);
        QCOMPARE(b.property(ui::kPropVariant).toString(), QStringLiteral("danger"));
        ui::setVariant(&b, ui::ButtonVariant::Danger); // no-op
        QCOMPARE(b.property("variant").toString(), QStringLiteral("danger"));
        QLabel l(QStringLiteral("y"));
        ui::setRole(&l, ui::LabelRole::Caption);
        QCOMPARE(l.property(ui::kPropRole).toString(), QStringLiteral("caption"));
        ui::setVariant(nullptr, ui::ButtonVariant::Primary); // pas de crash
        ui::setRole(nullptr, ui::LabelRole::Title);
        QGroupBox box(QStringLiteral("Titre"));
        ui::styleGroupBox(&box);
        ui::styleGroupBox(&box); // idempotent
        QCOMPARE(box.title(), QStringLiteral("TITRE"));
        QCOMPARE(box.accessibleName(), QStringLiteral("Titre"));
        QCOMPARE(box.property("titleSource").toString(), QStringLiteral("Titre"));
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::AppStyleTest)
#include "test_app_style.moc"
