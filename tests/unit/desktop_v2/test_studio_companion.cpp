// SPDX-License-Identifier: Apache-2.0
// Studio -- famille H : compagnon IA (faux fournisseur scripté, aucun réseau ni clé réelle),
// palette universelle, préférences, aide, focus clavier du canevas. Vrai moteur, vraie coque QML.
#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <deque>
#include <filesystem>

#include "action_controller.hpp"
#include "companion_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "help_controller.hpp"
#include "node_edit.hpp"
#include "preferences_controller.hpp"
#include "studio_controller.hpp"
#include "tool_controller.hpp"
#include "vector_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

constexpr const char* kKey = "sk-ant-TEST-SECRET-0123456789";

document::Project anchorProject() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    const double ys[] = {-30.0, 30.0};
    for (int i = 0; i < 2; ++i) {
        document::VectorObject vector;
        vector.id = ObjectId{11u + static_cast<unsigned>(i)};
        geometry::Path path;
        path.closed = false;
        const auto y = Micrometers{static_cast<std::int32_t>(ys[i] * 1000)};
        path.nodes = {{{Micrometers{-30'000}, y}}, {{Micrometers{30'000}, y}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{101u + static_cast<unsigned>(i)};
        object.source_vector = vector.id;
        object.name = "Repere";
        object.rgb = {20, 40, 200};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(300);
    return project;
}

struct Script {
    std::deque<agent::ProviderResult> replies;
};

agent::ProviderResult textReply(const std::string& text) {
    agent::ProviderResult r;
    r.ok = true;
    agent::ContentBlock b;
    b.text = text;
    r.response.blocks = {b};
    r.response.usage = {100, 20};
    return r;
}

agent::ProviderResult toolReply(const std::string& name, const std::string& args) {
    agent::ProviderResult r;
    r.ok = true;
    agent::ContentBlock b;
    b.kind = agent::ContentBlock::Kind::ToolUse;
    b.id = "t1";
    b.name = name;
    b.input_json = args;
    r.response.blocks = {b};
    r.response.usage = {50, 10};
    return r;
}

class ScriptedProvider final : public agent::IAgentProvider {
public:
    ScriptedProvider(std::shared_ptr<Script> script, std::shared_ptr<std::string> seenKey,
                     std::string key)
        : script_(std::move(script)), seenKey_(std::move(seenKey)), key_(std::move(key)) {}
    [[nodiscard]] std::string name() const override { return "Faux"; }
    [[nodiscard]] agent::ProviderCapabilities capabilities() const override { return {}; }
    agent::ProviderResult complete(const agent::ProviderRequest&,
                                   const agent::CancelToken&) override {
        *seenKey_ = key_;
        if (script_->replies.empty()) {
            agent::ProviderResult r;
            r.error = {agent::code::kNetwork, "plus de réponse", false, 0};
            return r;
        }
        auto r = script_->replies.front();
        script_->replies.pop_front();
        return r;
    }

private:
    std::shared_ptr<Script> script_;
    std::shared_ptr<std::string> seenKey_;
    std::string key_;
};

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

struct Rig {
    QTemporaryDir dir;
    agent::MemorySecretStore secrets;
    std::shared_ptr<Script> script = std::make_shared<Script>();
    std::shared_ptr<std::string> seenKey = std::make_shared<std::string>();
    DocumentSession session;
    StudioController controller{session};
    ActionController actions{controller};
    EditController editor{controller};
    ToolController tools{controller};
    VectorController vectors{controller};
    PreferencesController prefs{std::filesystem::path(dir.path().toStdWString())};
    HelpController help;
    CompanionController companion{
        controller,
        {std::filesystem::path(dir.filePath(QStringLiteral("live")).toStdWString()), &secrets,
         [this](const std::string& key, const std::string&) {
             return std::static_pointer_cast<agent::IAgentProvider>(
                 std::make_shared<ScriptedProvider>(script, seenKey, key));
         }}};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    GpuCanvas* canvas{nullptr};
};

} // namespace

class StudioCompanionTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig, bool withWindow = true);
    void settle(Rig& rig) {
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy() && rig.controller.scene() &&
                                     rig.controller.scene()->revision == rig.session.revision(),
                                 15'000);
    }
    [[nodiscard]] QPoint at(Rig& rig, double xMm, double yModelMm) {
        const auto s = rig.canvas->camera().worldToScreen({xMm, -yModelMm});
        return rig.canvas->mapToScene(QPointF(s.x, s.y)).toPoint();
    }
    [[nodiscard]] std::array<std::uint8_t, 3> colour(Rig& rig) {
        return rig.session.project().embroidery_objects.front().rgb;
    }
    // Attend une proposition, puis la décide ; renvoie son titre.
    void decideWhenPending(Rig& rig, bool approve, QString* title = nullptr, int* items = nullptr);

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("StudioCompanion"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by the application"));
        qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                                   QStringLiteral("Provided by the application"));
        qmlRegisterUncreatableType<ToolController>("OpenStitch.Studio", 1, 0, "ToolController",
                                                   QStringLiteral("Provided by the application"));
    }
    void init() { QSettings().clear(); }
    void canvasRegainsKeyboardFocusAfterClick();
    void paletteListsCommandsWithoutAccentsAndStates();
    void forgetSessionClearsAndPersists();
    void paletteCommandsRunThroughTheWindowDispatcher();
    void preferencesPersistAndRestore();
    void helpIsGeneratedFromDefinitions();
    void companionApproveIsOneUndoStep();
    void companionRejectChangesNothing();
    void documentReplacementRejectsPendingProposal();
    void keyIsOnlyInTheSecretStore();
    void bridgeFollowsTheOpenDocument();
    void companionPanelAndDialogsOpenFromQml();
};

