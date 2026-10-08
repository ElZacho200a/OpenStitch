// SPDX-License-Identifier: Apache-2.0
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include "autosave.hpp"
#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "help_dialogs.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "properties_panel.hpp"
#include "recent_files.hpp"
#include "satin_guide_item.hpp"
#include "workflow_panel.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::RegionId;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::DocumentPanel;
using openstitch::desktop::MainWindow;
using openstitch::desktop::NodeHandleItem;
using openstitch::desktop::PropertiesPanel;
using openstitch::desktop::SatinGuideItem;
using openstitch::stitch_generation::ObjectEditState;

namespace {

struct Fixture {
    openstitch::document::Project project;
    ObjectId vectorId{};
    ObjectId embroideryId{};
    ObjectId embroideryId2{};
    RegionId regionId{};
};

// Un objet vectoriel triangulaire (1 mm de côté) lié à un objet de broderie
// contour, plus une région de segmentation factice — assez pour exercer
// sélection canevas/liste/inspecteur et undo/redo sans image réelle ni
// dialogue modal. Une image 2x2 minimale est incluse : sans elle,
// refreshImage() s'arrête tôt et ne rafraîchit ni le panneau Document ni la
// séquence de points (cf. MainWindow::refreshImage).
Fixture buildFixture() {
    Fixture fx;

    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Triangle";
    openstitch::geometry::Path tri;
    tri.closed = true;
    tri.nodes.push_back(openstitch::geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                                       openstitch::geometry::NodeType::Corner,
                                                       std::nullopt, std::nullopt});
    tri.nodes.push_back(openstitch::geometry::PathNode{Vec2um{Micrometers{1000}, Micrometers{0}},
                                                       openstitch::geometry::NodeType::Corner,
                                                       std::nullopt, std::nullopt});
    tri.nodes.push_back(openstitch::geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{1000}},
                                                       openstitch::geometry::NodeType::Corner,
                                                       std::nullopt, std::nullopt});
    vec.paths.push_back(openstitch::geometry::PathSet{tri, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);

    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Triangle - contour";
    emb.source_vector = vec.id;
    emb.params = openstitch::document::RunningStitchParams{};
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(emb);

    openstitch::segmentation::Segmentation seg;
    seg.width = 1;
    seg.height = 1;
    seg.labels = {1};
    seg.region_slots.push_back(openstitch::segmentation::Region{RegionId{1}, {200, 30, 30}, 1});
    fx.regionId = RegionId{1};
    fx.project.segmentation = std::move(seg);

    return fx;
}

// Ajoute un triangle (1 mm de côté, coin bas-gauche à x = xMm) au projet et
// retourne son id ; sert aux tests de multi-sélection (L5-T4a).
ObjectId addTriangle(openstitch::document::Project& project, const char* name, int xMm) {
    openstitch::document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = name;
    openstitch::geometry::Path tri;
    tri.closed = true;
    const std::int32_t x0 = xMm * 1000;
    for (const auto& [dx, dy] : {std::pair{0, 0}, std::pair{1000, 0}, std::pair{0, 1000}}) {
        tri.nodes.push_back(openstitch::geometry::PathNode{
            Vec2um{Micrometers{x0 + dx}, Micrometers{dy}}, openstitch::geometry::NodeType::Corner,
            std::nullopt, std::nullopt});
    }
    vec.paths.push_back(openstitch::geometry::PathSet{tri, {}});
    project.vector_objects.push_back(vec);
    return vec.id;
}

// Projet de test : image 2x2 + trois triangles A, B, C (sans broderie ni région).
struct TrianglesFixture {
    openstitch::document::Project project;
    ObjectId a{};
    ObjectId b{};
    ObjectId c{};
};

TrianglesFixture buildTriangles() {
    TrianglesFixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);
    fx.a = addTriangle(fx.project, "A", 0);
    fx.b = addTriangle(fx.project, "B", 3);
    fx.c = addTriangle(fx.project, "C", 6);
    return fx;
}

// Carré 10 x 10 mm centré en (cxMm, 0), assez grand pour cliquer au centre sans frôler un nœud.
ObjectId addSquare(openstitch::document::Project& project, const char* name, int cxMm) {
    openstitch::document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = name;
    openstitch::geometry::Path path;
    path.closed = true;
    for (const auto& [dx, dy] :
         {std::pair{-5, -5}, std::pair{5, -5}, std::pair{5, 5}, std::pair{-5, 5}}) {
        path.nodes.push_back(openstitch::geometry::PathNode{
            Vec2um{Micrometers{(cxMm + dx) * 1000}, Micrometers{dy * 1000}},
            openstitch::geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vec.paths.push_back(openstitch::geometry::PathSet{path, {}});
    project.vector_objects.push_back(vec);
    return vec.id;
}

// Le menu contextuel du canevas s'ouvre par un exec() bloquant : programme, AVANT l'appel,
// une inspection qui s'exécute dès qu'un QMenu visible apparaît (sonde à 10 ms), lui laisse
// lire/déclencher ses actions, puis le ferme.
void scheduleContextMenuInspection(std::function<void(QMenu&)> inspect) {
    auto* timer = new QTimer(qApp);
    timer->setInterval(10);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, inspect = std::move(inspect)] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* menu = qobject_cast<QMenu*>(w);
            if (menu != nullptr && menu->isVisible() && !menu->actions().isEmpty()) {
                timer->stop();
                timer->deleteLater();
                inspect(*menu);
                menu->close();
                return;
            }
        }
    });
    timer->start();
}

QStringList actionTexts(const QMenu& menu) {
    QStringList out;
    for (const QAction* a : menu.actions()) {
        if (!a->isSeparator()) {
            out << a->text().remove(QLatin1Char('&'));
        }
    }
    return out;
}

// tabs_/objectsList_/regionsList_ sont privés (comme dans test_document_panel.cpp) :
// on retrouve les listes par leur ordre d'ajout, seul contrat stable observé
// depuis l'extérieur. objectsList_ est un QTreeWidget depuis §21 (regroupement
// des sections d'un même plan satin, 2026-08-15) -- regionsList_ reste un
// QListWidget (aucun regroupement là).
QTreeWidget* objectsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QTreeWidget*>(tabs->widget(0));
}
QListWidget* regionsList(DocumentPanel& panel) {
    auto* tabs = panel.findChild<QTabWidget*>();
    return qobject_cast<QListWidget*>(tabs->widget(1));
}

// Carré de 10 mm (mêmes dimensions que make_running_square_project dans
// tests/unit/stitch/test_overrides.cpp) : contrairement au triangle de
// buildFixture() (1 mm de côté, sous la longueur de point par défaut), son
// périmètre produit plusieurs points TopStitch déplaçables — nécessaire pour
// exercer le mode d'édition des points (Lot 8.2), qui a besoin d'au moins une
// poignée réelle à glisser.
Fixture buildRunningSquareFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Square";
    openstitch::geometry::Path square;
    square.closed = true;
    constexpr std::int32_t s = 10'000; // 10 mm
    square.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{s}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{s}, Micrometers{s}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{0}, Micrometers{s}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
    };
    vec.paths.push_back(openstitch::geometry::PathSet{square, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);

    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Square - contour";
    emb.source_vector = vec.id;
    emb.params = openstitch::document::RunningStitchParams{};
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(emb);

    return fx;
}

// Réseau en T (barre horizontale 30x4 mm + pied vertical 4x26 mm) : même
// forme branchée que "reseau en T" dans tests/unit/autodigitize/
// test_autodigitize.cpp, exprimée directement en µm plutôt qu'en pixels
// rasterisés -- assez pour exercer la décomposition récursive du planner
// satin (au moins une jonction réelle, deux branches distinctes).
Fixture buildTShapeFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "T";
    openstitch::geometry::Path t;
    t.closed = true;
    const std::vector<std::pair<std::int32_t, std::int32_t>> pts = {
        {13'000, 0},      {17'000, 0}, {17'000, 26'000}, {30'000, 26'000},
        {30'000, 30'000}, {0, 30'000}, {0, 26'000},      {13'000, 26'000},
    };
    for (const auto& [x, y] : pts) {
        t.nodes.push_back(openstitch::geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                                         openstitch::geometry::NodeType::Corner,
                                                         std::nullopt, std::nullopt});
    }
    vec.paths.push_back(openstitch::geometry::PathSet{t, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);
    return fx;
}

// Bande 40x5 mm entaillée d'une encoche en V quasi traversante (même
// géométrie que la fixture "pinch" de libs/auto_satin/src/shapes.cpp) :
// AUCUNE jonction de squelette (chemin topologique unique), un cas que le
// solveur local échoue clairement à couvrir entièrement même après la
// famille de coupes concavité (§14 suite, 2026-08-14 -- ~86 % de couverture
// mesurée dans tests/unit/satin_planning, un reliquat largement au-dessus du
// seuil de significativité) -- fixture dédiée pour exercer le dialogue à
// choix multiples §23 (askAboutIncompleteSatinCoverage), qui ne s'affiche
// QUE quand le reliquat est réellement significatif.
Fixture buildPinchShapeFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Pinch";
    openstitch::geometry::Path p;
    p.closed = true;
    constexpr std::int32_t w = 2'500;
    const std::vector<std::pair<std::int32_t, std::int32_t>> pts = {
        {0, -w}, {40'000, -w}, {40'000, w}, {25'000, w}, {20'000, -2'200}, {15'000, w}, {0, w},
    };
    for (const auto& [x, y] : pts) {
        p.nodes.push_back(openstitch::geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                                         openstitch::geometry::NodeType::Corner,
                                                         std::nullopt, std::nullopt});
    }
    vec.paths.push_back(openstitch::geometry::PathSet{p, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);
    return fx;
}

