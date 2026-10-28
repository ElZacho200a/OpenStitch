// SPDX-License-Identifier: Apache-2.0
//
// Fournisseur Claude dans le dock (AI Companion 4), sans réseau ni clé réelle : faux fournisseur
// scripté, faux stockage de secrets. Question en lecture seule, outils refusés, approbation d'une
// modification dans le dock, erreurs non bloquantes, interruption, secret jamais affiché.
#include <deque>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>

#include "claude_backend.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_dock.hpp"
#include "companion_launcher.hpp"
#include "companion_settings_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/project_ops/inspect.hpp"

namespace openstitch::desktop {
namespace {

constexpr const char* kKey = "sk-ant-TEST-SECRET-0123456789";

agent::ProviderResult textReply(const std::string& text) {
    agent::ProviderResult r;
    r.ok = true;
    agent::ContentBlock b;
    b.text = text;
    r.response.blocks = {b};
    r.response.usage = {100, 20};
    return r;
}

agent::ProviderResult toolReply(const std::string& id, const std::string& name,
                                const std::string& args) {
    agent::ProviderResult r;
    r.ok = true;
    agent::ContentBlock b;
    b.kind = agent::ContentBlock::Kind::ToolUse;
    b.id = id;
    b.name = name;
    b.input_json = args;
    r.response.blocks = {b};
    r.response.usage = {50, 10};
    return r;
}

agent::ProviderResult errorReply(const char* code, const std::string& message) {
    agent::ProviderResult r;
    r.error = {code, message, false, 0};
    return r;
}

struct Script {
    std::deque<agent::ProviderResult> replies;
    std::vector<agent::ProviderRequest> requests;
    std::string seenKey;
    std::function<void()> onCall;
};

class ScriptedProvider final : public agent::IAgentProvider {
public:
    explicit ScriptedProvider(std::shared_ptr<Script> script) : script_(std::move(script)) {}
    [[nodiscard]] std::string name() const override { return "Faux"; }
    [[nodiscard]] agent::ProviderCapabilities capabilities() const override { return {}; }
    agent::ProviderResult complete(const agent::ProviderRequest& request,
                                   const agent::CancelToken& cancel) override {
        script_->requests.push_back(request);
        if (script_->onCall) {
            script_->onCall();
        }
        if (cancel.cancelled()) {
            return errorReply(agent::code::kCancelled, "interrompu");
        }
        if (script_->replies.empty()) {
            return errorReply(agent::code::kNetwork, "plus de réponse");
        }
        auto r = script_->replies.front();
        script_->replies.pop_front();
        return r;
    }

private:
    std::shared_ptr<Script> script_;
};

struct Fixture {
    document::Project project;
    ObjectId first;
};

Fixture oneShape() {
    Fixture f;
    auto& p = f.project;
    document::VectorObject v;
    v.id = p.object_ids.next();
    v.paths = {{geometry::rectangle_path(Vec2um{Micrometers{0}, Micrometers{-10'000}},
                                         Vec2um{Micrometers{10'000}, Micrometers{0}}),
                {}}};
    p.vector_objects.push_back(v);
    document::EmbroideryObject e;
    e.id = p.object_ids.next();
    e.name = "Emb";
    e.source_vector = v.id;
    e.rgb = {60, 30, 30};
    e.params = document::TatamiParams{};
    f.first = e.id;
    p.embroidery_objects.push_back(e);
    return f;
}

template <class T> T* child(QWidget* root, const char* name) {
    return root->findChild<T*>(QString::fromLatin1(name));
}

QString transcriptOf(QWidget* dock) {
    return child<QTextBrowser>(dock, "browser_companionTranscript")->toPlainText();
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<CompanionToolCall>();
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("ClaudeBackend"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void init() {
        QSettings().clear();
        companion_settings::setProviderEnabled(true);
    }
    void noKeyMeansNoProviderAndTheWelcomeScreen();
    void readOnlyQuestionKeepsTheProjectIntact();
    void forbiddenToolsAreRefusedAndNeverExecuted();
    void modificationIsApprovedInTheDockAndUndoneInOneStep();
    void providerErrorsAreNonBlockingAndNeverShowTheKey();
    void cancelStopsTheRequestAndTheDockStaysUsable();
    void disabledCompanionOffersNoProvider();
    void settingsDialogStoresKeyOnlyInTheSecretStore();
    void targetPhraseWorksEndToEndFromTheDesktop();
    void claudeCodeProcessParsesStructuredEvents();
    void claudeCodeStreamsAssistantTextWithoutRepeatingFinalResult();
    void launcherPreparesWorkspaceAndStartsTheUsersClaude();
    void launcherReportsMissingClaudeAndMissingMcpWithoutLaunching();
    void openClaudeCodeActionEnablesTheBridgeOnlyOnExplicitUse();
    void claudeCodeChatCanBeSelectedWithoutApiKey();
};

namespace {

// Configure un backend de test branché sur la fenêtre et le dock.
struct Rig {
    agent::MemorySecretStore secrets;
    std::shared_ptr<Script> script = std::make_shared<Script>();
    std::unique_ptr<ClaudeCompanionBackend> backend;

    Rig(MainWindow& w, bool withKey = true) {
        if (withKey) {
            secrets.write(companion_settings::kSecretKey, kKey);
        }
        auto s = script;
        backend = std::make_unique<ClaudeCompanionBackend>(
            w.liveService(), secrets, [s](const std::string& key, const std::string&) {
                s->seenKey = key;
                return std::make_shared<ScriptedProvider>(s);
            });
        w.companionDock()->setBackend(backend.get());
        w.companionDock()->show();
    }
    void ask(MainWindow& w, const QString& text) {
        child<QPlainTextEdit>(w.companionDock(), "edit_companionInput")->setPlainText(text);
        child<QPushButton>(w.companionDock(), "button_companionSend")->click();
    }
};

} // namespace

void MainWindowTest::noKeyMeansNoProviderAndTheWelcomeScreen() {
    MainWindow w;
    Rig rig(w, /*withKey=*/false);
    QVERIFY(!rig.backend->available());
    QCOMPARE(w.companionDock()->findChild<QStackedWidget*>()->currentIndex(), 0);
    QVERIFY(child<QPushButton>(w.companionDock(), "button_companionConfigure") != nullptr);
}

void MainWindowTest::readOnlyQuestionKeepsTheProjectIntact() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    Rig rig(w);
    QVERIFY(rig.backend->available());
    rig.script->replies = {toolReply("a", "live_status", "{}"),
                           toolReply("b", "project_inspect", "{}"),
                           textReply("Le projet contient 1 objet de broderie.")};
    rig.ask(w, QStringLiteral("Combien d'objets ?"));
    QTRY_VERIFY_WITH_TIMEOUT(
        transcriptOf(w.companionDock()).contains(QStringLiteral("contient 1 objet")), 8000);
    QTRY_VERIFY_WITH_TIMEOUT(!w.companionDock()->busy(), 8000);
    const QString text = transcriptOf(w.companionDock());
    QVERIFY(text.contains(QStringLiteral("project_inspect")));
    QVERIFY(!w.undoAct_->isEnabled()); // projet intact, aucune étape d'annulation
    QCOMPARE(w.project_.embroidery_objects.front().rgb, (std::array<std::uint8_t, 3>{60, 30, 30}));
    QCOMPARE(QString::fromStdString(rig.script->seenKey), QString::fromLatin1(kKey));
    // Seuls les outils du périmètre OpenStitch sont proposés au modèle (pas de fichier, pas de
    // shell).
    QVERIFY(!rig.script->requests.empty());
    for (const auto& tool : rig.script->requests.front().tools) {
        QVERIFY2(tool.name != "project_open" && tool.name != "project_save" &&
                     tool.name != "project_export" && tool.name != "bash",
                 tool.name.c_str());
    }
    // L'utilisation est affichée.
    QVERIFY(child<QLabel>(w.companionDock(), "label_companionHeader")
                ->text()
                .contains(QStringLiteral("jetons")));
}

void MainWindowTest::forbiddenToolsAreRefusedAndNeverExecuted() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    Rig rig(w);
    rig.script->replies = {toolReply("a", "project_open", "{\"path\":\"C:/secret.osp\"}"),
                           toolReply("b", "bash", "{\"cmd\":\"dir\"}"),
                           textReply("Je ne peux pas.")};
    rig.ask(w, QStringLiteral("ouvre un fichier"));
    QTRY_VERIFY_WITH_TIMEOUT(transcriptOf(w.companionDock()).contains(QStringLiteral("Je ne peux")),
                             8000);
    const QString text = transcriptOf(w.companionDock());
    QVERIFY(text.contains(QStringLiteral("Refusé")));
    QVERIFY(!w.undoAct_->isEnabled());
    QVERIFY(w.currentProjectPath_.isEmpty()); // aucun fichier ouvert
}

void MainWindowTest::modificationIsApprovedInTheDockAndUndoneInOneStep() {
    MainWindow w;
    auto f = oneShape();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    Rig rig(w);
    const std::string apply =
        "{\"operations\":[{\"op\":\"set_color\",\"object\":" + std::to_string(first.value) +
        ",\"rgb\":\"#FF0000\"}]}";
    rig.script->replies = {toolReply("a", "project_apply", apply), textReply("Fait.")};
    // L'utilisateur approuve dans le dock dès que la proposition apparaît.
    QTimer approve;
    approve.setInterval(20);
    connect(&approve, &QTimer::timeout, &approve, [&] {
        auto* group = child<QWidget>(w.companionDock(), "group_companionProposal");
        if (group != nullptr && group->isVisibleTo(w.companionDock())) {
            child<QPushButton>(w.companionDock(), "button_companionConfirm")->click();
        }
    });
    approve.start();
    rig.ask(w, QStringLiteral("passe l'objet en rouge"));
    QTRY_VERIFY_WITH_TIMEOUT(transcriptOf(w.companionDock()).contains(QStringLiteral("Fait.")),
                             8000);
    approve.stop();
    QCOMPARE(w.project_.embroidery_objects.front().rgb, (std::array<std::uint8_t, 3>{255, 0, 0}));
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{1});
    w.undoAct_->trigger();
    QCOMPARE(w.project_.embroidery_objects.front().rgb, (std::array<std::uint8_t, 3>{60, 30, 30}));
}

void MainWindowTest::providerErrorsAreNonBlockingAndNeverShowTheKey() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    Rig rig(w);
    rig.script->replies = {errorReply(agent::code::kQuota, std::string("quota pour ") + kKey)};
    rig.ask(w, QStringLiteral("bonjour"));
    QTRY_VERIFY_WITH_TIMEOUT(!w.companionDock()->busy(), 8000);
    const QString text = transcriptOf(w.companionDock());
    QVERIFY(text.contains(QStringLiteral("Erreur")));
    QVERIFY(!text.contains(QStringLiteral("SECRET")));
    QVERIFY(!child<QLabel>(w.companionDock(), "label_companionError")
                 ->text()
                 .contains(QStringLiteral("SECRET")));
    // L'édition standard reste utilisable et le dock accepte une nouvelle demande.
    QVERIFY(w.centralWidget()->isEnabled());
    rig.script->replies = {textReply("De retour.")};
    rig.ask(w, QStringLiteral("encore"));
    QTRY_VERIFY_WITH_TIMEOUT(transcriptOf(w.companionDock()).contains(QStringLiteral("De retour")),
                             8000);
}

void MainWindowTest::cancelStopsTheRequestAndTheDockStaysUsable() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    Rig rig(w);
    auto* cancel = child<QPushButton>(w.companionDock(), "button_companionCancel");
    // Le fournisseur « attend » : l'interruption de l'utilisateur est vue au tour suivant.
    rig.script->onCall = [&] {
        QMetaObject::invokeMethod(cancel, &QPushButton::click, Qt::QueuedConnection);
        QThread::msleep(150);
    };
    rig.script->replies = {toolReply("a", "project_inspect", "{}"), textReply("jamais affiché")};
    rig.ask(w, QStringLiteral("lent"));
    QTRY_VERIFY_WITH_TIMEOUT(!w.companionDock()->busy(), 8000);
    QVERIFY(!transcriptOf(w.companionDock()).contains(QStringLiteral("jamais affiché")));
    QVERIFY(!w.undoAct_->isEnabled());
}

void MainWindowTest::disabledCompanionOffersNoProvider() {
    MainWindow w;
    Rig rig(w);
    QVERIFY(rig.backend->available());
    companion_settings::setFullyDisabled(true);
    rig.backend->refresh();
    QVERIFY(!rig.backend->available());
    companion_settings::setFullyDisabled(false);
    companion_settings::setProviderEnabled(false);
    rig.backend->refresh();
    QVERIFY(!rig.backend->available()); // fournisseur non activé : rien n'est jamais envoyé
}

void MainWindowTest::settingsDialogStoresKeyOnlyInTheSecretStore() {
    agent::MemorySecretStore secrets;
    agent::DiagnosticLog log;
    CompanionSettingsDialog dialog(secrets, log);
    child<QLineEdit>(&dialog, "edit_companionKey")->setText(QString::fromLatin1(kKey));
    child<QPushButton>(&dialog, "button_companionSaveKey")->click();
    QCOMPARE(secrets.read(companion_settings::kSecretKey), std::optional<std::string>(kKey));
    QVERIFY(child<QLineEdit>(&dialog, "edit_companionKey")->text().isEmpty());
    // Jamais de clé dans QSettings.
    const QSettings settings;
    for (const QString& k : settings.allKeys()) {
        QVERIFY2(!settings.value(k).toString().contains(QStringLiteral("SECRET")), qPrintable(k));
    }
    child<QCheckBox>(&dialog, "check_companionProvider")->setChecked(true);
    dialog.applyToSettings();
    QVERIFY(companion_settings::providerEnabled());
    child<QPushButton>(&dialog, "button_companionRemoveKey")->click();
    QVERIFY(!secrets.read(companion_settings::kSecretKey).has_value());
}

namespace {

// Extrait le tableau JSON equilibre qui suit `marker` (sans analyseur JSON : le desktop ne lie pas
// nlohmann).
std::string arrayAfter(const std::string& text, const std::string& marker) {
    const auto start = text.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const auto open = text.find('[', start);
    int depth = 0;
    bool inString = false;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\')) {
            inString = !inString;
        } else if (!inString && c == '[') {
            ++depth;
        } else if (!inString && c == ']' && --depth == 0) {
            return text.substr(open, i - open + 1);
        }
    }
    return {};
}

std::string lastToolResultText(const agent::ProviderRequest& request) {
    const auto& blocks = request.messages.back().blocks;
    if (blocks.empty() || blocks.front().content.empty()) {
        return {};
    }
    return blocks.front().content.front().text;
}

class DynamicProvider final : public agent::IAgentProvider {
public:
    std::function<agent::ProviderResult(const agent::ProviderRequest&)> fn;
    [[nodiscard]] std::string name() const override { return "Faux"; }
    [[nodiscard]] agent::ProviderCapabilities capabilities() const override { return {}; }
    agent::ProviderResult complete(const agent::ProviderRequest& r,
                                   const agent::CancelToken&) override {
        return fn(r);
    }
};

} // namespace

void MainWindowTest::targetPhraseWorksEndToEndFromTheDesktop() {
    MainWindow w;
    // Trois colonnes orange (fils) + une rouge : "mets les formes orange en satin, Predicted".
    document::Project project;
    const auto column = [&](int x_mm, std::array<std::uint8_t, 3> rgb) {
        document::VectorObject v;
        v.id = project.object_ids.next();
        v.paths = {
            {geometry::rectangle_path(Vec2um{Micrometers{x_mm * 1000}, Micrometers{0}},
                                      Vec2um{Micrometers{x_mm * 1000 + 4000}, Micrometers{20'000}}),
             {}}};
        project.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.name = "Colonne";
        e.source_vector = v.id;
        e.rgb = rgb;
        e.params = document::TatamiParams{};
        project.embroidery_objects.push_back(e);
    };
    column(0, {232, 116, 42});
    column(6, {240, 140, 30});
    column(12, {230, 100, 20});
    column(18, {200, 30, 30});
    w.applyLoadedProject(std::move(project));
    const auto typesNow = [&] {
        std::vector<std::string> types;
        for (const auto& e : w.project_.embroidery_objects) {
            types.emplace_back(project_ops::stitch_type_name(e));
        }
        return types;
    };
    const auto before = typesNow();

    Rig rig(w);
    int step = 0;
    std::string appliedOps;
    auto provider = std::make_shared<DynamicProvider>();
    provider->fn = [&](const agent::ProviderRequest& request) {
        ++step;
        const std::string last = lastToolResultText(request);
        switch (step) {
        case 1:
            return toolReply("1", "live_context", "{}");
        case 2:
            return toolReply("2", "live_target", "{\"query\":\"orange\"}");
        case 3:
            return toolReply("3", "live_plan_satin", "{\"objects\":[2,4,6]}");
        case 4:
            appliedOps = arrayAfter(last, "\"batch\":{\"operations\":");
            return toolReply("4", "live_preview", "{\"operations\":" + appliedOps + "}");
        case 5:
            return toolReply("5", "project_apply", "{\"operations\":" + appliedOps + "}");
        default:
            return textReply("Les formes orange compatibles sont en satin (Predicted).");
        }
    };
    rig.backend = std::make_unique<ClaudeCompanionBackend>(
        w.liveService(), rig.secrets,
        [provider](const std::string&, const std::string&) { return provider; });
    w.companionDock()->setBackend(rig.backend.get());

    QTimer approve;
    approve.setInterval(20);
    connect(&approve, &QTimer::timeout, &approve, [&] {
        auto* group = child<QWidget>(w.companionDock(), "group_companionProposal");
        if (group != nullptr && group->isVisibleTo(w.companionDock())) {
            child<QPushButton>(w.companionDock(), "button_companionConfirm")->click();
        }
    });
    approve.start();
    rig.ask(w, QStringLiteral("Mets toutes les formes orange compatibles en satin avec Predicted"));
    QTRY_VERIFY_WITH_TIMEOUT(
        transcriptOf(w.companionDock()).contains(QStringLiteral("compatibles sont en satin")),
        60000);
    approve.stop();

    const QString text = transcriptOf(w.companionDock());
    for (const char* tool :
         {"live_context", "live_target", "live_plan_satin", "live_preview", "project_apply"}) {
        QVERIFY2(text.contains(QString::fromLatin1(tool)), tool);
    }
    QVERIFY(!appliedOps.empty());
    // Sans export ni rechargement : le document ouvert a change, en UNE etape, la rouge est
    // intacte.
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{1});
    const auto after = typesNow();
    QVERIFY(after != before);
    QCOMPARE(after.back(), before.back());
    w.undoAct_->trigger();
    QCOMPARE(typesNow(), before);
}

namespace {

QString makeFakeMcp(const QString& appDir) {
    QDir().mkpath(appDir + QStringLiteral("/mcp"));
    QFile f(appDir + QStringLiteral("/mcp/openstitch-mcp.exe"));
    f.open(QIODevice::WriteOnly);
    f.write("x");
    return f.fileName();
}

} // namespace

void MainWindowTest::claudeCodeProcessParsesStructuredEvents() {
    ClaudeCodeProcessBackend backend(QStringLiteral("fake-claude"),
                                     QStringLiteral("/virtual/workspace"));
    QSignalSpy text(&backend, &CompanionBackend::assistantText);
    QSignalSpy tools(&backend, &CompanionBackend::toolCalled);
    QSignalSpy usage(&backend, &CompanionBackend::usage);
    backend.ingestEventForTest(
        R"({"type":"assistant","message":{"content":[{"type":"tool_use","id":"t1","name":"mcp__openstitch__live_context","input":{}}]}})");
    backend.ingestEventForTest(
        R"({"type":"assistant","message":{"content":[{"type":"tool_use","id":"t1","name":"mcp__openstitch__live_context","input":{}}]}})");
    backend.ingestEventForTest(
        R"({"type":"result","is_error":false,"session_id":"unit-session","result":"Trois objets orange","usage":{"input_tokens":120,"output_tokens":22}})");
    QCOMPARE(tools.size(), 1);
    QCOMPARE(text.size(), 1);
    QCOMPARE(text.at(0).at(0).toString(), QStringLiteral("Trois objets orange"));
    QCOMPARE(usage.size(), 1);
    QCOMPARE(usage.at(0).at(0).toULongLong(), quint64{120});
}

void MainWindowTest::claudeCodeStreamsAssistantTextWithoutRepeatingFinalResult() {
    ClaudeCodeProcessBackend backend(QStringLiteral("fake-claude"),
                                     QStringLiteral("/virtual/workspace"));
    QSignalSpy text(&backend, &CompanionBackend::assistantText);

    const QByteArray assistant =
        R"({"type":"assistant","message":{"id":"msg-live-1",)"
        R"("content":[{"type":"text","text":"Trois objets orange identifies."}]}})";
    backend.ingestEventForTest(assistant);
    // Le texte arrive AVANT l'evenement final.
    QCOMPARE(text.size(), 1);
    backend.ingestEventForTest(assistant); // Retansmission d'un meme message : ignoree.
    backend.ingestEventForTest(
        R"({"type":"result","is_error":false,"session_id":"12345678-1234-1234-1234-123456789abc","result":"Trois objets orange identifies."})");
    QCOMPARE(text.size(), 1);
    QCOMPARE(text.at(0).at(0).toString(), QStringLiteral("Trois objets orange identifies."));

    // Les versions de CLI qui n'envoient pas de message assistant conservent le resultat final.
    ClaudeCodeProcessBackend fallback(QStringLiteral("fake-claude"),
                                      QStringLiteral("/virtual/workspace"));
    QSignalSpy fallbackText(&fallback, &CompanionBackend::assistantText);
    fallback.ingestEventForTest(R"({"type":"result","result":"Reponse finale"})");
    QCOMPARE(fallbackText.size(), 1);
    QCOMPARE(fallbackText.at(0).at(0).toString(), QStringLiteral("Reponse finale"));
}

void MainWindowTest::launcherPreparesWorkspaceAndStartsTheUsersClaude() {
    QTemporaryDir tmp;
    const QString app = tmp.path() + QStringLiteral("/app");
    makeFakeMcp(app);
    QString program;
    QStringList args;
    ClaudeCodeLauncher::Env env;
    env.appDir = app;
    env.dataDir = tmp.path() + QStringLiteral("/data");
    env.findExecutable = [](const QString& name) {
        return name == QLatin1String("claude")
                   ? QStringLiteral("C:/bin/claude.exe")
                   : (name == QLatin1String("wt") ? QStringLiteral("C:/bin/wt.exe") : QString());
    };
    env.startDetached = [&](const QString& p, const QStringList& a, const QString&) {
        program = p;
        args = a;
        return true;
    };
    QCOMPARE(ClaudeCodeLauncher::launch(env), ClaudeCodeLauncher::Result::Started);
    QCOMPARE(program, QStringLiteral("C:/bin/wt.exe"));
    QVERIFY(args.contains(QStringLiteral("C:/bin/claude.exe")));
    const QString ws = ClaudeCodeLauncher::workspaceDir(env.dataDir);
    QVERIFY(QFileInfo::exists(ws + QStringLiteral("/.mcp.json")));
    QFile mcpConfig(ws + QStringLiteral("/.mcp.json"));
    QVERIFY(mcpConfig.open(QIODevice::ReadOnly));
    const QByteArray configText = mcpConfig.readAll();
    QVERIFY(configText.contains("--pid"));
    QVERIFY(configText.contains(QByteArray::number(QCoreApplication::applicationPid())));
    QVERIFY(QFileInfo::exists(ws + QStringLiteral("/.claude/settings.json")));
    QVERIFY(
        QFileInfo::exists(ws + QStringLiteral("/.claude/skills/openstitch-live-satin/SKILL.md")));
}

void MainWindowTest::launcherReportsMissingClaudeAndMissingMcpWithoutLaunching() {
    QTemporaryDir tmp;
    ClaudeCodeLauncher::Env env;
    env.appDir = tmp.path();
    env.dataDir = tmp.path() + QStringLiteral("/data");
    bool launched = false;
    env.startDetached = [&](const QString&, const QStringList&, const QString&) {
        launched = true;
        return true;
    };
    env.findExecutable = [](const QString&) { return QString(); };
    QCOMPARE(ClaudeCodeLauncher::launch(env), ClaudeCodeLauncher::Result::ClaudeNotFound);
    QVERIFY(ClaudeCodeLauncher::describe(ClaudeCodeLauncher::Result::ClaudeNotFound)
                .contains(QStringLiteral("installé")));
    env.findExecutable = [](const QString& n) {
        return n == QLatin1String("claude") ? QStringLiteral("C:/c.exe") : QString();
    };
    QCOMPARE(ClaudeCodeLauncher::launch(env), ClaudeCodeLauncher::Result::McpNotFound);
    QVERIFY(!launched);
    QVERIFY(!QFileInfo::exists(ClaudeCodeLauncher::workspaceDir(env.dataDir)));
}

void MainWindowTest::openClaudeCodeActionEnablesTheBridgeOnlyOnExplicitUse() {
    QTemporaryDir tmp;
    makeFakeMcp(tmp.path() + QStringLiteral("/app"));
    MainWindow w;
    auto* bridge = w.findChild<QAction*>(QStringLiteral("action_live_bridge"));
    QVERIFY(bridge != nullptr && !bridge->isChecked()); // jamais actif d'office
    bool launched = false;
    w.launcherEnv_.appDir = tmp.path() + QStringLiteral("/app");
    w.launcherEnv_.dataDir = tmp.path() + QStringLiteral("/data");
    w.launcherEnv_.findExecutable = [](const QString& n) {
        return n == QLatin1String("claude") ? QStringLiteral("C:/c.exe") : QString();
    };
    w.launcherEnv_.startDetached = [&](const QString&, const QStringList&, const QString&) {
        launched = true;
        return true;
    };
    w.findChild<QAction*>(QStringLiteral("action_open_claude_code"))->trigger();
    QVERIFY(launched);
    launched = false;
    auto* launchButton = w.companionDock()->findChild<QPushButton*>(
        QStringLiteral("button_companionOpenClaudeCode"));
    QVERIFY(launchButton != nullptr);
    launchButton->click();
    QVERIFY(launched);
    QVERIFY(bridge->isChecked()); // activé par cette action explicite
    w.setLiveBridgeEnabled(false);
}

void MainWindowTest::claudeCodeChatCanBeSelectedWithoutApiKey() {
    QTemporaryDir tmp;
    makeFakeMcp(tmp.path() + QStringLiteral("/app"));
    MainWindow w;
    w.launcherEnv_.appDir = tmp.path() + QStringLiteral("/app");
    w.launcherEnv_.dataDir = tmp.path() + QStringLiteral("/data");
    w.launcherEnv_.findExecutable = [](const QString& n) {
        return n == QLatin1String("claude") ? QStringLiteral("C:/bin/claude.exe") : QString();
    };
    w.launcherEnv_.startDetached = [](const QString&, const QStringList&, const QString&) {
        return false; // Le chat dans le dock n'ouvre PAS un terminal.
    };
    auto* button = w.companionDock()->findChild<QPushButton*>(
        QStringLiteral("button_companionChatClaudeCode"));
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(w.companionDock()->backend()->providerName(), QStringLiteral("Claude Code (MCP)"));
    auto* bridge = w.findChild<QAction*>(QStringLiteral("action_live_bridge"));
    QVERIFY(bridge != nullptr && bridge->isChecked());
    w.setLiveBridgeEnabled(false);
}

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_claude_backend.moc"
