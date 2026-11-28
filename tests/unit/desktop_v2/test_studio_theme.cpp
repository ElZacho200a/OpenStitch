// SPDX-License-Identifier: Apache-2.0
// Moteur de thèmes côté Qt (studio/theme_controller) : changement à chaud sans recréer les
// composants, thème défectueux refusé, persistance entre deux sessions, retour au défaut,
// archives (export / import), apparence suivie, document jamais touché, coût d'un changement.
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>
#include <fstream>
#include <memory>

#include "document_session.hpp"
#include "preferences_controller.hpp"
#include "studio_controller.hpp"
#include "theme_controller.hpp"

using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

std::filesystem::path path(const QString& p) {
    return std::filesystem::path(p.toStdWString());
}

void writeFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// Un bouton et quelques jetons liés au singleton Theme, lui-même alimenté par le contrôleur
// comme dans Main.qml.
constexpr auto kQml = R"(
import QtQuick
import "qrc:/studio/qml"
Item {
    width: 400; height: 200
    property color accent: Theme.accent
    property int radius: Theme.radius
    property string family: Theme.fontFamily
    property var buttonStyle: Theme.style("button", "default", "normal")
    Binding { target: Theme; property: "tokens"; value: themeEngine.tokens }
    Binding { target: Theme; property: "styles"; value: themeEngine.styles }
    Binding { target: Theme; property: "icons"; value: themeEngine.icons }
    Binding { target: Theme; property: "meta"; value: themeEngine.meta }
    StudioButton { objectName: "probe"; text: "Essai"; iconName: "save" }
    StudioCard { objectName: "card"; y: 60; width: 100; height: 60 }
}
)";

} // namespace

class StudioThemeTest : public QObject {
    Q_OBJECT

    QTemporaryDir config_;
    std::filesystem::path builtin_{OPENSTITCH_THEMES_DIR};

    std::unique_ptr<PreferencesController> prefs() {
        return std::make_unique<PreferencesController>(path(config_.path()));
    }
    std::unique_ptr<ThemeController> engine(PreferencesController* p) {
        return std::make_unique<ThemeController>(builtin_, path(config_.path()) / "themes", p);
    }

private slots:
    void init() {
        QVERIFY(config_.isValid());
        std::filesystem::remove_all(path(config_.path()));
        std::filesystem::create_directories(path(config_.path()));
    }

    void startsOnNeoStudio() {
        auto p = prefs();
        auto e = engine(p.get());
        QCOMPARE(e->activeId(), QStringLiteral("neo-studio"));
        QVERIFY(e->lastError().isEmpty());
        QVERIFY(e->themes().size() >= 3);
        QCOMPARE(e->tokens().value(QStringLiteral("color.accent")).value<QColor>(),
                 QColor("#ef91bb"));
    }

    void hotSwitchKeepsItemsAndUpdatesBindings() {
        auto p = prefs();
        auto e = engine(p.get());
        QQmlEngine qml;
        qml.rootContext()->setContextProperty(QStringLiteral("themeEngine"), e.get());
        QQmlComponent component(&qml);
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test/theme.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* button = root->findChild<QQuickItem*>(QStringLiteral("probe"));
        auto* card = root->findChild<QQuickItem*>(QStringLiteral("card"));
        QVERIFY(button && card);
        QCOMPARE(root->property("radius").toInt(), 8);
        QVERIFY(root->property("buttonStyle").toMap().isEmpty()); // Neo : style intégré

        QSignalSpy changed(e.get(), &ThemeController::themeChanged);
        QVERIFY(e->activate(QStringLiteral("bubblegum")));
        QCOMPARE(changed.count(), 1);
        // Mêmes objets : rien n'est recréé, seules les liaisons sont réévaluées.
        QCOMPARE(root->findChild<QQuickItem*>(QStringLiteral("probe")), button);
        QCOMPARE(root->findChild<QQuickItem*>(QStringLiteral("card")), card);
        QCOMPARE(root->property("accent").value<QColor>(), QColor("#ad2169"));
        QCOMPARE(root->property("radius").toInt(), 16);
        QCOMPARE(root->property("buttonStyle").toMap().value(QStringLiteral("radius")).toDouble(),
                 21.0);
        // Police embarquée chargée depuis le dossier du thème.
        QCOMPARE(root->property("family").toString(), QStringLiteral("Nunito"));
        QCOMPARE(button->property("themeVariant").toString(), QStringLiteral("default"));
        QVERIFY(e->diagnostics().isEmpty());

        QVERIFY(e->activate(QStringLiteral("neo-studio")));
        QCOMPARE(root->property("accent").value<QColor>(), QColor("#ef91bb"));
        QCOMPARE(root->property("radius").toInt(), 8);
    }