// Rectangle allongé 40x5 mm (même forme que le test createSatinObject sur
// rectangle "Suitable" ci-dessous -- une élongation d'au moins 2,5 est
// nécessaire pour une direction de rail non ambiguë, cf. docs/source/
// satin.md § Analyse de satinabilité ; un simple carré, comme
// buildRunningSquareFixture(), a une élongation de 1,0 et produirait un
// statut Ambiguous plutôt qu'une vraie colonne satin).
Fixture buildRunningRectangleFixture() {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::VectorObject vec;
    vec.id = fx.project.object_ids.next();
    vec.name = "Rectangle";
    openstitch::geometry::Path rect;
    rect.closed = true;
    constexpr std::int32_t w = 40'000; // 40 mm
    constexpr std::int32_t h = 5'000;  // 5 mm
    rect.nodes = {
        {Vec2um{Micrometers{0}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{w}, Micrometers{0}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{w}, Micrometers{h}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
        {Vec2um{Micrometers{0}, Micrometers{h}}, openstitch::geometry::NodeType::Corner,
         std::nullopt, std::nullopt},
    };
    vec.paths.push_back(openstitch::geometry::PathSet{rect, {}});
    fx.vectorId = vec.id;
    fx.project.vector_objects.push_back(vec);

    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Rectangle - contour";
    emb.source_vector = vec.id;
    emb.params = openstitch::document::RunningStitchParams{};
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(emb);

    return fx;
}

Fixture buildSatinGuideFixture(bool withStartJunction = false) {
    Fixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);

    openstitch::document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{{Micrometers{0}, Micrometers{0}}},
                          {{Micrometers{10'000}, Micrometers{0}}}};
    satin.rail_b.nodes = {{{Micrometers{0}, Micrometers{4'000}}},
                          {{Micrometers{10'000}, Micrometers{4'000}}}};
    satin.rungs = {
        {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
        {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
        {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    if (withStartJunction) {
        satin.topology =
            openstitch::document::SatinSectionTopology{0, 3, std::uint32_t{7}, std::nullopt};
    }
    openstitch::document::EmbroideryObject emb;
    emb.id = fx.project.object_ids.next();
    emb.name = "Satin guides";
    emb.params = satin;
    fx.embroideryId = emb.id;
    fx.project.embroidery_objects.push_back(std::move(emb));
    return fx;
}

Fixture buildSatinJunctionFixture() {
    Fixture fx = buildSatinGuideFixture(true);
    constexpr ObjectId source{77};
    auto& first = fx.project.embroidery_objects.front();
    first.source_vector = source;
    auto& firstSatin = std::get<openstitch::document::SatinParams>(first.params);
    firstSatin.rungs[1].a.x = Micrometers{2'000};
    firstSatin.rungs[1].b.x = Micrometers{2'000};
    firstSatin.topology =
        openstitch::document::SatinSectionTopology{0, 2, std::uint32_t{7}, std::nullopt};

    auto second = first;
    second.id = fx.project.object_ids.next();
    second.name = "Satin guides - branche 2";
    auto& secondSatin = std::get<openstitch::document::SatinParams>(second.params);
    secondSatin.topology =
        openstitch::document::SatinSectionTopology{1, 2, std::uint32_t{7}, std::nullopt};
    fx.embroideryId2 = second.id;
    fx.project.embroidery_objects.push_back(std::move(second));
    return fx;
}

// Accepte automatiquement jusqu'à `maxDialogs` boîtes modales successives
// (QDialog ou QMessageBox) au fur et à mesure qu'elles apparaissent, en se
// réarmant après chacune -- nécessaire depuis que certains chemins peuvent
// en ouvrir plusieurs à la suite (ex. createSatinObject() suivi de
// warnAboutIncompleteSatinCoverage() si la couverture est incomplète, § plan
// de refonte satin 2026-08-14), contrairement au singleShot unique utilisé
// jusqu'ici pour une seule boîte garantie.
void autoDismissModalDialogs(QWidget* parent, int maxDialogs = 4) {
    if (maxDialogs <= 0) {
        return;
    }
    QTimer::singleShot(0, parent, [parent, maxDialogs] {
        if (auto* widget = QApplication::activeModalWidget()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                // Clique RÉELLEMENT le bouton par défaut (comme un Entrée
                // utilisateur) plutôt que d'appeler accept() directement :
                // askAboutIncompleteSatinCoverage() (§23) n'utilise QUE des
                // boutons personnalisés (addButton(text, role), aucun
                // QMessageBox::StandardButton) -- accept() seul ne route
                // jamais par la machinerie de clic de Qt et laisse
                // clickedButton() à nullptr, ce qui se lirait à tort comme
                // "Annuler" côté MainWindow. Cliquer le bouton par défaut
                // (`partialButton`, "Continuer avec satin partiel") reste le
                // choix le plus sûr pour un test qui ne teste pas
                // spécifiquement ce dialogue -- jamais une fabrication
                // silencieuse de tatami, jamais une annulation surprise.
                if (auto* def = box->defaultButton()) {
                    def->click();
                } else {
                    box->accept();
                }
            } else if (auto* dlg = qobject_cast<QDialog*>(widget)) {
                dlg->accept();
            }
        }
        autoDismissModalDialogs(parent, maxDialogs - 1);
    });
}

// Ferme chaque boîte modale successive en cliquant, SI PRÉSENT, le bouton
// dont le texte CONTIENT `buttonText` (recherche insensible à la casse) --
// sinon clique le bouton par défaut pour passer à la suivante, et se réarme.
// Nécessaire pour choisir explicitement "Utiliser tatami" ou "Annuler" dans
// le dialogue §23 (`askAboutIncompleteSatinCoverage`), qui n'apparaît
// qu'APRÈS une première QDialog densité/compensation sans rapport (aucun
// bouton "tatami"/"Annuler" personnalisé à y trouver) -- une seule séquence
// gère les deux dialogues plutôt que deux helpers à orchestrer à la main.
void clickModalDialogButton(QWidget* parent, const QString& buttonText, int maxDialogs = 4) {
    if (maxDialogs <= 0) {
        return;
    }
    QTimer::singleShot(0, parent, [parent, buttonText, maxDialogs] {
        if (auto* widget = QApplication::activeModalWidget()) {
            QAbstractButton* target = nullptr;
            for (QAbstractButton* button : widget->findChildren<QAbstractButton*>()) {
                if (button->text().contains(buttonText, Qt::CaseInsensitive)) {
                    target = button;
                    break;
                }
            }
            if (target != nullptr) {
                target->click();
            } else if (auto* box = qobject_cast<QMessageBox*>(widget)) {
                if (auto* def = box->defaultButton()) {
                    def->click();
                } else {
                    box->accept();
                }
            } else if (auto* dlg = qobject_cast<QDialog*>(widget)) {
                dlg->accept();
            }
        }
        clickModalDialogButton(parent, buttonText, maxDialogs - 1);
    });
}

// Dossier autosave (AppDataLocation/autosave) calculé via slotFor() -- la
// convention de chemin n'est pas exposée ailleurs, et la dupliquer ici
// romprait si autosave.cpp en changeait un jour. Partagé par TOUT le binaire
// de test (QStandardPaths::setTestModeOn, posé par initTestCase, le rend
// stable mais PERSISTANT entre deux exécutions du binaire -- pas remis à
// zéro automatiquement).
QString autosaveTestDir() {
    return QFileInfo(openstitch::desktop::slotFor(QString()).osp_path).absolutePath();
}

// Vide le dossier autosave partagé. Indispensable avant/après chaque test
// HP-FILE-004 : un créneau orphelin oublié y ferait apparaître le dialogue
// de checkAutosaveRecovery() -- différé par QTimer::singleShot(0,...) depuis
// LE CONSTRUCTEUR DE TOUT MainWindow -- dès le premier traitement
// d'évènements d'un test qui n'a jamais entendu parler d'autosave, bloquant
// sur un QMessageBox::exec() que personne n'arme.
void clearAutosaveDir() {
    QDir(autosaveTestDir()).removeRecursively();
}

// Seule poignée trouvée parmi les items de la couche de base : valable quand
// aucun objet vectoriel n'est sélectionné en parallèle (cf. tests ci-dessous,
// qui sélectionnent l'objet de broderie via selectedEmbroidery_ seul, jamais
// selectedObject_) -- sinon les poignées de nœuds vectoriels (même classe
// NodeHandleItem) s'y mêleraient. Prend la liste par valeur (pas MainWindow&) :
// baseItems_ est privé, seul MainWindowTest (friend) peut le lire ; cette
// fonction libre n'a pas besoin d'un accès privilégié une fois la liste en main.
NodeHandleItem* firstHandle(const QList<QGraphicsItem*>& items) {
    for (QGraphicsItem* item : items) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            return handle;
        }
    }
    return nullptr;
}

// Premier index de `view.raw` réellement déplaçable (entrée TopStitch/Stitch,
// cf. is_movable_point) : utilisé quand une commande d'édition de point est
// construite directement (sans passer par un vrai glisser QTest), pour ne
// jamais cibler par erreur un Jump/point de sous-couche.
std::size_t firstMovableIndex(const openstitch::stitch_generation::ObjectEditView& view) {
    for (std::size_t i = 0; i < view.raw.size(); ++i) {
        if (openstitch::stitch_generation::is_movable_point(view.raw[i])) {
            return i;
        }
    }
    return 0;
}

// Évènement souris explicite sur le viewport (modificateurs maîtrisés : QTest::mouseMove n'en
// accepte pas, et l'état clavier global est ignoré).
void sendMouseEvent(QWidget* viewport, QEvent::Type type, const QPoint& at, Qt::MouseButton button,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
    QMouseEvent event(type, QPointF(at), QPointF(viewport->mapToGlobal(at)), button, buttons, mods);
    QApplication::sendEvent(viewport, &event);
}

// Remet le réglage d'accrochage des nœuds à sa valeur par défaut (désactivé) en fin de test.
struct SnapSettingReset {
    ~SnapSettingReset() { QSettings().setValue(QStringLiteral("edit/snapNodesOnDrag"), false); }
};

// Rend le préréglage de navigation à OpenStitch (état global + QSettings de test) en fin de test.
struct PresetRestorer {
    ~PresetRestorer() {
        openstitch::desktop::InteractionMap::setPreset(openstitch::desktop::Preset::OpenStitch);
        openstitch::desktop::InteractionMap::savePreset();
    }
};

} // namespace

namespace openstitch::desktop {

// Couvre des comportements de MainWindow laissés non testés par la fondation
// QTest (commit 0e396ab) : mise à jour des actions/menus selon le contexte,
// synchronisation canevas -> panneau Document -> inspecteur, rafraîchissement
// de l'UI après undo/redo, et non-fuite de sélection entre deux chargements
// de projet. MainWindow est instanciable en test grâce à deux seams
// minimaux : QSettings() par défaut (redirigée ici vers un fichier temporaire,
// jamais le registre réel) et applyLoadedProject() (applique un projet sans
// QFileDialog) — privée en production, accessible ici via `friend class
// MainWindowTest` (déclaré dans main_window.hpp) plutôt que par une méthode
// publique ajoutée uniquement pour les tests.
class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void clickingVectorObjectSyncsDocumentPanelAndInspector();
    void regionAndVectorSelectionToggleContextActionsOppositely();
    // ---- L5-T4a : modèle de sélection (multi-sélection) et Suppr universel ----
    void selectionAddAndToggleKeepInvariants();
    void togglingPrimaryPromotesPrevious();
    void addOfAlreadySelectedObjectIsNoOp();
    void clickOnEmptyReplaceDeselectsButModifiersKeepSelection();
    void rectangleSelectionHonoursModes();
    void regionSelectionClearsMultiSelection();
    void embroiderySelectionClearsMultiSelection();
    void danglingIdsArePrunedAfterDeleteUndoRedo();
    void multiSelectionDisablesSingleObjectActions();
    void inspectorShowsNObjets();
    void multiSelectionDrawsEveryObjectAsSelected();
    void nudgeMovesWholeMultiSelectionInOneUndoStep();
    void deleteRemovesWholeMultiSelectionInOneUndoStep();
    void draggingOneOfThreeSelectedBodiesMovesAllInOneUndoStep();
    void rightClickOnSelectedMemberKeepsMultiSelectionAndDeletesAll();
    void rightClickOnUnselectedObjectReplacesTheSelection();
    void deleteSelectionPrefersEmbroideryOverRegion();
    void deleteKeyInDrawToolsDeletesObjectAndBackspaceStillRemovesLastPoint();
    void deleteKeyInFocusedInspectorFieldDoesNotDeleteTheObject();
    void deleteOnSingleVectorObjectIsOneUndoStep();
    void deleteOnEmbroideryKeepsVisibleSourceAndRemovesHiddenProxy();
    void deleteOnRegionKeepsLegacyBehaviour();
    void deleteActionIsEnabledForAnySelectionKind();
    void undoRedoRestoresDeletedRegionAndRefreshesDocumentPanel();
    // ---- L5-T4b : câblage du modèle d'interaction, aide, préréglages ----
    void helpMenuHasThreeEntriesAndF1OpensGestures();
    void oldShortcutsMessageBoxIsGone();
    void quickStartIsNonModalSingleInstanceBoundToMemberActions();
    void hintsLabelSurvivesShowMessage();
    void hintsFollowToolSelectionAndHeldModifiers();
    void plainClickReplacesAndEmptyClickDeselects();
    void shiftClickAddsAndCtrlClickToggles();
    void rectangleLeftToRightIsWindowRightToLeftIsCrossing();
    void shiftRectangleAddsToTheSelection();
    void longPressOpensSelectBelowMenuAndChoosingSelectsIt();
    void altClickOpensSelectBelowMenu();
    void navigationPresetMenuSwitchesTableAndPersists();
    void gesturesDialogPresetChangeUpdatesMenuAndHints();
    void panToolStillPansAfterNoDrag();
    void legacyDrawAndCropToolsStillWorkWithTheInteractionModel();
    void escapeAndDeleteKeepWorkingThroughTheCanvas();
    void shiftDragMovesAlongDominantAxis();
    void shiftDragOfNodeKeepsOneAxis();
    void ctrlDragSkipsSnap();
    void ctrlSuspendsSnapWhenPlacingPolygonVertex();
    void altDragDuplicatesAndMovesCopyInOneUndoStep();
    void altBoxDrawGrowsFromCenter();
    void hoverHighlightShowsOnlyUnselectedObjectUnderCursorInSelectTool();
    void hoverHighlightHidesOnLeaveAndDuringGestures();
    void hoverBurstIsCoalescedAndPathsAreCached();
    void leftDragOnEmptySpaceStillPansInNodeEditAndStitchEditContexts();
    void altClickOnSelectedBodyOpensSelectBelowWithoutDuplicating();
    void nodeSnapIsOffByDefaultAndLandsExactlyWhereReleased();
    void nodeSnapSettingPersistsAndOnlySnapsToOtherObjectVertices();
    void hintsFollowTheSameContextPriorityAsTheCanvas();
    void selectBelowMenuDisambiguatesDuplicateAndEmptyNames();
    void embroiderySelectionDoesNotLeakAcrossProjectLoadWithReusedId();
    // Cache de l'image de travail (audit perf 2026-09) : toujours égale au
    // pipeline rejoué, quelle que soit la mutation.
    void processedImageCacheFollowsOpsUndoRedoAndProjectChange();

    // Remplissage directionnel : conversion, outil de guides, undo/redo.
    void convertingTatamiToDirectionalIsUndoable();
    void directionGuideToolDrawsGuidesAndBreakLinesThroughUndoStack();

    // Lot 8.2 (mode d'édition des points) — revue corrective.
    void stitchEditModeGatingTracksSelectionAndDirtyState();
    void draggingStitchHandleAtDefaultZoomMovesPointOnce();
    void draggingStitchHandleAfterZoomingInMovesPointOnce();
    void clickingStitchHandleWithoutMovingCreatesNoCommand();
    void loadingNewProjectExitsStitchEditModeAndBumpsGeneration();
    void draggingHandleThenLoadingNewProjectDoesNotMutateIt();
    void discardingOverridesOnDirtyObjectIsUndoable();
    void stitchEditModeRefusesWhenTooManyMovablePoints();
    void satinGuideModeMovesEndpointOnRailAndUndoRestoresIt();
    void satinGuideSelectionAddsAndRemovesWithUndoRedo();
    void satinJunctionGuideIsLockedInUi();
    void satinJunctionAddsInternalGuidesToEveryBranchAtomically();

    void satinLinkedGuideGroupMoveShiftDragUpdatesBothSectionsAtomically();
    void satinLinkedGuideShiftClickCreatesNoUndoCommand();
    void satinLinkedGuideGroupRemoveDeletesBothSectionsAtomically();
    void satinLinkedGuideEndpointDragWithoutShiftStaysLocal();

    // Création manuelle de formes (mission « auto-satin béton », suite : le
    // pipeline ne savait créer un VectorObject que depuis une image importée).
    void drawRectangleToolCreatesUndoableVectorObject();
    // Contrairement au test ci-dessus (qui injecte boxDrawnMm directement,
    // court-circuitant tout le mécanisme de glisser réel), celui-ci simule
    // un VRAI glisser souris sur la vue réelle de MainWindow, outil choisi
    // via le bouton de la palette -- le chemin complet emprunté en usage
    // réel, jamais exercé par les tests existants (retour utilisateur :
    // « rectangle/ellipse ne fonctionne toujours pas au clic-glisser »).
    void drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject();
    void drawEllipseToolCreatesUndoableVectorObject();
    void drawEllipseWithShiftConstrainsToCircle();
    void drawingTooSmallABoxCreatesNoObject();
    // Polygone régulier (audit "outils de sketch façon Fusion 360", suite de
    // l'accroche et du DXF) : nombre de côtés réglable dans la palette,
    // inscrit dans le cadre glissé (même mécanique que rectangle/ellipse).
    void drawRegularPolygonToolCreatesUndoableVectorObjectWithConfiguredSides();
    // Décalage (outil d'édition façon Fusion 360, suite du polygone régulier) :
    // crée une nouvelle forme rétrécie/agrandie sans toucher l'originale ;
    // un décalage qui consomme toute la forme ne crée rien.
    void offsetVectorObjectCoreShrinksIntoNewObjectWithoutTouchingOriginal();
    void offsetVectorObjectCoreTooLargeCreatesNothing();
    // Accroche façon Fusion 360 (audit "outils de sketch") : un clic/survol
    // proche d'un sommet existant se pose exactement dessus, avec repère
    // visuel ; loin de tout candidat, la position brute du curseur est
    // conservée.
    void drawPolygonSnapsToExistingVertexAndShowsIndicator();
    void drawRectangleSnapsCornersToExistingVectorVertices();
    void drawPolygonAccumulatesVerticesAndClosesOnDoubleClick();
    void drawPolygonWithFewerThanThreeVerticesCancelsOnDoubleClick();
    void switchingToolDuringPolygonDrawCancelsIt();
    void polygonDoubleClickDoesNotAddADuplicateVertex();
    void drawFreeformCreatesUndoableVectorObject();
    void drawFreeformWithTooFewPointsCancelsOnRelease();
    void switchingToolDuringFreeformDrawCancelsIt();

    // « Créer une colonne satin… » (createSatinObject) : brancher sur le
    // moteur squelette (auto_satin::build_satin_columns, mode Parametric)
    // plutôt que sur l'heuristique naïve rails_from_contour (audit satin
    // demandé par l'utilisateur, § docs/source/satin.md).
    void createSatinObjectOnSuitableRectangleProducesOneSatinWithStitches();
    // Chemin direct HP-STI-018 : sur une forme BRANCHÉE, createSatinObject()
    // doit produire les branches satin sans subdivision SGSD automatique —
    // bout en bout depuis le VRAI chemin UI, pas seulement les tests de
    // bibliothèque de libs/satin_planning.
    void createSatinObjectOnBranchedShapeProducesMultipleSatinSectionsDirectly();
    // setStitchType() (conversion de type, cas satin) passe désormais par le
    // même point d'entrée unifié (autodigitize::build_satin_sections) que
    // les créations manuelles — vérifie que le résultat porte de vrais
    // rails/barreaux (pas un objet SatinParams vide) et reste annulable.
    void setStitchTypeSatinCaseProducesRealRailsAndIsUndoable();
    // Défaut réel signalé par l'utilisateur (2026-09-04, « résidu de satin
    // qui reste même en revenant en tatami ») : un réseau satin
    // auto-généré en plusieurs sections (buildTShapeFixture + createSatin
    // Object, comme le test ci-dessus) partage un seul source_vector entre
    // plusieurs EmbroideryObject. setStitchType() ne doit JAMAIS laisser
    // les autres sections en satin réel une fois qu'une seule est
    // convertie -- bout en bout depuis le VRAI chemin UI.
    void setStitchTypeOnMultiSectionSatinNetworkRemovesSiblingsInsteadOfLeavingResidue();
    // Import SVG direct (2026-09-11, demande utilisateur : "éviter la
    // segmentation" quand le tracé existe déjà) -- vérifie le VRAI chemin
    // UI (openSvg(), appelé directement comme le ferait openImage() une
    // fois le fichier choisi) plutôt que seulement formats::decode_svg en
    // isolation (déjà testé dans tests/unit/formats/test_svg_import.cpp).
    void openSvgCreatesVectorObjectsDirectlySkippingImage();
    // Demande utilisateur (2026-09-12) : pouvoir lancer "Numérisation
    // automatique" directement après un import SVG, comme si la
    // segmentation avait déjà eu lieu -- autoDigitize() doit détecter
    // l'absence de project_.segmentation et basculer sur
    // autodigitize::auto_digitize_vectors(project_.vector_objects, ...)
    // plutôt que d'exiger une segmentation qui n'aura jamais lieu pour ce
    // chemin. Chemin vectoriel : contrairement au chemin segmentation,
    // aucune QDialog (fond présumé) ne s'affiche -- testable sans
    // autoDismissModalDialogs.
    void autoDigitizeAfterOpenSvgClassifiesVectorObjectsDirectly();
    // Lot A (audit marine plein cadre, 2026-09-22) : « Ignorer la plus grande
    // région » n'est plus cochée sur le seul critère « pas d'alpha » -- le
    // ciel d'une image plein cadre n'était pas brodé. Cochée seulement pour
    // un fond quasi blanc qui encadre le motif ; pastille + pourcentage
    // affichés dans les deux cas.
    void autoDigitizeDialogDoesNotSkipColoredFullFrameRegion();
    void autoDigitizeDialogSkipsNearWhiteFramingBackground();
    // Strategie « Contours » : choix de strategie, curseur de detail (defaut
    // 50) et techniques point droit presentes dans le dialogue.
    void autoDigitizeDialogOffersContoursStrategy();
    // Vectorisation manuelle d'une region : expose le meme controle de detail
    // sans forcer l'utilisateur a passer par l'auto-numerisation complete.
    void vectorizeSelectedRegionOffersDetailSlider();
    // §23 du plan de refonte satin (2026-08-14) : le dialogue à choix
    // multiples (askAboutIncompleteSatinCoverage) remplace l'ancienne
    // information à sens unique -- un test par choix réel, bout en bout
    // depuis createSatinObject() sur une forme dont le reliquat est
    // significatif (buildPinchShapeFixture()).
    void createSatinObjectContinuePartialLeavesResidualUncovered();
    void createSatinObjectUseTatamiFillsResidualWithFallback();
    void createSatinObjectCancelLeavesDocumentUnchanged();

    // Panneau Workflow (audit ergonomie) : les étapes « Régions »/« Vecteurs »
    // ne doivent jamais rester « à faire » quand des objets vectoriels
    // existent déjà via un chemin qui ne remplit jamais project_.segmentation
    // (Segmenter avec l'IA -> autodigitize::auto_digitize directement).
    void workflowRegionsAndVectorsStepsReflectVectorObjectsWithoutClassicSegmentation();

    // Glisser le CORPS d'une forme sélectionnée la déplace tout entière
    // (défaut remonté en usage réel : « la manipulation des vecteurs est
    // pénible » -- seule l'édition nœud par nœud existait). VRAI glisser
    // souris sur la vue réelle (pas une injection de signal), comme
    // drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject ci-dessus,
    // pour la même raison : un défaut de dragMode ne se voit qu'ainsi.
    void draggingSelectedShapeBodyWithRealMouseTranslatesWholeObject();
    // Même retour utilisateur ("manipulation des vecteurs pénible") :
    // redimensionner exigeait aussi de déplacer chaque nœud un par un.
    void draggingResizeHandleWithRealMouseScalesWholeObjectAroundOppositeCorner();
    // Flèches clavier : déplace l'objet sélectionné d'un pas fixe (0,1 mm ;
    // 1 mm avec Maj) -- aucune alternative au glisser souris n'existait pour
    // un ajustement fin, impossible à la souris passé un certain zoom.
    void arrowKeyNudgesSelectedObjectByFixedStepAndShiftUsesBiggerStep();

    // Colonne satin manuelle (outil DrawSatinColumn) : création par paires
    // alternées, rejet propre d'un point orphelin, annulation par changement
    // d'outil, remodelage d'un nœud de rail.
    void manualSatinColumnCreatesRailsAndRungsFromAlternatingPairs();
    void manualSatinColumnDropsOrphanPointOnOddCountAtFinish();
    void switchingToolDuringSatinColumnDrawCancelsIt();
    void satinRailEditModeDragsNodeAndUndoRestoresIt();

    // Ligne de coupe satin (outil DrawSatinCutLine, façon Ink/Stitch
    // "cut line") : glisser réel à travers une forme sélectionnée -> deux
    // morceaux, chacun converti en colonne satin indépendante.
    void satinCutLineToolSplitsSelectedShapeIntoTwoSatinColumns();

    // Réponses à l'audit UI (boutons "ne faisant rien", pas de courbes de
    // Bézier, menu contextuel pauvre, mode satin confus) : outil Bézier réel
    // (clic + clic-glisser), bouton/touche Entrée pour terminer un tracé,
    // suppression/duplication depuis le menu contextuel, mode satin unifié.
    void bezierToolClickCreatesCornerNodes();
    void bezierToolDragCreatesSmoothNodeWithSymmetricHandles();
    void finishDrawActionAndEnterKeyBothClosePolygon();
    void deleteVectorObjectRemovesShapeAndDependentEmbroidery();
    void duplicateVectorObjectOffsetsCopyAndIsUndoable();
    void satinEditModeTogglesBothUnderlyingModes();
    // Retour utilisateur en usage réel : la pastille "Aucun document ouvert"
    // restait affichée en permanence par-dessus le canevas dès lors qu'aucune
    // image n'était chargée, même après avoir dessiné des formes -- un flux
    // purement vectoriel (sans jamais importer d'image) était donc gêné par
    // une pastille qui ne disparaissait jamais.
    void emptyStateHidesOnceContentExistsEvenWithoutImage();

    // HP-FILE-001 (roadmap de parité Hatch) : « Nouveau projet » (Ctrl+N).
    // L'état d'édition vit dans la FENÊTRE, pas dans le document (modes
    // exclusifs, tracé en cours, sélections, simulation) : sans
    // réinitialisation explicite, il survivrait au nouveau document.
    void newProjectActionIsInFileMenuWithStandardShortcut();
    void newProjectResetsDocumentEditModesAndPanels();
    void newProjectOnModifiedDocumentCancelsOrDiscardsAsChosen();

    // HP-FILE-002 — chemin d'enregistrement mémorisé.
    void saveActionsAreInFileMenuWithStandardShortcuts();
    void savingAnOpenedProjectRewritesItWithoutAskingAPath();
    void newProjectForgetsTheSaveTargetAndResetsTheTitle();

    // HP-FILE-003 — fichiers récents. Les deux premiers slots couvrent
    // addRecentFile/pruneMissingRecentFiles (fonctions pures, sans construire
    // de MainWindow) ; les deux suivants l'intégration MainWindow/EmptyStateWidget.
    void addRecentFileDeduplicatesAndTruncatesToTen();
    void pruneMissingRecentFilesRemovesDeletedPathsPreservingOrder();
    void recentFilesAndMenuReflectTwoSavesAndOpensInOrder();
    // Régression : refreshRecentFilesUi() doit différer la reconstruction du
    // menu/de l'écran d'accueil (QTimer::singleShot), sinon un clic réel sur
    // un bouton récent pointant vers un fichier supprimé détruirait ce même
    // bouton pendant l'exécution de son propre gestionnaire clicked().
    void clickingRecentButtonForDeletedFileWarnsAndPrunesWithoutCrashing();

    // HP-FILE-004 — sauvegarde automatique et récupération après plantage.
    // Invariant central (voir l'architecture S11) : un tick sur un document
    // modifié dont currentProjectPath_ pointe vers un fichier "utilisateur"
    // temporaire laisse ce fichier strictement inchangé et produit un
    // fichier distinct sous le dossier autosave.
    void autosaveTickWritesASeparateFileAndLeavesTheUserFileUntouched();
    void autosaveTickSkipsWhenDocumentUnmodifiedOrEmpty();
    // Un candidat orphelin (écrit directement via writeAutosave(), comme un
    // VRAI plantage -- aucun MainWindow vivant à ce moment) déclenche le
    // dialogue de récupération ; "Récupérer" charge le contenu comme document
    // SANS NOM (jamais réassocié au chemin d'origine ni au créneau autosave
    // lui-même) et purge le créneau.
    void autosaveRecoveryAcceptLoadsAsUntitledDocumentAndPurgesSlot();
    // "Ignorer" purge aussi le créneau, mais sans rien charger : le document
    // par défaut de la fenêtre reste intact.
    void autosaveRecoveryIgnoreDiscardsSlotWithoutLoading();
    // Une fermeture RÉELLEMENT acceptée (closeEvent) ne laisse rien à
    // récupérer au prochain lancement.
    void cleanCloseDiscardsTheCurrentAutosaveSlot();

private:
    // Active le mode d'édition (sélection directe via selectedEmbroidery_,
    // pas via le signal DocumentPanel::embroiderySelected -- qui sélectionne
    // aussi l'objet vectoriel source et ferait apparaître les poignées de
    // nœuds vectoriels, une classe NodeHandleItem elle aussi, dans la même
    // couche), glisse la première poignée trouvée d'un décalage donné en
    // pixels vue, et attend l'exécution de la commande différée
    // (QTimer::singleShot). Partagée par les deux tests de glisser (deux
    // niveaux de zoom) pour ne pas dupliquer la mécanique QTest.
    void dragFirstStitchHandle(MainWindow& window, ObjectId embroideryId, QPoint delta);

    // Sélectionne la jonction structurelle du guide de section 0, déclenche
    // l'ajout coordonné (comme satinJunctionAddsInternalGuidesToEveryBranchAtomically),
    // et laisse le guide lié fraîchement créé (index 1, link_id 0) sélectionné.
    // Point de départ partagé par les tests de groupe lié ci-dessous.
    void selectJunctionAndAddLinkedGuides(MainWindow& window, const Fixture& fx);

    // Fenêtre visible, trois carrés 10 x 10 mm centrés en x = -15, 0, 15 (mm), vue à
    // 10 px/mm centrée sur l'origine, outil Sélection. Retourne la vue du canevas.
    CanvasView* openSquares(MainWindow& window, ObjectId& a, ObjectId& b, ObjectId& c);
    // Point du viewport au-dessus d'un point scène (mm).
    static QPoint vp(const CanvasView* view, double xMm, double yMm) {
        return view->mapFromScene(QPointF(xMm, yMm));
    }
    // Glisser avec modificateurs : appui sans modificateur, déplacements et relâchement avec.
    static void dragWith(CanvasView* view, QPoint from, QPoint to, Qt::KeyboardModifiers mods,
                         Qt::KeyboardModifiers pressMods = Qt::NoModifier);

    QTemporaryDir settingsDir_;
};

void MainWindowTest::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    // Isole les QSettings de MainWindow (géométrie/état de fenêtre) du
    // registre réel de l'utilisateur : organisation/application dédiées à ce
    // binaire de test, stockage forcé en fichier INI temporaire.
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
    QCoreApplication::setApplicationName(QStringLiteral("MainWindowTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    // Le stockage réel utilisé par QSettings() appartient bien au répertoire
    // temporaire (pas au profil utilisateur) : on le prouve en le lisant.
    QSettings probe;
    QVERIFY(probe.fileName().startsWith(settingsDir_.path()));

    // HP-FILE-004 : isole QStandardPaths::AppDataLocation (dossier autosave)
    // du profil utilisateur réel, comme ci-dessus pour QSettings. Qt pointe
    // alors vers un sous-dossier "qttest" STABLE MAIS PERSISTANT entre deux
    // exécutions de ce binaire -- purge défensive d'un créneau laissé par une
    // exécution précédente interrompue, sans quoi il ferait apparaître le
    // dialogue de récupération, non armé, dès le premier traitement
    // d'évènements du premier test venu.
    QStandardPaths::setTestModeEnabled(true);
    clearAutosaveDir();
}

void MainWindowTest::clickingVectorObjectSyncsDocumentPanelAndInspector() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* view = window.findChild<CanvasView*>();
    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(view != nullptr);
    QVERIFY(docPanel != nullptr);
    QVERIFY(propsPanel != nullptr);

    // Avant sélection : rien à inspecter (pas de spin box de paramètres).
    QCOMPARE(propsPanel->findChildren<QDoubleSpinBox*>().size(), 0);

    // Point intérieur au triangle (0,0)-(1,0)-(0,1) mm ; la scène est en Y
    // vers le bas (ADR-003), d'où le Y négatif.
    view->canvasClickedMm(QPointF(0.25, -0.25));

    // Le clic sélectionne l'objet vectoriel ; MainWindow retrouve le point de
    // contour qui lui est rattaché et synchronise liste + inspecteur dessus.
    QCOMPARE(objectsList(*docPanel)->currentItem(), objectsList(*docPanel)->topLevelItem(0));
    QVERIFY(!propsPanel->findChildren<QDoubleSpinBox*>().isEmpty());

    auto* createStitch = window.findChild<QAction*>(QStringLiteral("action_createStitch"));
    QVERIFY(createStitch != nullptr);
    QVERIFY(createStitch->isEnabled()); // un objet vectoriel est sélectionné
}

void MainWindowTest::regionAndVectorSelectionToggleContextActionsOppositely() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* view = window.findChild<CanvasView*>();
    auto* deleteRegion = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    auto* createStitch = window.findChild<QAction*>(QStringLiteral("action_createStitch"));
    QVERIFY(docPanel != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(deleteRegion != nullptr);
    QVERIFY(createStitch != nullptr);

    QVERIFY(!deleteRegion->isEnabled()); // rien de sélectionné au départ
    QVERIFY(!createStitch->isEnabled());

    docPanel->regionSelected(fx.regionId);
    QVERIFY(deleteRegion->isEnabled());
    QVERIFY(!createStitch->isEnabled()); // une région, pas un objet vectoriel

    view->canvasClickedMm(QPointF(0.25, -0.25)); // sélectionne le triangle
    QVERIFY(createStitch->isEnabled());
    QVERIFY(
        !window.selectedRegion_.has_value()); // la sélection au canevas prime (cf. onCanvasClicked)
    // Suppr universel (L5-T4a) : l'action reste active, désormais pour l'objet vectoriel.
    QVERIFY(deleteRegion->isEnabled());
}

// ---------------------------------------------------------------------------
// L5-T4a : modèle de sélection (specs/plans/ui-interaction-model.md §2.7).
// ---------------------------------------------------------------------------

void MainWindowTest::selectionAddAndToggleKeepInvariants() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    // Replace : cas legacy, multiSelection_ reste vide.
    window.applySelectionClick(fx.a, SelectMode::Replace);
    QVERIFY(window.selectedObject_ == fx.a);
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.checkSelectionInvariants());

    // Maj : ajoute, le dernier ajouté devient le principal.
    window.applySelectionClick(fx.b, SelectMode::Add);
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.selectedObject_ == fx.b);
    QVERIFY(window.checkSelectionInvariants());

    // Ctrl sur un absent : l'ajoute en dernier.
    window.applySelectionClick(fx.c, SelectMode::Toggle);
    QCOMPARE(window.multiSelection_.size(), std::size_t{3});
    QVERIFY(window.selectedObject_ == fx.c);
    QVERIFY(window.multiSelection_.back() == fx.c);

    // Ctrl sur un présent non principal : le retire, le principal ne change pas.
    window.applySelectionClick(fx.a, SelectMode::Toggle);
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.selectedObject_ == fx.c);
    QVERIFY(window.checkSelectionInvariants());

    // Replace repart d'un seul objet.
    window.applySelectionClick(fx.b, SelectMode::Replace);
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.selectedObject_ == fx.b);
}

void MainWindowTest::togglingPrimaryPromotesPrevious() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);
    window.applySelectionClick(fx.c, SelectMode::Add);
    QVERIFY(window.selectedObject_ == fx.c);

    window.applySelectionClick(fx.c, SelectMode::Toggle); // retire le principal
    QVERIFY(window.selectedObject_ == fx.b);              // le précédent est promu
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.checkSelectionInvariants());

    window.applySelectionClick(fx.b, SelectMode::Toggle); // il ne reste qu'un objet
    QVERIFY(window.selectedObject_ == fx.a);
    QVERIFY(window.multiSelection_.empty()); // retour au cas legacy
    QVERIFY(window.checkSelectionInvariants());

    window.applySelectionClick(fx.a, SelectMode::Toggle); // plus rien
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(window.multiSelection_.empty());
}

void MainWindowTest::addOfAlreadySelectedObjectIsNoOp() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);
    window.applySelectionClick(fx.a, SelectMode::Add); // déjà présent : rien
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.selectedObject_ == fx.b); // le principal ne change pas
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::clickOnEmptyReplaceDeselectsButModifiersKeepSelection() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);

    window.applySelectionClick(std::nullopt, SelectMode::Add);
    window.applySelectionClick(std::nullopt, SelectMode::Toggle);
    QCOMPARE(window.multiSelection_.size(), std::size_t{2}); // Maj/Ctrl dans le vide : rien

    window.applySelectionClick(std::nullopt, SelectMode::Replace);
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::rectangleSelectionHonoursModes() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionRectangle({fx.a, fx.b, fx.a}, SelectMode::Replace); // doublon ignoré
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.selectedObject_ == fx.b);

    window.applySelectionRectangle({fx.b, fx.c}, SelectMode::Toggle); // b retiré, c ajouté
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{fx.a, fx.c}));

    window.applySelectionRectangle({fx.a}, SelectMode::Add); // déjà présent
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{fx.a, fx.c}));

    window.applySelectionRectangle({fx.b}, SelectMode::Add);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{fx.a, fx.c, fx.b}));

    window.applySelectionRectangle({}, SelectMode::Replace); // rectangle vide : désélectionne
    QVERIFY(window.selectedObjectIds().empty());
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::regionSelectionClearsMultiSelection() {
    MainWindow window;
    Fixture fx = buildFixture();
    const ObjectId extra = addTriangle(fx.project, "Extra", 3);
    window.applyLoadedProject(fx.project);
    auto* docPanel = window.findChild<DocumentPanel*>();
    QVERIFY(docPanel != nullptr);

    window.applySelectionClick(fx.vectorId, SelectMode::Replace);
    window.applySelectionClick(extra, SelectMode::Add);
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});

    docPanel->regionSelected(fx.regionId);
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(window.selectedRegion_ == fx.regionId);
    QVERIFY(window.checkSelectionInvariants());

    // Même garantie au niveau du mutateur : région + 2 objets => seul le principal reste.
    window.setSelection(
        {.region = fx.regionId, .embroidery = std::nullopt, .objects = {fx.vectorId, extra}});
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.selectedObject_ == extra);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::embroiderySelectionClearsMultiSelection() {
    MainWindow window;
    Fixture fx = buildFixture();
    const ObjectId extra = addTriangle(fx.project, "Extra", 3);
    window.applyLoadedProject(fx.project);
    auto* docPanel = window.findChild<DocumentPanel*>();
    QVERIFY(docPanel != nullptr);

    window.applySelectionClick(fx.vectorId, SelectMode::Replace);
    window.applySelectionClick(extra, SelectMode::Add);
    docPanel->embroiderySelected(fx.embroideryId);

    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.selectedEmbroidery_ == fx.embroideryId);
    QVERIFY(window.selectedObject_ == fx.vectorId); // forme source mise en évidence
    QVERIFY(window.checkSelectionInvariants());

    // Une sélection d'objets depuis le canevas écarte la broderie.
    window.applySelectionClick(extra, SelectMode::Add);
    QVERIFY(!window.selectedEmbroidery_.has_value());
    QCOMPARE(window.multiSelection_.size(), std::size_t{2});
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::danglingIdsArePrunedAfterDeleteUndoRedo() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);
    window.applySelectionClick(fx.c, SelectMode::Add);

    // Suppression hors sélection (menu contextuel, autre chemin) du principal :
    // refreshImage élague l'id disparu et re-promeut le dernier restant.
    window.deleteVectorObject(fx.c);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{fx.a, fx.b}));
    QVERIFY(window.selectedObject_ == fx.b);
    QVERIFY(window.checkSelectionInvariants());

    // Suppression directe du document puis refresh : même élagage.
    window.project_.vector_objects.erase(window.project_.vector_objects.begin());
    window.refreshImage();
    QVERIFY(window.selectedObject_ == fx.b);
    QVERIFY(window.multiSelection_.empty()); // un seul restant : cas legacy
    QVERIFY(window.checkSelectionInvariants());

    // Undo de la suppression de C : C réapparaît mais n'est pas re-sélectionné ;
    // aucun id périmé.
    window.undo();
    QVERIFY(window.project_.findObject(fx.c) != nullptr);
    QVERIFY(window.checkSelectionInvariants());
    window.redo();
    QVERIFY(window.project_.findObject(fx.c) == nullptr);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::multiSelectionDisablesSingleObjectActions() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    QVERIFY(window.createStitchAct_->isEnabled());
    QVERIFY(window.createTatamiAct_->isEnabled());
    QVERIFY(window.createSatinAct_->isEnabled());
    QVERIFY(window.autoSatinAct_->isEnabled());

    window.applySelectionClick(fx.b, SelectMode::Add);
    QVERIFY(!window.createStitchAct_->isEnabled());
    QVERIFY(!window.createTatamiAct_->isEnabled());
    QVERIFY(!window.createSatinAct_->isEnabled());
    QVERIFY(!window.autoSatinAct_->isEnabled());

    // Les slots eux-mêmes refusent d'agir (menus/barres qui contourneraient l'état d'action).
    const std::size_t embBefore = window.project_.embroidery_objects.size();
    window.createRunningStitchObject();
    window.createTatamiObject();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    QVERIFY(!window.undoStack_.canUndo());

    // Retour à un seul objet : réactivées.
    window.applySelectionClick(fx.b, SelectMode::Toggle);
    QVERIFY(window.createStitchAct_->isEnabled());
    QVERIFY(window.autoSatinAct_->isEnabled());
}

void MainWindowTest::inspectorShowsNObjets() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(propsPanel != nullptr);

    const auto hasText = [&](const QString& needle) {
        const auto labels = propsPanel->findChildren<QLabel*>();
        return std::any_of(labels.begin(), labels.end(),
                           [&](const QLabel* l) { return l->text().contains(needle); });
    };

    window.applySelectionClick(fx.a, SelectMode::Replace);
    QVERIFY(!hasText(QStringLiteral("2 objets")));

    window.applySelectionClick(fx.b, SelectMode::Add);
    QVERIFY(hasText(QStringLiteral("2 objets")));
    QCOMPARE(propsPanel->findChildren<QDoubleSpinBox*>().size(), 0); // texte seul

    window.applySelectionClick(fx.c, SelectMode::Add);
    QVERIFY(hasText(QStringLiteral("3 objets")));

    window.applySelectionClick(fx.c, SelectMode::Toggle);
    window.applySelectionClick(fx.b, SelectMode::Toggle); // retour mono-objet
    QVERIFY(!hasText(QStringLiteral("2 objets")));
}

void MainWindowTest::multiSelectionDrawsEveryObjectAsSelected() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    // Le rendu se base sur isObjectSelected : tous les objets de l'ensemble, pas
    // seulement le principal.
    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);
    QVERIFY(window.isObjectSelected(fx.a));
    QVERIFY(window.isObjectSelected(fx.b));
    QVERIFY(!window.isObjectSelected(fx.c));

    // Les poignées de nœuds (principal seul) ne sont plus posées en multi-sélection.
    int handles = 0;
    for (QGraphicsItem* item : window.scene_->items()) {
        if (dynamic_cast<NodeHandleItem*>(item) != nullptr) {
            ++handles;
        }
    }
    QCOMPARE(handles, 0);
    window.applySelectionClick(fx.b, SelectMode::Toggle); // a seul
    handles = 0;
    for (QGraphicsItem* item : window.scene_->items()) {
        if (dynamic_cast<NodeHandleItem*>(item) != nullptr) {
            ++handles;
        }
    }
    QVERIFY(handles > 0);
}

