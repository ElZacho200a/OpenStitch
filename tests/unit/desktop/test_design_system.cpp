// SPDX-License-Identifier: Apache-2.0
// Design system v2 appliqué à une vraie QApplication (offscreen) : style Fusion forcé,
// bascule thème/densité sans crash, métriques par densité, choix « Système », glyphes
// enregistrés en mémoire et leur repli.
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QGroupBox>
#include <QGuiApplication>
#include <QMenu>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolTip>
#include <QVBoxLayout>

#include "app_style.hpp"
#include "app_theme.hpp"
#include "design_tokens.hpp"
#include "main_window.hpp"
#include "style_assets.hpp"
#include "ui_style.hpp"

namespace openstitch::desktop {

namespace {

QStringList g_messages;
QtMessageHandler g_previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (msg.contains(QLatin1String("This plugin does not support"))) {
        return; // bruit de la plateforme offscreen
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        g_messages << msg;
    }
}

// Nom du style de base : avec une QSS d'application, QApplication::style() est le proxy
// QStyleSheetStyle (nom vide) -> on lit le style sans feuille, puis on la remet.
QString baseStyleName() {
    const QString qss = qApp->styleSheet();
    qApp->setStyleSheet(QString());
    const QString name = qApp->style()->name();
    qApp->setStyleSheet(qss);
    return name;
}

} // namespace

class DesignSystemTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        // Isolation des QSettings : jamais le profil réel (AppTheme persiste).
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchDesignSystemTest"));
        QCoreApplication::setApplicationName(QStringLiteral("DesignSystemTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        savedFont_ = qApp->font();
        savedPalette_ = qApp->palette();
        savedStyleName_ = qApp->style()->name();
    }

    void init() {
        g_messages.clear();
        QSettings s(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
        s.clear();
        s.sync();
        auto& theme = AppTheme::instance();
        theme.setSystemPreferenceForTesting(std::nullopt);
        style_assets::forceFailureForTesting(false);
        theme.setMode(ThemeMode::Light);
        theme.setDensity(Density::Comfortable);
        QSettings s2(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
        s2.clear();
        s2.sync();
    }

    void cleanup() {
        if (g_previous != nullptr) {
            qInstallMessageHandler(g_previous);
            g_previous = nullptr;
        }
        style_assets::forceFailureForTesting(false);
        AppTheme::instance().setSystemPreferenceForTesting(std::nullopt);
    }

    void cleanupTestCase() {
        // Restitue l'état global (style/palette/police/feuille/glyphes/thème).
        auto& theme = AppTheme::instance();
        theme.setMode(ThemeMode::Light);
        theme.setDensity(Density::Comfortable);
        style_assets::uninstall();
        qApp->setStyleSheet(QString());
        QApplication::setStyle(savedStyleName_);
        qApp->setPalette(savedPalette_);
        qApp->setFont(savedFont_);
    }

    void applyToAppForcesFusion() {
        // Part d'un autre style : applyToApp doit le remplacer par Fusion.
        qApp->setStyleSheet(QString());
        QVERIFY(QApplication::setStyle(QStringLiteral("Windows")) != nullptr);
        QCOMPARE(qApp->style()->name().toLower(), QStringLiteral("windows"));
        AppTheme::instance().applyToApp(*qApp);
        QCOMPARE(baseStyleName().toLower(), QStringLiteral("fusion"));
        // La palette appliquée est la nôtre (setStyle ne l'a pas écrasée : ordre correct).
        QCOMPARE(qApp->palette().color(QPalette::Window), AppTheme::instance().tokens().window);
        QCOMPARE(qApp->palette().color(QPalette::Highlight),
                 AppTheme::instance().tokens().textSelection);
        QCOMPARE(qApp->font().pointSizeF(), AppTheme::instance().tokens().fontBase);
    }

    void reapplyIsIdempotentAndWarningFree() {
        g_previous = qInstallMessageHandler(capture);
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        const QString first = qApp->styleSheet();
        const QPalette firstPalette = qApp->palette();
        theme.applyToApp(*qApp);
        QCOMPARE(qApp->styleSheet().size() > 4000, true);
        // Même thème/densité => même feuille hors racine des glyphes (n incrémenté).
        auto normalised = [](QString s) {
            return s.replace(QRegularExpression(QStringLiteral("openstitch-\\d+")),
                             QStringLiteral("openstitch-N"));
        };
        QCOMPARE(normalised(qApp->styleSheet()), normalised(first));
        QCOMPARE(qApp->palette(), firstPalette);
        QCOMPARE(baseStyleName().toLower(), QStringLiteral("fusion"));
        qInstallMessageHandler(g_previous);
        g_previous = nullptr;
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(QLatin1Char('\n'))));
    }

    void themeAndDensitySwitchUpdatesWidgetsOfEveryKind() {
        g_previous = qInstallMessageHandler(capture);
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);

        QWidget host;
        auto* lay = new QVBoxLayout(&host);
        auto* primary = new QPushButton(QStringLiteral("Primaire"));
        ui::setVariant(primary, ui::ButtonVariant::Primary);
        auto* tonal = new QPushButton(QStringLiteral("Tonal"));
        ui::setVariant(tonal, ui::ButtonVariant::Tonal);
        auto* ghost = new QPushButton(QStringLiteral("Ghost"));
        ui::setVariant(ghost, ui::ButtonVariant::Ghost);
        auto* danger = new QPushButton(QStringLiteral("Danger"));
        ui::setVariant(danger, ui::ButtonVariant::Danger);
        auto* check = new QCheckBox(QStringLiteral("Case"));
        check->setChecked(true);
        auto* slider = new QSlider(Qt::Horizontal);
        auto* progress = new QProgressBar;
        auto* tabs = new QTabBar;
        tabs->addTab(QStringLiteral("A"));
        tabs->addTab(QStringLiteral("B"));
        auto* group = new QGroupBox(QStringLiteral("Groupe"));
        auto* bar = new QScrollBar(Qt::Vertical);
        for (QWidget* w : std::initializer_list<QWidget*>{primary, tonal, ghost, danger, check,
                                                          slider, progress, tabs, group, bar}) {
            lay->addWidget(w);
        }
        host.show();
        QApplication::processEvents();

        QSignalSpy spy(&theme, &AppTheme::changed);
        const int comfortableHeight = primary->minimumSizeHint().height();
        QVERIFY(comfortableHeight >= theme.tokens().controlHeight);
        QCOMPARE(comfortableHeight, theme.tokens().controlHeight);
        QCOMPARE(tonal->minimumSizeHint().height(), comfortableHeight);
        QCOMPARE(ghost->minimumSizeHint().height(), comfortableHeight);
        QCOMPARE(danger->minimumSizeHint().height(), comfortableHeight);
        QCOMPARE(bar->sizeHint().width(), theme.tokens().scrollbarWidth);

        theme.setDensity(Density::Compact);
        QApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        const int compactHeight = primary->minimumSizeHint().height();
        QVERIFY(compactHeight < comfortableHeight);
        QCOMPARE(compactHeight, theme.tokens().controlHeight);
        QVERIFY(compactHeight >= 24);

        theme.setMode(ThemeMode::Dark);
        QApplication::processEvents();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(qApp->palette().color(QPalette::Window), dark_tokens(Density::Compact).window);
        QCOMPARE(theme.mode(), ThemeMode::Dark);

        // Idempotence : mêmes valeurs => aucun signal, aucune ré-application.
        theme.setMode(ThemeMode::Dark);
        theme.setDensity(Density::Compact);
        QCOMPARE(spy.count(), 2);

        theme.setMode(ThemeMode::Light);
        theme.setDensity(Density::Comfortable);
        QApplication::processEvents();
        QCOMPARE(spy.count(), 4);
        QCOMPARE(primary->minimumSizeHint().height(), comfortableHeight);
        QCOMPARE(qApp->palette().color(QPalette::Window), light_tokens().window);

        // Menu et infobulle habillés : construction + affichage sans crash.
        QMenu menu;
        menu.addAction(QStringLiteral("Action"));
        menu.addSeparator();
        menu.addAction(QStringLiteral("Autre"))->setEnabled(false);
        menu.ensurePolished();
        menu.show();
        QToolTip::showText(QPoint(10, 10), QStringLiteral("Infobulle"), &host);
        QApplication::processEvents();
        QToolTip::hideText();
        menu.hide();
        host.hide();

        qInstallMessageHandler(g_previous);
        g_previous = nullptr;
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(QLatin1Char('\n'))));
    }

    void mainWindowSurvivesEverySwitch() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QSignalSpy spy(&theme, &AppTheme::changed);
        theme.setMode(ThemeMode::Dark);
        theme.setDensity(Density::Compact);
        theme.setMode(ThemeMode::Light);
        theme.setDensity(Density::Comfortable);
        theme.setThemeChoice(ThemeChoice::Dark);
        theme.setMode(ThemeMode::Light);
        QApplication::processEvents();
        QCOMPARE(spy.count(), 6);
        QCOMPARE(qApp->palette().color(QPalette::Window), light_tokens().window);
        QVERIFY(window.isVisible());
    }

    void variantAndRoleContract() {
        AppTheme::instance().applyToApp(*qApp);
        QPushButton b(QStringLiteral("x"));
        ui::setVariant(&b, ui::ButtonVariant::Primary);
        QCOMPARE(b.property("variant").toString(), QStringLiteral("primary"));
        // Valeur inconnue : rendu par défaut, jamais d'erreur.
        g_previous = qInstallMessageHandler(capture);
        b.setProperty("variant", QStringLiteral("bogus"));
        ui::repolish(&b);
        b.show();
        QApplication::processEvents();
        QVERIFY(b.minimumSizeHint().height() >= AppTheme::instance().tokens().controlHeight);
        qInstallMessageHandler(g_previous);
        g_previous = nullptr;
        QVERIFY(g_messages.isEmpty());

        // QDialogButtonBox : le bouton Ok est le bouton par défaut (:default => primaire).
        QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        auto* ok = box.button(QDialogButtonBox::Ok);
        QVERIFY(ok != nullptr);
        ok->setDefault(true);
        QVERIFY(ok->isDefault());
        box.show();
        QApplication::processEvents();
        QVERIFY(ok->minimumSizeHint().height() >= AppTheme::instance().tokens().controlHeight);
    }

    void systemChoiceFollowsThePreference() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        QSignalSpy spy(&theme, &AppTheme::changed);

        theme.setSystemPreferenceForTesting(true);
        theme.setThemeChoice(ThemeChoice::System);
        QCOMPARE(theme.themeChoice(), ThemeChoice::System);
        QCOMPARE(theme.resolvedMode(), ThemeMode::Dark);
        QCOMPARE(theme.mode(), ThemeMode::Dark);
        QCOMPARE(qApp->palette().color(QPalette::Window), dark_tokens().window);

        theme.setSystemPreferenceForTesting(false);
        theme.setThemeChoice(ThemeChoice::Light);
        theme.setThemeChoice(ThemeChoice::System);
        QCOMPARE(theme.resolvedMode(), ThemeMode::Light);
        QCOMPARE(qApp->palette().color(QPalette::Window), light_tokens().window);
        QVERIFY(spy.count() >= 3);

        // setMode explicite quitte le mode Système.
        theme.setMode(ThemeMode::Dark);
        QCOMPARE(theme.themeChoice(), ThemeChoice::Dark);
    }

    void systemChoiceWithoutOverrideFallsBackSanely() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        theme.setThemeChoice(ThemeChoice::System);
        const ThemeMode resolved = theme.resolvedMode();
        QVERIFY(resolved == ThemeMode::Light || resolved == ThemeMode::Dark);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown) {
            QCOMPARE(resolved,
                     scheme == Qt::ColorScheme::Dark ? ThemeMode::Dark : ThemeMode::Light);
        }