    void brokenThemeKeepsThePreviousOne() {
        auto p = prefs();
        auto e = engine(p.get());
        writeFile(path(config_.path()) / "themes" / "broken" / "manifest.json",
                  R"({"schemaVersion":1,"id":"broken","name":"Cassé","parent":"ghost"})");
        writeFile(path(config_.path()) / "themes" / "future" / "manifest.json",
                  R"({"schemaVersion":99,"id":"future","name":"Futur"})");
        e->reload();
        QVERIFY(e->activate(QStringLiteral("bubblegum")));
        QVERIFY(!e->activate(QStringLiteral("broken")));
        QVERIFY(e->lastError().contains(QStringLiteral("ghost")));
        QCOMPARE(e->activeId(), QStringLiteral("bubblegum"));
        QCOMPARE(p->themeId(), QStringLiteral("bubblegum"));
        // Le thème au format plus récent est listé, non activable, avec sa raison.
        bool listed = false;
        for (const auto& item : e->themes()) {
            const auto m = item.toMap();
            if (m.value(QStringLiteral("dir")).toString().endsWith(QStringLiteral("future"))) {
                listed = true;
                QVERIFY(!m.value(QStringLiteral("loadable")).toBool());
                QVERIFY(m.value(QStringLiteral("messages"))
                            .toStringList()
                            .join(' ')
                            .contains(QStringLiteral("plus récent")));
            }
        }
        QVERIFY(listed);
    }

    void choicePersistsAcrossSessions() {
        {
            auto p = prefs();
            auto e = engine(p.get());
            QVERIFY(e->activate(QStringLiteral("neo-cobalt")));
        }
        auto p = prefs();
        auto e = engine(p.get());
        QCOMPARE(e->activeId(), QStringLiteral("neo-cobalt"));
        // Essai sans persistance (recette --theme) : la préférence ne bouge pas.
        QVERIFY(e->activate(QStringLiteral("bubblegum"), false));
        auto again = prefs();
        QCOMPARE(again->themeId(), QStringLiteral("neo-cobalt"));
    }

    // Disposition des panneaux (DockPanel.qml) : fusion champ par champ, relue telle quelle à la
    // session suivante, sans toucher à la session de document.
    void panelLayoutPersistsAcrossSessions() {
        {
            auto p = prefs();
            QVERIFY(p->panelLayout().isEmpty());
            QSignalSpy changed(p.get(), &PreferencesController::panelLayoutChanged);
            p->setPanelState(QStringLiteral("vectorSideCard"),
                             {{QStringLiteral("hidden"), false},
                              {QStringLiteral("collapsed"), true},
                              {QStringLiteral("freePositioned"), true},
                              {QStringLiteral("x"), 120.0},
                              {QStringLiteral("y"), 48.0}});
            QCOMPARE(changed.count(), 1);
            // Fusion : un second appel partiel garde les autres champs et les autres panneaux.
            p->setPanelState(QStringLiteral("vectorSideCard"), {{QStringLiteral("hidden"), true}});
            p->setPanelState(QStringLiteral("imageSidePanel"),
                             {{QStringLiteral("collapsed"), false}});
            // Identifiant vide ou état vide : ignorés.
            p->setPanelState(QString(), {{QStringLiteral("hidden"), true}});
            p->setPanelState(QStringLiteral("pointsObjectsCard"), {});
            QCOMPARE(changed.count(), 3);
            p->rememberSession({{QStringLiteral("file"), QStringLiteral("C:/a.osp")}});
        }
        auto p = prefs();
        const auto layout = p->panelLayout();
        QCOMPARE(layout.size(), 2);
        const auto vector = layout.value(QStringLiteral("vectorSideCard")).toMap();
        QCOMPARE(vector.value(QStringLiteral("hidden")).toBool(), true);
        QCOMPARE(vector.value(QStringLiteral("collapsed")).toBool(), true);
        QCOMPARE(vector.value(QStringLiteral("freePositioned")).toBool(), true);
        QCOMPARE(vector.value(QStringLiteral("x")).toDouble(), 120.0);
        QCOMPARE(vector.value(QStringLiteral("y")).toDouble(), 48.0);
        QCOMPARE(layout.value(QStringLiteral("imageSidePanel"))
                     .toMap()
                     .value(QStringLiteral("collapsed"))
                     .toBool(),
                 false);
        // La disposition ne se mélange pas à la session de document (et réciproquement).
        QCOMPARE(p->session().value(QStringLiteral("file")).toString(), QStringLiteral("C:/a.osp"));
        p->forgetSession();
        QCOMPARE(prefs()->panelLayout().size(), 2);
    }

    void missingPreferredThemeFallsBackToDefault() {
        {
            auto p = prefs();
            p->setThemeId(QStringLiteral("disparu"));
        }
        auto p = prefs();
        auto e = engine(p.get());
        QCOMPARE(e->activeId(), QStringLiteral("neo-studio"));
        QVERIFY(e->lastError().contains(QStringLiteral("disparu")));
    }