void MainWindowTest::nudgeMovesWholeMultiSelectionInOneUndoStep() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const auto originX = [&](ObjectId id) {
        return window.project_.findObject(id)->paths.front().outer.nodes.front().pos.x.value;
    };
    const auto ax = originX(fx.a);
    const auto bx = originX(fx.b);
    const auto cx = originX(fx.c);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.applySelectionClick(fx.b, SelectMode::Add);
    emit view->nudgeRequestedMm(QPointF(1.0, 0.0));
    QCOMPARE(originX(fx.a), ax + 1000);
    QCOMPARE(originX(fx.b), bx + 1000);
    QCOMPARE(originX(fx.c), cx); // hors sélection : immobile

    window.undo(); // UN seul pas annule les deux déplacements
    QCOMPARE(originX(fx.a), ax);
    QCOMPARE(originX(fx.b), bx);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::deleteRemovesWholeMultiSelectionInOneUndoStep() {
    MainWindow window;
    Fixture fx = buildFixture(); // triangle + broderie contour dépendante
    const ObjectId extra = addTriangle(fx.project, "Extra", 3);
    const ObjectId keep = addTriangle(fx.project, "Keep", 6);
    window.applyLoadedProject(fx.project);
    auto* deleteAct = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    QVERIFY(deleteAct != nullptr);

    window.applySelectionClick(fx.vectorId, SelectMode::Replace);
    window.applySelectionClick(extra, SelectMode::Add);
    QVERIFY(deleteAct->isEnabled());

    const auto vecCount = window.project_.vector_objects.size();
    const auto embCount = window.project_.embroidery_objects.size();
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});

    deleteAct->trigger();
    QVERIFY(window.project_.findObject(fx.vectorId) == nullptr);
    QVERIFY(window.project_.findObject(extra) == nullptr);
    QVERIFY(window.project_.findObject(keep) != nullptr);
    QVERIFY(window.project_.embroidery_objects.empty()); // la broderie dépendante part avec
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.checkSelectionInvariants());
    QCOMPARE(window.undoStack_.undoName(), std::string("Supprimer 2 objets"));

    // UN seul pas d'annulation restaure tout, aux mêmes index.
    window.undo();
    QVERIFY(!window.undoStack_.canUndo());
    QCOMPARE(window.project_.vector_objects.size(), vecCount);
    QCOMPARE(window.project_.embroidery_objects.size(), embCount);
    QVERIFY(window.project_.vector_objects[0].id == fx.vectorId);
    QVERIFY(window.project_.vector_objects[1].id == extra);
    QVERIFY(window.project_.vector_objects[2].id == keep);
    QVERIFY(window.checkSelectionInvariants());

    window.redo();
    QVERIFY(window.project_.findObject(fx.vectorId) == nullptr);
    QVERIFY(window.project_.findObject(extra) == nullptr);
    QVERIFY(window.project_.findObject(keep) != nullptr);
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::draggingOneOfThreeSelectedBodiesMovesAllInOneUndoStep() {
    MainWindow window;
    TrianglesFixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);
    const ObjectId a = addSquare(fx.project, "A", -15);
    const ObjectId b = addSquare(fx.project, "B", 0);
    const ObjectId c = addSquare(fx.project, "C", 15);
    window.applyLoadedProject(fx.project);
    window.applySelectionRectangle({a, b, c}, SelectMode::Replace);
    window.setTool(Tool::Select);
    window.refreshImage();
    QCOMPARE(window.multiSelection_.size(), std::size_t{3});

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    const auto originX = [&](ObjectId id) {
        return window.project_.findObject(id)->paths.front().outer.nodes.front().pos.x.value;
    };
    const auto ax = originX(a);
    const auto bx = originX(b);
    const auto cx = originX(c);

    // On glisse B (centre du carré central) de +3 mm.
    const QPoint from = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint mid = view->mapFromScene(QPointF(1.5, -1.5));
    const QPoint to = view->mapFromScene(QPointF(3.0, -3.0));
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    QTRY_COMPARE_WITH_TIMEOUT(originX(b), bx + 3000, 2000);
    QCOMPARE(originX(a), ax + 3000); // les deux autres membres suivent
    QCOMPARE(originX(c), cx + 3000);
    QCOMPARE(window.multiSelection_.size(), std::size_t{3});

    window.undo(); // UN seul pas
    QCOMPARE(originX(a), ax);
    QCOMPARE(originX(b), bx);
    QCOMPARE(originX(c), cx);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::rightClickOnSelectedMemberKeepsMultiSelectionAndDeletesAll() {
    MainWindow window;
    TrianglesFixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);
    const ObjectId a = addSquare(fx.project, "A", -15);
    const ObjectId b = addSquare(fx.project, "B", 0);
    const ObjectId c = addSquare(fx.project, "C", 15);
    window.applyLoadedProject(fx.project);
    window.applySelectionRectangle({a, b}, SelectMode::Replace);

    QStringList texts;
    bool deleteEnabled = false;
    QAction* deleteAct = nullptr;
    scheduleContextMenuInspection([&](QMenu& menu) {
        texts = actionTexts(menu);
        for (QAction* act : menu.actions()) {
            if (act->objectName() == QLatin1String("contextDeleteSelection")) {
                deleteAct = act;
                deleteEnabled = act->isEnabled();
                act->trigger();
            }
        }
    });
    window.onCanvasContextMenu(QPointF(0.0, 0.0), QPoint(20, 20)); // sur B, membre sélectionné

    QVERIFY(texts.contains(QStringLiteral("2 objets")));
    QVERIFY(texts.contains(QStringLiteral("Supprimer 2 objets")));
    QVERIFY(!texts.contains(QStringLiteral("Dupliquer"))); // entrées mono-objet absentes
    QVERIFY(!texts.contains(QStringLiteral("Décaler…")));
    QVERIFY(!texts.contains(QStringLiteral("Type de points")));
    QVERIFY(deleteAct != nullptr && deleteEnabled);

    // « Supprimer N objets » a agi sur tout l'ensemble, en un pas.
    QVERIFY(window.project_.findObject(a) == nullptr);
    QVERIFY(window.project_.findObject(b) == nullptr);
    QVERIFY(window.project_.findObject(c) != nullptr);
    window.undo();
    QVERIFY(window.project_.findObject(a) != nullptr);
    QVERIFY(window.project_.findObject(b) != nullptr);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::rightClickOnUnselectedObjectReplacesTheSelection() {
    MainWindow window;
    TrianglesFixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);
    const ObjectId a = addSquare(fx.project, "A", -15);
    const ObjectId b = addSquare(fx.project, "B", 0);
    const ObjectId c = addSquare(fx.project, "C", 15);
    window.applyLoadedProject(fx.project);
    window.applySelectionRectangle({a, b}, SelectMode::Replace);

    QStringList texts;
    scheduleContextMenuInspection([&](QMenu& menu) { texts = actionTexts(menu); });
    window.onCanvasContextMenu(QPointF(15.0, 0.0), QPoint(20, 20)); // sur C, non sélectionné

    QVERIFY(window.multiSelection_.empty());
    QVERIFY(window.selectedObject_ == c);
    QVERIFY(texts.contains(QStringLiteral("Dupliquer"))); // menu mono-objet habituel
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::deleteSelectionPrefersEmbroideryOverRegion() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    // Le dock Ordre garde la région en sélectionnant une broderie.
    window.setSelection({.region = fx.regionId, .embroidery = fx.embroideryId, .objects = {}});
    window.deleteSelection();
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) == nullptr);
    QVERIFY(window.project_.segmentation->find(fx.regionId) != nullptr); // région intacte
    window.undo();
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::deleteKeyInDrawToolsDeletesObjectAndBackspaceStillRemovesLastPoint() {
    // Comportement ACCEPTÉ et épinglé : en outil de dessin, Suppr supprime l'objet
    // sélectionné (annulable) ; Retour arrière retire toujours le dernier point en
    // cours. La suppression de nœuds (contexte NodeEdit, ligne N4) est PLANIFIÉE, pas
    // encore implémentée.
    MainWindow window;
    TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.applySelectionClick(fx.a, SelectMode::Replace);
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(30.0, 30.0));
    view->canvasClickedMm(QPointF(35.0, 30.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2});

    QTest::keyClick(&window, Qt::Key_Backspace);
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    QVERIFY(window.project_.findObject(fx.a) != nullptr); // Retour arrière ne supprime pas

    QTest::keyClick(&window, Qt::Key_Delete);
    QVERIFY(window.project_.findObject(fx.a) == nullptr);
    window.undo();
    QVERIFY(window.project_.findObject(fx.a) != nullptr);
}

void MainWindowTest::deleteKeyInFocusedInspectorFieldDoesNotDeleteTheObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Broderie sélectionnée : l'inspecteur affiche ses champs de paramètres.
    window.setSelection({.region = std::nullopt, .embroidery = fx.embroideryId, .objects = {}});
    window.updateActions();
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(propsPanel != nullptr);
    const auto spins = propsPanel->findChildren<QDoubleSpinBox*>();
    QVERIFY(!spins.isEmpty());
    spins.front()->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(QApplication::focusWidget() != nullptr &&
                                 spins.front()->isAncestorOf(QApplication::focusWidget()),
                             2000);

    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Delete);
    // Le champ garde Suppr : l'objet reste (le champ peut, lui, éditer sa valeur).
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
    QVERIFY(window.project_.findObject(fx.vectorId) != nullptr);
}

void MainWindowTest::deleteOnSingleVectorObjectIsOneUndoStep() {
    MainWindow window;
    const TrianglesFixture fx = buildTriangles();
    window.applyLoadedProject(fx.project);

    window.applySelectionClick(fx.b, SelectMode::Replace);
    window.deleteSelection();
    QVERIFY(window.project_.findObject(fx.b) == nullptr);
    QVERIFY(!window.selectedObject_.has_value());
    window.undo();
    QVERIFY(window.project_.findObject(fx.b) != nullptr);
    QVERIFY(!window.undoStack_.canUndo());
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::deleteOnEmbroideryKeepsVisibleSourceAndRemovesHiddenProxy() {
    {
        MainWindow window;
        const Fixture fx = buildFixture();
        window.applyLoadedProject(fx.project);
        window.setSelection(
            {.region = std::nullopt, .embroidery = fx.embroideryId, .objects = {fx.vectorId}});
        window.deleteSelection();
        QVERIFY(window.project_.findEmbroidery(fx.embroideryId) == nullptr);
        QVERIFY(window.project_.findObject(fx.vectorId) != nullptr); // forme conservée
        QVERIFY(!window.selectedEmbroidery_.has_value());
        QVERIFY(window.checkSelectionInvariants());
        window.undo();
        QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
        QVERIFY(!window.undoStack_.canUndo());
    }
    {
        // Proxy invisible (colonne satin manuelle) : la source part avec la broderie.
        MainWindow window;
        Fixture fx = buildFixture();
        fx.project.vector_objects.front().visible = false;
        window.applyLoadedProject(fx.project);
        window.setSelection({.region = std::nullopt, .embroidery = fx.embroideryId, .objects = {}});
        window.deleteSelection();
        QVERIFY(window.project_.findEmbroidery(fx.embroideryId) == nullptr);
        QVERIFY(window.project_.findObject(fx.vectorId) == nullptr);
        window.undo();
        QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
        QVERIFY(window.project_.findObject(fx.vectorId) != nullptr);
        QVERIFY(!window.undoStack_.canUndo());
    }
}

void MainWindowTest::deleteOnRegionKeepsLegacyBehaviour() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    window.setSelection({.region = fx.regionId, .embroidery = std::nullopt, .objects = {}});
    window.deleteSelection();
    QVERIFY(window.project_.segmentation->find(fx.regionId) == nullptr);
    QVERIFY(!window.selectedRegion_.has_value());
    QCOMPARE(window.undoStack_.undoName(), std::string("Suppression de région"));
    QVERIFY(window.project_.findObject(fx.vectorId) != nullptr); // rien d'autre supprimé
    window.undo();
    QVERIFY(window.project_.segmentation->find(fx.regionId) != nullptr);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::deleteActionIsEnabledForAnySelectionKind() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* deleteAct = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    QVERIFY(deleteAct != nullptr);
    QCOMPARE(deleteAct->text().remove(QLatin1Char('&')), QStringLiteral("Supprimer la sélection"));
    QCOMPARE(deleteAct->shortcut(), QKeySequence(QKeySequence::Delete));

    QVERIFY(!deleteAct->isEnabled());
    window.setSelection({.region = fx.regionId, .embroidery = std::nullopt, .objects = {}});
    window.updateActions();
    QVERIFY(deleteAct->isEnabled());
    window.setSelection({.region = std::nullopt, .embroidery = fx.embroideryId, .objects = {}});
    window.updateActions();
    QVERIFY(deleteAct->isEnabled());
    window.setSelection(
        {.region = std::nullopt, .embroidery = std::nullopt, .objects = {fx.vectorId}});
    window.updateActions();
    QVERIFY(deleteAct->isEnabled());
    window.setSelection({});
    window.updateActions();
    QVERIFY(!deleteAct->isEnabled());
}

void MainWindowTest::undoRedoRestoresDeletedRegionAndRefreshesDocumentPanel() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* deleteRegion = window.findChild<QAction*>(QStringLiteral("action_deleteRegion"));
    auto* undoAction = window.findChild<QAction*>(QStringLiteral("action_undo"));
    auto* redoAction = window.findChild<QAction*>(QStringLiteral("action_redo"));
    QVERIFY(docPanel != nullptr);
    QVERIFY(deleteRegion != nullptr);
    QVERIFY(undoAction != nullptr);
    QVERIFY(redoAction != nullptr);

    docPanel->regionSelected(fx.regionId);
    QCOMPARE(regionsList(*docPanel)->count(), 1);
    QVERIFY(!undoAction->isEnabled());

    deleteRegion->trigger();
    QCOMPARE(regionsList(*docPanel)->count(), 0); // la région a disparu de la liste
    QVERIFY(undoAction->isEnabled());
    QVERIFY(!redoAction->isEnabled());

    undoAction->trigger();
    // L'UI reflète la restauration du modèle, pas seulement la pile undo.
    QCOMPARE(regionsList(*docPanel)->count(), 1);
    QVERIFY(!undoAction->isEnabled());
    QVERIFY(redoAction->isEnabled());

    redoAction->trigger();
    QCOMPARE(regionsList(*docPanel)->count(), 0);
}

// Régression : applyLoadedProject() n'oubliait de réinitialiser que
// selectedEmbroidery_ (selectedRegion_ et selectedObject_ l'étaient déjà).
// Deux projets construits par buildFixture() partent chacun d'un
// document::Project{} par défaut, donc allouent les mêmes ObjectId — le cas
// piège où un ID de broderie est recyclé entre deux documents distincts.
void MainWindowTest::embroiderySelectionDoesNotLeakAcrossProjectLoadWithReusedId() {
    MainWindow window;
    const Fixture fx1 = buildFixture();
    window.applyLoadedProject(fx1.project);

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* propsPanel = window.findChild<PropertiesPanel*>();
    QVERIFY(docPanel != nullptr);
    QVERIFY(propsPanel != nullptr);

    docPanel->embroiderySelected(fx1.embroideryId);
    QCOMPARE(objectsList(*docPanel)->currentItem(), objectsList(*docPanel)->topLevelItem(0));
    QVERIFY(
        !propsPanel->findChildren<QDoubleSpinBox*>().isEmpty()); // inspecteur montre la broderie

    const Fixture fx2 = buildFixture();
    QCOMPARE(fx2.embroideryId.value, fx1.embroideryId.value); // même ID recyclé, autre document
    window.applyLoadedProject(fx2.project);

    // Rien n'a été sélectionné explicitement dans le nouveau projet : ni le
    // panneau Document ni l'inspecteur ne doivent refléter la broderie
    // choisie dans le projet précédent.
    QCOMPARE(objectsList(*docPanel)->currentItem(), nullptr);
    QVERIFY(propsPanel->findChildren<QDoubleSpinBox*>().isEmpty());
}

// ---------------------------------------------------------------------------
// Lot 8.2 : mode d'édition des points générés (revue corrective)
// ---------------------------------------------------------------------------

void MainWindowTest::stitchEditModeGatingTracksSelectionAndDirtyState() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);

    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QSignalSpy toggledSpy(editAct, &QAction::toggled);

    QVERIFY(!editAct->isEnabled()); // rien de sélectionné au départ
    QVERIFY(!editAct->isChecked());

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(editAct->isEnabled());

    editAct->setChecked(true);
    QCOMPARE(toggledSpy.count(), 1);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditTarget_.has_value());
    QCOMPARE(window.stitchEditTarget_->value, fx.embroideryId.value);
    QVERIFY(window.stitchEditView_.has_value());

    // Perte de sélection : sortie propre (updateActions) via QSignalBlocker
    // côté production -- aucun second toggled() émis, seulement des lectures.
    window.selectedEmbroidery_.reset();
    window.updateActions();
    QCOMPARE(toggledSpy.count(), 1);
    QVERIFY(!editAct->isChecked());
    QVERIFY(!editAct->isEnabled());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());

    // Réactiver, retoucher un point, puis rendre l'objet Dirty par un
    // changement de paramètres (sans toucher à la sélection) : le mode doit
    // ressortir de lui-même.
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditView_.has_value());

    const auto baseIndex = firstMovableIndex(*window.stitchEditView_);
    window.undoStack_.execute(
        std::make_unique<openstitch::commands::MoveStitchPointCommand>(
            fx.embroideryId, baseIndex, Vec2um{Micrometers{1'234}, Micrometers{5'678}},
            window.stitchEditView_->fingerprint, window.stitchEditView_->point_count),
        window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::ManuallyEdited);
    QVERIFY(editAct->isChecked()); // ManuallyEdited reste éditable

    window.undoStack_.execute(
        std::make_unique<openstitch::commands::SetStitchParamsCommand>(
            fx.embroideryId, openstitch::document::RunningStitchParams{Micrometers{500}}),
        window.project_);
    window.refreshImage();
    window.updateActions();

    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);
    QVERIFY(!editAct->isChecked()); // sorti proprement, sans action utilisateur
    QVERIFY(!editAct->isEnabled()); // reste désactivé tant que Dirty
    QVERIFY(!window.stitchEditTarget_.has_value());
}

void MainWindowTest::satinGuideModeMovesEndpointOnRailAndUndoRestoresIt() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* action = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(action->isEnabled());
    action->setChecked(true);
    QVERIFY(action->isChecked());
    QCOMPARE(window.satinGuideTarget_->value, fx.embroideryId.value);

    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    QCOMPARE(handles.size(), 6); // deux extrémités pour chacun des trois guides
    // Cadre de vue déterministe : la minuscule image factice ferait sinon
    // arrondir 1 mm à 0 px selon la géométrie de fenêtre restaurée.
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(5.0, -2.0));
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 5.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* first = *it; // extrémité A du guide central, loin des bords de vue
    const QPoint start = window.view_->mapFromScene(first->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(40, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    const QPoint middle = (start + end) / 2;
    QVERIFY(window.view_->viewport()->rect().contains(start));
    QVERIFY(window.view_->viewport()->rect().contains(end));
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(window.view_->itemAt(start)), first);
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window.view_->viewport(), middle);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    QTRY_VERIFY_WITH_TIMEOUT(window.undoStack_.canUndo(), 1000);
    const auto& moved = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QVERIFY(moved.rungs[1].a.x.value != 5'000);
    QCOMPARE(moved.rungs[1].a.y.value, 0);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Déplacer un guide satin"));

    window.undo();
    const auto& restored = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QCOMPARE(restored.rungs[1].a.x.value, 5'000);
    QCOMPARE(restored.rungs[1].a.y.value, 0);
    action->setChecked(false);
    QVERIFY(!window.satinGuideTarget_.has_value());
    QCoreApplication::processEvents();
}

void MainWindowTest::satinGuideSelectionAddsAndRemovesWithUndoRedo() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remove != nullptr);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);
    QVERIFY(mode->isChecked());
    QVERIFY(add->isEnabled());
    QVERIFY(!remove->isEnabled());

    // Sélectionne le guide central par le même hit-test que l'utilisateur. Le
    // repère de vue explicite évite toute dépendance à la résolution ou aux
    // QSettings de géométrie de fenêtre.
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(5.0, -2.0));
    SatinGuideItem* middleGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x() - 5.0) < 0.01) {
            middleGuide = guide;
            break;
        }
    }
    QVERIFY(middleGuide != nullptr);
    const QPoint guidePoint = window.view_->mapFromScene(middleGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QCOMPARE(dynamic_cast<SatinGuideItem*>(window.view_->itemAt(guidePoint)), middleGuide);
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QVERIFY(remove->isEnabled());

    remove->trigger();
    const auto* afterRemove = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(afterRemove != nullptr);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterRemove->params).rungs.size(),
             std::size_t{2});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer un guide satin"));
    QVERIFY(!remove->isEnabled());

    window.undo();
    const auto* afterRemoveUndo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterRemoveUndo->params).rungs,
             std::get<openstitch::document::SatinParams>(
                 fx.project.findEmbroidery(fx.embroideryId)->params)
                 .rungs);

    add->trigger();
    const auto* afterAdd = window.project_.findEmbroidery(fx.embroideryId);
    const auto& added = std::get<openstitch::document::SatinParams>(afterAdd->params);
    QCOMPARE(added.rungs.size(), std::size_t{4});
    QCOMPARE(added.rungs[1].a.x.value, 2'500);
    QCOMPARE(added.rungs[1].b.x.value, 2'500);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Ajouter un guide satin"));
    QVERIFY(remove->isEnabled());

    window.undo();
    const auto* afterAddUndo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterAddUndo->params).rungs.size(),
             std::size_t{3});
    window.redo();
    const auto* afterAddRedo = window.project_.findEmbroidery(fx.embroideryId);
    QCOMPARE(std::get<openstitch::document::SatinParams>(afterAddRedo->params).rungs.size(),
             std::size_t{4});
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinJunctionGuideIsLockedInUi() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture(true);
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(remove != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    int handleCount = 0;
    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        if (dynamic_cast<NodeHandleItem*>(item) != nullptr) {
            ++handleCount;
        }
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
        }
    }
    QCOMPARE(handleCount, 4); // aucun handle sur le guide structurel de départ
    QVERIFY(junctionGuide != nullptr);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QCOMPARE(dynamic_cast<SatinGuideItem*>(window.view_->itemAt(guidePoint)), junctionGuide);
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});
    QVERIFY(!remove->isEnabled());

    const auto before = std::get<openstitch::document::SatinParams>(
                            window.project_.findEmbroidery(fx.embroideryId)->params)
                            .rungs;
    remove->trigger();
    const auto after = std::get<openstitch::document::SatinParams>(
                           window.project_.findEmbroidery(fx.embroideryId)->params)
                           .rungs;
    QCOMPARE(after, before);
    QVERIFY(!window.undoStack_.canUndo());
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinJunctionAddsInternalGuidesToEveryBranchAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
            break;
        }
    }
    QVERIFY(junctionGuide != nullptr);
    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});

    add->trigger();
    const auto rungCount = [&](ObjectId id) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs.size();
    };
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    const auto guideAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(guideAt(fx.embroideryId, 1).a.x, Micrometers{1'000});
    QCOMPARE(guideAt(fx.embroideryId2, 1).a.x, Micrometers{1'000});
    QCOMPARE(guideAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(guideAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Ajouter des guides satin coordonnés"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));

    window.undo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});
    window.redo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::selectJunctionAndAddLinkedGuides(MainWindow& window, const Fixture& fx) {
    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    auto* add = window.findChild<QAction*>(QStringLiteral("action_addSatinGuide"));
    QVERIFY(mode != nullptr);
    QVERIFY(add != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    mode->setChecked(true);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.scene_->setSceneRect(QRectF(-5.0, -10.0, 20.0, 20.0));

    SatinGuideItem* junctionGuide = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* guide = dynamic_cast<SatinGuideItem*>(item);
        if (guide != nullptr && std::abs(guide->line().p1().x()) < 0.01) {
            junctionGuide = guide;
            break;
        }
    }
    QVERIFY(junctionGuide != nullptr);
    window.view_->centerOn(junctionGuide->line().center());
    const QPoint guidePoint = window.view_->mapFromScene(junctionGuide->line().center());
    QVERIFY(window.view_->viewport()->rect().contains(guidePoint));
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, guidePoint);
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{0});

    add->trigger();
    QTRY_COMPARE(window.selectedSatinGuide_, std::optional<std::size_t>{1});
}

void MainWindowTest::satinLinkedGuideGroupMoveShiftDragUpdatesBothSectionsAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);

    const auto rungAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(rungAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(rungAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});

    window.view_->centerOn(QPointF(1.0, -2.0));
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 1.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* handle = *it; // extrémité rail A du guide lié (index 1)

    const QPoint start = window.view_->mapFromScene(handle->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(24, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    const QPoint middle = (start + end) / 2;
    QVERIFY(window.view_->viewport()->rect().contains(start));
    QVERIFY(window.view_->viewport()->rect().contains(end));
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(window.view_->itemAt(start)), handle);

    // Maj+glisser : geste explicite du groupe lié (documenté dans le statut du
    // mode et l'infobulle du guide) — un glisser SANS Maj resterait local.
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, start);
    QTest::mouseMove(window.view_->viewport(), middle);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, end);

    // canUndo() est déjà vrai (commande "Ajouter" du setup partagé) : on
    // attend le NOM de la commande, pas juste canUndo(), pour laisser le
    // QTimer::singleShot du glisser différé s'exécuter.
    QTRY_COMPARE(QString::fromStdString(window.undoStack_.undoName()),
                 QStringLiteral("Déplacer des guides satin coordonnés"));
    const auto movedX = rungAt(fx.embroideryId, 1).a.x;
    QVERIFY(movedX.value != 1'000); // a bougé
    // Même géométrie de section -> même delta normalisé -> même résultat exact
    // dans les deux sections (groupe déplacé de façon cohérente, pas juste la
    // section glissée).
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, movedX);
    QCOMPARE(rungAt(fx.embroideryId, 1).a.y, Micrometers{0});
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));

    window.undo();
    QCOMPARE(rungAt(fx.embroideryId, 1).a.x, Micrometers{1'000});
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, Micrometers{1'000});
    window.redo();
    QCOMPARE(rungAt(fx.embroideryId, 1).a.x, movedX);
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, movedX);

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinLinkedGuideShiftClickCreatesNoUndoCommand() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    const auto undoNameBefore = window.undoStack_.undoName();
    window.view_->centerOn(QPointF(1.0, -2.0));
    NodeHandleItem* handle = nullptr;
    for (QGraphicsItem* item : window.baseItems_) {
        auto* candidate = dynamic_cast<NodeHandleItem*>(item);
        if (candidate != nullptr && std::abs(candidate->scenePos().x() - 1.0) < 0.01 &&
            std::abs(candidate->scenePos().y()) < 0.01) {
            handle = candidate;
            break;
        }
    }
    QVERIFY(handle != nullptr);
    const QPoint point = window.view_->mapFromScene(handle->scenePos());
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::ShiftModifier, point);
    QCoreApplication::processEvents();

    QCOMPARE(window.undoStack_.undoName(), undoNameBefore);
    QCOMPARE(std::get<openstitch::document::SatinParams>(
                 window.project_.findEmbroidery(fx.embroideryId)->params)
                 .rungs[1]
                 .a.x,
             Micrometers{1'000});

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::satinLinkedGuideGroupRemoveDeletesBothSectionsAtomically() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    auto* remove = window.findChild<QAction*>(QStringLiteral("action_removeSatinGuide"));
    QVERIFY(remove != nullptr);
    QVERIFY(remove->isEnabled());

    const auto rungCount = [&](ObjectId id) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs.size();
    };
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});

    remove->trigger();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer des guides satin coordonnés"));
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2 guides")));
    QVERIFY(!window.selectedSatinGuide_.has_value());

    window.undo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{4});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{4});
    const auto guideAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };
    QCOMPARE(guideAt(fx.embroideryId, 1).link_id, std::optional<std::uint32_t>{0});
    QCOMPARE(guideAt(fx.embroideryId2, 1).link_id, std::optional<std::uint32_t>{0});

    window.redo();
    QCOMPARE(rungCount(fx.embroideryId), std::size_t{3});
    QCOMPARE(rungCount(fx.embroideryId2), std::size_t{3});

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