#else
        // Qt 6.4 : pas de suivi en direct, valeur lue au démarrage (palette de la plateforme
        // offscreen : claire).
        QCOMPARE(resolved, ThemeMode::Light);
#endif
        QCOMPARE(qApp->palette().color(QPalette::Window),
                 tokens_for(resolved, theme.density()).window);
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void nativeColorSchemeFollowsTheChoice() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        theme.setThemeChoice(ThemeChoice::Dark);
        QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);
        theme.setThemeChoice(ThemeChoice::Light);
        QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Light);
        theme.setThemeChoice(ThemeChoice::System);
        // Pas de récursion infinie ni de blocage : la palette reste celle d'un thème.
        QVERIFY(qApp->palette().color(QPalette::Window) == light_tokens().window ||
                qApp->palette().color(QPalette::Window) == dark_tokens().window);
    }
#endif

    void themeChoiceIsPersistedAndRestored() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        theme.setThemeChoice(ThemeChoice::System);
        theme.setDensity(Density::Compact);
        {
            QSettings s(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.sync();
            QCOMPARE(s.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("system"));
            QCOMPARE(s.value(QStringLiteral("ui/density")).toString(), QStringLiteral("compact"));
        }
        theme.setThemeChoice(ThemeChoice::Dark);
        {
            QSettings s(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.sync();
            QCOMPARE(s.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("dark"));
        }
        // Aller-retour : applyToApp recharge ce qui a été persisté.
        theme.setSystemPreferenceForTesting(false);
        theme.setThemeChoice(ThemeChoice::System);
        theme.setThemeChoice(ThemeChoice::Light); // état en mémoire différent du disque
        {
            QSettings s(QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            s.setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
            s.sync();
        }
        theme.applyToApp(*qApp);
        QCOMPARE(theme.themeChoice(), ThemeChoice::Dark);
        QCOMPARE(theme.density(), Density::Compact);
        QCOMPARE(theme.mode(), ThemeMode::Dark);
    }

    void glyphAssetsAreRegisteredFromMemory() {
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        const QString root = style_assets::currentRoot();
        QVERIFY2(!root.isEmpty(), "racine de glyphes non enregistree");
        QVERIFY(root.startsWith(QStringLiteral(":/openstitch-")));
        for (const QString& name : style_assets::glyph_names()) {
            QVERIFY2(QFile::exists(root + QLatin1Char('/') + name), qPrintable(name));
            const QString two = name.left(name.size() - 4) + QStringLiteral("@2x.png");
            QVERIFY2(QFile::exists(root + QLatin1Char('/') + two), qPrintable(two));
        }
        QFile f(root + QStringLiteral("/check.png"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.size() > 50);
        QPixmap pm(root + QStringLiteral("/check.png"));
        QVERIFY(!pm.isNull());
        QCOMPARE(pm.width(), 12);
        QPixmap pm2(root + QStringLiteral("/check@2x.png"));
        QCOMPARE(pm2.width(), 24);
        // Le glyphe est coloré par les tokens (coche = onAccent) : un pixel opaque en provient.
        const QImage img = pm2.toImage().convertToFormat(QImage::Format_ARGB32);
        bool found = false;
        for (int y = 0; y < img.height() && !found; ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                if (c.alpha() == 255) {
                    QCOMPARE(c.rgb(), theme.tokens().onAccent.rgb());
                    found = true;
                    break;
                }
            }
        }
        QVERIFY(found);

        // La feuille de style référence cette racine entre guillemets.
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("url(\"%1/check.png\")").arg(root)));

        // Bascule : nouvelle racine, l'ancienne est désenregistrée.
        theme.setMode(ThemeMode::Dark);
        const QString second = style_assets::currentRoot();
        QVERIFY(!second.isEmpty());
        QVERIFY(second != root);
        QVERIFY(!QFile::exists(root + QStringLiteral("/check.png")));
        QVERIFY(QFile::exists(second + QStringLiteral("/check.png")));
        QPixmap dark(second + QStringLiteral("/check@2x.png"));
        QVERIFY(!dark.isNull());
    }

    void glyphFallbackProducesAStyleSheetWithoutImages() {
        g_previous = qInstallMessageHandler(capture);
        auto& theme = AppTheme::instance();
        theme.applyToApp(*qApp);
        QVERIFY(!style_assets::currentRoot().isEmpty());
        style_assets::forceFailureForTesting(true);
        theme.setMode(ThemeMode::Dark);
        QVERIFY(style_assets::currentRoot().isEmpty());
        QVERIFY(!qApp->styleSheet().contains(QStringLiteral("image:")));
        QVERIFY(!qApp->styleSheet().contains(QStringLiteral("url(")));
        QVERIFY(qApp->styleSheet().size() > 4000);
        // Widgets habillés malgré tout, sans avertissement.
        QCheckBox box(QStringLiteral("c"));
        box.setChecked(true);
        box.show();
        QApplication::processEvents();
        style_assets::forceFailureForTesting(false);
        theme.setMode(ThemeMode::Light);
        QVERIFY(!style_assets::currentRoot().isEmpty());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("image:")));
        qInstallMessageHandler(g_previous);
        g_previous = nullptr;
        QVERIFY2(g_messages.isEmpty(), qPrintable(g_messages.join(QLatin1Char('\n'))));
    }

    void resourceBlobIsDeterministic() {
        const Tokens t = light_tokens();
        QCOMPARE(style_assets::build_resource_blob(t), style_assets::build_resource_blob(t));
        QVERIFY(style_assets::build_resource_blob(t) !=
                style_assets::build_resource_blob(dark_tokens()));
        QVERIFY(style_assets::build_resource_blob(t).startsWith("qres"));
        QVERIFY(style_assets::glyph_image(QStringLiteral("nope.png"), t, 1).isNull());
    }

private:
    QTemporaryDir settingsDir_;
    QFont savedFont_;
    QPalette savedPalette_;
    QString savedStyleName_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::DesignSystemTest)
#include "test_design_system.moc"