    void resetToDefaultAndAppearance() {
        auto p = prefs();
        auto e = engine(p.get());
        QVERIFY(e->activate(QStringLiteral("neo-cobalt")));
        p->setTheme(QStringLiteral("light"));
        QCOMPARE(e->meta().value(QStringLiteral("appearance")).toString(), QStringLiteral("light"));
        QCOMPARE(e->tokens().value(QStringLiteral("color.accent")).value<QColor>(),
                 QColor("#1f56b8"));
        p->setTheme(QStringLiteral("dark"));
        QCOMPARE(e->meta().value(QStringLiteral("appearance")).toString(), QStringLiteral("dark"));
        // Bubblegum n'a qu'une variante claire : l'apparence sombre demandée retombe dessus.
        QVERIFY(e->activate(QStringLiteral("bubblegum")));
        QCOMPARE(e->meta().value(QStringLiteral("appearance")).toString(), QStringLiteral("light"));
        e->resetToDefault();
        QCOMPARE(e->activeId(), QStringLiteral("neo-studio"));
        QCOMPARE(p->themeId(), QStringLiteral("neo-studio"));
        // « Valeurs par défaut » des préférences ramène aussi le thème par défaut.
        QVERIFY(e->activate(QStringLiteral("bubblegum")));
        p->resetDefaults();
        QCOMPARE(e->activeId(), QStringLiteral("neo-studio"));
    }

    void archiveExportImportRoundTrip() {
        auto p = prefs();
        auto e = engine(p.get());
        const auto created = e->createDerived(
            QStringLiteral("bubblegum"), QStringLiteral("mon-theme"), QStringLiteral("Mon thème"));
        QVERIFY2(created.value(QStringLiteral("ok")).toBool(),
                 qPrintable(created.value(QStringLiteral("messages")).toStringList().join('\n')));
        const auto zip = QDir(config_.path()).filePath(QStringLiteral("mon-theme.zip"));
        QVERIFY(e->exportTheme(QStringLiteral("mon-theme"), QUrl::fromLocalFile(zip)));
        QVERIFY(QFileInfo(zip).size() > 0);
        QVERIFY(e->removeTheme(QStringLiteral("mon-theme")));
        auto imported = e->importArchive(QUrl::fromLocalFile(zip));
        QVERIFY2(imported.value(QStringLiteral("ok")).toBool(),
                 qPrintable(imported.value(QStringLiteral("messages")).toStringList().join('\n')));
        QCOMPARE(imported.value(QStringLiteral("id")).toString(), QStringLiteral("mon-theme"));
        // Réimport : signalé comme existant, accepté en remplacement.
        imported = e->importArchive(QUrl::fromLocalFile(zip));
        QVERIFY(!imported.value(QStringLiteral("ok")).toBool());
        QVERIFY(imported.value(QStringLiteral("exists")).toBool());
        QVERIFY(
            e->importArchive(QUrl::fromLocalFile(zip), true).value(QStringLiteral("ok")).toBool());
        QVERIFY(e->activate(QStringLiteral("mon-theme")));
        QCOMPARE(e->tokens().value(QStringLiteral("color.accent")).value<QColor>(),
                 QColor("#ad2169"));
        // Une archive qui n'en est pas une est refusée proprement.
        writeFile(path(config_.path()) / "faux.zip", "pas une archive");
        QVERIFY(!e->importArchive(QUrl::fromLocalFile(
                                      QDir(config_.path()).filePath(QStringLiteral("faux.zip"))))
                     .value(QStringLiteral("ok"))
                     .toBool());
        // Un thème livré ne peut pas être écrasé par une archive.
        const auto builtinZip = QDir(config_.path()).filePath(QStringLiteral("neo.zip"));
        QVERIFY(e->exportTheme(QStringLiteral("neo-studio"), QUrl::fromLocalFile(builtinZip)));
        QVERIFY(!e->importArchive(QUrl::fromLocalFile(builtinZip), true)
                     .value(QStringLiteral("ok"))
                     .toBool());
    }

    void themeSwitchNeverTouchesTheDocument() {
        DocumentSession session;
        StudioController controller(session);
        const auto before = session.snapshot();
        const auto history = session.historySize();
        auto p = prefs();
        auto e = engine(p.get());
        for (int i = 0; i < 6; ++i) {
            QVERIFY(
                e->activate(i % 2 ? QStringLiteral("neo-studio") : QStringLiteral("bubblegum")));
        }
        QCOMPARE(session.snapshot().get(), before.get()); // même version du projet
        QCOMPARE(session.historySize(), history);
        QVERIFY(!session.dirty());
    }

    void switchingIsCheap() {
        auto p = prefs();
        auto e = engine(p.get());
        QQmlEngine qml;
        qml.rootContext()->setContextProperty(QStringLiteral("themeEngine"), e.get());
        QQmlComponent component(&qml);
        component.setData(kQml, QUrl(QStringLiteral("qrc:/test/theme.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        QElapsedTimer timer;
        timer.start();
        constexpr int kSwitches = 20;
        for (int i = 0; i < kSwitches; ++i) {
            QVERIFY(
                e->activate(i % 2 ? QStringLiteral("neo-studio") : QStringLiteral("bubblegum")));
        }
        const auto ms = timer.elapsed();
        qInfo("theme switch: %.1f ms on average (%d switches, resolution + QML bindings)",
              double(ms) / kSwitches, kSwitches);
        // Borne large (machine de CI lente) : un changement reste une affaire de millisecondes.
        QVERIFY2(ms / kSwitches < 250, qPrintable(QString::number(ms)));
    }
};

QTEST_MAIN(StudioThemeTest)
#include "test_studio_theme.moc"