// Régression : un guide lié glissé SANS Maj reste un geste local (édition
// d'angle propre à cette section) — seule la section 0 doit bouger, la
// section 1 doit rester intacte. Couvre la non-régression du comportement
// pré-existant (guides non liés / édition locale) une fois le geste de groupe
// introduit.
void MainWindowTest::satinLinkedGuideEndpointDragWithoutShiftStaysLocal() {
    MainWindow window;
    const Fixture fx = buildSatinJunctionFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectJunctionAndAddLinkedGuides(window, fx);
    const auto rungAt = [&](ObjectId id, std::size_t index) {
        return std::get<openstitch::document::SatinParams>(
                   window.project_.findEmbroidery(id)->params)
            .rungs[index];
    };

    window.view_->centerOn(QPointF(1.0, -2.0));
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x() - 1.0) < 0.01 &&
               std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* handle = *it;

    const QPoint start = window.view_->mapFromScene(handle->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(24, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));

    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window.view_->viewport(), (start + end) / 2);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    // canUndo() est déjà vrai (commande "Ajouter" du setup partagé) : on
    // attend le NOM de la commande pour laisser le glisser différé s'exécuter.
    QTRY_COMPARE(QString::fromStdString(window.undoStack_.undoName()),
                 QStringLiteral("Déplacer un guide satin"));
    QVERIFY(rungAt(fx.embroideryId, 1).a.x.value != 1'000);
    QCOMPARE(rungAt(fx.embroideryId2, 1).a.x, Micrometers{1'000}); // section 1 intacte

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_satinGuideMode"));
    mode->setChecked(false);
    QCoreApplication::processEvents();
}

void MainWindowTest::dragFirstStitchHandle(MainWindow& window, ObjectId embroideryId,
                                           QPoint delta) {
    window.selectedEmbroidery_ = embroideryId;
    window.updateActions();

    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QVERIFY(editAct->isEnabled());
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);

    const QPoint startVp = window.view_->mapFromScene(handle->pos());
    const QPoint endVp = startVp + delta;

    QVERIFY(!window.undoStack_.canUndo());
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, startVp);
    QTest::mouseMove(window.view_->viewport(), endVp);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, endVp);

    // La commande est différée (QTimer::singleShot(0), cf. renderBase) pour ne
    // pas détruire la poignée pendant son propre événement souris.
    QTRY_VERIFY(window.undoStack_.canUndo());
    QCOMPARE(window.undoStack_.undoName(), std::string("Déplacement de point"));

    const auto* obj = window.project_.findEmbroidery(embroideryId);
    QVERIFY(obj != nullptr);
    QCOMPARE(obj->overrides.size(), std::size_t(1)); // une seule commande pour tout le glisser
    QVERIFY(obj->overrides[0].moved_to.has_value());
    const auto droppedOverride = obj->overrides[0]; // copie : comparée après undo/redo

    auto* docPanel = window.findChild<DocumentPanel*>();
    QVERIFY(docPanel != nullptr);
    QVERIFY(
        objectsList(*docPanel)->topLevelItem(0)->toolTip(0).contains(QStringLiteral("Retouché")));

    // Undo/redo exact : annuler retire la retouche entièrement (aucun résidu
    // partiel), rétablir restaure très exactement la même entrée.
    QVERIFY(window.undoStack_.undo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterUndo = window.project_.findEmbroidery(embroideryId);
    QVERIFY(objAfterUndo != nullptr);
    QVERIFY(objAfterUndo->overrides.empty());
    QVERIFY(!window.undoStack_.canUndo()); // une seule commande existait
    QVERIFY(window.undoStack_.canRedo());
    QCOMPARE(window.editStateOf(embroideryId), ObjectEditState::Clean);

    QVERIFY(window.undoStack_.redo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterRedo = window.project_.findEmbroidery(embroideryId);
    QVERIFY(objAfterRedo != nullptr);
    QCOMPARE(objAfterRedo->overrides.size(), std::size_t(1));
    QCOMPARE(objAfterRedo->overrides[0].base_index, droppedOverride.base_index);
    QVERIFY(objAfterRedo->overrides[0].moved_to.has_value());
    QVERIFY(*objAfterRedo->overrides[0].moved_to == *droppedOverride.moved_to); // exact, pas approx
    QCOMPARE(window.editStateOf(embroideryId), ObjectEditState::ManuallyEdited);
}

void MainWindowTest::draggingStitchHandleAtDefaultZoomMovesPointOnce() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    dragFirstStitchHandle(window, fx.embroideryId, QPoint(20, 15));
    QVERIFY(!QTest::currentTestFailed());
}

void MainWindowTest::draggingStitchHandleAfterZoomingInMovesPointOnce() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // Second niveau de zoom : la conversion écran <-> mm change d'échelle,
    // seule façon de distinguer un bug de placement/mapping d'un hasard au
    // niveau de zoom par défaut.
    window.view_->zoomIn();
    window.view_->zoomIn();

    dragFirstStitchHandle(window, fx.embroideryId, QPoint(20, 15));
    QVERIFY(!QTest::currentTestFailed());
}

void MainWindowTest::clickingStitchHandleWithoutMovingCreatesNoCommand() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);
    const QPoint clickVp = window.view_->mapFromScene(handle->pos());

    QVERIFY(!window.undoStack_.canUndo());
    QTest::mouseClick(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, clickVp);
    // Le callback de relâchement retourne avant même de programmer un QTimer
    // quand la position n'a pas changé (cf. renderBase, garde `newPos == pos`).
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::loadingNewProjectExitsStitchEditModeAndBumpsGeneration() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);
    const auto generationAfterFirstLoad = window.documentGeneration_;

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(editAct->isChecked());
    QVERIFY(window.stitchEditTarget_.has_value());

    const Fixture fx2 = buildRunningSquareFixture();
    window.applyLoadedProject(fx2.project);

    QVERIFY(window.documentGeneration_ != generationAfterFirstLoad);
    QVERIFY(!editAct->isChecked());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());
}

// Régression (revue corrective Lot 8.2, point 2) : la commande de glisser est
// différée d'un cycle d'événements (QTimer::singleShot(0)) pour ne pas
// détruire sa propre poignée. Si un nouveau projet est chargé dans cette
// fenêtre pendant cette fenêtre de temps, la commande différée doit être
// abandonnée -- jamais exécutée sur, ni pire mutant, un projet qui n'est plus
// celui pour lequel elle a été construite.
void MainWindowTest::draggingHandleThenLoadingNewProjectDoesNotMutateIt() {
    MainWindow window;
    const Fixture fx1 = buildRunningSquareFixture();
    window.applyLoadedProject(fx1.project);
    window.view_->fitCanvas();
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.selectedEmbroidery_ = fx1.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    auto* handle = firstHandle(window.baseItems_);
    QVERIFY(handle != nullptr);
    const QPoint startVp = window.view_->mapFromScene(handle->pos());
    const QPoint endVp = startVp + QPoint(20, 15);

    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, startVp);
    QTest::mouseMove(window.view_->viewport(), endVp);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, endVp);
    // À cet instant, la commande différée est en file d'attente mais ne s'est
    // pas encore exécutée : on simule ici un changement de projet, avant
    // qu'elle ait pu tourner.
    const Fixture fx2 = buildRunningSquareFixture();
    QCOMPARE(fx2.embroideryId.value, fx1.embroideryId.value); // même id recyclé, autre document
    window.applyLoadedProject(fx2.project);

    // Traite explicitement le prochain cycle de la boucle Qt : le QTimer(0)
    // est ainsi exécuté sans attente temporelle ni hypothèse sur la machine.
    QCoreApplication::processEvents();

    const auto* obj = window.project_.findEmbroidery(fx2.embroideryId);
    QVERIFY(obj != nullptr);
    QVERIFY(obj->overrides.empty());       // la commande différée a été abandonnée
    QVERIFY(!window.undoStack_.canUndo()); // aucune commande fantôme empilée
}

void MainWindowTest::discardingOverridesOnDirtyObjectIsUndoable() {
    MainWindow window;
    const Fixture fx = buildRunningSquareFixture();
    window.applyLoadedProject(fx.project);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditView_.has_value());

    const auto baseIndex = firstMovableIndex(*window.stitchEditView_);
    const Vec2um droppedPos{Micrometers{1'234}, Micrometers{5'678}};
    window.undoStack_.execute(std::make_unique<openstitch::commands::MoveStitchPointCommand>(
                                  fx.embroideryId, baseIndex, droppedPos,
                                  window.stitchEditView_->fingerprint,
                                  window.stitchEditView_->point_count),
                              window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::ManuallyEdited);

    // Changement de paramètres : invalide l'empreinte stockée -> Dirty.
    window.undoStack_.execute(
        std::make_unique<openstitch::commands::SetStitchParamsCommand>(
            fx.embroideryId, openstitch::document::RunningStitchParams{Micrometers{500}}),
        window.project_);
    window.refreshImage();
    window.updateActions();
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);

    // « Abandonner les retouches » demande confirmation (QMessageBox modale) :
    // on l'auto-accepte, comme le ferait un utilisateur cliquant Oui --
    // programmé avant l'appel, puisque exec() pompe la boucle d'événements en
    // interne.
    QTimer::singleShot(0, &window, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            box->button(QMessageBox::Yes)->click();
        }
    });
    window.discardOverrides(fx.embroideryId);

    const auto* objAfterDiscard = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(objAfterDiscard != nullptr);
    QVERIFY(objAfterDiscard->overrides.empty());
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Clean);
    QVERIFY(window.undoStack_.canUndo());
    QCOMPARE(window.undoStack_.undoName(), std::string("Abandonner les retouches"));

    // Annuler l'abandon restaure exactement les retouches et l'état Dirty
    // d'avant (aucune perte : c'est le contrat de DiscardOverridesCommand).
    QVERIFY(window.undoStack_.undo(window.project_));
    window.refreshImage();
    window.updateActions();
    const auto* objAfterUndo = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(objAfterUndo != nullptr);
    QCOMPARE(objAfterUndo->overrides.size(), std::size_t(1));
    QCOMPARE(objAfterUndo->overrides[0].base_index, baseIndex);
    QVERIFY(objAfterUndo->overrides[0].moved_to.has_value());
    QVERIFY(*objAfterUndo->overrides[0].moved_to == droppedPos);
    QCOMPARE(window.editStateOf(fx.embroideryId), ObjectEditState::Dirty);
}

// Régression (revue corrective Lot 8.2, point 3) : le seuil doit porter sur
// les points réellement déplaçables, pas sur la taille totale de la vue
// brute -- ce test l'exerce avec un objet bien au-delà de la limite.
void MainWindowTest::stitchEditModeRefusesWhenTooManyMovablePoints() {
    MainWindow window;
    Fixture fx = buildRunningSquareFixture();
    // Densité extrême (10 µm/point, fusion désactivée) sur un périmètre de
    // 40 mm : plusieurs milliers de points, bien au-delà de
    // kMaxEditableStitchHandles (2000).
    fx.project.embroidery_objects[0].params =
        openstitch::document::RunningStitchParams{Micrometers{10}, Micrometers{1}};
    window.applyLoadedProject(fx.project);

    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    QVERIFY(editAct->isEnabled()); // Clean, sélectionné : activable a priori

    editAct->setChecked(true);

    // Refusé avec explication (barre de statut), jamais laissé actif sans
    // aucune poignée -- cf. updateActions, bloc stitchEditTooManyPoints.
    QVERIFY(!editAct->isChecked());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("2000")));
    QVERIFY(firstHandle(window.baseItems_) == nullptr); // aucune poignée affichée
}

// --- Création manuelle de formes (mission « auto-satin béton », suite) ------

void MainWindowTest::drawRectangleToolCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawRectangle);
    // Scène Y vers le bas (ADR-003) : cadre 5 x 3 mm.
    view->boxDrawnMm(QRectF(10.0, -5.0, 5.0, 3.0), Qt::NoModifier);

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths.size(), std::size_t{1});
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    QVERIFY(obj.paths[0].outer.closed);
    for (const auto& n : obj.paths[0].outer.nodes) {
        QVERIFY(n.type == openstitch::geometry::NodeType::Corner);
    }
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(*window.selectedObject_, obj.id);

    // Aire exacte : 5 x 3 mm = 15 000 000 µm².
    QCOMPARE(std::abs(openstitch::geometry::signed_area_um2(obj.paths[0].outer)), 15'000'000.0);

    // Annulable : undo retire l'objet, redo le restaure.
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
    window.redo();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
}

void MainWindowTest::createSatinObjectOnSuitableRectangleProducesOneSatinWithStitches() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    // Rectangle allongé 40 x 5 mm : Suitable côté satinabilité (même forme
    // que la fixture "rectangle" de libs/auto_satin), une seule colonne
    // attendue -- ni décomposition en branches, ni refus.
    window.setTool(Tool::DrawRectangle);
    view->boxDrawnMm(QRectF(0.0, 0.0, 40.0, 5.0), Qt::NoModifier);
    QVERIFY(window.selectedObject_.has_value());
    const ObjectId vectorId = *window.selectedObject_;

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();

    // createSatinObject() ouvre une QDialog modale (densité/compensation/
    // sous-couche) : programmé avant l'appel, comme les autres tests de ce
    // fichier qui pilotent une boîte modale (cf. discardOverrides ci-dessus)
    // -- exec() pompe la boucle d'événements en interne.
    QTimer::singleShot(0, &window, [] {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dlg->accept();
        }
    });
    window.createSatinObject();

    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore + 1);
    const auto& emb = window.project_.embroidery_objects.back();
    QCOMPARE(emb.source_vector, vectorId);
    QVERIFY(emb.is_satin());
    const auto& satin = std::get<openstitch::document::SatinParams>(emb.params);
    QVERIFY(satin.rail_a.nodes.size() >= 2);
    QVERIFY(satin.rail_b.nodes.size() >= 2);
    // Le moteur squelette pose des barreaux par défaut (correspondance
    // ladder) : sans eux, la génération retomberait sur fill_satin seul.
    QVERIFY(!satin.rungs.empty());

    window.refreshImage();
    QVERIFY(window.sequence_.has_value());
    const auto stats = openstitch::stitch::compute_stats(*window.sequence_);
    QVERIFY(stats.stitches > 0);

    // Annulable en un seul geste (AddObjectBatchCommand), comme les autres
    // créations de forme de ce fichier.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
    window.redo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore + 1);
}

void MainWindowTest::createSatinObjectOnBranchedShapeProducesMultipleSatinSectionsDirectly() {
    MainWindow window;
    const Fixture fx = buildTShapeFixture();
    window.applyLoadedProject(fx.project);
    window.selectedObject_ = fx.vectorId;
    window.updateActions();

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();

    // createSatinObject() ouvre la QDialog densité/compensation/sous-couche,
    // et potentiellement UNE SECONDE boîte modale ensuite
    // (warnAboutIncompleteSatinCoverage()) si la mesure de couverture laisse
    // un résidu significatif sur cette forme -- les deux sont acceptées au fur
    // et à mesure qu'elles apparaissent.
    autoDismissModalDialogs(&window);
    window.createSatinObject();

    // Intention SATIN sur une forme branchée : le chemin direct doit produire
    // les sections de branche sans demander au planner récursif de subdiviser
    // la région. C'est le VRAI chemin UI (MainWindow::createSatinObject), pas
    // seulement `build_satin_sections(..., DirectColumns)` appelé directement.
    const std::size_t createdCount =
        window.project_.embroidery_objects.size() - embroideryCountBefore;
    QVERIFY2(
        createdCount >= 2,
        qPrintable(QStringLiteral("attendu >= 2 sections satin, obtenu %1").arg(createdCount)));
    for (std::size_t i = embroideryCountBefore; i < window.project_.embroidery_objects.size();
         ++i) {
        const auto& emb = window.project_.embroidery_objects[i];
        QCOMPARE(emb.source_vector, fx.vectorId);
        QVERIFY(emb.is_satin());
        const auto& satin = std::get<openstitch::document::SatinParams>(emb.params);
        QVERIFY(satin.rail_a.nodes.size() >= 2);
        QVERIFY(satin.rail_b.nodes.size() >= 2);
        QVERIFY(!satin.rungs.empty());
    }

    // Toute la création arrive en un seul geste annulable
    // (AddObjectBatchCommand), comme sur la forme simple.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
}

void MainWindowTest::setStitchTypeSatinCaseProducesRealRailsAndIsUndoable() {
    MainWindow window;
    const Fixture fx = buildRunningRectangleFixture(); // 40x5 mm, embroidery en contour
    window.applyLoadedProject(fx.project);

    window.setStitchType(fx.embroideryId, /*type=*/2); // 2 = satin

    const auto* emb = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(emb != nullptr);
    QVERIFY(emb->is_satin());
    const auto& satin = std::get<openstitch::document::SatinParams>(emb->params);
    // Le point clé de cette migration (§ plan de refonte satin, 2026-08-14) :
    // passe par autodigitize::build_satin_sections (planner unifié), jamais
    // l'ancien appel direct à build_satin_columns -- vérifié indirectement
    // par la présence de rails/barreaux RÉELS (une géométrie vide ou
    // dégénérée trahirait un chemin cassé), pas seulement le type du variant.
    QVERIFY(satin.rail_a.nodes.size() >= 2);
    QVERIFY(satin.rail_b.nodes.size() >= 2);
    QVERIFY(!satin.rungs.empty());

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    const auto* restored = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(restored != nullptr);
    QVERIFY(!restored->is_satin());
}

void MainWindowTest::
    setStitchTypeOnMultiSectionSatinNetworkRemovesSiblingsInsteadOfLeavingResidue() {
    MainWindow window;
    const Fixture fx = buildTShapeFixture();
    window.applyLoadedProject(fx.project);
    window.selectedObject_ = fx.vectorId;
    window.updateActions();

    autoDismissModalDialogs(&window);
    window.createSatinObject();

    // Même garde-fou que le test de création ci-dessus : au moins 2
    // sections satin partageant fx.vectorId, sinon ce test ne prouve rien.
    std::vector<ObjectId> sectionIds;
    for (const auto& emb : window.project_.embroidery_objects) {
        if (emb.source_vector == fx.vectorId) {
            sectionIds.push_back(emb.id);
        }
    }
    QVERIFY2(sectionIds.size() >= 2,
             qPrintable(
                 QStringLiteral("attendu >= 2 sections satin, obtenu %1").arg(sectionIds.size())));
    const std::size_t totalEmbroideryBefore = window.project_.embroidery_objects.size();

    // `setStitchType` résout l'objet cible via `embroideryForVector` (le
    // premier trouvé) exactement comme le VRAI menu contextuel "Type de
    // points" -- même chemin que l'utilisateur emprunte.
    window.setStitchType(sectionIds.front(), /*type=*/1); // 1 = tatami

    // Plus AUCUNE section satin ne doit rester pour ce vecteur -- c'est
    // exactement le résidu signalé par l'utilisateur.
    std::size_t remainingForVector = 0;
    for (const auto& emb : window.project_.embroidery_objects) {
        if (emb.source_vector == fx.vectorId) {
            ++remainingForVector;
            QVERIFY2(emb.is_tatami(), "aucune section satin residuelle attendue apres conversion");
        }
    }
    QCOMPARE(remainingForVector, std::size_t{1});
    // Les sections supprimées ont bien disparu du document (pas seulement
    // masquées) -- vérifie qu'aucun autre objet du projet ne pointe
    // dessus non plus (source_vector orphelin), même garde-fou que
    // RemoveVectorObjectCommand.
    QCOMPARE(window.project_.embroidery_objects.size(),
             totalEmbroideryBefore - (sectionIds.size() - 1));

    // Annulation : reconstitue le réseau satin complet, section par
    // section, dans l'ordre d'origine.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), totalEmbroideryBefore);
    std::size_t satinCountAfterUndo = 0;
    for (const auto& emb : window.project_.embroidery_objects) {
        if (emb.source_vector == fx.vectorId) {
            QVERIFY(emb.is_satin());
            ++satinCountAfterUndo;
        }
    }
    QCOMPARE(satinCountAfterUndo, sectionIds.size());
}

void MainWindowTest::openSvgCreatesVectorObjectsDirectlySkippingImage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString svgPath = dir.filePath("test.svg");
    QFile file(svgPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QByteArrayLiteral("<svg viewBox=\"0 0 100 100\" width=\"10mm\" height=\"10mm\">"
                                 "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\"/>"
                                 "</svg>"));
    file.close();

    MainWindow window;
    window.openSvg(svgPath);

    // Aucune image : le document est passe directement en objets
    // vectoriels, sans jamais traverser segmentation/vectorisation.
    QVERIFY(!window.project_.hasImage());
    QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{0});
    QCOMPARE(window.project_.vector_objects.front().paths.size(), std::size_t{1});
    QCOMPARE(window.project_.vector_objects.front().paths.front().outer.nodes.size(),
             std::size_t{4});
}

namespace {

// Projet opaque (sans alpha) w x h : `bg` partout sauf le rectangle `fg`,
// segmenté réellement (deux couleurs) -- l'image d'entrée d'autoDigitize().
openstitch::document::Project opaqueSegmentedProject(std::array<std::uint8_t, 3> bg,
                                                     std::array<std::uint8_t, 3> fg, int x0, int y0,
                                                     int x1, int y1) {
    constexpr int kW = 40;
    constexpr int kH = 30;
    openstitch::document::Project project;
    project.original.width = kW;
    project.original.height = kH;
    project.original.source_had_alpha = false;
    project.original.rgba.resize(std::size_t{kW} * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            const auto& c = inside ? fg : bg;
            auto* px = project.original.rgba.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
            px[3] = 255;
        }
    }
    auto seg =
        openstitch::segmentation::segment(project.original, {.max_colors = 2, .min_region_px = 1});
    project.segmentation = std::move(*seg);
    return project;
}

// Lance autoDigitize() et relève l'état du dialogue d'options (case « fond »,
// texte d'information) avant de l'annuler -- rien n'est numérisé.
struct BackgroundDialogState {
    bool seen{false};
    bool checked{false};
    QString info;
};
// `run` déclenche autoDigitize() (privé : appelé depuis une méthode de
// MainWindowTest, seul ami de MainWindow).
template <typename Run> BackgroundDialogState inspectBackgroundDialog(MainWindow& window, Run run) {
    BackgroundDialogState state;
    QTimer::singleShot(0, &window, [&state] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg == nullptr) {
            return;
        }
        if (auto* check = dlg->findChild<QCheckBox*>("skipBackgroundCheck")) {
            state.seen = true;
            state.checked = check->isChecked();
        }
        if (auto* info = dlg->findChild<QLabel*>("backgroundInfo")) {
            state.info = info->text();
        }
        state.seen = state.seen && dlg->findChild<QLabel*>("backgroundSwatch") != nullptr;
        dlg->reject();
    });
    run();
    return state;
}

} // namespace

void MainWindowTest::autoDigitizeDialogDoesNotSkipColoredFullFrameRegion() {
    MainWindow window;
    // Ciel bleu (plus grande région, 3 bords touchés) au-dessus d'une mer verte.
    window.applyLoadedProject(opaqueSegmentedProject({57, 93, 213}, {6, 101, 60}, 0, 20, 40, 30));
    const auto state = inspectBackgroundDialog(window, [&] { window.autoDigitize(); });
    QVERIFY(state.seen);
    QVERIFY(!state.checked);
    QVERIFY(state.info.contains('%'));
    QVERIFY(window.project_.embroidery_objects.empty()); // dialogue annulé
}

void MainWindowTest::autoDigitizeDialogSkipsNearWhiteFramingBackground() {
    MainWindow window;
    window.applyLoadedProject(
        opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
    const auto state = inspectBackgroundDialog(window, [&] { window.autoDigitize(); });
    QVERIFY(state.seen);
    QVERIFY(state.checked);
    QVERIFY(state.info.contains('%'));
}

void MainWindowTest::autoDigitizeDialogOffersContoursStrategy() {
    MainWindow window;
    window.applyLoadedProject(
        opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
    bool seen = false;
    int detail = -1;
    bool shapesChecked = false;
    int shapeDetail = -1;
    bool shapesPanelEnabledBefore = false;
    bool shapesPanelEnabledAfter = true;
    bool panelEnabledBefore = true;
    bool panelEnabledAfter = false;
    bool autoChecked = false;
    bool runningPresent = false;
    bool satinAbsent = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg == nullptr) {
            return;
        }
        auto* contours = dlg->findChild<QRadioButton*>("strategyContoursRadio");
        auto* shapes = dlg->findChild<QRadioButton*>("strategyShapesRadio");
        auto* shapesPanel = dlg->findChild<QWidget*>("shapesPanel");
        auto* shapeSlider = dlg->findChild<QSlider*>("shapeVectorizeDetailSlider");
        auto* slider = dlg->findChild<QSlider*>("contourDetailSlider");
        auto* panel = dlg->findChild<QWidget*>("contoursPanel");
        auto* autoRadio = dlg->findChild<QRadioButton*>("contourTechniqueAutoRadio");
        auto* runningRadio = dlg->findChild<QRadioButton*>("contourTechniqueRunningRadio");
        auto* satinRadio = dlg->findChild<QRadioButton*>("contourTechniqueSatinRadio");
        if (contours == nullptr || shapes == nullptr || shapesPanel == nullptr ||
            shapeSlider == nullptr || slider == nullptr || panel == nullptr ||
            autoRadio == nullptr || runningRadio == nullptr) {
            dlg->reject();
            return;
        }
        seen = true;
        detail = slider->value();
        shapeDetail = shapeSlider->value();
        shapesChecked = shapes->isChecked();
        autoChecked = autoRadio->isChecked();
        runningPresent = runningRadio != nullptr;
        satinAbsent = satinRadio == nullptr;
        shapesPanelEnabledBefore = shapesPanel->isEnabled();
        panelEnabledBefore = panel->isEnabled();
        contours->setChecked(true);
        shapesPanelEnabledAfter = shapesPanel->isEnabled();
        panelEnabledAfter = panel->isEnabled();
        dlg->reject();
    });
    window.autoDigitize();
    QVERIFY(seen);
    QCOMPARE(detail, 50);
    QCOMPARE(shapeDetail, 50);
    QVERIFY(shapesChecked);
    QVERIFY(autoChecked);
    QVERIFY(runningPresent);
    QVERIFY(satinAbsent);
    QVERIFY(shapesPanelEnabledBefore);
    QVERIFY(!shapesPanelEnabledAfter);
    QVERIFY(!panelEnabledBefore);
    QVERIFY(panelEnabledAfter);
    QVERIFY(window.project_.embroidery_objects.empty()); // dialogue annule
}

void MainWindowTest::vectorizeSelectedRegionOffersDetailSlider() {
    MainWindow window;
    constexpr std::array<std::uint8_t, 3> fg{200, 30, 30};
    auto project = opaqueSegmentedProject({250, 250, 250}, fg, 10, 8, 30, 22);

    std::optional<RegionId> regionId;
    for (const auto& slot : project.segmentation->region_slots) {
        if (slot && slot->rgb == fg) {
            regionId = slot->id;
            break;
        }
    }
    QVERIFY(regionId.has_value());

    window.applyLoadedProject(project);
    window.selectedRegion_ = *regionId;
    window.updateActions();

    bool seen = false;
    int defaultDetail = -1;
    QString valueAfterChange;
    QTimer::singleShot(0, &window, [&] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg == nullptr) {
            return;
        }
        auto* slider = dlg->findChild<QSlider*>("vectorizeDetailSlider");
        auto* value = dlg->findChild<QLabel*>("vectorizeDetailValue");
        if (slider == nullptr || value == nullptr) {
            dlg->reject();
            return;
        }
        seen = true;
        defaultDetail = slider->value();
        slider->setValue(100);
        valueAfterChange = value->text();
        dlg->accept();
    });

    window.vectorizeSelectedRegion();

    QVERIFY(seen);
    QCOMPARE(defaultDetail, 50);
    QCOMPARE(valueAfterChange, QStringLiteral("100"));
    QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(*window.selectedObject_, window.project_.vector_objects.back().id);
}

void MainWindowTest::autoDigitizeAfterOpenSvgClassifiesVectorObjectsDirectly() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString svgPath = dir.filePath("bande.svg");
    QFile file(svgPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    // Bande 50x3 mm : aire suffisante pour un tatami automatique.
    // Le satin reste un choix manuel, meme pour une forme fine.
    file.write(QByteArrayLiteral("<svg viewBox=\"0 0 500 30\" width=\"50mm\" height=\"3mm\">"
                                 "<rect x=\"0\" y=\"0\" width=\"500\" height=\"30\"/>"
                                 "</svg>"));
    file.close();

    MainWindow window;
    window.openSvg(svgPath);
    QVERIFY(!window.project_.hasImage());
    QVERIFY(!window.project_.segmentation.has_value());
    QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{0});
    const ObjectId sourceVecId = window.project_.vector_objects.front().id;

    // Chemin vectoriel : aucune QDialog (la question "fond présumé" n'a de
    // sens que pour une segmentation pixel) -- appel direct sans dismiss.
    window.autoDigitize();

    QVERIFY(!window.project_.hasImage()); // toujours aucune image traversee
    QVERIFY(!window.project_.embroidery_objects.empty());
    // La voie vectorielle conserve l'objet source et cree un seul tatami.
    // Aucune copie du vecteur ni section satin automatique n'est attendue.
    QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
    const auto& embroidery = window.project_.embroidery_objects.front();
    QVERIFY(embroidery.is_tatami());
    QVERIFY(embroidery.intent == document::EmbroideryIntent::AutoChoice);
    QCOMPARE(embroidery.source_vector, sourceVecId);
    QCOMPARE(window.project_.vector_objects.front().id, sourceVecId);
}

void MainWindowTest::createSatinObjectContinuePartialLeavesResidualUncovered() {
    MainWindow window;
    const Fixture fx = buildPinchShapeFixture();
    window.applyLoadedProject(fx.project);
    window.selectedObject_ = fx.vectorId;
    window.updateActions();

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();
    const std::size_t vectorCountBefore = window.project_.vector_objects.size();

    // Ferme la QDialog densité (bouton par défaut = Ok), PUIS le dialogue
    // §23 avec son propre bouton par défaut ("Continuer avec satin
    // partiel") -- le choix testé ici.
    autoDismissModalDialogs(&window);
    window.createSatinObject();

    // Satin créé (au moins une section), mais AUCUN objet tatami de repli :
    // le résidu reste honnêtement non couvert, comme le choix le demande.
    const std::size_t createdCount =
        window.project_.embroidery_objects.size() - embroideryCountBefore;
    QVERIFY(createdCount >= 1);
    for (std::size_t i = embroideryCountBefore; i < window.project_.embroidery_objects.size();
         ++i) {
        QVERIFY(window.project_.embroidery_objects[i].is_satin());
    }
    QCOMPARE(window.project_.vector_objects.size(),
             vectorCountBefore); // aucun VectorObject de repli

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
}

