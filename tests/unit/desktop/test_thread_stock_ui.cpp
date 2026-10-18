// SPDX-License-Identifier: Apache-2.0
// « Mes fils » (HP-THR-007) : panneau (cases, liste, signaux), persistance du stock dans les
// preferences utilisateur, et recherche du fil le plus proche limitee au stock.
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "app_settings.hpp"
#include "main_window.hpp"
#include "thread_panel.hpp"

using namespace openstitch;
using openstitch::desktop::MainWindow;
using openstitch::desktop::ThreadPanel;

namespace {

document::Project twoTrianglesProject() {
    document::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(2 * 2 * 4, 255);
    const std::array<std::uint8_t, 3> colors[2] = {{200, 16, 46}, {0, 114, 206}};
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Triangle";
        geometry::Path tri;
        tri.closed = true;
        const std::int32_t ox = i * 5'000;
        const auto node = [](std::int32_t x, std::int32_t y) {
            return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        tri.nodes = {node(ox, 0), node(ox + 1'000, 0), node(ox, 1'000)};
        vec.paths.push_back(geometry::PathSet{tri, {}});
        project.vector_objects.push_back(vec);

        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "Triangle " + std::to_string(i + 1);
        emb.source_vector = vec.id;
        emb.rgb = colors[i];
        emb.params = document::RunningStitchParams{};
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

thread_palette::Thread personalRed() {
    thread_palette::Thread t;
    t.key = {thread_palette::kPersonalChartId, "M-001"};
    t.name = "Rouge maison";
    t.brand = "Perso";
    t.rgb = {200, 16, 46};
    return t;
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

    QTemporaryDir settingsDir_;

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QStandardPaths::setTestModeEnabled(true);
    }

    void init() {
        QFile::remove(threadStockFilePath());
        saveLimitToThreadStock(false);
    }

    void stockFileLivesNextToTheSettingsFileAndRoundTrips() {
        QCOMPARE(QFileInfo(threadStockFilePath()).absolutePath(),
                 QFileInfo(settingsFilePath()).absolutePath());
        thread_palette::ThreadStock stock;
        QVERIFY(stock.own({"generic", "G02"}));
        QVERIFY(stock.add_personal(personalRed()).has_value());
        QString error;
        QVERIFY(saveThreadStock(stock, threadStockFilePath(), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(loadThreadStock(threadStockFilePath()) == stock);
        // Deterministe : deux ecritures du meme stock = memes octets.
        QFile first(threadStockFilePath());
        QVERIFY(first.open(QIODevice::ReadOnly));
        const QByteArray bytes = first.readAll();
        first.close();
        QVERIFY(saveThreadStock(stock, threadStockFilePath()));
        QFile second(threadStockFilePath());
        QVERIFY(second.open(QIODevice::ReadOnly));
        QCOMPARE(second.readAll(), bytes);
    }

    void missingFileGivesAnEmptyStockWithoutError() {
        QString error;
        const auto stock = loadThreadStock(threadStockFilePath(), &error);
        QVERIFY(stock.empty());
        QVERIFY(error.isEmpty());
    }

    void corruptFileIsKeptAsideAndReported() {
        QDir().mkpath(QFileInfo(threadStockFilePath()).absolutePath());
        QFile bad(threadStockFilePath());
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{ pas du json");
        bad.close();
        QString error;
        const auto stock = loadThreadStock(threadStockFilePath(), &error);
        QVERIFY(stock.empty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(threadStockFilePath()));
        QVERIFY(QFile::exists(threadStockFilePath() + QStringLiteral(".invalide")));
        QFile::remove(threadStockFilePath() + QStringLiteral(".invalide"));
    }

    void limitPreferenceRoundTrips() {
        QVERIFY(!loadLimitToThreadStock());
        saveLimitToThreadStock(true);
        QVERIFY(loadLimitToThreadStock());
    }

    void panelListsOwnedAndPersonalThreads() {
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        ThreadPanel panel;
        panel.setLibrary(&library);
        auto* list = panel.findChild<QListWidget*>("threadStockList");
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);
        thread_palette::ThreadStock stock;
        QVERIFY(stock.own({"generic", "G02"}));
        QVERIFY(stock.add_personal(personalRed()).has_value());
        panel.setStock(stock);
        QCOMPARE(list->count(), 2);
        QVERIFY(panel.stock() == stock);
    }

    void ownCheckFollowsTheSelectedCatalogThreadAndEmitsToggles() {
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        ThreadPanel panel;
        panel.setLibrary(&library);
        auto* catalog = panel.findChild<QListWidget*>("threadCatalogList");
        auto* own = panel.findChild<QCheckBox*>("threadOwnCheck");
        QVERIFY(catalog != nullptr && own != nullptr);
        QVERIFY(!own->isEnabled()); // rien de selectionne
        catalog->setCurrentRow(1);  // generic G02
        QVERIFY(own->isEnabled());
        QVERIFY(!own->isChecked());

        std::vector<std::pair<std::string, bool>> seen;
        connect(&panel, &ThreadPanel::stockOwnedToggled, &panel,
                [&seen](const thread_palette::ThreadKey& key, bool owned) {
                    seen.emplace_back(key.chart_id + "/" + key.code, owned);
                });
        own->click();
        QCOMPARE(seen.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(seen[0].first), QStringLiteral("generic/G02"));
        QVERIFY(seen[0].second);

        // Le panneau n'edite pas le stock : c'est MainWindow qui le renvoie via setStock.
        thread_palette::ThreadStock stock;
        QVERIFY(stock.own({"generic", "G02"}));
        panel.setStock(stock);
        catalog->setCurrentRow(0);
        catalog->setCurrentRow(1);
        QVERIFY(own->isChecked());
    }

    void limitCheckEmitsButProgrammaticSetDoesNot() {
        ThreadPanel panel;
        auto* limit = panel.findChild<QCheckBox*>("threadLimitStockCheck");
        QVERIFY(limit != nullptr);
        int emitted = 0;
        bool last = false;
        connect(&panel, &ThreadPanel::limitToStockToggled, &panel, [&](bool v) {
            ++emitted;
            last = v;
        });
        panel.setLimitToStock(true);
        QCOMPARE(emitted, 0);
        QVERIFY(panel.limitToStock());
        limit->click();
        QCOMPARE(emitted, 1);
        QVERIFY(!last);
    }

    void removeButtonEmitsTheSelectedStockKey() {
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        ThreadPanel panel;
        panel.setLibrary(&library);
        thread_palette::ThreadStock stock;
        QVERIFY(stock.add_personal(personalRed()).has_value());
        panel.setStock(stock);
        auto* list = panel.findChild<QListWidget*>("threadStockList");
        auto* remove = panel.findChild<QPushButton*>("threadRemoveStockBtn");
        QVERIFY(!remove->isEnabled());
        list->setCurrentRow(0);
        QVERIFY(remove->isEnabled());
        std::string removed;
        connect(&panel, &ThreadPanel::personalThreadRemoveRequested, &panel,
                [&removed](const thread_palette::ThreadKey& key) { removed = key.code; });
        remove->click();
        QCOMPARE(QString::fromStdString(removed), QStringLiteral("M-001"));
    }

    void addPersonalDialogEmitsTheTypedThread() {
        ThreadPanel panel;
        std::optional<thread_palette::Thread> got;
        connect(&panel, &ThreadPanel::personalThreadAddRequested, &panel,
                [&got](const thread_palette::Thread& t) { got = t; });
        QTimer::singleShot(0, [] {
            auto* d = QApplication::activeModalWidget();
            if (d == nullptr) {
                return;
            }
            d->findChild<QLineEdit*>("personalThreadCode")->setText(QStringLiteral("  X-9 "));
            d->findChild<QLineEdit*>("personalThreadName")->setText(QStringLiteral("Bleu nuit"));
            auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
            if (ok->isEnabled()) {
                ok->click();
            }
        });
        panel.findChild<QPushButton*>("threadAddPersonalBtn")->click();
        QVERIFY(got.has_value());
        QCOMPARE(QString::fromStdString(got->key.chart_id), QStringLiteral("personal"));
        QCOMPARE(QString::fromStdString(got->key.code), QStringLiteral("X-9"));
        QCOMPARE(QString::fromStdString(got->name), QStringLiteral("Bleu nuit"));
    }

    void ownedThreadIsPersistedAndNeverTouchesTheDocument() {
        MainWindow window;
        window.applyLoadedProject(twoTrianglesProject());
        const auto undoBefore = window.undoStack_.undoNames().size();
        emit window.threadPanel_->stockOwnedToggled({"generic", "G02"}, true);
        QVERIFY(window.threadPanel_->stock().owns({"generic", "G02"}));
        QVERIFY(loadThreadStock(threadStockFilePath()).owns({"generic", "G02"}));
        // Le stock est une preference : aucun pas d'annulation, aucun objet modifie.
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QVERIFY(!window.project_.embroidery_objects[0].thread.has_value());

        // Un autre lancement relit le stock.
        MainWindow second;
        QVERIFY(second.threadPanel_->stock().owns({"generic", "G02"}));
    }

    void limitedSnapOnlyUsesOwnedThreadsAndIsUndoable() {
        MainWindow window;
        window.applyLoadedProject(twoTrianglesProject());
        emit window.threadPanel_->stockOwnedToggled({"generic", "G02"}, true);
        window.threadPanel_->setLimitToStock(true);
        emit window.threadPanel_->snapFreeColorsRequested(QString());
        for (const auto& obj : window.project_.embroidery_objects) {
            QVERIFY(obj.thread.has_value());
            QCOMPARE(QString::fromStdString(obj.thread->chart_id), QStringLiteral("generic"));
            QCOMPARE(QString::fromStdString(obj.thread->code), QStringLiteral("G02"));
        }
        window.undo();
        for (const auto& obj : window.project_.embroidery_objects) {
            QVERIFY(!obj.thread.has_value());
        }
    }

    void limitedSnapCanPickAPersonalThread() {
        MainWindow window;
        window.applyLoadedProject(twoTrianglesProject());
        emit window.threadPanel_->personalThreadAddRequested(personalRed());
        QVERIFY(window.threadPanel_->stock().owns({thread_palette::kPersonalChartId, "M-001"}));
        window.threadPanel_->setLimitToStock(true);
        emit window.threadPanel_->snapFreeColorsRequested(QString());
        const auto& first = window.project_.embroidery_objects[0];
        QVERIFY(first.thread.has_value());
        QCOMPARE(QString::fromStdString(first.thread->chart_id), QStringLiteral("personal"));
        QCOMPARE(QString::fromStdString(first.thread->code), QStringLiteral("M-001"));
    }

    void limitedSnapWithAnEmptyStockChangesNothing() {
        MainWindow window;
        window.applyLoadedProject(twoTrianglesProject());
        window.threadPanel_->setLimitToStock(true);
        const auto undoBefore = window.undoStack_.undoNames().size();
        emit window.threadPanel_->snapFreeColorsRequested(QString());
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QVERIFY(!window.project_.embroidery_objects[0].thread.has_value());
    }

    void unlimitedSnapStillUsesTheWholeCatalogue() {
        MainWindow window;
        window.applyLoadedProject(twoTrianglesProject());
        emit window.threadPanel_->stockOwnedToggled({"generic", "G02"}, true);
        emit window.threadPanel_->snapFreeColorsRequested(QString()); // limite coche = faux
        const auto library = thread_palette::ThreadLibrary::with_builtin();
        for (const auto& obj : window.project_.embroidery_objects) {
            QVERIFY(obj.thread.has_value());
            const auto expected = library.nearest(obj.rgb, std::string{}, 1);
            QVERIFY(!expected.empty());
            QVERIFY(*obj.thread == expected.front().key);
        }
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_thread_stock_ui.moc"