void StudioCompanionTest::open(Rig& rig, bool withWindow) {
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
    ctx->setContextProperty(QStringLiteral("prefs"), &rig.prefs);
    ctx->setContextProperty(QStringLiteral("help"), &rig.help);
    ctx->setContextProperty(QStringLiteral("companion"), &rig.companion);
    if (withWindow) {
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
        QVERIFY(rig.canvas);
    }
    rig.session.replaceProject(anchorProject(), true);
    settle(rig);
    if (withWindow) {
        QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->geometryBuildCount() > 0, 10'000);
    }
    rig.tools.setSnapVertices(false);
}

void StudioCompanionTest::decideWhenPending(Rig& rig, bool approve, QString* title, int* items) {
    auto* timer = new QTimer(&rig.companion);
    timer->setInterval(20);
    connect(timer, &QTimer::timeout, timer, [&rig, timer, approve, title, items] {
        if (!rig.companion.proposalPending()) {
            return;
        }
        timer->stop();
        if (title != nullptr) {
            *title = rig.companion.proposal().value(QStringLiteral("title")).toString();
        }
        if (items != nullptr) {
            *items = rig.companion.proposal().value(QStringLiteral("items")).toList().size();
        }
        if (approve) {
            rig.companion.approve();
        } else {
            rig.companion.reject();
        }
        timer->deleteLater();
    });
    timer->start();
}