void MainWindowTest::createSatinObjectUseTatamiFillsResidualWithFallback() {
    MainWindow window;
    const Fixture fx = buildPinchShapeFixture();
    window.applyLoadedProject(fx.project);
    window.selectedObject_ = fx.vectorId;
    window.updateActions();

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();
    const std::size_t vectorCountBefore = window.project_.vector_objects.size();

    // Une seule séquence gère la QDialog densité (pas de bouton "tatami",
    // donc son bouton par défaut est cliqué) PUIS le dialogue §23 (où
    // "Utiliser tatami pour le reliquat" EST trouvé et cliqué).
    clickModalDialogButton(&window, "tatami");
    window.createSatinObject();

    // Attend À LA FOIS du satin ET au moins un remplissage tatami de repli
    // (VectorObject + EmbroideryObject, même schéma que autodigitize.cpp).
    bool sawSatin = false;
    bool sawTatami = false;
    for (std::size_t i = embroideryCountBefore; i < window.project_.embroidery_objects.size();
         ++i) {
        const auto& emb = window.project_.embroidery_objects[i];
        if (emb.is_satin())
            sawSatin = true;
        if (emb.is_tatami())
            sawTatami = true;
    }
    QVERIFY(sawSatin);
    QVERIFY(sawTatami);
    QVERIFY(window.project_.vector_objects.size() >
            vectorCountBefore); // le VectorObject de repli existe

    // Un seul geste annulable : satin + tatami de repli disparaissent ensemble.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
    QCOMPARE(window.project_.vector_objects.size(), vectorCountBefore);
}

void MainWindowTest::createSatinObjectCancelLeavesDocumentUnchanged() {
    MainWindow window;
    const Fixture fx = buildPinchShapeFixture();
    window.applyLoadedProject(fx.project);
    window.selectedObject_ = fx.vectorId;
    window.updateActions();

    const std::size_t embroideryCountBefore = window.project_.embroidery_objects.size();
    const std::size_t vectorCountBefore = window.project_.vector_objects.size();
    const bool couldUndoBefore = window.undoStack_.canUndo();

    clickModalDialogButton(&window, "Annuler");
    window.createSatinObject();

    // Document totalement inchangé : "Annuler" doit rester possible SANS
    // avoir rien créé (§23), pas un "undo" après coup sur quelque chose de
    // déjà committé.
    QCOMPARE(window.project_.embroidery_objects.size(), embroideryCountBefore);
    QCOMPARE(window.project_.vector_objects.size(), vectorCountBefore);
    QCOMPARE(window.undoStack_.canUndo(), couldUndoBefore);
}

void MainWindowTest::
    workflowRegionsAndVectorsStepsReflectVectorObjectsWithoutClassicSegmentation() {
    MainWindow window;
    Fixture fx = buildFixture();
    // Simule le chemin « Segmenter avec l'IA » : un objet vectoriel existe
    // (fx.project.vector_objects contient déjà "Triangle"), mais
    // project_.segmentation n'a JAMAIS été rempli -- exactement l'état
    // produit par AiSegmentationDialog + autodigitize::auto_digitize, qui ne
    // passe jamais par la segmentation classique par pixels.
    fx.project.segmentation.reset();
    window.applyLoadedProject(fx.project);
    window.updateActions();

    auto* workflow = window.findChild<WorkflowPanel*>();
    QVERIFY(workflow != nullptr);
    QCOMPARE(workflow->currentState(1), WorkflowPanel::State::Done); // Régions
    QCOMPARE(workflow->currentState(2), WorkflowPanel::State::Done); // Vecteurs
}

void MainWindowTest::draggingSelectedShapeBodyWithRealMouseTranslatesWholeObject() {
    MainWindow window;
    Fixture fx = buildFixture();
    // Carré 10 x 10 mm centré sur l'origine (le triangle de buildFixture,
    // 1 mm de côté, est trop petit pour cliquer sûrement en son centre sans
    // frôler un nœud) : nœuds à -5/-5, 5/-5, 5/5, -5/5 mm.
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);

    window.applyLoadedProject(fx.project);
    window.selectedObject_ = squareId; // sélectionné -> corps glissable (VectorObjectBodyItem)
    window.setTool(Tool::Select);
    window.refreshImage();

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    // Centre du carré (loin des 4 nœuds, ~7 mm) -> déplacement de 3 mm en
    // diagonale, largement au-delà de startDragDistance à ce zoom.
    const QPoint from = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint mid = view->mapFromScene(QPointF(1.5, -1.5));
    const QPoint to = view->mapFromScene(QPointF(3.0, -3.0));
    const QRect vpRect = view->viewport()->rect();
    QVERIFY2(vpRect.contains(from), qPrintable(QStringLiteral("from hors du viewport")));
    QVERIFY2(vpRect.contains(to), qPrintable(QStringLiteral("to hors du viewport")));
    QVERIFY2((to - from).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((to - from).manhattanLength())));

    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}}));

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    // Le glisser est validé de façon différée (QTimer::singleShot(0, ...)) :
    // laisse la boucle d'événements le traiter, comme pour les poignées de
    // nœud (cf. commentaire dans renderBase/VectorObjectBodyItem). Delta
    // scène (+3, -3) mm -> delta modèle (+3, +3) mm (sceneMmToModel : x
    // inchangé, y inversé -- donc -3 devient +3), d'où -5+3=-2 sur les deux
    // axes du premier nœud.
    QTRY_COMPARE_WITH_TIMEOUT(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
                              (Vec2um{Micrometers{-2'000}, Micrometers{-2'000}}), 2000);
    const auto* moved = window.project_.findObject(squareId);
    QVERIFY(moved != nullptr);
    QCOMPARE(moved->paths[0].outer.nodes[1].pos, (Vec2um{Micrometers{8'000}, Micrometers{-2'000}}));
    QCOMPARE(moved->paths[0].outer.nodes[2].pos, (Vec2um{Micrometers{8'000}, Micrometers{8'000}}));
    QCOMPARE(moved->paths[0].outer.nodes[3].pos, (Vec2um{Micrometers{-2'000}, Micrometers{8'000}}));

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}}));
    window.redo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-2'000}, Micrometers{-2'000}}));
}

void MainWindowTest::draggingResizeHandleWithRealMouseScalesWholeObjectAroundOppositeCorner() {
    MainWindow window;
    Fixture fx = buildFixture();
    // Carré 10 x 10 mm, coin bas-gauche à l'origine (pas centré, pour un
    // ancrage sans ambiguïté) : nœuds à 0/0, 10/0, 10/10, 0/10 mm.
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);

    window.applyLoadedProject(fx.project);
    window.selectedObject_ = squareId; // sélectionné -> poignées de redimensionnement visibles
    window.setTool(Tool::Select);
    window.refreshImage();

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(5.0, -5.0)); // centre du carré en scène (Y vers le bas)

    // Poignée au coin (10000,10000)µm = (10,-10) mm scène -- glissée jusqu'à
    // (20,-20) mm scène : double la taille, ancrée sur le coin opposé (0,0),
    // qui reste par construction en dehors de toute poignée de nœud
    // co-localisée grâce au z-order (ResizeHandleItem au-dessus).
    const QPoint from = view->mapFromScene(QPointF(10.0, -10.0));
    const QPoint mid = view->mapFromScene(QPointF(15.0, -15.0));
    const QPoint to = view->mapFromScene(QPointF(20.0, -20.0));
    const QRect vpRect = view->viewport()->rect();
    QVERIFY2(vpRect.contains(from), qPrintable(QStringLiteral("from hors du viewport")));
    QVERIFY2(vpRect.contains(to), qPrintable(QStringLiteral("to hors du viewport")));

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    // Validation différée (QTimer::singleShot(0, ...)), comme le glisser du
    // corps entier ci-dessus.
    QTRY_COMPARE_WITH_TIMEOUT(window.project_.findObject(squareId)->paths[0].outer.nodes[2].pos,
                              (Vec2um{Micrometers{20'000}, Micrometers{20'000}}), 2000);
    const auto* scaled = window.project_.findObject(squareId);
    QVERIFY(scaled != nullptr);
    QCOMPARE(scaled->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{0}, Micrometers{0}})); // ancre fixe
    QCOMPARE(scaled->paths[0].outer.nodes[1].pos, (Vec2um{Micrometers{20'000}, Micrometers{0}}));
    QCOMPARE(scaled->paths[0].outer.nodes[3].pos, (Vec2um{Micrometers{0}, Micrometers{20'000}}));

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[2].pos,
             (Vec2um{Micrometers{10'000}, Micrometers{10'000}}));
    window.redo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[2].pos,
             (Vec2um{Micrometers{20'000}, Micrometers{20'000}}));
}

void MainWindowTest::arrowKeyNudgesSelectedObjectByFixedStepAndShiftUsesBiggerStep() {
    MainWindow window;
    Fixture fx = buildFixture();
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{-5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{-5'000}, Micrometers{5'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);

    window.applyLoadedProject(fx.project);
    window.selectedObject_ = squareId;
    window.setTool(Tool::Select);
    window.refreshImage();

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Droite, pas normal (0,1 mm) : x scène inchangé -> x modèle +100 µm.
    QTest::keyClick(view, Qt::Key_Right);
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-4'900}, Micrometers{-5'000}}));

    // Haut, pas normal : y scène décroît -> y modèle +100 µm (repère inversé).
    QTest::keyClick(view, Qt::Key_Up);
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-4'900}, Micrometers{-4'900}}));

    // Maj+Bas : grand pas (1 mm) -> y modèle -1000 µm.
    QTest::keyClick(view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-4'900}, Micrometers{-5'900}}));

    // Chaque appui pousse une commande distincte -- annulable individuellement.
    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.findObject(squareId)->paths[0].outer.nodes[0].pos,
             (Vec2um{Micrometers{-4'900}, Micrometers{-4'900}}));
}

void MainWindowTest::drawRectangleToolWithRealMouseDragOnMainWindowCreatesObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    // Fenêtre large : à 900x700 (taille utilisée ailleurs dans ce fichier),
    // les docks (Document/Propriétés/Ordre/Filtres/Workflow) compriment le
    // viewport du canevas à ~68 px de large -- fenêtre réaliste ici pour ne
    // pas confondre un artefact de taille de fenêtre avec un vrai bug.
    window.resize(1600, 1000);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Outil choisi via le VRAI bouton de la palette (pas window.setTool()
    // appelé directement) : si le déclic du bouton lui-même était en cause,
    // ce test le révélerait alors que les autres (setTool direct) non.
    QVERIFY(window.toolDrawRectAct_ != nullptr);
    window.toolDrawRectAct_->trigger();
    QCOMPARE(window.currentTool_, Tool::DrawRectangle);
    QVERIFY(view->boxDrawMode());

    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    const std::size_t before = window.project_.vector_objects.size();
    const QPoint from = view->mapFromScene(QPointF(-5.0, -5.0));
    const QPoint mid = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint to = view->mapFromScene(QPointF(10.0, 8.0));
    QVERIFY2((to - from).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((to - from).manhattanLength())));
    const QRect vpRect = view->viewport()->rect();
    QVERIFY2(vpRect.contains(from),
             qPrintable(QStringLiteral("from=(%1,%2) hors du viewport (%3,%4,%5,%6)")
                            .arg(from.x())
                            .arg(from.y())
                            .arg(vpRect.x())
                            .arg(vpRect.y())
                            .arg(vpRect.width())
                            .arg(vpRect.height())));
    QVERIFY2(vpRect.contains(to),
             qPrintable(QStringLiteral("to=(%1,%2) hors du viewport (%3,%4,%5,%6)")
                            .arg(to.x())
                            .arg(to.y())
                            .arg(vpRect.x())
                            .arg(vpRect.y())
                            .arg(vpRect.width())
                            .arg(vpRect.height())));

    QSignalSpy boxSpy(view, &CanvasView::boxDrawnMm);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view->viewport(), mid);
    QTest::mouseMove(view->viewport(), to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);

    QVERIFY2(boxSpy.count() >= 1,
             qPrintable(QStringLiteral("boxDrawnMm jamais emis (count=%1) -- bug dans "
                                       "CanvasView, pas dans MainWindow::onBoxDrawn")
                            .arg(boxSpy.count())));
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    QVERIFY(obj.paths[0].outer.closed);
}

void MainWindowTest::drawEllipseToolCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawEllipse);
    view->boxDrawnMm(QRectF(0.0, -10.0, 20.0, 10.0),
                     Qt::NoModifier); // 20 x 10 mm -> rx=10, ry=5 mm

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{4});
    for (const auto& n : obj.paths[0].outer.nodes) {
        QVERIFY(n.type == openstitch::geometry::NodeType::Smooth);
        QVERIFY(n.tan_in.has_value());
        QVERIFY(n.tan_out.has_value());
    }
}

void MainWindowTest::drawRegularPolygonToolCreatesUndoableVectorObjectWithConfiguredSides() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygonRegular);
    QVERIFY(window.polygonSidesSpin_ != nullptr);
    window.polygonSidesSpin_->setValue(5); // pentagone, pas la valeur par défaut (6)

    // Cadre 10 x 10 mm, loin du triangle de buildFixture() (rayon d'accroche
    // max 2 mm, cf. findSnapPointMm) pour ne pas interférer avec les coins.
    view->boxDrawnMm(QRectF(20.0, -30.0, 10.0, 10.0), Qt::NoModifier);

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{5});
    QVERIFY(obj.paths[0].outer.closed);

    Vec2um sum{Micrometers{0}, Micrometers{0}};
    for (const auto& n : obj.paths[0].outer.nodes) {
        QVERIFY(n.type == openstitch::geometry::NodeType::Corner);
        sum.x.value += n.pos.x.value;
        sum.y.value += n.pos.y.value;
    }
    const double cx = sum.x.value / 5.0;
    const double cy = sum.y.value / 5.0;
    // Rayon = min(largeur, hauteur) / 2 = 5 mm (cadre carré ici).
    for (const auto& n : obj.paths[0].outer.nodes) {
        const double dx = n.pos.x.value - cx;
        const double dy = n.pos.y.value - cy;
        QVERIFY(std::abs(std::sqrt(dx * dx + dy * dy) - 5'000.0) < 2.0);
    }

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::offsetVectorObjectCoreShrinksIntoNewObjectWithoutTouchingOriginal() {
    MainWindow window;
    Fixture fx = buildFixture();
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);
    window.applyLoadedProject(fx.project);

    const std::size_t before = window.project_.vector_objects.size();
    // Retrait de 2 mm (delta positif, convention geometry::inset_path_set) :
    // carre 10x10 -> 6x6 mm, centre inchange.
    window.offsetVectorObjectCore(squareId, Micrometers{2'000});

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    // L'original n'a pas bouge.
    const auto* original = window.project_.findObject(squareId);
    QVERIFY(original != nullptr);
    QCOMPARE(original->paths[0].outer.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{0}}));

    const auto& created = window.project_.vector_objects.back();
    QVERIFY(created.id != squareId);
    QCOMPARE(created.paths.size(), std::size_t{1});
    QCOMPARE(created.paths[0].outer.nodes.size(), std::size_t{4});
    std::int32_t minX = created.paths[0].outer.nodes[0].pos.x.value;
    std::int32_t maxX = minX;
    std::int32_t minY = created.paths[0].outer.nodes[0].pos.y.value;
    std::int32_t maxY = minY;
    for (const auto& n : created.paths[0].outer.nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
        minY = std::min(minY, n.pos.y.value);
        maxY = std::max(maxY, n.pos.y.value);
    }
    QCOMPARE(minX, std::int32_t{2'000});
    QCOMPARE(maxX, std::int32_t{8'000});
    QCOMPARE(minY, std::int32_t{2'000});
    QCOMPARE(maxY, std::int32_t{8'000});

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::offsetVectorObjectCoreTooLargeCreatesNothing() {
    MainWindow window;
    Fixture fx = buildFixture();
    document::VectorObject square;
    square.id = fx.project.object_ids.next();
    square.name = "Carre";
    geometry::Path outer;
    outer.closed = true;
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{0}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{10'000}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    outer.nodes.push_back(geometry::PathNode{Vec2um{Micrometers{0}, Micrometers{10'000}},
                                             geometry::NodeType::Corner, std::nullopt,
                                             std::nullopt});
    square.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId squareId = square.id;
    fx.project.vector_objects.push_back(square);
    window.applyLoadedProject(fx.project);

    const std::size_t before = window.project_.vector_objects.size();
    // Retrait de 20 mm sur un carre de 10 mm de cote : consomme toute la forme.
    window.offsetVectorObjectCore(squareId, Micrometers{20'000});

    QCOMPARE(window.project_.vector_objects.size(), before); // rien de cree
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::drawEllipseWithShiftConstrainsToCircle() {
    // Défaut trouvé par revue : le test « Maj = cercle » avait dû être écarté
    // à la création de cette fonctionnalité (QTest::keyPress sur une fenêtre
    // non affichée ne met pas à jour QGuiApplication::keyboardModifiers() de
    // façon fiable en offscreen). Corrigé en capturant les modificateurs
    // directement sur l'évènement de relâchement qui termine le glisser
    // (CanvasView::mouseReleaseEvent), transmis par `boxDrawnMm` — plus de
    // dépendance à l'état clavier global, donc testable directement en
    // passant Qt::ShiftModifier.
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawEllipse);
    // Cadre non carré (20 x 6 mm) : sans Maj -> ellipse ; avec Maj -> cercle
    // (côté = le plus petit des deux, 6 mm).
    view->boxDrawnMm(QRectF(0.0, -10.0, 20.0, 6.0), Qt::ShiftModifier);

    const auto& obj = window.project_.vector_objects.back();
    const auto& nodes = obj.paths[0].outer.nodes;
    QCOMPARE(nodes.size(), std::size_t{4});
    // Les 4 sommets (est/nord/ouest/sud) doivent être équidistants du centre :
    // un cercle, pas une ellipse.
    Vec2um sum{Micrometers{0}, Micrometers{0}};
    for (const auto& n : nodes) {
        sum = Vec2um{Micrometers{sum.x.value + n.pos.x.value},
                     Micrometers{sum.y.value + n.pos.y.value}};
    }
    const Vec2um center{Micrometers{sum.x.value / 4}, Micrometers{sum.y.value / 4}};
    double first = -1.0;
    for (const auto& n : nodes) {
        const double d = openstitch::length_um(n.pos - center);
        if (first < 0.0) {
            first = d;
        } else {
            QVERIFY(std::abs(d - first) < 5.0); // tolérance arrondi µm
        }
    }
    QVERIFY(first > 2'900.0 && first < 3'100.0); // rayon attendu ~3 mm
}

void MainWindowTest::drawingTooSmallABoxCreatesNoObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawRectangle);
    view->boxDrawnMm(QRectF(0.0, 0.0, 0.05, 0.05),
                     Qt::NoModifier); // très en dessous du seuil minimal

    QCOMPARE(window.project_.vector_objects.size(), before);
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::drawPolygonSnapsToExistingVertexAndShowsIndicator() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    // Triangle de buildFixture() : sommets modèle (0,0), (1000,0), (0,1000) µm
    // -> scène mm (Y inversé) (0,0), (1,0), (0,-1). Rayon d'accroche par
    // défaut (10 px écran / pixelsPerMm() == 1.0 sans zoom appliqué) = 10 mm.
    window.setTool(Tool::DrawPolygon);
    QVERIFY(window.snapIndicatorItem_ == nullptr || !window.snapIndicatorItem_->isVisible());

    // Survol proche du sommet scène (1,0) : le repère d'accroche apparaît.
    view->cursorMovedMm(QPointF(1.05, 0.03));
    QVERIFY(window.snapIndicatorItem_ != nullptr);
    QVERIFY(window.snapIndicatorItem_->isVisible());

    // Le clic pose EXACTEMENT le sommet existant, pas la position brute.
    view->canvasClickedMm(QPointF(1.05, -0.03));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    QCOMPARE(window.pendingPolygonVertices_.back(), (Vec2um{Micrometers{1000}, Micrometers{0}}));

    // Loin de tout candidat (> 10 mm) : position brute conservée, pas d'accroche.
    view->canvasClickedMm(QPointF(50.0, 50.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2});
    QCOMPARE(window.pendingPolygonVertices_.back(),
             (Vec2um{Micrometers{50'000}, Micrometers{-50'000}}));

    // Sortir de l'outil masque le repère, même sans mouvement de souris.
    window.setTool(Tool::Select);
    QVERIFY(window.snapIndicatorItem_ == nullptr || !window.snapIndicatorItem_->isVisible());
}

void MainWindowTest::drawRectangleSnapsCornersToExistingVectorVertices() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawRectangle);
    // Cadre brut proche des sommets scène (0,-1) et (1,0) du triangle de
    // buildFixture() : chaque coin s'accroche indépendamment à son sommet le
    // plus proche plutôt que de garder la position brute du glisser.
    view->boxDrawnMm(QRectF(QPointF(0.05, -0.95), QPointF(0.95, -0.05)), Qt::NoModifier);

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& nodes = window.project_.vector_objects.back().paths[0].outer.nodes;
    QVERIFY(!nodes.empty());
    std::int32_t minX = nodes.front().pos.x.value;
    std::int32_t maxX = nodes.front().pos.x.value;
    std::int32_t minY = nodes.front().pos.y.value;
    std::int32_t maxY = nodes.front().pos.y.value;
    for (const auto& n : nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
        minY = std::min(minY, n.pos.y.value);
        maxY = std::max(maxY, n.pos.y.value);
    }
    QCOMPARE(minX, std::int32_t{0});
    QCOMPARE(maxX, std::int32_t{1000});
    QCOMPARE(minY, std::int32_t{0});
    QCOMPARE(maxY, std::int32_t{1000});
}

void MainWindowTest::drawPolygonAccumulatesVerticesAndClosesOnDoubleClick() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3});
    QVERIFY(window.polygonPreviewItem_ != nullptr);

    view->canvasDoubleClickedMm(QPointF(10.0, -10.0));

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{3});
    QVERIFY(obj.paths[0].outer.closed);
    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(window.polygonPreviewItem_ == nullptr);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::drawPolygonWithFewerThanThreeVerticesCancelsOnDoubleClick() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(5.0, 0.0)); // seulement deux sommets
    view->canvasDoubleClickedMm(QPointF(5.0, 0.0));

    QCOMPARE(window.project_.vector_objects.size(), before); // rien créé
    QVERIFY(window.pendingPolygonVertices_.empty());         // état nettoyé quand même
    QVERIFY(window.polygonPreviewItem_ == nullptr);
}

void MainWindowTest::switchingToolDuringPolygonDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(5.0, 0.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{2});
    QVERIFY(window.polygonPreviewItem_ != nullptr);

    window.setTool(Tool::Select); // simule Échap / changement d'outil

    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(window.polygonPreviewItem_ == nullptr);
}

void MainWindowTest::polygonDoubleClickDoesNotAddADuplicateVertex() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    // Séquence Qt réelle d'un double-clic : la 2e pression émet AUSSI un clic
    // simple, quasi au même point que le dernier sommet posé -- doit être
    // ignoré (cf. onCanvasClicked) pour ne pas poser un sommet fantôme.
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3}); // pas 4
    view->canvasDoubleClickedMm(QPointF(10.0, -10.0));

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QCOMPARE(window.project_.vector_objects.back().paths[0].outer.nodes.size(), std::size_t{3});
}

// --- Tracé à main levée (lasso) -----------------------------------------
//
// Complète la fonctionnalité « formes dessinées à la main » (rectangle/
// ellipse/polygone) : un quatrième outil, capturant un tracé continu au
// glisser plutôt qu'un cadre élastique ou des clics successifs, simplifié
// (Douglas-Peucker) à la fermeture pour rester exploitable.

void MainWindowTest::drawFreeformCreatesUndoableVectorObject() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawFreeform);

    // Trace approximativement un carré de 10 x 10 mm, avec plusieurs points
    // intermédiaires colinéaires sur chaque bord (comme le ferait un glisser
    // souris réel, un point par évènement de déplacement) : la simplification
    // doit les absorber et ne garder que les coins.
    const std::vector<QPointF> stroke = {
        {0, 0},   {2, 0},   {4, 0},   {6, 0},    {8, 0},   {10, 0},  {10, -2},
        {10, -4}, {10, -6}, {10, -8}, {10, -10}, {8, -10}, {6, -10}, {4, -10},
        {2, -10}, {0, -10}, {0, -8},  {0, -6},   {0, -4},  {0, -2},
    };
    for (const auto& p : stroke) {
        view->freeformPointMm(p);
    }
    QCOMPARE(window.pendingFreeformPoints_.size(), stroke.size());
    QVERIFY(window.freeformPreviewItem_ != nullptr);

    view->freeformStrokeFinished();

    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QVERIFY(obj.paths[0].outer.closed);
    // Simplifié : nettement moins que les 20 points bruts, mais un contour
    // exploitable (>= 3 sommets).
    QVERIFY(obj.paths[0].outer.nodes.size() >= 3);
    QVERIFY(obj.paths[0].outer.nodes.size() < stroke.size());
    // Aire proche de 10 x 10 mm = 100 000 000 µm² (tolérance : arrondi de
    // simplification près des coins).
    const double area = std::abs(openstitch::geometry::signed_area_um2(obj.paths[0].outer));
    QVERIFY(area > 90'000'000.0 && area < 110'000'000.0);
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(*window.selectedObject_, obj.id);
    QVERIFY(window.pendingFreeformPoints_.empty());
    QVERIFY(window.freeformPreviewItem_ == nullptr);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
    window.redo();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
}

void MainWindowTest::drawFreeformWithTooFewPointsCancelsOnRelease() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawFreeform);
    view->freeformPointMm(QPointF(0.0, 0.0));
    view->freeformPointMm(QPointF(1.0, 0.0)); // seulement deux points
    view->freeformStrokeFinished();

    QCOMPARE(window.project_.vector_objects.size(), before); // rien créé
    QVERIFY(window.pendingFreeformPoints_.empty());          // état nettoyé quand même
    QVERIFY(window.freeformPreviewItem_ == nullptr);
}

void MainWindowTest::switchingToolDuringFreeformDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawFreeform);
    view->freeformPointMm(QPointF(0.0, 0.0));
    view->freeformPointMm(QPointF(5.0, 0.0));
    QCOMPARE(window.pendingFreeformPoints_.size(), std::size_t{2});
    QVERIFY(window.freeformPreviewItem_ != nullptr);

    window.setTool(Tool::Select); // simule Échap / changement d'outil

    QVERIFY(window.pendingFreeformPoints_.empty());
    QVERIFY(window.freeformPreviewItem_ == nullptr);
}

// ---------------------------------------------------------------------------
// Colonne satin manuelle (outil DrawSatinColumn)
// ---------------------------------------------------------------------------

