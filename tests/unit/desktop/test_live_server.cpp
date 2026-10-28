// SPDX-License-Identifier: Apache-2.0
//
// Pont IA local (ADR 0001) : un vrai client (tube nommé / socket local, processus distinct
// simulé par un thread) lit et modifie le document OUVERT, sous jeton et approbation.
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <chrono>
#include <filesystem>
#include <future>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/mcp/live.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::desktop {
namespace {

struct Fixture {
    document::Project project;
    ObjectId first;
    ObjectId second;
};

Fixture twoShapes() {
    Fixture f;
    auto& p = f.project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        v.name = i == 0 ? "A" : "B";
        v.paths = {
            {geometry::rectangle_path(Vec2um{Micrometers{i * 20'000}, Micrometers{-10'000}},
                                      Vec2um{Micrometers{i * 20'000 + 10'000}, Micrometers{0}}),
             {}}};
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.name = i == 0 ? "Emb A" : "Emb B";
        e.source_vector = v.id;
        e.rgb = {static_cast<std::uint8_t>(60 + i * 100), 30, 30};
        e.params = document::TatamiParams{};
        (i == 0 ? f.first : f.second) = e.id;
        p.embroidery_objects.push_back(e);
    }
    return f;
}

std::array<std::uint8_t, 3> colorOf(const document::Project& p, ObjectId id) {
    for (const auto& e : p.embroidery_objects) {
        if (e.id == id) {
            return e.rgb;
        }
    }
    return {};
}

std::string applyArgs(ObjectId id, const char* hex) {
    return "{\"operations\":[{\"op\":\"set_color\",\"object\":" + std::to_string(id.value) +
           ",\"rgb\":\"" + hex + "\"}]}";
}

// Client MCP simulé : découvre l'hôte comme `openstitch-mcp --live`, appelle un outil.
std::string externalCall(const std::filesystem::path& dir, const std::string& tool,
                         const std::string& args) {
    const auto found = mcp::find_live_endpoint(dir);
    if (!found) {
        return "NO_ENDPOINT";
    }
    auto client = mcp::LocalLineClient::connect(found->endpoint);
    if (!client) {
        return "NO_CONNECT";
    }
    mcp::LiveRelay relay(*client, [token = found->token] { return token; });
    return relay.call(tool, args);
}

template <class F> std::string waitFor(std::future<std::string>& f, F&& pump) {
    while (f.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
        pump();
    }
    return f.get();
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("LiveServer"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void externalClientReadsAndModifiesTheOpenDocument();
    void refusedApprovalLeavesTheDocumentUntouched();
    void disablingTheBridgeDuringApprovalDoesNotModifyTheDocument();
    void wrongTokenIsRefusedAndDisabledBridgeLeavesNoEndpoint();
};

void MainWindowTest::externalClientReadsAndModifiesTheOpenDocument() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const std::filesystem::path dir = tmp.path().toStdString();
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    w.undoStack_.markClean();
    w.setWindowModified(false);
    QString error;
    QVERIFY2(w.setLiveBridgeEnabled(true, dir, &error), qPrintable(error));
    int asked = 0;
    w.liveGate_.setOverride([&](const mcp::ApprovalRequest&) {
        ++asked;
        return true;
    });

    auto pump = [] { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); };
    auto status = std::async(std::launch::async, externalCall, dir, "live_status", "{}");
    const std::string statusReply = waitFor(status, pump);
    QVERIFY2(statusReply.find("\"isError\":false") != std::string::npos, statusReply.c_str());
    QCOMPARE(asked, 0);

    auto inspect = std::async(std::launch::async, externalCall, dir, "project_inspect", "{}");
    const std::string inspectReply = waitFor(inspect, pump);
    QVERIFY2(inspectReply.find("\"isError\":false") != std::string::npos, inspectReply.c_str());
    QCOMPARE(asked, 0);
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{0});

    auto apply = std::async(std::launch::async, externalCall, dir, "project_apply",
                            applyArgs(first, "#FF0000"));
    const std::string applyReply = waitFor(apply, pump);
    QVERIFY2(applyReply.find("\"applied\":true") != std::string::npos, applyReply.c_str());
    QCOMPARE(asked, 1);
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{255, 0, 0}));
    QVERIFY(w.isWindowModified());
    QCOMPARE(w.undoStack_.undoNames().size(), std::size_t{1});
    w.undoAct_->trigger();
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
    QVERIFY(!w.isWindowModified());
}

