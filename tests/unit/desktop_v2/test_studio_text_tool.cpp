// SPDX-License-Identifier: Apache-2.0
// UX-TEXT-01 lot B : outil Texte natif. Le vrai Main.qml, le vrai canevas, de vrais clics et de
// vraies frappes (QTest sur la fenêtre) : T dans le rail, clic au canevas, saisie sur place,
// Ctrl+Entrée, Échap, clic ailleurs, rouvrir un texte existant, annuler. Aucun appel direct au
// contrôleur pour le parcours : seul le décor (projet, zoom) est posé par le test.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "studio_controller.hpp"
#include "text_tool.hpp"
#include "tool_controller.hpp"
#include "vector_controller.hpp"
#include "vector_space_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

QQuickItem* deepItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = deepItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// Frappe `text` dans la fenêtre comme au clavier : un appui et un relâchement par caractère, avec
// la VRAIE touche (T, F, R… sont des raccourcis globaux de Studio : une saisie ne doit jamais les
// déclencher). QTest::keyClicks ne connaît que les QWidget.
int keyOf(QChar c) {
    if (c == QLatin1Char(' ')) {
        return Qt::Key_Space;
    }
    const auto u = c.toUpper().unicode();
    return (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ? static_cast<int>(u) : 0;
}

void typeText(QQuickWindow* window, const QString& text) {
    for (const QChar c : text) {
        const Qt::KeyboardModifiers mods = c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
        QKeyEvent press(QEvent::KeyPress, keyOf(c), mods, QString(c));
        QCoreApplication::sendEvent(window, &press);
        QKeyEvent release(QEvent::KeyRelease, keyOf(c), mods, QString(c));
        QCoreApplication::sendEvent(window, &release);
    }
}

struct Rig {
    DocumentSession session;
    StudioController controller{session};
    ActionController actions{controller};
    EditController editor{controller};
    ToolController tools{controller};
    VectorController vectors{controller};
    VectorSpaceController stage{controller};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    WorkspaceController* workspace{nullptr};
    GpuCanvas* canvas{nullptr};
};

} // namespace

class StudioTextToolTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig) {
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) {
                        rig.warnings.push_back(error.toString());
                    }
                });
        auto* ctx = rig.engine.rootContext();
        ctx->setContextProperty(QStringLiteral("studio"), &rig.controller);
        ctx->setContextProperty(QStringLiteral("actions"), &rig.actions);
        ctx->setContextProperty(QStringLiteral("editor"), &rig.editor);
        ctx->setContextProperty(QStringLiteral("tools"), &rig.tools);
        ctx->setContextProperty(QStringLiteral("vectors"), &rig.vectors);
        ctx->setContextProperty(QStringLiteral("vectorStage"), &rig.stage);
        auto created = project_ops::create_empty_project({});
        QVERIFY(created.has_value());
        rig.session.replaceProject(std::move(*created), true);
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.workspace = rig.window->findChild<WorkspaceController*>();
        QVERIFY(rig.workspace);
        if (const auto size = qEnvironmentVariable("OPENSTITCH_TEXT_SIZE").split(QLatin1Char('x'));
            size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
            rig.window->resize(size[0].toInt(), size[1].toInt());
        }
        QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
        rig.workspace->setCurrent(QStringLiteral("vector"));
        QTRY_VERIFY_WITH_TIMEOUT(
            rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas")) != nullptr, 5'000);
        rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas"));
        QVERIFY(rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->width() > 0 && rig.canvas->height() > 0, 5'000);
        // 6 px/mm, ou moins si la fenêtre est étroite : « Ecole de Paris » (~100 mm) doit tenir.
        rig.canvas->zoomToScale(std::min(6.0, rig.canvas->width() / 130.0));
        rig.canvas->centerOnScene(0.0, 0.0);
        // Changer d'espace remet les outils à la Sélection en différé : vider la file d'abord.
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    // Pixel fenêtre d'un point de scène (mm, Y vers le bas).
    QPoint at(Rig& rig, double xMm, double yMm) {
        const auto s = rig.canvas->camera().worldToScreen({xMm, yMm});
        return rig.canvas->mapToScene(QPointF(s.x, s.y)).toPoint();
    }
    static QPoint centerOf(QQuickItem* item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    [[nodiscard]] static std::size_t steps(Rig& rig) { return rig.session.undoNames().size(); }
    // Recette visuelle : si OPENSTITCH_TEXT_SHOTS pointe un dossier, sauvegarde une vraie capture
    // de la fenêtre (rendu logiciel) sous `<dossier>/<nom>-<taille>.png`. Sans variable : aucun
    // effet.
    void shot(Rig& rig, const char* name) {
        const auto dir = qEnvironmentVariable("OPENSTITCH_TEXT_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        QTest::qWait(150); // laisse le scene graph logiciel peindre l'état courant
        const auto image = rig.window->grabWindow();
        const auto file = QStringLiteral("%1/%2-%3x%4.png")
                              .arg(dir, QString::fromLatin1(name))
                              .arg(rig.window->width())
                              .arg(rig.window->height());
        QVERIFY2(image.save(file), qPrintable(file));
    }
    // Clique sur le bouton T du rail (le vrai chemin d'un utilisateur).
    void chooseTextTool(Rig& rig) {
        auto* button = deepItem(rig.window->contentItem(), QStringLiteral("tool_text"));
        QVERIFY2(button, "le rail ne propose pas l'outil Texte");
        QVERIFY(button->isEnabled());
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, centerOf(button));
        QTRY_COMPARE_WITH_TIMEOUT(rig.tools.activeTool(), QStringLiteral("text"), 3'000);
    }
    QQuickItem* field(Rig& rig) {
        return deepItem(rig.window->contentItem(), QStringLiteral("textEditorField"));
    }
    // Ouvre la saisie par un clic canevas ; le champ prend le focus tout de suite.
    void clickToType(Rig& rig, double xMm, double yMm) {
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, xMm, yMm));
        QTRY_VERIFY_WITH_TIMEOUT(textTool(rig).editing(), 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(field(rig) != nullptr && field(rig)->hasActiveFocus(), 3'000);
    }
    static TextTool& textTool(Rig& rig) { return rig.tools.textTool(); }
    void waitForScene(Rig& rig) {
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy() && rig.controller.scene() != nullptr,
                                 15'000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto& scene = rig.controller.scene();
                const auto& project = rig.session.project();
                for (const auto& emb : project.embroidery_objects) {
                    if (scene->revision != 0 && scene->boundsOf(emb.id.value)) {
                        return true;
                    }
                }
                return false;
            }(),
            15'000);
    }

private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by the application"));
        qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                                   QStringLiteral("Provided by the application"));
        // Police par défaut de `text.add` : celle du dépôt (sans variable, repli sur le répertoire
        // courant, qui n'est pas la racine du dépôt pendant CTest).
        qputenv("OPENSTITCH_FONTS_DIR", QByteArray(OPENSTITCH_FONTS_DIR));
    }

    // Parcours du novice : T, un clic, taper un prénom, Ctrl+Entrée. Ni palette ni dialogue.
    void noviceTypesAFirstNameOnTheCanvas() {
        Rig rig;
        open(rig);
        QVERIFY(rig.session.project().text_objects.empty());
        const auto before = steps(rig);
        chooseTextTool(rig);
        shot(rig, "1-outil-texte-actif");
        clickToType(rig, 0.0, 0.0);
        QVERIFY(textTool(rig).creating());
        shot(rig, "2-saisie-ouverte");
        typeText(rig.window, QStringLiteral("Marie"));
        shot(rig, "3-saisie-marie");
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        waitForScene(rig);
        shot(rig, "4-texte-cree");
        QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
                 QStringLiteral("Marie"));
        QCOMPARE(steps(rig), before + 1); // une seule étape d'annulation
        QVERIFY(!textTool(rig).editing());
        // Le texte est posé AU clic : centré sur lui, sur la ligne de base descendue d'une
        // demi-capitale.
        const auto& origin = rig.session.project().text_objects.front().origin;
        QVERIFY(std::abs(origin.x.value) < 1'000);              // ~0 mm
        QVERIFY(std::abs(origin.y.value + 6'000) < 1'000);      // ~ -6 mm (capitale 12 mm)
        QVERIFY(rig.session.selection().primary().has_value()); // ses lettres sont sélectionnées
        QVERIFY(rig.session.undo());
        QVERIFY(rig.session.project().text_objects.empty()); // aucun résidu
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void escapeCancelsWithoutLeavingAGhost() {
        Rig rig;
        open(rig);
        const auto before = steps(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("brouillon"));
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
        QVERIFY(rig.session.project().text_objects.empty());
        QCOMPARE(steps(rig), before);
    }

    void emptyTextCreatesNothing() {
        Rig rig;
        open(rig);
        const auto before = steps(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier); // rien saisi
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
        QVERIFY(rig.session.project().text_objects.empty());
        QCOMPARE(steps(rig), before);
        // Des espaces seuls ne font pas un texte non plus.
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("   "));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
        QVERIFY(rig.session.project().text_objects.empty());
    }

    void clickElsewhereValidatesANonEmptyText() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, -20.0, -15.0);
        typeText(rig.window, QStringLiteral("A"));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 25.0, 18.0)); // loin de l'éditeur
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        QVERIFY(!textTool(rig).editing()); // ce clic a validé, il n'ouvre pas un second texte
        QCOMPARE(rig.session.project().text_objects.size(), std::size_t{1});
    }

    // « École de Paris » : accents et espaces ; rouvrir par un clic, corriger, sans déplacer le
    // bloc.
    void editingAnExistingTextKeepsItsPlaceAndUndoesInOneStep() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Ecole de Paris"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        waitForScene(rig);
        const auto originBefore = rig.session.project().text_objects.front().origin;
        const auto before = steps(rig);

        // Clic sur le texte lui-même : la saisie se rouvre, pré-remplie.
        const auto id = rig.session.project().embroidery_objects.front().id.value;
        const auto bounds = rig.controller.scene()->boundsOf(id);
        QVERIFY(bounds.has_value());
        QTest::mouseClick(rig.window, Qt::LeftButton, {},
                          at(rig, (bounds->x0 + bounds->x1) / 2, (bounds->y0 + bounds->y1) / 2));
        QTRY_VERIFY_WITH_TIMEOUT(textTool(rig).editing(), 3'000);
        QVERIFY(!textTool(rig).creating());
        QTRY_VERIFY_WITH_TIMEOUT(field(rig) != nullptr && field(rig)->hasActiveFocus(), 3'000);
        QCOMPARE(field(rig)->property("text").toString(), QStringLiteral("Ecole de Paris"));
        shot(rig, "5-reedition-preremplie");
        // La carte se pose SOUS le texte : elle ne masque pas ce qu'on corrige.
        auto* card = deepItem(rig.window->contentItem(), QStringLiteral("textEditorCard"));
        QVERIFY(card != nullptr);
        QVERIFY2(card->mapToScene(QPointF(0, 0)).y() >= at(rig, 0.0, bounds->y1).y() - 1,
                 "la carte de réédition masque le texte");
        QTest::keyClick(rig.window, Qt::Key_A, Qt::ControlModifier); // tout sélectionner
        typeText(rig.window, QString::fromUtf8("École de Paris"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);

        QCOMPARE(rig.session.project().text_objects.size(), std::size_t{1}); // pas de doublon
        QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
                 QString::fromUtf8("École de Paris"));
        QCOMPARE(steps(rig), before + 1);
        const auto& originAfter = rig.session.project().text_objects.front().origin;
        QCOMPARE(originAfter.x.value, originBefore.x.value); // le bloc n'a pas bougé
        QCOMPARE(originAfter.y.value, originBefore.y.value);
        QVERIFY(rig.session.undo());
        QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
                 QStringLiteral("Ecole de Paris"));
    }

    void editingWithoutChangeAddsNoUndoStepAndEmptyingIsRefused() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Zoe"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        waitForScene(rig);
        const auto before = steps(rig);
        const auto id = rig.session.project().embroidery_objects.front().id.value;
        const auto bounds = rig.controller.scene()->boundsOf(id);
        QVERIFY(bounds.has_value());
        const auto centre = at(rig, (bounds->x0 + bounds->x1) / 2, (bounds->y0 + bounds->y1) / 2);

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, centre);
        QTRY_VERIFY_WITH_TIMEOUT(textTool(rig).editing(), 3'000);
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier); // inchangé
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
        QCOMPARE(steps(rig), before);

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, centre);
        QTRY_VERIFY_WITH_TIMEOUT(textTool(rig).editing(), 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(field(rig) != nullptr && field(rig)->hasActiveFocus(), 3'000);
        QTest::keyClick(rig.window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(rig.window, Qt::Key_Delete);
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        // Un texte existant ne devient jamais vide en silence : refus affiché, saisie ouverte.
        QVERIFY(textTool(rig).editing());
        QVERIFY(!textTool(rig).error().isEmpty());
        shot(rig, "6-refus-texte-vide");
        QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
                 QStringLiteral("Zoe"));
        QCOMPARE(steps(rig), before);
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
    }

    void savedProjectKeepsTheTextEditable() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Lea"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        // L'intention (texte, police, hauteur) est dans le document, pas dans l'interface.
        const auto& text = rig.session.project().text_objects.front();
        QCOMPARE(QString::fromStdString(text.text), QStringLiteral("Lea"));
        QVERIFY(!text.font.file.empty());
        QCOMPARE(text.cap_height.value, 12'000);
        QVERIFY(!rig.session.project().embroidery_objects.empty());
    }
    // P1 de la revue UX : « Valider » sortait de la carte. Chaque contrôle reste dans sa bordure,
    // et la carte reste dans la fenêtre, aux deux tailles.
    void editorCardKeepsEveryControlInsideItsBorder() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Marie"));
        auto* card = deepItem(rig.window->contentItem(), QStringLiteral("textEditorCard"));
        QVERIFY(card != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(card->isVisible() && card->width() > 0, 3'000);
        const QRectF cardRect = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
        const QRectF windowRect(0, 0, rig.window->width(), rig.window->height());
        QVERIFY2(windowRect.contains(cardRect), "la carte sort de la fenêtre");
        for (const char* name : {"textEditorScroll", "textEditorSizeRow", "textEditorButtonRow",
                                 "textEditorCapHeight", "textEditorCancel", "textEditorOk"}) {
            auto* item = deepItem(card, QString::fromLatin1(name));
            QVERIFY2(item != nullptr, name);
            QVERIFY2(item->isVisible(), name);
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            QVERIFY2(cardRect.adjusted(-1, -1, 1, 1).contains(r),
                     qPrintable(QStringLiteral("%1 sort de la carte : %2,%3 %4x%5 hors %6,%7 %8x%9")
                                    .arg(QString::fromLatin1(name))
                                    .arg(r.x())
                                    .arg(r.y())
                                    .arg(r.width())
                                    .arg(r.height())
                                    .arg(cardRect.x())
                                    .arg(cardRect.y())
                                    .arg(cardRect.width())
                                    .arg(cardRect.height())));
        }
        QTest::keyClick(rig.window, Qt::Key_Escape);
    }

    // P1 : l'aide de l'outil T n'était lisible qu'au fond d'un onglet. Une pastille s'affiche sur
    // le canevas dès que T est choisi, dans la fenêtre, sans masquer la pastille d'aperçu.
    void helpPillShowsOnTheCanvasOnceTheTextToolIsChosen() {
        Rig rig;
        open(rig);
        auto* hint = deepItem(rig.window->contentItem(), QStringLiteral("textToolHint"));
        QVERIFY(hint != nullptr);
        QVERIFY(!hint->isVisible()); // outil Sélection : rien
        chooseTextTool(rig);
        QTRY_VERIFY_WITH_TIMEOUT(hint->isVisible(), 3'000);
        auto* label = hint->childItems().isEmpty() ? nullptr : hint->childItems().front();
        QVERIFY(label != nullptr);
        QVERIFY(label->property("text").toString().contains(QStringLiteral("Cliquer")));
        const QRectF r = hint->mapRectToScene(QRectF(0, 0, hint->width(), hint->height()));
        QVERIFY(QRectF(0, 0, rig.window->width(), rig.window->height()).contains(r));
        clickToType(rig, 0.0, 0.0); // en saisie, la carte remplace la pastille
        QVERIFY(!hint->isVisible());
        QTest::keyClick(rig.window, Qt::Key_Escape);
    }

    // P1 : le texte noir disparaissait sur le fond sombre du canevas Vecteurs. Le texte créé doit
    // se voir : des pixels du fil par défaut (rouge chaud) à l'endroit du texte dans la fenêtre.
    void newTextIsReadableOnTheVectorCanvas() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Marie"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        waitForScene(rig);
        const auto& rgb = rig.session.project().text_objects.front().rgb;
        QCOMPARE(int(rgb[0]), 0xD2);
        QCOMPARE(int(rgb[1]), 0x48);
        QCOMPARE(int(rgb[2]), 0x3C);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto image = rig.window->grabWindow();
                const QPoint a = at(rig, -20.0, -8.0);
                const QPoint b = at(rig, 20.0, 8.0);
                int visible = 0;
                for (int y = a.y(); y < b.y(); ++y) {
                    for (int x = a.x(); x < b.x(); ++x) {
                        const auto c = image.pixelColor(x, y);
                        visible += c.red() > 150 && c.green() < 120 && c.blue() < 110 ? 1 : 0;
                    }
                }
                return visible > 150; // un trait de lettre fait bien plus de 150 pixels
            }(),
            8'000);
    }

    // Un novice tape de vrais mots : aucune lettre ne doit déclencher un raccourci d'outil
    // (T, F, R, E, P, N, S, D, K… sont globaux). Pangramme = toutes les lettres.
    void typingEveryLetterNeverFiresAShortcut() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        const auto words = QStringLiteral("The quick brown fox jumps over the lazy dog Theo Felix");
        typeText(rig.window, words);
        QCOMPARE(field(rig)->property("text").toString(), words);
        QCOMPARE(rig.tools.activeTool(), QStringLiteral("text")); // pas d'autre outil choisi
        QVERIFY(textTool(rig).editing());
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!textTool(rig).editing(), 3'000);
        QVERIFY(rig.session.project().text_objects.empty());
    }
    // Entrée avant de valider (réflexe du novice) ajoute un retour à la ligne : il ne doit pas
    // finir dans le texte brodé.
    void trailingNewlineBeforeValidatingIsNotPartOfTheText() {
        Rig rig;
        open(rig);
        chooseTextTool(rig);
        clickToType(rig, 0.0, 0.0);
        typeText(rig.window, QStringLiteral("Anne"));
        QTest::keyClick(rig.window, Qt::Key_Return); // Entrée seule : nouvelle ligne
        QCOMPARE(field(rig)->property("text").toString(), QStringLiteral("Anne\n"));
        QTest::keyClick(rig.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.project().text_objects.size(), std::size_t{1}, 5'000);
        QCOMPARE(QString::fromStdString(rig.session.project().text_objects.front().text),
                 QStringLiteral("Anne"));
    }
};

QTEST_MAIN(StudioTextToolTest)
#include "test_studio_text_tool.moc"