void MainWindowTest::manualSatinColumnCreatesRailsAndRungsFromAlternatingPairs() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t embBefore = window.project_.embroidery_objects.size();
    const std::size_t vecBefore = window.project_.vector_objects.size();
    window.setTool(Tool::DrawSatinColumn);

    // Deux paires A/B (scène Y vers le bas -> modèle Y vers le haut, ADR-003) :
    // A1(0,0) B1(0,4) A2(10,0) B2(10,4) en mm modèle.
    view->canvasClickedMm(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{1});
    view->canvasClickedMm(QPointF(0.0, -4.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -4.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{4});
    QVERIFY(window.satinPreviewItem_ != nullptr);

    view->canvasDoubleClickedMm(QPointF(10.0, -4.0));

    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
    QCOMPARE(window.project_.vector_objects.size(), vecBefore + 1); // contour source synthétique
    QVERIFY(window.pendingSatinPoints_.empty());
    QVERIFY(window.satinPreviewItem_ == nullptr);

    const auto& obj = window.project_.embroidery_objects.back();
    QVERIFY(obj.is_satin());
    const auto& satin = std::get<openstitch::document::SatinParams>(obj.params);
    QCOMPARE(satin.rail_a.nodes.size(), std::size_t{2});
    QCOMPARE(satin.rail_b.nodes.size(), std::size_t{2});
    QVERIFY(!satin.rail_a.closed);
    QCOMPARE(satin.rail_a.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{0}}));
    QCOMPARE(satin.rail_a.nodes[1].pos, (Vec2um{Micrometers{10'000}, Micrometers{0}}));
    QCOMPARE(satin.rail_b.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{4'000}}));
    QCOMPARE(satin.rail_b.nodes[1].pos, (Vec2um{Micrometers{10'000}, Micrometers{4'000}}));
    QCOMPARE(satin.rungs.size(), std::size_t{2});
    QCOMPARE(satin.rungs[0].a, (Vec2um{Micrometers{0}, Micrometers{0}}));
    QCOMPARE(satin.rungs[0].b, (Vec2um{Micrometers{0}, Micrometers{4'000}}));
    QCOMPARE(satin.rungs[1].a, (Vec2um{Micrometers{10'000}, Micrometers{0}}));
    QCOMPARE(satin.rungs[1].b, (Vec2um{Micrometers{10'000}, Micrometers{4'000}}));

    QVERIFY(window.selectedEmbroidery_.has_value());
    QCOMPARE(window.selectedEmbroidery_->value, obj.id.value);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Colonne satin (création manuelle)"));

    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    QCOMPARE(window.project_.vector_objects.size(), vecBefore);
    window.redo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
}

void MainWindowTest::manualSatinColumnDropsOrphanPointOnOddCountAtFinish() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    const std::size_t embBefore = window.project_.embroidery_objects.size();
    window.setTool(Tool::DrawSatinColumn);

    // Deux paires completes + un point orphelin (A3 sans B3) : la finalisation
    // doit abandonner ce dernier point plutôt que planter ou bloquer, et créer
    // l'objet avec les deux paires complètes seulement.
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(0.0, -4.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -4.0));
    view->canvasClickedMm(QPointF(20.0, 0.0)); // A3 orphelin
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{5});

    window.finishSatinColumn();

    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 1);
    const auto& satin = std::get<openstitch::document::SatinParams>(
        window.project_.embroidery_objects.back().params);
    QCOMPARE(satin.rungs.size(), std::size_t{2}); // le point orphelin n'a pas créé de 3e paire
    QVERIFY(window.pendingSatinPoints_.empty());
}

void MainWindowTest::switchingToolDuringSatinColumnDrawCancelsIt() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);

    window.setTool(Tool::DrawSatinColumn);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(0.0, -4.0));
    QCOMPARE(window.pendingSatinPoints_.size(), std::size_t{2});
    QVERIFY(window.satinPreviewItem_ != nullptr);

    window.setTool(Tool::Select); // simule Échap / changement d'outil

    QVERIFY(window.pendingSatinPoints_.empty());
    QVERIFY(window.satinPreviewItem_ == nullptr);
    QVERIFY(!window.undoStack_.canUndo()); // rien n'a été créé
}

void MainWindowTest::satinRailEditModeDragsNodeAndUndoRestoresIt() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* action = window.findChild<QAction*>(QStringLiteral("action_satinRailEditMode"));
    QVERIFY(action != nullptr);
    QVERIFY(!action->isEnabled());
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(action->isEnabled());
    action->setChecked(true);
    QVERIFY(action->isChecked());
    QCOMPARE(window.railEditTarget_->value, fx.embroideryId.value);

    // Rail A (2 noeuds) + rail B (2 noeuds) : 4 poignées.
    QList<NodeHandleItem*> handles;
    for (QGraphicsItem* item : window.baseItems_) {
        if (auto* handle = dynamic_cast<NodeHandleItem*>(item)) {
            handles.push_back(handle);
        }
    }
    QCOMPARE(handles.size(), 4);

    window.view_->resetTransform();
    window.view_->scale(40.0, 40.0);
    window.view_->centerOn(QPointF(0.0, 0.0));
    auto it = std::find_if(handles.begin(), handles.end(), [](const NodeHandleItem* handle) {
        return std::abs(handle->scenePos().x()) < 0.01 && std::abs(handle->scenePos().y()) < 0.01;
    });
    QVERIFY(it != handles.end());
    auto* first = *it; // noeud de départ du rail A, en (0,0)

    const QPoint start = window.view_->mapFromScene(first->scenePos());
    const int availableRight = window.view_->viewport()->width() - 1 - start.x();
    const int availableLeft = start.x();
    const int direction = availableRight >= availableLeft ? 1 : -1;
    const int movement = std::min(40, std::max(availableRight, availableLeft));
    const QPoint end = start + QPoint(movement * direction, 0);
    const QPoint middle = (start + end) / 2;
    QVERIFY(window.view_->viewport()->rect().contains(start));
    QVERIFY(window.view_->viewport()->rect().contains(end));
    QVERIFY2((end - start).manhattanLength() >= QApplication::startDragDistance(),
             qPrintable(QStringLiteral("déplacement écran insuffisant: %1 px")
                            .arg((end - start).manhattanLength())));
    QCOMPARE(dynamic_cast<NodeHandleItem*>(window.view_->itemAt(start)), first);
    QTest::mousePress(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window.view_->viewport(), middle);
    QTest::mouseMove(window.view_->viewport(), end);
    QTest::mouseRelease(window.view_->viewport(), Qt::LeftButton, Qt::NoModifier, end);

    QTRY_VERIFY_WITH_TIMEOUT(window.undoStack_.canUndo(), 1000);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Déplacement de nœud de rail satin"));
    const auto& moved = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QVERIFY(moved.rail_a.nodes[0].pos.x.value != 0);

    window.undo();
    const auto& restored = std::get<openstitch::document::SatinParams>(
        window.project_.findEmbroidery(fx.embroideryId)->params);
    QCOMPARE(restored.rail_a.nodes[0].pos, (Vec2um{Micrometers{0}, Micrometers{0}}));

    action->setChecked(false);
    QVERIFY(!window.railEditTarget_.has_value());
    QCoreApplication::processEvents();
}

void MainWindowTest::satinCutLineToolSplitsSelectedShapeIntoTwoSatinColumns() {
    MainWindow window;
    Fixture fx = buildFixture();

    // Rectangle allongé 40 x 4 mm centré sur l'origine (même famille que le
    // rectangle "Suitable" de createSatinObject() ci-dessus, juste assez
    // court pour que chaque moitié post-coupe (~20 x 4 mm) reste elle-même
    // sans ambiguïté satinable).
    document::VectorObject rect;
    rect.id = fx.project.object_ids.next();
    rect.name = "Rectangle allonge";
    geometry::Path outer;
    outer.closed = true;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    outer.nodes = {corner(-20'000, -2'000), corner(20'000, -2'000), corner(20'000, 2'000),
                   corner(-20'000, 2'000)};
    rect.paths.push_back(geometry::PathSet{outer, {}});
    const ObjectId rectId = rect.id;
    fx.project.vector_objects.push_back(rect);

    window.applyLoadedProject(fx.project);
    window.selectedObject_ = rectId;
    window.setTool(Tool::DrawSatinCutLine);

    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));

    // Glisser vertical à x=0, de part et d'autre du rectangle (scène en mm,
    // Y vers le bas) : traverse toute la largeur, à mi-longueur.
    const QPoint anchorVp = view->mapFromScene(QPointF(0.0, 3.0));
    const QPoint midVp = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint handleVp = view->mapFromScene(QPointF(0.0, -3.0));
    QVERIFY(view->viewport()->rect().contains(anchorVp));
    QVERIFY(view->viewport()->rect().contains(handleVp));

    const std::size_t embBefore = window.project_.embroidery_objects.size();

    // createSatinObjectWithCutLine() ouvre une QDialog modale (densité/
    // compensation/sous-couche), identique à createSatinObject() : même
    // mécanisme de pilotage différé (exec() pompe la boucle en interne).
    QTimer::singleShot(0, &window, [] {
        if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dlg->accept();
        }
    });
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, anchorVp);
    QTest::mouseMove(view->viewport(), midVp);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, handleVp);

    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 2);
    for (std::size_t i = embBefore; i < window.project_.embroidery_objects.size(); ++i) {
        const auto& emb = window.project_.embroidery_objects[i];
        QCOMPARE(emb.source_vector, rectId);
        QVERIFY(emb.is_satin());
    }
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Colonne satin (ligne de coupe)"));

    // Un seul geste = une seule coupe (v1) : succès -> retour auto sur
    // Sélection, pour enchaîner naturellement sur la retouche du résultat.
    QCOMPARE(window.currentTool_, Tool::Select);

    QVERIFY(window.undoStack_.canUndo());
    window.undo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    window.redo();
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore + 2);
}

// ---------------------------------------------------------------------------
// Réponses à l'audit UI (retour utilisateur en usage réel)
// ---------------------------------------------------------------------------

void MainWindowTest::bezierToolClickCreatesCornerNodes() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawBezier);
    view->resetTransform();
    view->scale(4.0, 4.0);
    view->centerOn(QPointF(0.0, 0.0));

    // Trois clics NETS (pas de glisser) : trois nœuds Coin, sans poignées.
    const auto clickAt = [&](QPointF sceneMm) {
        const QPoint vp = view->mapFromScene(sceneMm);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp);
    };
    clickAt(QPointF(0.0, 0.0));
    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{1});
    clickAt(QPointF(20.0, 0.0));
    clickAt(QPointF(20.0, -20.0));
    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{3});
    for (const auto& node : window.pendingBezierNodes_) {
        QVERIFY(node.type == openstitch::geometry::NodeType::Corner);
        QVERIFY(!node.tan_in.has_value());
        QVERIFY(!node.tan_out.has_value());
    }

    window.finishBezier();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& obj = window.project_.vector_objects.back();
    QCOMPARE(obj.paths[0].outer.nodes.size(), std::size_t{3});
    QVERIFY(obj.paths[0].outer.closed);
    QVERIFY(window.pendingBezierNodes_.empty());

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::bezierToolDragCreatesSmoothNodeWithSymmetricHandles() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.setTool(Tool::DrawBezier);
    view->resetTransform();
    view->scale(4.0, 4.0);
    view->centerOn(QPointF(0.0, 0.0));

    const QPoint anchorVp = view->mapFromScene(QPointF(0.0, 0.0));
    const QPoint handleVp = anchorVp + QPoint(40, 0); // glisser net, bien au-dessus du seuil
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, anchorVp);
    QTest::mouseMove(view->viewport(), anchorVp + QPoint(20, 0));
    QTest::mouseMove(view->viewport(), handleVp);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, handleVp);

    QCOMPARE(window.pendingBezierNodes_.size(), std::size_t{1});
    const auto& node = window.pendingBezierNodes_[0];
    QVERIFY(node.type == openstitch::geometry::NodeType::Smooth);
    QVERIFY(node.tan_out.has_value());
    QVERIFY(node.tan_in.has_value());
    // Poignées symétriques : tan_in = -tan_out exactement.
    QCOMPARE(node.tan_in->x.value, -node.tan_out->x.value);
    QCOMPARE(node.tan_in->y.value, -node.tan_out->y.value);
    QVERIFY(node.tan_out->x.value > 0); // glissé vers la droite

    window.cancelBezierDraw();
    QVERIFY(window.pendingBezierNodes_.empty());
}

void MainWindowTest::finishDrawActionAndEnterKeyBothClosePolygon() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    QVERIFY(window.finishDrawAct_ != nullptr);

    const std::size_t before = window.project_.vector_objects.size();
    window.setTool(Tool::DrawPolygon);
    QVERIFY(!window.finishDrawAct_->isEnabled()); // rien tracé encore

    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QVERIFY(window.finishDrawAct_->isEnabled());

    // Le bouton Terminer clôt le polygone -- pas seulement le double-clic
    // (défaut remonté en usage réel : aucun moyen visible de valider la forme).
    window.finishDrawAct_->trigger();
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(!window.finishDrawAct_->isEnabled());
    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);

    // Le raccourci Entrée (QShortcut dédié) fait de même -- déclenché via le
    // système de méta-objets Qt plutôt qu'un évènement clavier synthétique :
    // QTest::keyPress n'est pas fiable en offscreen pour les raccourcis
    // fenêtre (cf. commentaire de drawEllipseWithShiftConstrainsToCircle).
    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QShortcut* enterShortcut = nullptr;
    for (auto* sc : window.findChildren<QShortcut*>()) {
        if (sc->key() == QKeySequence(Qt::Key_Return)) {
            enterShortcut = sc;
            break;
        }
    }
    QVERIFY(enterShortcut != nullptr);
    QMetaObject::invokeMethod(enterShortcut, "activated");
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    QVERIFY(window.pendingPolygonVertices_.empty());
}

void MainWindowTest::deleteVectorObjectRemovesShapeAndDependentEmbroidery() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    const std::size_t vecBefore = window.project_.vector_objects.size();
    const std::size_t embBefore = window.project_.embroidery_objects.size();
    window.deleteVectorObject(fx.vectorId);

    QCOMPARE(window.project_.vector_objects.size(), vecBefore - 1);
    QCOMPARE(window.project_.embroidery_objects.size(),
             embBefore - 1); // broderie liee partie aussi
    QVERIFY(window.project_.findObject(fx.vectorId) == nullptr);
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) == nullptr);
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Supprimer l'objet vectoriel"));

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), vecBefore);
    QCOMPARE(window.project_.embroidery_objects.size(), embBefore);
    QVERIFY(window.project_.findObject(fx.vectorId) != nullptr);
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId) != nullptr);
}

void MainWindowTest::duplicateVectorObjectOffsetsCopyAndIsUndoable() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);

    const std::size_t before = window.project_.vector_objects.size();
    const auto* original = window.project_.findObject(fx.vectorId);
    QVERIFY(original != nullptr);
    const Vec2um originalFirstNode = original->paths[0].outer.nodes[0].pos;

    window.duplicateVectorObject(fx.vectorId);
    QCOMPARE(window.project_.vector_objects.size(), before + 1);
    const auto& copy = window.project_.vector_objects.back();
    QVERIFY(copy.id.value != fx.vectorId.value);
    QVERIFY(copy.paths[0].outer.nodes[0].pos != originalFirstNode); // décalée, pas superposée
    QVERIFY(window.selectedObject_.has_value());
    QCOMPARE(window.selectedObject_->value, copy.id.value);

    window.undo();
    QCOMPARE(window.project_.vector_objects.size(), before);
}

void MainWindowTest::satinEditModeTogglesBothUnderlyingModes() {
    MainWindow window;
    const Fixture fx = buildSatinGuideFixture();
    window.applyLoadedProject(fx.project);

    QVERIFY(window.satinEditModeAct_ != nullptr);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    QVERIFY(window.satinEditModeAct_->isEnabled());
    QVERIFY(!window.satinGuideModeAct_->isChecked());
    QVERIFY(!window.railEditModeAct_->isChecked());

    window.satinEditModeAct_->setChecked(true);
    QVERIFY(window.satinGuideModeAct_->isChecked());
    QVERIFY(window.railEditModeAct_->isChecked());
    QVERIFY(window.satinGuideTarget_.has_value());
    QVERIFY(window.railEditTarget_.has_value());

    window.satinEditModeAct_->setChecked(false);
    QVERIFY(!window.satinGuideModeAct_->isChecked());
    QVERIFY(!window.railEditModeAct_->isChecked());
}

void MainWindowTest::emptyStateHidesOnceContentExistsEvenWithoutImage() {
    MainWindow window;
    window.resize(900, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QVERIFY(window.emptyState_ != nullptr);
    QVERIFY(!window.project_.hasImage());
    window.updateActions();
    QVERIFY(window.emptyState_->isVisible()); // aucun contenu -> pastille visible

    // Dessine un rectangle SANS jamais ouvrir d'image : flux purement
    // vectoriel valide (canevas par défaut 100x100 mm, cf. document::Canvas).
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(view != nullptr);
    window.setTool(Tool::DrawRectangle);
    view->boxDrawnMm(QRectF(0.0, 0.0, 10.0, 10.0), Qt::NoModifier);

    QVERIFY(!window.project_.vector_objects.empty());
    QVERIFY(!window.project_.hasImage());      // toujours aucune image
    QVERIFY(!window.emptyState_->isVisible()); // mais la pastille s'est effacée
}

} // namespace openstitch::desktop

namespace {

Fixture buildTatamiSquareFixture() {
    Fixture fx = buildRunningSquareFixture();
    openstitch::document::TatamiParams tp;
    tp.angle = openstitch::Angle{0.5};
    tp.row_spacing = Micrometers{450};
    fx.project.embroidery_objects[0].params = tp;
    return fx;
}

} // namespace

namespace openstitch::desktop {

void MainWindowTest::convertingTatamiToDirectionalIsUndoable() {
    MainWindow window;
    const Fixture fx = buildTatamiSquareFixture();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();

    window.convertToDirectional(fx.embroideryId);
    const auto* emb = window.project_.findEmbroidery(fx.embroideryId);
    QVERIFY(emb != nullptr && emb->is_directional());
    const auto& dp = std::get<openstitch::document::DirectionalFillParams>(emb->params);
    QCOMPARE(dp.row_spacing.value, 450);        // réglages du tatami repris
    QCOMPARE(dp.guides.size(), std::size_t{1}); // guide initial à l'angle du tatami
    QCOMPARE(dp.seed, static_cast<std::uint32_t>(fx.embroideryId.value));
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Type : remplissage directionnel"));
    QVERIFY(window.sequence_.has_value()); // la génération a bien tourné

    window.undo();
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId)->is_tatami());
}

void MainWindowTest::directionGuideToolDrawsGuidesAndBreakLinesThroughUndoStack() {
    MainWindow window;
    const Fixture fx = buildTatamiSquareFixture();
    window.applyLoadedProject(fx.project);
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();

    auto* mode = window.findChild<QAction*>(QStringLiteral("action_directionGuideMode"));
    QVERIFY(mode != nullptr);
    QVERIFY(!mode->isEnabled()); // tatami : pas de guides
    window.convertToDirectional(fx.embroideryId);
    QVERIFY(mode->isEnabled());
    mode->setChecked(true);
    QVERIFY(window.directionGuideTarget_.has_value());
    // Une poignée par nœud du guide initial (2 nœuds).
    int handles = 0;
    for (QGraphicsItem* item : window.baseItems_) {
        if (dynamic_cast<NodeHandleItem*>(item) != nullptr) {
            ++handles;
        }
    }
    QCOMPARE(handles, 2);

    const auto params = [&] {
        return std::get<openstitch::document::DirectionalFillParams>(
            window.project_.findEmbroidery(fx.embroideryId)->params);
    };

    // Guide : trois clics (scène en mm, Y vers le bas) puis Terminer.
    window.findChild<QAction*>(QStringLiteral("action_drawDirectionGuide"))->trigger();
    QCOMPARE(window.currentTool_, Tool::DrawDirectionGuide);
    window.onCanvasClicked(QPointF(1.0, -2.0));
    window.onCanvasClicked(QPointF(5.0, -6.0));
    window.onCanvasClicked(QPointF(9.0, -2.0));
    window.finishDirectionGuide();
    QCOMPARE(params().guides.size(), std::size_t{2});
    const auto afterGuide = params(); // copie : params() renvoie une valeur
    const auto& drawn = afterGuide.guides.back();
    QCOMPARE(drawn.nodes.size(), std::size_t{3});
    QCOMPARE(drawn.nodes[1].pos, (Vec2um{Micrometers{5'000}, Micrometers{6'000}}));
    QVERIFY(drawn.nodes[1].type == openstitch::geometry::NodeType::Smooth); // courbe lisse
    QCOMPARE(QString::fromStdString(window.undoStack_.undoName()),
             QStringLiteral("Tracer un guide de direction"));

    // Ligne de rupture : polyligne à angles vifs.
    window.findChild<QAction*>(QStringLiteral("action_drawBreakLine"))->trigger();
    window.onCanvasClicked(QPointF(5.0, 0.5));
    window.onCanvasClicked(QPointF(5.0, -10.5));
    window.finishDirectionGuide();
    QCOMPARE(params().break_lines.size(), std::size_t{1});
    QVERIFY(!params().break_lines[0].nodes[0].tan_out.has_value());

    // Un tracé à moins de deux points ne crée rien.
    window.onCanvasClicked(QPointF(2.0, -2.0));
    window.finishDirectionGuide();
    QCOMPARE(params().break_lines.size(), std::size_t{1});

    window.undo();
    QCOMPARE(params().break_lines.size(), std::size_t{0});
    window.undo();
    QCOMPARE(params().guides.size(), std::size_t{1});
    window.redo();
    QCOMPARE(params().guides.size(), std::size_t{2});

    // Annuler la conversion fait sortir proprement du mode guides.
    window.undo(); // guide
    window.undo(); // conversion
    QVERIFY(window.project_.findEmbroidery(fx.embroideryId)->is_tatami());
    QVERIFY(!mode->isChecked());
    QVERIFY(!window.directionGuideTarget_.has_value());
    QCOMPARE(window.currentTool_, Tool::Select);
}

namespace {

openstitch::image::Image gradient_image(int w, int h, std::uint8_t seed) {
    openstitch::image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = img.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            px[0] = static_cast<std::uint8_t>(seed + x * 13);
            px[1] = static_cast<std::uint8_t>(y * 17);
            px[2] = static_cast<std::uint8_t>(seed ^ (x * y));
            px[3] = 255;
        }
    }
    return img;
}

} // namespace

void MainWindowTest::processedImageCacheFollowsOpsUndoRedoAndProjectChange() {
    using openstitch::image::apply_pipeline;
    MainWindow window;
    document::Project first;
    first.original = gradient_image(16, 12, 3);
    window.applyLoadedProject(first);
    QVERIFY(window.processed_.rgba == first.original.rgba);

    // Nouvelle opération (vrai chemin : commande + rafraîchissement).
    window.undoStack_.execute(std::make_unique<commands::AppendImageOpCommand>(image::FlipOp{true}),
                              window.project_);
    window.refreshImage();
    const auto flipped = apply_pipeline(first.original, window.project_.ops);
    QVERIFY(flipped.has_value());
    QVERIFY(flipped->rgba != first.original.rgba);
    QVERIFY(window.processed_.rgba == flipped->rgba);

    // Mutation sans rapport avec l'image : résultat inchangé et toujours exact.
    document::VectorObject shape;
    shape.id = window.project_.object_ids.next();
    window.undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(shape),
                              window.project_);
    window.refreshImage();
    QVERIFY(window.processed_.rgba == flipped->rgba);

    window.undo(); // objet
    window.undo(); // symétrie
    QVERIFY(window.project_.ops.empty());
    QVERIFY(window.processed_.rgba == first.original.rgba);
    window.redo(); // symétrie
    QVERIFY(window.processed_.rgba == flipped->rgba);

    // Autre projet, MÊME pile d'opérations, autre image source : recalculé.
    document::Project second;
    second.original = gradient_image(16, 12, 99);
    second.ops = {image::FlipOp{true}};
    window.applyLoadedProject(second);
    const auto expected = apply_pipeline(second.original, second.ops);
    QVERIFY(expected.has_value());
    QVERIFY(window.processed_.rgba == expected->rgba);
}

void MainWindowTest::newProjectActionIsInFileMenuWithStandardShortcut() {
    MainWindow window;
    auto* act = window.findChild<QAction*>(QStringLiteral("action_newProject"));
    QVERIFY(act != nullptr);
    QCOMPARE(act->shortcut(), QKeySequence(QKeySequence::New));
}

void MainWindowTest::newProjectResetsDocumentEditModesAndPanels() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    QVERIFY(!window.isWindowModified()); // projet fraîchement chargé = propre
    const auto generationAfterLoad = window.documentGeneration_;

    auto* docPanel = window.findChild<DocumentPanel*>();
    auto* view = window.findChild<CanvasView*>();
    QVERIFY(docPanel != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(objectsList(*docPanel)->topLevelItemCount() > 0);

    // État d'édition typique en cours au moment du « Nouveau » : objets
    // sélectionnés, mode d'édition des points actif sur une cible, polygone à
    // moitié posé (aperçu vivant dans la scène, qui survit au document).
    window.selectedObject_ = fx.vectorId;
    window.selectedEmbroidery_ = fx.embroideryId;
    window.updateActions();
    auto* editAct = window.findChild<QAction*>(QStringLiteral("action_stitchEditMode"));
    QVERIFY(editAct != nullptr);
    editAct->setChecked(true);
    QVERIFY(window.stitchEditTarget_.has_value());

    window.setTool(Tool::DrawPolygon);
    view->canvasClickedMm(QPointF(0.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, 0.0));
    view->canvasClickedMm(QPointF(10.0, -10.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3});
    QVERIFY(window.polygonPreviewItem_ != nullptr);

    window.newProject(); // document propre : aucun dialogue de garde

    QVERIFY(!window.project_.hasImage());
    QVERIFY(window.project_.vector_objects.empty());
    QVERIFY(window.project_.embroidery_objects.empty());
    QVERIFY(!window.project_.segmentation.has_value());
    QVERIFY(!window.undoStack_.canUndo());
    QVERIFY(!window.sequence_.has_value());
    QVERIFY(!window.project_.imported_design.has_value());
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(!window.selectedEmbroidery_.has_value());
    QVERIFY(!window.selectedRegion_.has_value());
    QVERIFY(!editAct->isChecked());
    QVERIFY(!window.stitchEditTarget_.has_value());
    QVERIFY(!window.stitchEditView_.has_value());
    QVERIFY(window.pendingPolygonVertices_.empty());
    QVERIFY(window.polygonPreviewItem_ == nullptr);
    QCOMPARE(window.currentTool_, Tool::Select);
    QCOMPARE(window.simStep_, -1);
    // Les panneaux sont vidés : refreshImage() sort tôt sans image, c'est
    // applyLoadedProject qui doit les rafraîchir dans ce cas.
    QCOMPARE(objectsList(*docPanel)->topLevelItemCount(), 0);
    QVERIFY(window.documentGeneration_ != generationAfterLoad);
    QVERIFY(!window.isWindowModified()); // document vierge = propre
}

void MainWindowTest::newProjectOnModifiedDocumentCancelsOrDiscardsAsChosen() {
    MainWindow window;
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    window.setWindowModified(true); // travail non enregistré en cours

    // Annuler : le document en cours est intact et toujours signalé modifié.
    clickModalDialogButton(&window, QStringLiteral("Annuler"), 1);
    window.newProject();
    QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    QVERIFY(window.isWindowModified());

    // Ne pas enregistrer : le document est bien remplacé par un document vierge.
    clickModalDialogButton(&window, QStringLiteral("Ne pas enregistrer"), 1);
    window.newProject();
    QVERIFY(window.project_.vector_objects.empty());
    QVERIFY(!window.isWindowModified());
}

void MainWindowTest::saveActionsAreInFileMenuWithStandardShortcuts() {
    MainWindow window;
    auto* save = window.findChild<QAction*>(QStringLiteral("action_saveProject"));
    auto* saveAs = window.findChild<QAction*>(QStringLiteral("action_saveProjectAs"));
    QVERIFY(save != nullptr);
    QVERIFY(saveAs != nullptr);
    QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
    QCOMPARE(saveAs->shortcut(), QKeySequence(QKeySequence::SaveAs));
    // Document jamais enregistré : aucune cible, et le titre l'annonce.
    QVERIFY(window.currentProjectPath_.isEmpty());
    QVERIFY(window.windowTitle().contains(QStringLiteral("Sans titre")));
}

void MainWindowTest::savingAnOpenedProjectRewritesItWithoutAskingAPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("projet.osp"));
    const std::filesystem::path fsPath(path.toStdWString());
    const Fixture fx = buildFixture();
    QVERIFY(project_io::save_project(fsPath, fx.project).has_value());

    MainWindow window;
    QVERIFY(window.openProjectFile(path));
    QCOMPARE(window.currentProjectPath_, path);
    QVERIFY(window.windowTitle().contains(QStringLiteral("projet.osp")));
    QVERIFY(!window.isWindowModified());

    // Travail en cours puis Ctrl+S : le fichier ouvert est réécrit sans
    // dialogue. Si saveProject() en ouvrait un (le défaut corrigé par
    // HP-FILE-002), ce test se bloquerait sur un QFileDialog modal.
    QVERIFY(!window.project_.vector_objects.empty());
    window.project_.vector_objects.front().name = "Renomme";
    window.setWindowModified(true);
    window.saveProject();
    QVERIFY(!window.isWindowModified());
    QCOMPARE(window.currentProjectPath_, path);

    auto reloaded = project_io::load_project(fsPath);
    QVERIFY(reloaded.has_value());
    QVERIFY(!reloaded->vector_objects.empty());
    QCOMPARE(QString::fromStdString(reloaded->vector_objects.front().name),
             QStringLiteral("Renomme"));
}

