// SPDX-License-Identifier: Apache-2.0
// Ordre de couture dans le bureau (HP-SEQ-002 glisser-deposer, HP-SEQ-008 depart / arrivee,
// HP-SEQ-001 affinage) : une commande, un pas d'annulation par geste.
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"
#include "order_list_widget.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project threeObjects(std::vector<ObjectId>& ids) {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        for (int i = 0; i < 3; ++i) {
            document::VectorObject vec;
            vec.id = project.object_ids.next();
            vec.name = "Rect";
            geometry::Path path;
            path.closed = true;
            const auto node = [i](int x, int y) {
                return geometry::PathNode{
                    Vec2um{Micrometers{(x + i * 60) * 1000}, Micrometers{y * 1000}},
                    geometry::NodeType::Corner, std::nullopt, std::nullopt};
            };
            path.nodes = {node(0, 0), node(40, 0), node(40, 20), node(0, 20)};
            vec.paths.push_back(geometry::PathSet{path, {}});
            document::EmbroideryObject emb;
            emb.id = project.object_ids.next();
            emb.name = "Obj";
            emb.source_vector = vec.id;
            emb.params = document::RunningStitchParams{};
            ids.push_back(emb.id);
            project.vector_objects.push_back(std::move(vec));
            project.embroidery_objects.push_back(std::move(emb));
        }
        return project;
    }

    static std::vector<ObjectId> docOrder(const MainWindow& window) {
        std::vector<ObjectId> out;
        for (const auto& obj : window.project_.embroidery_objects) {
            out.push_back(obj.id);
        }
        return out;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("SewingOrderTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void dragAndDropMovesOneUndoStep() {
        MainWindow window;
        std::vector<ObjectId> ids;
        window.applyLoadedProject(threeObjects(ids));
        auto* list = window.findChild<OrderListWidget*>(QStringLiteral("list_sewingOrder"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);

        emit list->moveRequested(0, 2); // le premier objet est depose en dernier
        QCOMPARE(docOrder(window), (std::vector<ObjectId>{ids[1], ids[2], ids[0]}));
        window.undo(); // un seul pas
        QCOMPARE(docOrder(window), ids);
        emit list->moveRequested(1, 1); // meme place : aucun effet
        QCOMPARE(docOrder(window), ids);
    }

    void startEndEditedByOneCommandEach() {
        MainWindow window;
        std::vector<ObjectId> ids;
        window.applyLoadedProject(threeObjects(ids));
        auto* options = window.findChild<OrderOptionsWidget*>(QStringLiteral("orderOptions"));
        QVERIFY(options != nullptr);
        auto* check = options->findChild<QCheckBox*>(QStringLiteral("check_designStart"));
        auto* sx = options->findChild<QDoubleSpinBox*>(QStringLiteral("spin_designStartX"));
        QVERIFY(check != nullptr && sx != nullptr);
        QVERIFY(!window.project_.finishing.design_start.has_value());

        check->setChecked(true);
        QVERIFY(window.project_.finishing.design_start.has_value());
        sx->setValue(12.5);
        QCOMPARE(window.project_.finishing.design_start->x.value, 12'500);
        window.undo();
        QCOMPARE(window.project_.finishing.design_start->x.value, 0);
        window.undo();
        QVERIFY(!window.project_.finishing.design_start.has_value());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_sewing_order_ui.moc"