void StudioCompanionTest::canvasRegainsKeyboardFocusAfterClick() {
    Rig rig;
    open(rig);
    rig.window->requestActivate();
    // Le focus part sur un bouton visible, puis un clic sur le canevas le reprend.
    auto* fit = deepItem(rig.window->contentItem(), QStringLiteral("fitToolButton"));
    QVERIFY(fit);
    fit->forceActiveFocus();
    QCOMPARE(rig.window->activeFocusItem(), fit);
    rig.controller.selectObject(QStringLiteral("101"));
    rig.tools.setActiveTool(QStringLiteral("node.edit"));
    auto view = nodeView(rig.session.project(), 101);
    QVERIFY(view.has_value());
    const auto node = view->nodes[0].pos;
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, node.x, node.y));
    QVERIFY2(rig.window->activeFocusItem() == rig.canvas,
             qPrintable(QStringLiteral("focus sur %1")
                            .arg(rig.window->activeFocusItem()
                                     ? rig.window->activeFocusItem()->metaObject()->className()
                                     : "rien")));
    // Jamais de focus sur un élément invisible.
    QVERIFY(rig.window->activeFocusItem()->isVisible());
    QCOMPARE(rig.tools.selectedNodeCount(), 1);
    // Un nœud intermédiaire (double-clic sur le segment), puis S / C au clavier du canevas.
    const auto other = view->nodes[1].pos;
    QTest::mouseDClick(rig.window, Qt::LeftButton, {},
                       at(rig, (node.x + other.x) / 2, (node.y + other.y) / 2));
    const auto inserted = nodeView(rig.session.project(), 101);
    QCOMPARE(inserted->nodes.size(), std::size_t{3});
    const auto mid = inserted->nodes[1].pos;
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, mid.x, mid.y));
    QVERIFY(rig.window->activeFocusItem() == rig.canvas);
    QCOMPARE(rig.tools.selectedNodeCount(), 1);
    const auto base = rig.session.undoNames().size();
    QTest::keyClick(rig.window, Qt::Key_S);
    QVERIFY(nodeView(rig.session.project(), 101)->nodes[1].smooth);
    QCOMPARE(rig.session.undoNames().size(), base + 1);
    QTest::keyClick(rig.window, Qt::Key_C);
    QVERIFY(!nodeView(rig.session.project(), 101)->nodes[1].smooth);
    QCOMPARE(rig.session.undoNames().size(), base + 2);
    // Échap : le canevas garde le clavier et rien n'est modifié.
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QCOMPARE(rig.session.undoNames().size(), base + 2);
    QVERIFY(rig.window->activeFocusItem() == rig.canvas);
    // Un changement d'espace ne laisse pas le focus sur un contrôle masqué.
    // Les espaces gardent leurs canevas en cache ; seul celui de l'espace actif reçoit les clics.
    for (int space = 0; space < 4; ++space) {
        QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.workspace.%1").arg(space)).isEmpty());
        const auto activeCanvas = [&]() -> GpuCanvas* {
            GpuCanvas* found = nullptr;
            for (auto* candidate : rig.window->findChildren<GpuCanvas*>()) {
                if (candidate->isVisible()) {
                    if (found)
                        return nullptr; // exactly one interactive canvas
                    found = candidate;
                }
            }
            return found;
        };
        QTRY_VERIFY(activeCanvas() != nullptr);
        auto* active = activeCanvas();
        QVERIFY(active);
        QTRY_VERIFY(active->width() > 0 && active->height() > 0);
        if (space == 3) {
            rig.canvas = active; // l'espace Points est de retour : son canevas réutilisé
            QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, node.x, node.y));
        } else {
            QTest::mouseClick(
                rig.window, Qt::LeftButton, {},
                // Le centre peut être occupé par le bouton d'import d'un espace vide.
                active->mapToScene(QPointF(active->width() / 4, active->height() * 3 / 4))
                    .toPoint());
        }
        const auto* focused = rig.window->activeFocusItem();
        QVERIFY2(focused == active,
                 qPrintable(QStringLiteral("Workspace %1: expected %2, focused %3")
                                .arg(space)
                                .arg(active->objectName(),
                                     focused ? focused->objectName() : QStringLiteral("<none>"))));
    }
    // Une fenêtre modale fermée ne laisse pas le focus sur l'un de ses boutons masqués.
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.help.about")).isEmpty());
    auto* help = rig.window->findChild<QObject*>(QStringLiteral("helpDialog"));
    QVERIFY(help);
    QTRY_VERIFY(help->property("visible").toBool());
    QTest::keyClick(rig.window, Qt::Key_Escape);
    QTRY_VERIFY(!help->property("visible").toBool());
    QTRY_VERIFY(rig.window->activeFocusItem() == nullptr ||
                rig.window->activeFocusItem()->isVisible());
    QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, node.x, node.y));
    QVERIFY(rig.window->activeFocusItem() == rig.canvas);
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioCompanionTest::paletteListsCommandsWithoutAccentsAndStates() {
    Rig rig;
    open(rig, false);
    const auto find = [&](const QString& query, const QString& id) -> QVariantMap {
        for (const auto& item : rig.actions.searchAll(query)) {
            if (item.toMap().value(QStringLiteral("id")).toString() == id) {
                return item.toMap();
            }
        }
        return {};
    };
    // Sans accents, insensible à la casse, mots multiples.
    QVERIFY(!find(QStringLiteral("selectionner tout"), QStringLiteral("cmd.select.all")).isEmpty());
    QVERIFY(!find(QStringLiteral("PREFERENCES"), QStringLiteral("cmd.prefs.open")).isEmpty());
    QVERIFY(
        !find(QStringLiteral("espace segmentation"), QStringLiteral("cmd.workspace.1")).isEmpty());
    // Catalogue d'actions ET commandes dans la même liste ; outils inclus.
    const auto all = rig.actions.searchAll({});
    QVERIFY(all.size() > rig.actions.search({}).size());
    QVERIFY(!find({}, QStringLiteral("cmd.tool.node.edit")).isEmpty());
    QVERIFY(!find({}, QStringLiteral("cmd.file.open")).isEmpty());
    // États explicites : annuler grisé sans historique, sélection requise.
    rig.session.replaceProject(anchorProject(), false);
    settle(rig);
    rig.controller.clearSelection();
    const auto undo = find({}, QStringLiteral("cmd.edit.undo"));
    QVERIFY(!undo.value(QStringLiteral("enabled")).toBool());
    QVERIFY(!undo.value(QStringLiteral("reason")).toString().isEmpty());
    QVERIFY(!rig.actions.runCommand(QStringLiteral("cmd.edit.undo")).isEmpty());
    QVERIFY(!find({}, QStringLiteral("cmd.edit.delete")).value(QStringLiteral("enabled")).toBool());
    rig.controller.selectObject(QStringLiteral("101"));
    QVERIFY(find({}, QStringLiteral("cmd.edit.delete")).value(QStringLiteral("enabled")).toBool());
    // Les activées d'abord.
    bool seenDisabled = false;
    for (const auto& item : rig.actions.searchAll({})) {
        const bool enabled = item.toMap().value(QStringLiteral("enabled")).toBool();
        QVERIFY(!(seenDisabled && enabled));
        seenDisabled = seenDisabled || !enabled;
    }
    QVERIFY(!rig.actions.runCommand(QStringLiteral("cmd.nope")).isEmpty());
}