void MainWindowTest::newProjectForgetsTheSaveTargetAndResetsTheTitle() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("projet.osp"));
    const std::filesystem::path fsPath(path.toStdWString());
    const Fixture fx = buildFixture();
    QVERIFY(project_io::save_project(fsPath, fx.project).has_value());

    MainWindow window;
    QVERIFY(window.openProjectFile(path));
    QCOMPARE(window.currentProjectPath_, path);

    // Document propre : « Nouveau » passe sans garde. Le document vierge ne
    // doit plus viser le fichier précédent, sinon un Ctrl+S l'écraserait.
    window.newProject();
    QVERIFY(window.currentProjectPath_.isEmpty());
    QVERIFY(window.windowTitle().contains(QStringLiteral("Sans titre")));

    auto untouched = project_io::load_project(fsPath);
    QVERIFY(untouched.has_value());
    QCOMPARE(untouched->vector_objects.size(), fx.project.vector_objects.size());
}

void MainWindowTest::addRecentFileDeduplicatesAndTruncatesToTen() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 11 chemins distincts, réellement présents sur disque : canonicalFilePath()
    // (utilisé par addRecentFile pour dédupliquer) ne résout que des fichiers
    // existants.
    QStringList paths;
    for (int i = 0; i < 11; ++i) {
        const QString path = dir.filePath(QStringLiteral("f%1.osp").arg(i));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        paths.append(path);
    }

    QStringList current;
    for (const QString& path : paths) {
        current = addRecentFile(std::move(current), path);
    }
    QCOMPARE(current.size(), 10);
    // Le plus récemment ajouté (paths.last()) en tête, le plus ancien
    // (paths.first()) abandonné par la troncature à 10.
    QCOMPARE(current.first(), paths.last());
    QVERIFY(!current.contains(paths.first()));

    // Réinsertion d'une entrée déjà présente, sous une orthographe différente
    // mais canoniquement égale (segment "." redondant, éliminé par
    // QFileInfo::canonicalFilePath()) : déplacée en tête plutôt que dupliquée
    // -- toujours 10 entrées.
    const QString reAdded = current.at(3);
    const QFileInfo reAddedInfo(reAdded);
    const QString spelledDifferently =
        reAddedInfo.absolutePath() + QStringLiteral("/./") + reAddedInfo.fileName();
    QVERIFY(spelledDifferently != reAdded);
    current = addRecentFile(std::move(current), spelledDifferently);
    QCOMPARE(current.size(), 10);
    QCOMPARE(current.first(), spelledDifferently);
    QVERIFY(!current.contains(reAdded));
}

void MainWindowTest::pruneMissingRecentFilesRemovesDeletedPathsPreservingOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString existing = dir.filePath(QStringLiteral("existing.osp"));
    const QString deleted = dir.filePath(QStringLiteral("deleted.osp"));
    QFile existingFile(existing);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    existingFile.close();
    QFile deletedFile(deleted);
    QVERIFY(deletedFile.open(QIODevice::WriteOnly));
    deletedFile.close();
    QVERIFY(QFile::remove(deleted));

    const QStringList pruned = pruneMissingRecentFiles(QStringList{deleted, existing});
    QCOMPARE(pruned, QStringList({existing}));
}

void MainWindowTest::recentFilesAndMenuReflectTwoSavesAndOpensInOrder() {
    // Isole cette liste des autres tests du même binaire : QSettings est
    // partagé (fichier INI temporaire unique posé une fois par initTestCase).
    QSettings().remove(QStringLiteral("recent/files"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("premier.osp"));
    const QString secondPath = dir.filePath(QStringLiteral("second.osp"));
    const std::filesystem::path secondFsPath(secondPath.toStdWString());
    QVERIFY(project_io::save_project(secondFsPath, document::Project{}).has_value());

    MainWindow window;
    QCoreApplication::processEvents(); // consomme le refreshRecentFilesUi() initial (liste vide)

    // Enregistrer (saveProjectToPath) puis ouvrir (openProjectFile) sont les
    // deux seuls appelants de setCurrentProjectPath -- ce test couvre les deux.
    QVERIFY(window.saveProjectToPath(firstPath));
    QCoreApplication::processEvents();

    QVERIFY(window.openProjectFile(secondPath));
    QCoreApplication::processEvents();

    QCOMPARE(window.recentFiles_, QStringList({secondPath, firstPath}));
    QVERIFY(window.recentMenu_ != nullptr);
    const auto actions = window.recentMenu_->actions();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.at(0)->toolTip(), secondPath);
    QCOMPARE(actions.at(1)->toolTip(), firstPath);
}

void MainWindowTest::clickingRecentButtonForDeletedFileWarnsAndPrunesWithoutCrashing() {
    QSettings().remove(QStringLiteral("recent/files"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.filePath(QStringLiteral("premier.osp"));
    const QString secondPath = dir.filePath(QStringLiteral("second.osp"));

    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCoreApplication::processEvents(); // consomme le refreshRecentFilesUi() initial (liste vide)

    // Document resté vide (aucune image, aucun objet) : l'écran d'accueil
    // reste affiché tout du long -- condition nécessaire pour un vrai clic
    // QTest sur un de ses boutons.
    QVERIFY(window.saveProjectToPath(firstPath));
    QCoreApplication::processEvents();
    QVERIFY(window.saveProjectToPath(secondPath));
    QCoreApplication::processEvents();

    QCOMPARE(window.recentFiles_, QStringList({secondPath, firstPath}));
    QVERIFY(window.emptyState_->isVisible());

    QVERIFY(QFile::remove(firstPath));

    QPushButton* target = nullptr;
    for (QPushButton* button : window.emptyState_->findChildren<QPushButton*>()) {
        if (button->toolTip() == firstPath) {
            target = button;
            break;
        }
    }
    QVERIFY(target != nullptr);

    // openProjectFile() échoue sur le fichier supprimé -> QMessageBox::warning
    // (avertissement déjà existant) -- armé avant le clic, comme les autres
    // tests de dialogue modal de ce fichier.
    autoDismissModalDialogs(&window);
    QTest::mouseClick(target, Qt::LeftButton);
    // Le clic déclenche : lambda du bouton -> confirmDiscardChanges (document
    // propre, passe sans garde) -> openProjectFile -> échec ->
    // refreshRecentFilesUi() différée (QTimer::singleShot). C'est la
    // régression testée ici : sans ce report, le rafraîchissement détruirait
    // `target` alors que son propre gestionnaire clicked() est encore sur la
    // pile d'appels.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    QCOMPARE(window.recentFiles_, QStringList({secondPath}));
}

void MainWindowTest::autosaveTickWritesASeparateFileAndLeavesTheUserFileUntouched() {
    clearAutosaveDir();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userFile = dir.filePath(QStringLiteral("user.osp"));

    MainWindow window;
    QCoreApplication::processEvents(); // consomme checkAutosaveRecovery() initial (dossier vide)

    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    QVERIFY(window.saveProjectToPath(userFile)); // "fichier utilisateur" réel, non vide
    QCoreApplication::processEvents();
    QVERIFY(!window.isWindowModified());
    // Une modification après l'enregistrement : condition nécessaire pour
    // que le tick écrive quoi que ce soit (cf. test suivant).
    window.setWindowModified(true);
    QVERIFY(window.isWindowModified());

    QFile before(userFile);
    QVERIFY(before.open(QIODevice::ReadOnly));
    const QByteArray beforeContent = before.readAll();
    before.close();
    const QDateTime beforeModified = QFileInfo(userFile).lastModified();

    window.onAutosaveTick();

    // (a) Le fichier utilisateur est strictement inchangé (contenu et date).
    QFile after(userFile);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), beforeContent);
    after.close();
    QCOMPARE(QFileInfo(userFile).lastModified(), beforeModified);

    // (b) Un fichier DISTINCT est apparu sous le dossier autosave.
    const auto slot = slotFor(userFile);
    QVERIFY(slot.osp_path != userFile);
    QVERIFY2(QFile::exists(slot.osp_path),
             qPrintable(QStringLiteral("slot=%1 status=%2 current=%3")
                            .arg(slot.osp_path, window.statusBar()->currentMessage(),
                                 window.currentProjectPath_)));

    // (c) currentProjectPath_ reste le fichier utilisateur après le tick :
    // l'autosave ne compte jamais comme un enregistrement.
    QCOMPARE(window.currentProjectPath_, userFile);

    clearAutosaveDir();
}

void MainWindowTest::autosaveTickSkipsWhenDocumentUnmodifiedOrEmpty() {
    clearAutosaveDir();

    MainWindow window;
    QCoreApplication::processEvents();

    // Document vierge (vide) et non modifié : rien à protéger, rien écrit.
    QVERIFY(!window.isWindowModified());
    window.onAutosaveTick();
    QVERIFY(scanForRecoverableAutosaves().empty());

    // Document encore vide mais marqué "modifié" (cas limite) : la garde
    // porte sur le contenu, pas seulement sur isWindowModified().
    window.setWindowModified(true);
    window.onAutosaveTick();
    QVERIFY(scanForRecoverableAutosaves().empty());

    // Document non vide mais PAS modifié (cas réel : juste après un
    // enregistrement, applyLoadedProject() laisse setWindowModified(false)).
    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    QVERIFY(!window.isWindowModified());
    window.onAutosaveTick();
    QVERIFY(scanForRecoverableAutosaves().empty());

    clearAutosaveDir();
}

void MainWindowTest::autosaveRecoveryAcceptLoadsAsUntitledDocumentAndPurgesSlot() {
    clearAutosaveDir();

    MainWindow window;
    // Consomme checkAutosaveRecovery() initial pendant que le dossier est
    // encore vide.
    QCoreApplication::processEvents();

    // Simule un créneau laissé par un arrêt anormal : écrit directement via
    // writeAutosave() -- aucun MainWindow n'était vivant au moment du
    // "plantage", comme en réalité.
    const Fixture fx = buildFixture();
    const QString originalPath = QStringLiteral("C:/ancien/projet.osp"); // chemin affiché seulement
    const auto slot = slotFor(originalPath);
    QVERIFY(writeAutosave(slot, fx.project, originalPath).has_value());

    clickModalDialogButton(&window, QStringLiteral("Récupérer"));
    window.checkAutosaveRecovery();

    // Chargé en tant que document SANS NOM -- jamais réassocié au chemin
    // d'origine ni au créneau autosave lui-même (invariant central, cf.
    // specs/arch-plan/vision/20260929-102601-arm-1-ar-9.md).
    QVERIFY(window.currentProjectPath_.isEmpty());
    QVERIFY(window.isWindowModified());
    QVERIFY(!window.project_.vector_objects.empty()); // le contenu récupéré est bien chargé

    // Traité (récupéré) -> jamais reproposé au prochain démarrage.
    QVERIFY(scanForRecoverableAutosaves().empty());

    clearAutosaveDir();
}

void MainWindowTest::autosaveRecoveryIgnoreDiscardsSlotWithoutLoading() {
    clearAutosaveDir();

    MainWindow window;
    QCoreApplication::processEvents();

    const Fixture fx = buildFixture();
    const QString originalPath = QStringLiteral("C:/ancien/projet.osp");
    const auto slot = slotFor(originalPath);
    QVERIFY(writeAutosave(slot, fx.project, originalPath).has_value());

    clickModalDialogButton(&window, QStringLiteral("Ignorer"));
    window.checkAutosaveRecovery();

    // Document courant inchangé : "Ignorer" ne charge rien.
    QVERIFY(window.currentProjectPath_.isEmpty());
    QVERIFY(!window.isWindowModified());
    QVERIFY(window.project_.vector_objects.empty());

    // Traité (ignoré) -> jamais reproposé non plus.
    QVERIFY(scanForRecoverableAutosaves().empty());

    clearAutosaveDir();
}

void MainWindowTest::cleanCloseDiscardsTheCurrentAutosaveSlot() {
    clearAutosaveDir();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString userFile = dir.filePath(QStringLiteral("user.osp"));

    MainWindow window;
    QCoreApplication::processEvents();

    const Fixture fx = buildFixture();
    window.applyLoadedProject(fx.project);
    QVERIFY(window.saveProjectToPath(userFile));
    window.setWindowModified(true);
    window.onAutosaveTick(); // laisse un créneau, comme si l'app allait planter juste après

    const auto slot = slotFor(userFile);
    QVERIFY(QFile::exists(slot.osp_path));

    // Document propre : confirmDiscardChanges() passe sans dialogue, la
    // fermeture est donc acceptée de façon déterministe.
    window.setWindowModified(false);
    QCloseEvent event;
    window.closeEvent(&event);
    QVERIFY(event.isAccepted());

    // Fermeture acceptée -> le créneau courant est purgé : rien à récupérer
    // au prochain lancement.
    QVERIFY(!QFile::exists(slot.osp_path));
    QVERIFY(scanForRecoverableAutosaves().empty());

    clearAutosaveDir();
}

// ---------------------------------------------------------------------------
// L5-T4b : câblage du modèle d'interaction (specs/plans/ui-interaction-model.md §2.5, §3, §4).
// ---------------------------------------------------------------------------

CanvasView* MainWindowTest::openSquares(MainWindow& window, ObjectId& a, ObjectId& b, ObjectId& c) {
    TrianglesFixture fx;
    fx.project.original.width = 2;
    fx.project.original.height = 2;
    fx.project.original.rgba.assign(2 * 2 * 4, 255);
    a = addSquare(fx.project, "A", -15);
    b = addSquare(fx.project, "B", 0);
    c = addSquare(fx.project, "C", 15);
    window.applyLoadedProject(fx.project);
    window.setTool(Tool::Select);
    window.refreshImage();
    auto* view = window.findChild<CanvasView*>();
    if (view == nullptr) {
        return nullptr;
    }
    window.resize(1600, 1000);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) {
        return nullptr;
    }
    view->resetTransform();
    view->scale(10.0, 10.0);
    view->centerOn(QPointF(0.0, 0.0));
    return view;
}

void MainWindowTest::dragWith(CanvasView* view, QPoint from, QPoint to, Qt::KeyboardModifiers mods,
                              Qt::KeyboardModifiers pressMods) {
    QWidget* vpw = view->viewport();
    sendMouseEvent(vpw, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, pressMods);
    const QPoint mid = (from + to) / 2;
    sendMouseEvent(vpw, QEvent::MouseMove, mid, Qt::NoButton, Qt::LeftButton, mods);
    sendMouseEvent(vpw, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
    sendMouseEvent(vpw, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
}

void MainWindowTest::helpMenuHasThreeEntriesAndF1OpensGestures() {
    MainWindow window;
    auto* helpMenu = window.findChild<QMenu*>(QStringLiteral("menu_help"));
    QVERIFY(helpMenu != nullptr);
    QStringList names;
    for (const QAction* act : helpMenu->actions()) {
        if (!act->isSeparator()) {
            names << act->objectName();
        }
    }
    QCOMPARE(names,
             (QStringList{QStringLiteral("action_help_quickstart"),
                          QStringLiteral("action_help_gestures"), QStringLiteral("action_about")}));
    for (const QAction* act : helpMenu->actions()) {
        QVERIFY2(!act->toolTip().isEmpty() && !act->statusTip().isEmpty(),
                 qPrintable(act->objectName()));
    }
    auto* gestures = window.findChild<QAction*>(QStringLiteral("action_help_gestures"));
    QCOMPARE(gestures->shortcut(), QKeySequence(Qt::Key_F1));
    QVERIFY(window.findChild<GesturesDialog*>() == nullptr);

    // F1 réel sur la fenêtre active : ouvre le dialogue, non modal, une seule instance.
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QTest::keyClick(&window, Qt::Key_F1);
    auto* dialog = window.findChild<GesturesDialog*>();
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->isVisible());
    QVERIFY(!dialog->isModal());
    QVERIFY(dialog->testAttribute(Qt::WA_DeleteOnClose));
    gestures->trigger();
    QCOMPARE(window.findChildren<GesturesDialog*>().size(), 1);

    // Fermé -> détruit ; une nouvelle demande en recrée un.
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.gesturesDialog_.isNull());
    gestures->trigger();
    QVERIFY(window.findChild<GesturesDialog*>() != nullptr);
}

void MainWindowTest::oldShortcutsMessageBoxIsGone() {
    MainWindow window;
    auto* helpMenu = window.findChild<QMenu*>(QStringLiteral("menu_help"));
    QVERIFY(helpMenu != nullptr);
    for (const QAction* act : helpMenu->actions()) {
        QVERIFY2(!act->text().contains(QStringLiteral("Raccourcis")), qPrintable(act->text()));
    }
    // « À propos » : boîte standard construite sur aboutText().
    QString shownText;
    // Sonde répétée : la boîte est modale (exec).
    auto* probe = new QTimer(&window);
    probe->setInterval(10);
    connect(probe, &QTimer::timeout, &window, [&shownText] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(w); box != nullptr && box->isVisible()) {
                shownText = box->text();
                box->close();
            }
        }
    });
    probe->start();
    window.findChild<QAction*>(QStringLiteral("action_about"))->trigger();
    probe->stop();
    QVERIFY(!shownText.isEmpty());
    QCOMPARE(shownText, aboutText());
}

void MainWindowTest::quickStartIsNonModalSingleInstanceBoundToMemberActions() {
    MainWindow window;
    auto* act = window.findChild<QAction*>(QStringLiteral("action_help_quickstart"));
    QVERIFY(act != nullptr);
    act->trigger();
    auto* dialog = window.findChild<QuickStartDialog*>();
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->isVisible());
    QVERIFY(!dialog->isModal());
    QCOMPARE(dialog->stepCount(), 6);
    // Un bouton par commande réelle : libellé = texte de la QAction membre.
    const std::vector<QAction*> expected = {window.openImageAct_,       window.segmentAct_,
                                            window.vectorizeRegionAct_, window.createTatamiAct_,
                                            window.analyzeAct_,         window.exportDstAct_};
    for (int i = 0; i < 6; ++i) {
        QPushButton* button = dialog->stepButton(i, 0);
        QVERIFY2(button != nullptr, qPrintable(QString::number(i)));
        QCOMPARE(button->text(), plainActionText(expected[static_cast<std::size_t>(i)]->text()));
        QCOMPARE(button->isEnabled(), expected[static_cast<std::size_t>(i)]->isEnabled());
    }
    QVERIFY(dialog->stepButton(0, 0)->isEnabled());  // Ouvrir une image : toujours possible
    QVERIFY(!dialog->stepButton(4, 0)->isEnabled()); // Analyser : rien à analyser
    QVERIFY(dialog->stepButtonCount(2) == 2);        // vectoriser + numérisation automatique
    act->trigger();
    QCOMPARE(window.findChildren<QuickStartDialog*>().size(), 1);
}

void MainWindowTest::hintsLabelSurvivesShowMessage() {
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.hintsLabel_ != nullptr);
    QVERIFY(window.hintsLabel_->isVisible());
    QVERIFY(!window.hintsText().isEmpty());
    window.statusBar()->showMessage(QStringLiteral("Message sans délai"));
    QVERIFY(window.hintsLabel_->isVisible()); // widget permanent : jamais masqué
    QVERIFY(window.hintsLabel_->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored);
    QCOMPARE(window.hintsLabel_->minimumWidth(), 0);
    QVERIFY(!window.hintsLabel_->accessibleName().isEmpty());
}

void MainWindowTest::hintsFollowToolSelectionAndHeldModifiers() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    const auto contains = [&](const QList<Hint>& hints) {
        for (const Hint& h : hints) {
            if (!window.hintsText().contains(h.label)) {
                return false;
            }
        }
        return !hints.isEmpty();
    };
    QVERIFY(contains(InteractionMap::hintsFor(Context::Select, Qt::NoModifier)));
    const QString selectText = window.hintsText();

    // Maj tenu (évènement du canevas) : les lignes à Maj du contexte.
    sendMouseEvent(view->viewport(), QEvent::MouseMove, QPoint(100, 100), Qt::NoButton,
                   Qt::NoButton, Qt::ShiftModifier);
    QVERIFY(contains(InteractionMap::hintsFor(Context::Select, Qt::ShiftModifier)));
    QVERIFY(window.hintsText() != selectText);
    // Sans objet sélectionné, le verrou d'axe (déplacement) n'est pas proposé ;
    const QString moveLabel =
        InteractionMap::hintsFor(Context::Move, Qt::ShiftModifier).constFirst().label;
    QVERIFY(!window.hintsText().contains(moveLabel));
    // avec un objet sélectionné, il l'est (rafraîchi au changement de sélection).
    window.applySelectionClick(b, SelectMode::Replace);
    QVERIFY(window.hintsText().contains(moveLabel));
    sendMouseEvent(view->viewport(), QEvent::MouseMove, QPoint(101, 100), Qt::NoButton,
                   Qt::NoButton, Qt::NoModifier);
    QCOMPARE(window.hintsText(), selectText);

    // Changement d'outil : le texte suit le contexte.
    window.setTool(Tool::Pan);
    QVERIFY(contains(InteractionMap::hintsFor(Context::Pan, Qt::NoModifier)));
    QVERIFY(window.hintsText() != selectText);
    window.setTool(Tool::DrawPolygon);
    QVERIFY(contains(InteractionMap::hintsFor(Context::DrawClicks, Qt::NoModifier)));
    QVERIFY(window.hintsLabel_->toolTip() == window.hintsText());
}

void MainWindowTest::plainClickReplacesAndEmptyClickDeselects() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -15, 0));
    QVERIFY(window.selectedObject_ == a);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
    QVERIFY(window.selectedObject_ == b);
    QVERIFY(window.multiSelection_.empty()); // Replace : mono-sélection legacy
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 30));
    QVERIFY(!window.selectedObject_.has_value());
    QVERIFY(window.checkSelectionInvariants());
}

void MainWindowTest::shiftClickAddsAndCtrlClickToggles() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -15, 0));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, vp(view, 0, 0));
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, b}));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, vp(view, 15, 0));
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, b, c}));
    QVERIFY(window.selectedObject_ == c);
    // Maj sur un objet déjà sélectionné : sans effet.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, vp(view, 0, 0));
    QCOMPARE(window.selectedObjectIds().size(), std::size_t{3});
    // Ctrl : bascule (retire b), puis le principal est promu quand on retire c.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, vp(view, 0, 0));
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, c}));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, vp(view, 15, 0));
    QVERIFY(window.selectedObject_ == a); // promu
    QVERIFY(window.multiSelection_.empty());
    // Maj/Ctrl dans le vide : la sélection reste.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, vp(view, 0, 30));
    QVERIFY(window.selectedObject_ == a);
    QVERIFY(window.checkSelectionInvariants());
    // Un seul pas d'annulation n'a été créé : la sélection n'est pas une commande.
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::rectangleLeftToRightIsWindowRightToLeftIsCrossing() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);

    // Gauche -> droite : fenêtre. [-7, 12] x [-8, 8] contient B (-5..5), coupe C (10..20).
    dragWith(view, vp(view, -7, -8), vp(view, 12, 8), Qt::NoModifier);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{b}));
    // Droite -> gauche : croisement, même cadre -> B et C (ordre d'ObjectId).
    dragWith(view, vp(view, 12, 8), vp(view, -7, -8), Qt::NoModifier);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{b, c}));
    QVERIFY(window.checkSelectionInvariants());
    // Rectangle dans le vide, sans Maj/Ctrl : désélectionne (Replace).
    dragWith(view, vp(view, -7, 20), vp(view, 7, 30), Qt::NoModifier);
    QVERIFY(window.selectedObjectIds().empty());
    QVERIFY(!window.undoStack_.canUndo());
}

void MainWindowTest::shiftRectangleAddsToTheSelection() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -15, 0));
    // Maj dès l'appui : ajout du rectangle (fenêtre autour de C) à la sélection existante.
    dragWith(view, vp(view, 8, -8), vp(view, 22, 8), Qt::ShiftModifier, Qt::ShiftModifier);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, c}));
    // Ctrl : bascule (retire C, ajoute B) ; le principal est le dernier ajouté.
    dragWith(view, vp(view, -7, -8), vp(view, 22, 8), Qt::ControlModifier, Qt::ControlModifier);
    QVERIFY(window.checkSelectionInvariants());
    QVERIFY(window.selectedObject_ == b);
    QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{a, b}));
}

void MainWindowTest::longPressOpensSelectBelowMenuAndChoosingSelectsIt() {
    InteractionMap::setLongPressMsForTesting(1);
    const auto restore = qScopeGuard([] { InteractionMap::setLongPressMsForTesting(-1); });
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Deux objets superposés : un second carré exactement sur B.
    window.undoStack_.execute(std::make_unique<openstitch::commands::AddVectorObjectCommand>([&] {
                                  auto copy = *window.project_.findObject(b);
                                  copy.id = window.project_.object_ids.next();
                                  copy.name = "B2";
                                  return copy;
                              }()),
                              window.project_);
    window.refreshImage();
    QSignalSpy below(view, &CanvasView::selectBelowRequested);

    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
    QVERIFY(below.wait(1000));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
    QVERIFY(!window.selectedObject_.has_value()); // l'appui long consommé ne sélectionne pas

    QMenu* menu = window.findChild<QMenu*>(QStringLiteral("selectBelowMenu"));
    QVERIFY(menu != nullptr);
    QStringList names;
    for (const QAction* act : menu->actions()) {
        names << act->text();
    }
    // Du plus haut au plus bas.
    QCOMPARE(names, (QStringList{QStringLiteral("B2"), QStringLiteral("B")}));
    menu->actions().at(1)->trigger(); // l'objet du dessous
    QVERIFY(window.selectedObject_ == b);
    QVERIFY(window.multiSelection_.empty());
}

void MainWindowTest::altClickOpensSelectBelowMenu() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    QSignalSpy below(view, &CanvasView::selectBelowRequested);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::AltModifier, vp(view, 15, 0));
    QVERIFY(below.count() == 1 || below.wait(1000)); // émis en différé par le canevas
    QCOMPARE(below.count(), 1);
    QMenu* menu = window.findChild<QMenu*>(QStringLiteral("selectBelowMenu"));
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().size(), 1);
    menu->actions().first()->trigger();
    QVERIFY(window.selectedObject_ == c);
    // Aucun objet sous le point : pas de menu.
    menu->close();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::AltModifier, vp(view, 0, 30));
    QTRY_COMPARE_WITH_TIMEOUT(below.count(), 2, 1000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(window.selectBelowMenu_.isNull());
}

void MainWindowTest::navigationPresetMenuSwitchesTableAndPersists() {
    const PresetRestorer restorer;
    MainWindow window;
    auto* os = window.findChild<QAction*>(QStringLiteral("navPresetOpenStitch"));
    auto* touch = window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"));
    QVERIFY(os != nullptr && touch != nullptr);
    QVERIFY(os->isCheckable() && touch->isCheckable());
    QVERIFY(os->isChecked() && !touch->isChecked());
    QCOMPARE(InteractionMap::preset(), Preset::OpenStitch);
    const QString osHints = window.hintsText();
    const int osRows = InteractionMap::allRows().size();

    touch->trigger();
    QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
    QVERIFY(touch->isChecked() && !os->isChecked());    // exclusif
    QVERIFY(window.hintsText() != osHints);             // la ligne d'indications suit
    QVERIFY(InteractionMap::allRows().size() < osRows); // la table filtrée change
    QSettings stored(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenStitch"),
                     QStringLiteral("OpenStitch Studio"));
    QCOMPARE(stored.value(QStringLiteral("navigation/preset")).toString(),
             QStringLiteral("touchpad"));
    // Une nouvelle fenêtre relit le préréglage enregistré.
    {
        MainWindow second;
        QVERIFY(second.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->isChecked());
        QCOMPARE(second.hintsText(), window.hintsText());
    }
    os->trigger();
    QCOMPARE(InteractionMap::preset(), Preset::OpenStitch);
    QCOMPARE(window.hintsText(), osHints);
    QCOMPARE(QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                       QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"))
                 .value(QStringLiteral("navigation/preset"))
                 .toString(),
             QStringLiteral("openstitch"));
}