void MainWindowTest::refusedApprovalLeavesTheDocumentUntouched() {
    QTemporaryDir tmp;
    const std::filesystem::path dir = tmp.path().toStdString();
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    QVERIFY(w.setLiveBridgeEnabled(true, dir));
    w.liveGate_.setOverride([](const mcp::ApprovalRequest&) { return false; });

    auto pump = [] { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); };
    auto apply = std::async(std::launch::async, externalCall, dir, "project_apply",
                            applyArgs(first, "#FF0000"));
    const std::string reply = waitFor(apply, pump);
    QVERIFY2(reply.find("E_APPROVAL_DENIED") != std::string::npos, reply.c_str());
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
    QVERIFY(!w.undoAct_->isEnabled());
}

void MainWindowTest::disablingTheBridgeDuringApprovalDoesNotModifyTheDocument() {
    QTemporaryDir tmp;
    const std::filesystem::path dir = tmp.path().toStdString();
    MainWindow w;
    auto f = twoShapes();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    QVERIFY(w.setLiveBridgeEnabled(true, dir));

    bool asked = false;
    bool stopped = false;
    w.liveGate_.setOverride([&](const mcp::ApprovalRequest&) {
        asked = true;
        stopped = w.setLiveBridgeEnabled(false);
        return true; // Meme une approbation tardive ne doit plus rien appliquer.
    });

    auto pump = [] { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); };
    auto apply = std::async(std::launch::async, externalCall, dir, "project_apply",
                            applyArgs(first, "#FF0000"));
    const std::string reply = waitFor(apply, pump);
    QVERIFY2(asked, reply.c_str());
    QVERIFY(stopped);
    QCOMPARE(colorOf(w.project_, first), (std::array<std::uint8_t, 3>{60, 30, 30}));
    QVERIFY(!w.undoAct_->isEnabled());
    QVERIFY(!mcp::find_live_endpoint(dir).has_value());
}

void MainWindowTest::wrongTokenIsRefusedAndDisabledBridgeLeavesNoEndpoint() {
    QTemporaryDir tmp;
    const std::filesystem::path dir = tmp.path().toStdString();
    MainWindow w;
    auto f = twoShapes();
    w.applyLoadedProject(std::move(f.project));
    QVERIFY(w.setLiveBridgeEnabled(true, dir));

    const auto endpoint = mcp::find_live_endpoint(dir);
    QVERIFY(endpoint.has_value());
    auto pump = [] { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); };
    auto bad = std::async(std::launch::async, [endpoint] {
        auto client = mcp::LocalLineClient::connect(endpoint->endpoint);
        if (!client) {
            return std::string("NO_CONNECT");
        }
        return client->exchange("{\"v\":1,\"id\":1,\"token\":\"faux\",\"op\":\"ping\"}")
            .value_or("NO_REPLY");
    });
    const std::string reply = waitFor(bad, pump);
    QVERIFY2(reply.find("E_UNAUTHORIZED") != std::string::npos, reply.c_str());

    QVERIFY(w.setLiveBridgeEnabled(false));
    QVERIFY(!mcp::find_live_endpoint(dir).has_value()); // fichier de découverte supprimé
    auto gone = std::async(std::launch::async, externalCall, dir, "live_status", "{}");
    QCOMPARE(QString::fromStdString(waitFor(gone, pump)), QStringLiteral("NO_ENDPOINT"));
}

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_live_server.moc"