void StudioCompanionTest::paletteCommandsRunThroughTheWindowDispatcher() {
    Rig rig;
    open(rig);
    rig.controller.selectObject(QStringLiteral("101"));
    const auto steps = rig.session.undoNames().size();
    // Édition : même chemin que le raccourci Ctrl+D (une étape), puis annuler.
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.edit.duplicate")).isEmpty());
    QCOMPARE(rig.session.undoNames().size(), steps + 1);
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.edit.undo")).isEmpty());
    QCOMPARE(rig.session.undoNames().size(), steps);
    // Outil.
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.tool.draw.ellipse")).isEmpty());
    QCOMPARE(rig.tools.activeTool(), QStringLiteral("draw.ellipse"));
    // Sélection.
    rig.controller.selectObject(QStringLiteral("101"));
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.select.clear")).isEmpty());
    QVERIFY(!rig.controller.hasSelection());
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.select.all")).isEmpty());
    QVERIFY(rig.controller.hasSelection());
    // Boîtes de dialogue.
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.prefs.open")).isEmpty());
    auto* prefsDialog = rig.window->findChild<QObject*>(QStringLiteral("preferencesDialog"));
    QVERIFY(prefsDialog);
    QTRY_VERIFY(prefsDialog->property("visible").toBool());
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

// « Oublier la session » : l'état mémorisé disparaît, le signal part, et le choix persiste.
void StudioCompanionTest::forgetSessionClearsAndPersists() {
    QTemporaryDir dir;
    const std::filesystem::path path(dir.path().toStdWString());
    {
        PreferencesController prefs(path);
        prefs.rememberSession({{QStringLiteral("file"), QStringLiteral("C:/x/a.osp")}});
        QVERIFY(!prefs.session().isEmpty());
        QSignalSpy changed(&prefs, &PreferencesController::sessionChanged);
        prefs.forgetSession();
        QVERIFY(prefs.session().isEmpty());
        QCOMPARE(changed.count(), 1);
    }
    PreferencesController again(path);
    QVERIFY(again.session().isEmpty());
}

void StudioCompanionTest::preferencesPersistAndRestore() {
    QTemporaryDir dir;
    const std::filesystem::path path(dir.path().toStdWString());
    {
        PreferencesController prefs(path);
        QCOMPARE(prefs.theme(), QStringLiteral("system"));
        prefs.setTheme(QStringLiteral("light"));
        prefs.setDensity(QStringLiteral("compact"));
        prefs.setReducedMotion(true);
        prefs.setTheme(QStringLiteral("bogus")); // ignoré
        prefs.rememberSession({{QStringLiteral("file"), QStringLiteral("C:/x/a.osp")},
                               {QStringLiteral("workspace"), 2},
                               {QStringLiteral("editOpen"), true},
                               {QStringLiteral("zoom"), 3.5}});
    }
    PreferencesController again(path);
    QCOMPARE(again.theme(), QStringLiteral("light"));
    QCOMPARE(again.effectiveTheme(), QStringLiteral("light"));
    QCOMPARE(again.density(), QStringLiteral("compact"));
    QVERIFY(again.densityScale() < 1.0);
    QVERIFY(again.reducedMotion());
    const auto session = again.session();
    QCOMPARE(session.value(QStringLiteral("file")).toString(), QStringLiteral("C:/x/a.osp"));
    QCOMPARE(session.value(QStringLiteral("workspace")).toString(), QStringLiteral("2"));
    QCOMPARE(session.value(QStringLiteral("editOpen")).toString(), QStringLiteral("1"));
    QCOMPARE(again.languages(), QStringList{QStringLiteral("fr")});
    again.resetDefaults();
    QCOMPARE(again.theme(), QStringLiteral("system"));
    QVERIFY(!again.reducedMotion());
}

void StudioCompanionTest::helpIsGeneratedFromDefinitions() {
    HelpController help;
    QStringList seen;
    int rows = 0;
    for (const auto& group : help.shortcuts()) {
        for (const auto& item : group.toMap().value(QStringLiteral("items")).toList()) {
            seen << item.toMap().value(QStringLiteral("shortcut")).toString();
            ++rows;
        }
    }
    int expected = 0;
    for (const auto& command : studioCommands()) {
        expected += command.shortcut.empty() ? 0 : 1;
    }
    QCOMPARE(rows, expected);
    QVERIFY(seen.contains(QStringLiteral("Ctrl+K")) == false); // Ctrl+K est un geste documenté
    QVERIFY(seen.contains(QStringLiteral("Ctrl+Z")));
    QVERIFY(help.guide().size() >= 4);
    QVERIFY(!help.gestures().isEmpty());
    QVERIFY(help.aboutText().contains(QStringLiteral("Apache-2.0")));
    QVERIFY(help.toolHelp(QStringLiteral("node.edit"), QStringLiteral("astuce"))
                .contains(QStringLiteral("astuce")));
    QVERIFY(help.toolHelp(QStringLiteral("inconnu"), {}).isEmpty());
}

void StudioCompanionTest::companionApproveIsOneUndoStep() {
    Rig rig;
    open(rig, false);
    QVERIFY(!rig.companion.send(QStringLiteral("bonjour"))); // pas de clé : refus explicite
    QVERIFY(!rig.companion.error().isEmpty());
    rig.companion.resetConversation();
    QVERIFY(rig.companion.setApiKey(QString::fromLatin1(kKey)));
    rig.companion.setProviderEnabled(true);
    QVERIFY(rig.companion.providerReady());
    const auto before = colour(rig);
    const auto steps = rig.session.undoNames().size();
    rig.script->replies = {toolReply("project_apply",
                                     "{\"operations\":[{\"op\":\"set_color\",\"object\":101,"
                                     "\"rgb\":\"#FF0000\"}]}"),
                           textReply("Fait.")};
    QString title;
    int items = 0;
    decideWhenPending(rig, true, &title, &items);
    QVERIFY(rig.companion.send(QStringLiteral("passe l'objet en rouge")));
    QTRY_VERIFY_WITH_TIMEOUT(
        rig.companion.messages().last().toMap().value(QStringLiteral("text")).toString() == "Fait.",
        10'000);
    QVERIFY(!title.isEmpty());
    QVERIFY(items >= 1);
    QCOMPARE(colour(rig), (std::array<std::uint8_t, 3>{255, 0, 0}));
    QCOMPARE(rig.session.undoNames().size(), steps + 1); // UNE étape
    QCOMPARE(rig.companion.events().size(), 1);
    QCOMPARE(rig.companion.events().front().toMap().value(QStringLiteral("outcome")).toString(),
             QStringLiteral("applied"));
    QVERIFY(rig.companion.canUndo());
    rig.companion.undoLast();
    QCOMPARE(colour(rig), before);
    QCOMPARE(rig.session.undoNames().size(), steps);
    QVERIFY(!rig.companion.proposalPending());
}

void StudioCompanionTest::companionRejectChangesNothing() {
    Rig rig;
    open(rig, false);
    QVERIFY(rig.companion.setApiKey(QString::fromLatin1(kKey)));
    rig.companion.setProviderEnabled(true);
    const auto before = colour(rig);
    const auto steps = rig.session.undoNames().size();
    rig.script->replies = {toolReply("project_apply",
                                     "{\"operations\":[{\"op\":\"set_color\",\"object\":101,"
                                     "\"rgb\":\"#00FF00\"}]}"),
                           textReply("Refusé par l'utilisateur.")};
    decideWhenPending(rig, false);
    QVERIFY(rig.companion.send(QStringLiteral("passe en vert")));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.companion.busy() && !rig.companion.messages().isEmpty() &&
                                 rig.companion.messages().size() >= 2,
                             10'000);
    QCOMPARE(colour(rig), before);
    QCOMPARE(rig.session.undoNames().size(), steps);
    QCOMPARE(rig.companion.events().front().toMap().value(QStringLiteral("outcome")).toString(),
             QStringLiteral("refused"));
}

void StudioCompanionTest::documentReplacementRejectsPendingProposal() {
    Rig rig;
    open(rig, false);
    QVERIFY(rig.companion.setApiKey(QString::fromLatin1(kKey)));
    rig.companion.setProviderEnabled(true);
    rig.script->replies = {toolReply("project_apply",
                                     "{\"operations\":[{\"op\":\"set_color\",\"object\":101,"
                                     "\"rgb\":\"#FF0000\"}]}"),
                           textReply("Fin.")};
    auto* timer = new QTimer(&rig.companion);
    connect(timer, &QTimer::timeout, timer, [&] {
        if (rig.companion.proposalPending()) {
            timer->stop();
            rig.session.replaceProject(anchorProject(), true); // un autre document
        }
    });
    timer->start(20);
    QVERIFY(rig.companion.send(QStringLiteral("passe en rouge")));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.companion.busy() && !rig.companion.proposalPending(), 10'000);
    QCOMPARE(colour(rig), (std::array<std::uint8_t, 3>{20, 40, 200})); // rien d'appliqué
    QVERIFY(rig.session.undoNames().empty());
}

void StudioCompanionTest::keyIsOnlyInTheSecretStore() {
    Rig rig;
    open(rig, false);
    QVERIFY(!rig.companion.hasApiKey());
    QVERIFY(rig.companion.setApiKey(QString::fromLatin1(kKey)));
    QVERIFY(rig.companion.hasApiKey());
    rig.companion.setProviderEnabled(true);
    rig.script->replies = {textReply("Salut.")};
    QVERIFY(rig.companion.send(QStringLiteral("bonjour")));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.companion.busy() && rig.companion.messages().size() >= 2, 10'000);
    QCOMPARE(QString::fromStdString(*rig.seenKey), QString::fromLatin1(kKey)); // le fournisseur l'a
    QString everything;
    for (const auto& m : rig.companion.messages()) {
        everything += m.toMap().value(QStringLiteral("text")).toString();
    }
    everything += rig.companion.error() + rig.companion.usage();
    QVERIFY(!everything.contains(QString::fromLatin1(kKey)));
    // Aucun réglage QSettings ni fichier de préférences ne contient la clé.
    for (const auto& key : QSettings().allKeys()) {
        QVERIFY(!QSettings().value(key).toString().contains(QString::fromLatin1(kKey)));
    }
    rig.companion.clearApiKey();
    QVERIFY(!rig.companion.hasApiKey());
    QVERIFY(!rig.companion.providerReady());
}

void StudioCompanionTest::bridgeFollowsTheOpenDocument() {
    Rig rig;
    open(rig, false);
    QVERIFY(!rig.companion.bridgeActive());
    QVERIFY2(rig.companion.setBridgeEnabled(true).isEmpty(), "pont");
    QVERIFY(rig.companion.bridgeActive());
    QVERIFY(!rig.companion.endpoint().isEmpty());
    const auto session = rig.session.liveBridge().session_id();
    rig.session.replaceProject(anchorProject(), true); // nouveau document : nouvelle session
    QVERIFY(rig.session.liveBridge().session_id() != session);
    QVERIFY(rig.companion.prepareClaudeCode().isEmpty());
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("--live")));
    QVERIFY(rig.companion.setBridgeEnabled(false).isEmpty());
    QVERIFY(!rig.companion.bridgeActive());
    rig.companion.setFullyDisabled(true);
    QVERIFY(!rig.companion.setBridgeEnabled(true).isEmpty()); // désactivé : refus
    rig.companion.setFullyDisabled(false);
}

void StudioCompanionTest::companionPanelAndDialogsOpenFromQml() {
    Rig rig;
    open(rig);
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.companion.open")).isEmpty());
    auto* panel = rig.window->findChild<QObject*>(QStringLiteral("companionPanel"));
    QVERIFY(panel);
    QTRY_VERIFY(panel->property("visible").toBool());
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.help.shortcuts")).isEmpty());
    auto* sheet = rig.window->findChild<QObject*>(QStringLiteral("shortcutsSheet"));
    QVERIFY(sheet);
    QTRY_VERIFY(sheet->property("visible").toBool());
    QVERIFY(rig.actions.runCommand(QStringLiteral("cmd.help.about")).isEmpty());
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

QTEST_MAIN(StudioCompanionTest)
#include "test_studio_companion.moc"