void MainWindowTest::gesturesDialogPresetChangeUpdatesMenuAndHints() {
    const PresetRestorer restorer;
    MainWindow window;
    window.showGesturesDialog();
    auto* dialog = window.findChild<GesturesDialog*>();
    QVERIFY(dialog != nullptr);
    const QString before = window.hintsText();
    dialog->presetCombo()->setCurrentIndex(1); // Pavé tactile
    QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("navPresetTouchpad"))->isChecked());
    QVERIFY(window.hintsText() != before);
    // Dans l'autre sens : le menu met à jour le dialogue ouvert.
    window.findChild<QAction*>(QStringLiteral("navPresetOpenStitch"))->trigger();
    QCOMPARE(dialog->presetCombo()->currentIndex(), 0);
    QCOMPARE(window.hintsText(), before);
}

void MainWindowTest::panToolStillPansAfterNoDrag() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.setTool(Tool::Pan);
    view->setCanvasSizeMm(QSizeF(100.0, 100.0)); // scène bornée : les barres ont de la plage
    view->resetTransform();
    view->scale(40.0, 40.0);
    QVERIFY(view->horizontalScrollBar()->maximum() > 200);
    QCOMPARE(view->dragMode(), QGraphicsView::NoDrag);
    QCOMPARE(view->baseContext(), Context::Pan);
    QVERIFY(!view->selectionRectangleEnabled());
    view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum() / 2);
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum() / 2);
    const int h0 = view->horizontalScrollBar()->value();
    const int v0 = view->verticalScrollBar()->value();
    QSignalSpy clicked(view, &CanvasView::canvasClickedMm);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QTest::mouseMove(view->viewport(), QPoint(360, 270));
    QTest::mouseMove(view->viewport(), QPoint(340, 250));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(340, 250));
    QCOMPARE(view->horizontalScrollBar()->value(), h0 + 60);
    QCOMPARE(view->verticalScrollBar()->value(), v0 + 50);
    QCOMPARE(clicked.count(), 0);                 // le panoramique n'est pas un clic
    QVERIFY(!window.selectedObject_.has_value()); // et ne sélectionne rien
    // Retour à l'outil Sélection : le canevas réactive le rectangle.
    window.setTool(Tool::Select);
    QVERIFY(view->selectionRectangleEnabled());
    QCOMPARE(view->baseContext(), Context::Select);
}

void MainWindowTest::legacyDrawAndCropToolsStillWorkWithTheInteractionModel() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Aucun dialogue modal ne doit s'ouvrir (sinon le test resterait bloqué) : sonde + fermeture.
    QStringList modals;
    QTimer probe;
    probe.setInterval(10);
    connect(&probe, &QTimer::timeout, &window, [&modals] {
        if (QWidget* w = QApplication::activeModalWidget()) {
            modals << w->windowTitle();
            w->close();
        }
    });
    probe.start();

    // Recadrage : le cadre élastique émet toujours cropSelectedMm.
    window.setTool(Tool::Rect);
    QCOMPARE(view->dragMode(), QGraphicsView::RubberBandDrag);
    QSignalSpy crop(view, &CanvasView::cropSelectedMm);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -30, -20));
    QTest::mouseMove(view->viewport(), vp(view, -20, -10));
    QTest::mouseMove(view->viewport(), vp(view, -10, 0));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -10, 0));
    QCOMPARE(crop.count(), 1);
    window.setTool(Tool::Select);

    // Main levée : points captés pendant le glisser, trait fini au relâchement.
    window.setTool(Tool::DrawFreeform);
    QCOMPARE(view->dragMode(), QGraphicsView::NoDrag);
    QSignalSpy points(view, &CanvasView::freeformPointMm);
    QSignalSpy finished(view, &CanvasView::freeformStrokeFinished);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -30, 20));
    QTest::mouseMove(view->viewport(), vp(view, -25, 24));
    QTest::mouseMove(view->viewport(), vp(view, -20, 20));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -20, 20));
    QVERIFY(points.count() >= 3);
    QCOMPARE(finished.count(), 1);
    window.setTool(Tool::Select);

    // Polygone : un clic simple pose un sommet (le clic reste émis à l'appui).
    window.setTool(Tool::DrawPolygon);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 25, -25));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 35, -25));
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 30, -35));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{3});
    window.setTool(Tool::Select);
    QVERIFY(window.pendingPolygonVertices_.empty());

    // Satin : ligne de coupe (Bézier) -> le glisser émet toujours l'engagement du nœud.
    window.setTool(Tool::DrawSatinCutLine);
    QCOMPARE(view->dragMode(), QGraphicsView::NoDrag);
    QSignalSpy committed(view, &CanvasView::bezierPointCommittedMm);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, -30));
    QTest::mouseMove(view->viewport(), vp(view, 0, -20));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, -20));
    QCOMPARE(committed.count(), 1);
    window.setTool(Tool::Select);

    // Rectangle dessiné : déjà couvert par drawRectangleToolWithRealMouse...; ici seulement le
    // mode de glisser (cadre élastique) sous le modèle activé.
    window.setTool(Tool::DrawRectangle);
    QCOMPARE(view->dragMode(), QGraphicsView::RubberBandDrag);
    // Le recadrage hors image et la coupe sans forme affichent un avertissement modal (fermé
    // par la sonde) : preuve que les gestionnaires historiques ont bien reçu leur signal.
    QVERIFY(modals.contains(QStringLiteral("Coupe sans effet")));
}

void MainWindowTest::escapeAndDeleteKeepWorkingThroughTheCanvas() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
    QVERIFY(window.selectedObject_ == b);
    // Suppr universel : l'objet disparaît en un pas d'annulation.
    window.deleteSelectionAct_->trigger();
    QVERIFY(window.project_.findObject(b) == nullptr);
    window.undo();
    QVERIFY(window.project_.findObject(b) != nullptr);

    // Échap revient à la Sélection depuis un outil de dessin (contexte et rectangle réactivés).
    window.setTool(Tool::DrawPolygon);
    QCOMPARE(window.interactionContext(), Context::DrawClicks);
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(window.currentTool_, Tool::Select);
    QVERIFY(view->selectionRectangleEnabled());
    QCOMPARE(window.interactionContext(), Context::Select);
}

void MainWindowTest::shiftDragMovesAlongDominantAxis() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.applySelectionClick(b, SelectMode::Replace);
    window.selectionChanged();
    const auto origin = [&](ObjectId id) {
        return window.project_.findObject(id)->paths.front().outer.nodes.front().pos;
    };
    const Vec2um before = origin(b);

    // +3 mm en x, +1 mm vers le bas (scène) : Maj -> axe dominant x, y inchangé.
    dragWith(view, vp(view, 0, 0), vp(view, 3, 1), Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(origin(b).x.value, before.x.value + 3000, 2000);
    QCOMPARE(origin(b).y.value, before.y.value);
    window.undo();
    QCOMPARE(origin(b), before);

    // Axe dominant y.
    dragWith(view, vp(view, 0, 0), vp(view, 1, 3), Qt::ShiftModifier);
    QTRY_COMPARE_WITH_TIMEOUT(origin(b).y.value, before.y.value - 3000, 2000);
    QCOMPARE(origin(b).x.value, before.x.value);
    window.undo();

    // Sans Maj : déplacement libre (témoin).
    dragWith(view, vp(view, 0, 0), vp(view, 3, 1), Qt::NoModifier);
    QTRY_COMPARE_WITH_TIMEOUT(origin(b).x.value, before.x.value + 3000, 2000);
    QCOMPARE(origin(b).y.value, before.y.value - 1000);
}

void MainWindowTest::shiftDragOfNodeKeepsOneAxis() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Un nœud au milieu de l'arête haute de C (les 4 coins portent aussi les poignées de
    // redimensionnement) : physique (15, 5) -> scène (15, -5).
    auto& nodes = window.project_.findObject(c)->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3,
                 geometry::PathNode{Vec2um{Micrometers{15'000}, Micrometers{5'000}},
                                    geometry::NodeType::Corner, std::nullopt, std::nullopt});
    window.applySelectionClick(c, SelectMode::Replace);
    window.refreshImage();
    const auto nodePos = [&](std::size_t n) {
        return window.project_.findObject(c)->paths.front().outer.nodes[n].pos;
    };
    const Vec2um before = nodePos(3);
    // Glissé de (+2, +1) mm scène avec Maj : l'axe x domine, y reste verrouillé.
    dragWith(view, vp(view, 15, -5), vp(view, 17, -4), Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(nodePos(3) != before, 2000);
    QCOMPARE(nodePos(3).x.value, before.x.value + 2000);
    QCOMPARE(nodePos(3).y.value, before.y.value);
}

void MainWindowTest::ctrlDragSkipsSnap() {
    const SnapSettingReset reset;
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.snapNodesAct_->setChecked(true); // accrochage des nœuds : désactivé par défaut
    // Nœud au milieu de l'arête haute de A (physique (-15, 5) -> scène (-15, -5)) : les coins
    // portent aussi les poignées de redimensionnement. Cible : près du coin (-5, 5) de B.
    auto& nodes = window.project_.findObject(a)->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3,
                 geometry::PathNode{Vec2um{Micrometers{-15'000}, Micrometers{5'000}},
                                    geometry::NodeType::Corner, std::nullopt, std::nullopt});
    window.applySelectionClick(a, SelectMode::Replace);
    window.refreshImage();
    const auto node = [&] {
        return window.project_.findObject(a)->paths.front().outer.nodes[3].pos;
    };
    const Vec2um before = node();
    const Vec2um bCorner{Micrometers{-5'000}, Micrometers{5'000}};

    // Sans Ctrl : s'accroche au coin de B (exactement).
    dragWith(view, vp(view, -15, -5), vp(view, -4.8, -4.8), Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(node() != before, 2000);
    QCOMPARE(node(), bCorner);
    window.undo();
    QCOMPARE(node(), before);

    // Ctrl au relâchement : accroche suspendue, position brute (au pixel près : 0,1 mm).
    window.applySelectionClick(a, SelectMode::Replace);
    window.selectionChanged();
    dragWith(view, vp(view, -15, -5), vp(view, -4.8, -4.8), Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(node() != before, 2000);
    QVERIFY(node() != bCorner);
    QVERIFY(std::abs(node().x.value - (-4'800)) <= 150);
    QVERIFY(std::abs(node().y.value - 4'800) <= 150);
}

void MainWindowTest::ctrlSuspendsSnapWhenPlacingPolygonVertex() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.setTool(Tool::DrawPolygon);
    // Près du nœud (-5, 5 scène) de B, à 0,3 mm.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, -4.7, 5.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    QCOMPARE(window.pendingPolygonVertices_.back(),
             (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}})); // accroché
    window.cancelPolygonDraw();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, vp(view, -4.7, 5.0));
    QCOMPARE(window.pendingPolygonVertices_.size(), std::size_t{1});
    QVERIFY(window.pendingPolygonVertices_.back() !=
            (Vec2um{Micrometers{-5'000}, Micrometers{-5'000}})); // Ctrl : brut
}

void MainWindowTest::altDragDuplicatesAndMovesCopyInOneUndoStep() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.applySelectionClick(b, SelectMode::Replace);
    window.selectionChanged();
    const auto origin = [&](ObjectId id) {
        return window.project_.findObject(id)->paths.front().outer.nodes.front().pos;
    };
    const Vec2um before = origin(b);
    const std::size_t count = window.project_.vector_objects.size();

    // Alt tenu AVANT l'appui, sur le corps d'un objet déjà sélectionné : le glisser démarre.
    dragWith(view, vp(view, 0, 0), vp(view, 3, -2), Qt::AltModifier, Qt::AltModifier);
    QTRY_COMPARE_WITH_TIMEOUT(window.project_.vector_objects.size(), count + 1, 2000);
    QCOMPARE(origin(b), before); // l'original n'a pas bougé
    const ObjectId copy = window.project_.vector_objects.back().id;
    QVERIFY(copy != b);
    QCOMPARE(origin(copy).x.value, before.x.value + 3000);
    QCOMPARE(origin(copy).y.value, before.y.value + 2000);
    QVERIFY(window.selectedObject_ == copy); // la copie est sélectionnée
    QVERIFY(window.checkSelectionInvariants());

    window.undo(); // UN seul pas : copie ET déplacement
    QCOMPARE(window.project_.vector_objects.size(), count);
    QVERIFY(!window.undoStack_.canUndo());
    window.redo();
    QCOMPARE(window.project_.vector_objects.size(), count + 1);
    QCOMPARE(origin(copy).x.value, before.x.value + 3000);
}

void MainWindowTest::altBoxDrawGrowsFromCenter() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.setTool(Tool::DrawRectangle);
    const auto drawAndMeasure = [&](QPoint from, QPoint to, Qt::KeyboardModifiers mods) {
        window.setTool(Tool::DrawRectangle); // l'outil repasse en Sélection après une forme
        const std::size_t before = window.project_.vector_objects.size();
        dragWith(view, from, to, mods);
        if (window.project_.vector_objects.size() != before + 1) {
            return QRectF();
        }
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const auto& node : window.project_.vector_objects.back().paths.front().outer.nodes) {
            minX = std::min(minX, static_cast<double>(node.pos.x.value));
            maxX = std::max(maxX, static_cast<double>(node.pos.x.value));
            minY = std::min(minY, static_cast<double>(node.pos.y.value));
            maxY = std::max(maxY, static_cast<double>(node.pos.y.value));
        }
        return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    };
    // Sans Alt : le cadre va du point d'appui au point de relâchement (scène (0,25) -> (8,31),
    // soit physique x 0..8, y -31..-25).
    const QRectF plain = drawAndMeasure(vp(view, 0, 25), vp(view, 8, 31), Qt::NoModifier);
    QVERIFY(plain.isValid());
    QVERIFY(std::abs(plain.left() - 0.0) <= 150 && std::abs(plain.right() - 8000.0) <= 150);
    // Avec Alt (ailleurs : la forme précédente, sélectionnée, capterait l'appui) : le point
    // d'appui (30, 25) est le centre -> x 22..38, y(physique) -31..-19.
    const QRectF centered = drawAndMeasure(vp(view, 30, 25), vp(view, 38, 31), Qt::AltModifier);
    QVERIFY2(centered.isValid(), qPrintable(window.statusBar()->currentMessage()));
    QVERIFY(std::abs(centered.left() - 22000.0) <= 150);
    QVERIFY(std::abs(centered.right() - 38000.0) <= 150);
    QVERIFY(std::abs(centered.top() - (-31000.0)) <= 150);
    QVERIFY(std::abs(centered.bottom() - (-19000.0)) <= 150);
    // Cadre « mince » (0,4 mm de haut) : le vrai point d'appui reste le centre, le cadre centré
    // fait 0,8 mm de haut et est accepté (x 22..38, y(physique) 24,6..25,4 ; en haut, loin des
    // formes déjà dessinées pour que l'accroche des coins n'interfère pas).
    const QRectF thin = drawAndMeasure(vp(view, 30, -25), vp(view, 38, -25.4), Qt::AltModifier);
    QVERIFY2(thin.isValid(), qPrintable(window.statusBar()->currentMessage()));
    QVERIFY(std::abs(thin.left() - 22000.0) <= 150 && std::abs(thin.right() - 38000.0) <= 150);
    QVERIFY(std::abs(thin.center().y() - 25000.0) <= 150);
    // Appui d'un côté, relâchement vers le haut-gauche : même centre (le point d'appui).
    const QRectF reverse = drawAndMeasure(vp(view, -30, 25), vp(view, -38, 19), Qt::AltModifier);
    QVERIFY2(reverse.isValid(), qPrintable(window.statusBar()->currentMessage()));
    QVERIFY(std::abs(reverse.center().x() - (-30000.0)) <= 150);
    QVERIFY(std::abs(reverse.center().y() - (-25000.0)) <= 150);
}

void MainWindowTest::hoverHighlightShowsOnlyUnselectedObjectUnderCursorInSelectTool() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    const auto hover = [&](double x, double y) {
        sendMouseEvent(view->viewport(), QEvent::MouseMove, vp(view, x, y), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
        // Coalescence 16 ms : attend le traitement du dernier point.
        for (int i = 0; i < 100 && window.hoverTimer_ != nullptr && window.hoverTimer_->isActive();
             ++i) {
            QTest::qWait(2);
        }
    };
    const auto shown = [&] {
        return window.hoverItem_ != nullptr && window.hoverItem_->isVisible();
    };

    hover(-15, 0);
    QVERIFY(shown());
    QCOMPARE(window.hoverItem_->path().boundingRect(), QRectF(-20.0, -5.0, 10.0, 10.0));
    hover(0, 30); // le vide
    QVERIFY(!shown());
    hover(0, 0);
    QVERIFY(shown());
    // Le clic traverse la surbrillance : il sélectionne l'objet dessous ; sélectionné, il n'est
    // plus surligné.
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, vp(view, 0, 0));
    QVERIFY(window.selectedObject_ == b);
    hover(0.5, 0.5);
    QVERIFY(!shown());
    hover(15, 0);
    QVERIFY(shown());
    // Hors outil Sélection : jamais de surbrillance.
    window.setTool(Tool::Pan);
    QVERIFY(!shown());
    hover(15, 0);
    QVERIFY(!shown());
}

void MainWindowTest::hoverHighlightHidesOnLeaveAndDuringGestures() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    const auto shown = [&] {
        return window.hoverItem_ != nullptr && window.hoverItem_->isVisible();
    };
    sendMouseEvent(view->viewport(), QEvent::MouseMove, vp(view, 0, 0), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QVERIFY(shown());
    // Le curseur quitte le viewport : la surbrillance disparaît.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view->viewport(), &leave);
    QVERIFY(!shown());
    // Pendant un rectangle de sélection (geste actif), pas de surbrillance.
    QTest::qWait(30);
    sendMouseEvent(view->viewport(), QEvent::MouseButtonPress, vp(view, 0, 30), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    sendMouseEvent(view->viewport(), QEvent::MouseMove, vp(view, 0, 0), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
    QVERIFY(view->gestureActive());
    QVERIFY(!shown());
    sendMouseEvent(view->viewport(), QEvent::MouseButtonRelease, vp(view, 0, 0), Qt::LeftButton,
                   Qt::NoButton, Qt::NoModifier);
}

void MainWindowTest::hoverBurstIsCoalescedAndPathsAreCached() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.hoverCacheValid_ = false;
    window.hoverPathBuilds_ = 0;
    window.hoverComputations_ = 0;
    constexpr int kMoves = 60;
    for (int i = 0; i < kMoves; ++i) { // rafale sans traiter d'évènements
        sendMouseEvent(view->viewport(), QEvent::MouseMove,
                       vp(view, (i % 2 == 0) ? -15.0 : 15.0, 0.0), Qt::NoButton, Qt::NoButton,
                       Qt::NoModifier);
    }
    for (int i = 0; i < 100 && window.hoverTimer_->isActive(); ++i) {
        QTest::qWait(2);
    }
    QVERIFY(window.hoverComputations_ >= 1);
    QVERIFY2(window.hoverComputations_ < kMoves / 2,
             qPrintable(QString::number(window.hoverComputations_)));
    QCOMPARE(window.hoverPathBuilds_, 3); // un seul calcul de contours pour les 3 objets
    // Un rendu de la couche base invalide le cache.
    window.refreshImage();
    QVERIFY(!window.hoverCacheValid_);
}

void MainWindowTest::leftDragOnEmptySpaceStillPansInNodeEditAndStitchEditContexts() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    view->setCanvasSizeMm(QSizeF(100.0, 100.0));
    view->resetTransform();
    view->scale(40.0, 40.0);
    for (const Context ctx : {Context::NodeEdit, Context::StitchEdit}) {
        view->setBaseContext(ctx);
        QCOMPARE(view->dragMode(), QGraphicsView::ScrollHandDrag);
        view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum() / 2);
        const int h0 = view->horizontalScrollBar()->value();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
        QTest::mouseMove(view->viewport(), QPoint(360, 280));
        QTest::mouseMove(view->viewport(), QPoint(340, 270));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(340, 270));
        QVERIFY2(view->horizontalScrollBar()->value() != h0, qPrintable(QString::number(int(ctx))));
    }
    // Select / Pan gardent NoDrag.
    view->setBaseContext(Context::Select);
    QCOMPARE(view->dragMode(), QGraphicsView::NoDrag);
    // Via la fenêtre : un mode d'édition actif sous l'outil Sélection donne le contexte d'édition.
    window.setTool(Tool::Select);
    window.stitchEditModeAct_->setChecked(true);
    if (window.stitchEditModeAct_->isChecked()) {
        QCOMPARE(view->baseContext(), Context::StitchEdit);
        QCOMPARE(view->dragMode(), QGraphicsView::ScrollHandDrag);
    }
}

void MainWindowTest::altClickOnSelectedBodyOpensSelectBelowWithoutDuplicating() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    window.applySelectionClick(b, SelectMode::Replace);
    window.selectionChanged();
    const std::size_t count = window.project_.vector_objects.size();
    QSignalSpy below(view, &CanvasView::selectBelowRequested);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::AltModifier, vp(view, 0, 0));
    QVERIFY(below.count() == 1 || below.wait(1000));
    QCOMPARE(window.project_.vector_objects.size(), count); // pas de copie
    QVERIFY(!window.undoStack_.canUndo());
    QVERIFY(window.findChild<QMenu*>(QStringLiteral("selectBelowMenu")) != nullptr);
}

void MainWindowTest::nodeSnapIsOffByDefaultAndLandsExactlyWhereReleased() {
    const SnapSettingReset reset;
    QSettings().setValue(QStringLiteral("edit/snapNodesOnDrag"), false);
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    QVERIFY(window.snapNodesAct_ != nullptr);
    QVERIFY(window.snapNodesAct_->isCheckable());
    QVERIFY(!window.snapNodesAct_->isChecked());
    QCOMPARE(window.snapNodesAct_->objectName(), QStringLiteral("action_snapNodesOnDrag"));
    QVERIFY(!window.snapNodesAct_->toolTip().isEmpty());
    auto& nodes = window.project_.findObject(a)->paths.front().outer.nodes;
    nodes.insert(nodes.begin() + 3,
                 geometry::PathNode{Vec2um{Micrometers{-15'000}, Micrometers{5'000}},
                                    geometry::NodeType::Corner, std::nullopt, std::nullopt});
    window.applySelectionClick(a, SelectMode::Replace);
    window.refreshImage();
    const auto node = [&] {
        return window.project_.findObject(a)->paths.front().outer.nodes[3].pos;
    };
    // À 0,2 mm d'un sommet de B (-5, 5) : sans le réglage, le nœud reste là où on le lâche
    // (déplacement exact en pixels, 10 px/mm -> x +10 200 µm, y -200 µm).
    const QPoint from = vp(view, -15, -5);
    const QPoint to = vp(view, -4.8, -4.8);
    dragWith(view, from, to, Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(node() != (Vec2um{Micrometers{-15'000}, Micrometers{5'000}}), 2000);
    const double dxMm = (to.x() - from.x()) / 10.0;
    const double dyMm = (to.y() - from.y()) / 10.0;
    QVERIFY(std::abs(node().x.value - (-15'000 + dxMm * 1000.0)) <= 1.0);
    QVERIFY(std::abs(node().y.value - (5'000 - dyMm * 1000.0)) <= 1.0);
    QVERIFY(node() != (Vec2um{Micrometers{-5'000}, Micrometers{5'000}})); // pas accroché
}

void MainWindowTest::nodeSnapSettingPersistsAndOnlySnapsToOtherObjectVertices() {
    const SnapSettingReset reset;
    QSettings().setValue(QStringLiteral("edit/snapNodesOnDrag"), false);
    {
        MainWindow window;
        window.snapNodesAct_->trigger();
        QVERIFY(window.snapNodesAct_->isChecked());
        QVERIFY(QSettings().value(QStringLiteral("edit/snapNodesOnDrag")).toBool());
    }
    MainWindow window; // relit le réglage
    QVERIFY(window.snapNodesAct_->isChecked());
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Candidats : sommets des AUTRES objets seulement. Milieu d'arête (0, 5) de B et centre de B
    // ne sont pas des candidats ; le sommet (-5, 5) l'est ; rayon <= 1 mm.
    const QPointF nearMidEdge(0.2, -5.1); // scène : proche du milieu d'arête de B
    QVERIFY(!window.findNodeSnapMm(nearMidEdge, a).has_value());
    QVERIFY(!window.findNodeSnapMm(QPointF(0.2, 0.2), a).has_value()); // centre de B
    const auto corner = window.findNodeSnapMm(QPointF(-4.8, -4.8), a);
    QVERIFY(corner.has_value());
    QCOMPARE(*corner, QPointF(-5.0, -5.0));
    QVERIFY(!window.findNodeSnapMm(QPointF(-3.5, -3.5), a).has_value()); // > 1 mm
    QVERIFY(!window.findNodeSnapMm(QPointF(-4.8, -4.8), b).has_value() ||
            *window.findNodeSnapMm(QPointF(-4.8, -4.8), b) !=
                QPointF(-5.0, -5.0)); // pas son propre sommet
}

void MainWindowTest::hintsFollowTheSameContextPriorityAsTheCanvas() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Un mode d'édition actif avec un outil de dessin : le canevas est en contexte de dessin,
    // les indications aussi.
    window.setTool(Tool::DrawPolygon);
    bool toggled = false;
    for (QAction* act : {window.stitchEditModeAct_, window.railEditModeAct_,
                         window.satinGuideModeAct_, window.directionGuideModeAct_}) {
        QSignalBlocker block(act);
        act->setChecked(true);
        toggled = toggled || act->isChecked();
        break;
    }
    QVERIFY(toggled);
    window.updateInteractionContext();
    QCOMPARE(window.interactionContext(), Context::DrawClicks);
    QCOMPARE(view->baseContext(), Context::Select); // le contexte de dessin vient des booléens
    // Outil Pan : toujours Pan, même avec un mode d'édition coché.
    window.setTool(Tool::Pan);
    QCOMPARE(window.interactionContext(), Context::Pan);
    QCOMPARE(view->baseContext(), Context::Pan);
    // Outil Sélection : le mode d'édition passe devant.
    window.setTool(Tool::Select);
    const Context ctx = window.interactionContext();
    QVERIFY(ctx == Context::NodeEdit || ctx == Context::StitchEdit);
    QCOMPARE(view->baseContext(), ctx);
    QCOMPARE(window.hintsText().isEmpty(), InteractionMap::hintsFor(ctx, Qt::NoModifier).isEmpty());
}

void MainWindowTest::selectBelowMenuDisambiguatesDuplicateAndEmptyNames() {
    MainWindow window;
    ObjectId a, b, c;
    CanvasView* view = openSquares(window, a, b, c);
    QVERIFY(view != nullptr);
    // Trois objets superposés au centre de B : « B », « B » (doublon) et un sans nom.
    for (const char* name : {"B", ""}) {
        auto copy = *window.project_.findObject(b);
        copy.id = window.project_.object_ids.next();
        copy.name = name;
        window.undoStack_.execute(
            std::make_unique<openstitch::commands::AddVectorObjectCommand>(copy), window.project_);
    }
    window.refreshImage();
    window.onSelectBelow(QPointF(0.0, 0.0), QPoint(100, 100), SelectMode::Replace);
    QMenu* menu = window.findChild<QMenu*>(QStringLiteral("selectBelowMenu"));
    QVERIFY(menu != nullptr);
    QStringList labels;
    for (const QAction* act : menu->actions()) {
        labels << act->text();
    }
    QCOMPARE(labels.size(), 3);
    QCOMPARE(QSet<QString>(labels.begin(), labels.end()).size(), 3); // tous distincts
    for (const QString& label : labels) {
        QVERIFY2(label.contains(QStringLiteral("(#")), qPrintable(label));
    }
    QVERIFY(labels.constFirst().startsWith(QStringLiteral("(sans nom)")));
    menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // Noms uniques : pas de suffixe.
    window.onSelectBelow(QPointF(-15.0, 0.0), QPoint(100, 100), SelectMode::Replace);
    menu = window.findChild<QMenu*>(QStringLiteral("selectBelowMenu"));
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->actions().constFirst()->text(), QStringLiteral("A"));
}

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_main_window.moc"
