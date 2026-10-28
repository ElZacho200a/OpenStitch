// SPDX-License-Identifier: Apache-2.0
//
// Dock « Compagnon IA » (AI Companion 3) : ancrage et réouverture, écran d'accueil sans
// fournisseur, envoi (Entrée / Maj+Entrée), annulation, cartes d'outils, changement proposé
// (confirmer / rejeter / focus), changement de document, approbation d'un agent via le dock.
// Faux backend : aucun réseau, aucun fournisseur.
#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>

#include "companion_backend.hpp"
#include "companion_dock.hpp"
#include "main_window.hpp"
#include "openstitch/geometry/primitives.hpp"

namespace openstitch::desktop {
namespace {

class MockBackend final : public CompanionBackend {
    Q_OBJECT
public:
    bool isAvailable{true};
    QStringList sent;
    int cancels{0};
    QList<bool> decisions;

    [[nodiscard]] QString providerName() const override {
        return QStringLiteral("Faux fournisseur");
    }
    [[nodiscard]] bool available() const override { return isAvailable; }
public slots:
    void send(const QString& text) override { sent << text; }
    void cancel() override { ++cancels; }
    void decide(bool approved) override { decisions << approved; }
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

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<CompanionToolCall>();
        qRegisterMetaType<CompanionProposal>();
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTests"));
        QCoreApplication::setApplicationName(QStringLiteral("CompanionDock"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }
    void dockIsHiddenByDefaultAndReopensFromThePanelsMenu();
    void layoutRoundTripKeepsDockStateAndOtherDocks();
    void welcomeScreenWithoutProviderOffersTheLocalBridge();
    void enterSendsAndShiftEnterInsertsANewLine();
    void sendingShowsBusyStateAndCancelReachesTheBackend();
    void toolCardsAndErrorsAreShownWithoutBlocking();
    void proposalConfirmRejectAndFocusLink();
    void documentChangeRejectsPendingProposal();
    void longMessagesAndHugeToolResultsAreBounded();
    void tabMovesFromInputToSendButton();
    void agentApprovalGoesThroughTheDockAndIsUndoable();
    void proposalShowsBeforeAndAfterPreviews();
    void agentQuestionIsAnsweredInThePanel();
    void agentQuestionDeclinedReturnsNoChoice();
};

void MainWindowTest::dockIsHiddenByDefaultAndReopensFromThePanelsMenu() {
    MainWindow w;
    auto* dock = w.companionDock();
    QVERIFY(dock != nullptr);
    QVERIFY(dock->isHidden());
    QVERIFY(!dock->objectName().isEmpty());
    QVERIFY(!dock->accessibleName().isEmpty());
    auto* toggle = w.findChild<QAction*>(QStringLiteral("action_companionDock"));
    QVERIFY(toggle != nullptr);
    QVERIFY(!toggle->shortcut().isEmpty());
    auto* panels = w.findChild<QMenu*>(QStringLiteral("menu_panels"));
    QVERIFY(panels != nullptr && panels->actions().contains(toggle));
    toggle->trigger();
    QVERIFY(!dock->isHidden());
    dock->close();
    QVERIFY(dock->isHidden());
    toggle->trigger(); // une croix ne l'enterre pas : réouverture depuis le menu
    QVERIFY(!dock->isHidden());
}

void MainWindowTest::layoutRoundTripKeepsDockStateAndOtherDocks() {
    MainWindow w;
    w.show();
    auto* dock = w.companionDock();
    auto* props = w.propertiesDock_;
    dock->show();
    w.addDockWidget(Qt::LeftDockWidgetArea, dock); // relocalisation gauche
    QCOMPARE(w.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    const QByteArray state = w.saveState();
    dock->hide();
    w.addDockWidget(Qt::RightDockWidgetArea, dock);
    QVERIFY(w.restoreState(state));
    QCOMPARE(w.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(!dock->isHidden());
    QVERIFY(props != nullptr); // les autres docks existent toujours
    dock->setFloating(true);
    dock->setFloating(false);
    QVERIFY(!w.propertiesDock_->objectName().isEmpty());
}

void MainWindowTest::welcomeScreenWithoutProviderOffersTheLocalBridge() {
    MainWindow w;
    auto* dock = w.companionDock();
    dock->show();
    auto* stack = dock->findChild<QStackedWidget*>();
    QVERIFY(stack != nullptr);
    QCOMPARE(stack->currentIndex(), 0); // accueil : aucun fournisseur
    auto* enable = child<QPushButton>(dock, "button_companionEnableBridge");
    QVERIFY(enable != nullptr && enable->isVisibleTo(dock));
    // L'édition standard n'est pas affectée.
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    QVERIFY(w.hasDocumentContent());
    QSignalSpy spy(dock, &CompanionDock::enableLiveBridgeRequested);
    enable->click();
    QCOMPARE(spy.count(), 1);
}

void MainWindowTest::enterSendsAndShiftEnterInsertsANewLine() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    dock->show();
    auto* input = child<QPlainTextEdit>(dock, "edit_companionInput");
    QVERIFY(input != nullptr);
    input->setFocus();
    QTest::keyClicks(input, QStringLiteral("ligne 1"));
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(input, QStringLiteral("ligne 2"));
    QCOMPARE(input->toPlainText(), QStringLiteral("ligne 1\nligne 2"));
    QVERIFY(backend.sent.isEmpty());
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(backend.sent, QStringList{QStringLiteral("ligne 1\nligne 2")});
    QVERIFY(input->toPlainText().isEmpty());
    auto* transcript = child<QTextBrowser>(dock, "browser_companionTranscript");
    QVERIFY(transcript->toPlainText().contains(QStringLiteral("ligne 2")));
}

void MainWindowTest::sendingShowsBusyStateAndCancelReachesTheBackend() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    dock->show();
    auto* input = child<QPlainTextEdit>(dock, "edit_companionInput");
    auto* send = child<QPushButton>(dock, "button_companionSend");
    auto* cancel = child<QPushButton>(dock, "button_companionCancel");
    input->setPlainText(QStringLiteral("bonjour"));
    send->click();
    QVERIFY(dock->busy());
    QVERIFY(!send->isEnabled());
    QVERIFY(cancel->isEnabled());
    cancel->click();
    QCOMPARE(backend.cancels, 1);
    emit backend.finished();
    QVERIFY(!dock->busy());
    QVERIFY(!cancel->isEnabled());
    // Réponse puis erreur : non bloquantes, l'interface reste utilisable.
    emit backend.assistantText(QStringLiteral("Réponse"));
    emit backend.failed(QStringLiteral("quota dépassé"));
    auto* transcript = child<QTextBrowser>(dock, "browser_companionTranscript");
    QVERIFY(transcript->toPlainText().contains(QStringLiteral("Réponse")));
    QVERIFY(transcript->toPlainText().contains(QStringLiteral("quota dépassé")));
    QVERIFY(w.centralWidget() != nullptr && w.centralWidget()->isEnabled());
}

void MainWindowTest::toolCardsAndErrorsAreShownWithoutBlocking() {
    MainWindow w;
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    CompanionToolCall ok;
    ok.tool = QStringLiteral("project_satin_compare");
    ok.summary = QStringLiteral("3 variantes comparées sur #12");
    CompanionToolCall refused;
    refused.tool = QStringLiteral("project_apply");
    refused.summary = QStringLiteral("forme #14");
    refused.isError = true;
    refused.refusal = QStringLiteral("topologie non supportée");
    emit backend.toolCalled(ok);
    emit backend.toolCalled(refused);
    const QString text = child<QTextBrowser>(dock, "browser_companionTranscript")->toPlainText();
    QVERIFY(text.contains(QStringLiteral("project_satin_compare")));
    QVERIFY(text.contains(QStringLiteral("topologie non supportée")));
    QVERIFY(!text.contains(QStringLiteral("{"))); // jamais de JSON brut
}

void MainWindowTest::proposalConfirmRejectAndFocusLink() {
    MainWindow w;
    auto f = oneShape();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    dock->show();
    CompanionProposal proposal;
    proposal.summary = QStringLiteral("1 objet en satin");
    proposal.items.push_back({first.value, QStringLiteral("Emb → satin")});
    proposal.warnings << QStringLiteral("compensation empirique");
    emit backend.proposalReady(proposal);
    auto* group = child<QWidget>(dock, "group_companionProposal");
    QVERIFY(group->isVisibleTo(dock));
    auto* items = child<QListWidget>(dock, "list_companionProposalItems");
    QCOMPARE(items->count(), 1);
    QSignalSpy focus(dock, &CompanionDock::focusObjectRequested);
    emit items->itemActivated(items->item(0));
    QCOMPARE(focus.count(), 1);
    QVERIFY(w.isObjectSelected(first) || w.selectedEmbroideryIds() == std::vector<ObjectId>{first});
    child<QPushButton>(dock, "button_companionConfirm")->click();
    QCOMPARE(backend.decisions, QList<bool>{true});
    QVERIFY(!group->isVisibleTo(dock));
    emit backend.proposalReady(proposal);
    child<QPushButton>(dock, "button_companionReject")->click();
    QCOMPARE(backend.decisions, (QList<bool>{true, false}));
}

void MainWindowTest::documentChangeRejectsPendingProposal() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    dock->show();
    CompanionProposal proposal;
    proposal.summary = QStringLiteral("x");
    emit backend.proposalReady(proposal);
    QVERIFY(dock->hasPendingProposal());
    auto g = oneShape();
    w.applyLoadedProject(std::move(g.project)); // autre document
    QVERIFY(!dock->hasPendingProposal());
    QCOMPARE(backend.decisions, QList<bool>{false});
}

void MainWindowTest::longMessagesAndHugeToolResultsAreBounded() {
    MainWindow w;
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    emit backend.assistantText(QString(200'000, QLatin1Char('x')));
    CompanionToolCall huge;
    huge.tool = QStringLiteral("project_inspect");
    huge.summary = QString(500'000, QLatin1Char('y'));
    emit backend.toolCalled(huge);
    const auto length =
        child<QTextBrowser>(dock, "browser_companionTranscript")->toPlainText().size();
    QVERIFY2(length < 40'000, qPrintable(QString::number(length)));
}

void MainWindowTest::tabMovesFromInputToSendButton() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    MockBackend backend;
    auto* dock = w.companionDock();
    dock->setBackend(&backend);
    dock->show();
    w.show();
    auto* input = child<QPlainTextEdit>(dock, "edit_companionInput");
    auto* send = child<QPushButton>(dock, "button_companionSend");
    input->setPlainText(QStringLiteral("x"));
    input->setFocus();
    QVERIFY(send->isEnabled());
    QVERIFY(input->tabChangesFocus()); // Tab ne reste pas piégé dans le champ de saisie
    QVERIFY(!send->accessibleName().isEmpty() || !send->text().isEmpty());
}

void MainWindowTest::agentApprovalGoesThroughTheDockAndIsUndoable() {
    MainWindow w;
    auto f = oneShape();
    const ObjectId first = f.first;
    w.applyLoadedProject(std::move(f.project));
    auto* dock = w.companionDock();
    // Un agent demande une modification : le dock s'ouvre et attend la décision de l'utilisateur.
    QTimer::singleShot(30, &w, [dock] {
        auto* group = dock->findChild<QWidget*>(QStringLiteral("group_companionProposal"));
        if (group != nullptr && group->isVisibleTo(dock)) {
            dock->findChild<QPushButton*>(QStringLiteral("button_companionConfirm"))->click();
        }
    });
    const std::string args =
        "{\"operations\":[{\"op\":\"set_color\",\"object\":" + std::to_string(first.value) +
        ",\"rgb\":\"#FF0000\"}]}";
    const std::string reply = w.liveService().call("project_apply", args);
    QVERIFY2(reply.find("\"applied\":true") != std::string::npos, reply.c_str());
    QVERIFY(!dock->isHidden());
    QCOMPARE(w.project_.embroidery_objects.front().rgb, (std::array<std::uint8_t, 3>{255, 0, 0}));
    // Le bouton « Annuler » du dock annule la dernière application IA en une étape.
    auto* undo = child<QPushButton>(dock, "button_companionUndo");
    QVERIFY(undo != nullptr && undo->isVisibleTo(dock));
    undo->click();
    QCOMPARE(w.project_.embroidery_objects.front().rgb, (std::array<std::uint8_t, 3>{60, 30, 30}));
}

void MainWindowTest::proposalShowsBeforeAndAfterPreviews() {
    MainWindow w;
    auto* dock = w.companionDock();
    dock->show();
    const auto png = [](QColor color) {
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(color);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    };
    CompanionProposal proposal;
    proposal.summary = QStringLiteral("Points : 10 -> 12 (+2)");
    proposal.beforePng = png(Qt::red);
    proposal.afterPng = png(Qt::blue);
    dock->showProposal(proposal, [](bool) {});
    auto* before = child<QLabel>(dock, "label_companionBefore");
    auto* after = child<QLabel>(dock, "label_companionAfter");
    QVERIFY(before->isVisibleTo(dock) && after->isVisibleTo(dock));
    QVERIFY(!before->pixmap().isNull() && !after->pixmap().isNull());
    QVERIFY(!before->accessibleName().isEmpty());
    // Sans apercu, les images sont masquees (pas de zone vide).
    CompanionProposal plain;
    plain.summary = QStringLiteral("x");
    dock->showProposal(plain, [](bool) {});
    QVERIFY(!before->isVisibleTo(dock));
}

void MainWindowTest::agentQuestionIsAnsweredInThePanel() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    auto* dock = w.companionDock();
    QTimer::singleShot(30, &w, [dock] {
        auto* items = dock->findChild<QListWidget*>(QStringLiteral("list_companionProposalItems"));
        if (items != nullptr && items->count() == 2) {
            items->setCurrentRow(1);
            dock->findChild<QPushButton*>(QStringLiteral("button_companionConfirm"))->click();
        }
    });
    const std::string args =
        "{\"question\":\"Quels oranges ?\",\"options\":[{\"label\":\"Stricts\"},"
        "{\"label\":\"Avec la frontiere\",\"object_ids\":[2]}]}";
    const std::string reply = w.liveService().call("live_ask_user", args);
    QVERIFY2(reply.find("chosen\\\":true") != std::string::npos, reply.c_str());
    QVERIFY(reply.find("Avec la frontiere") != std::string::npos);
    QVERIFY(!dock->isHidden());
    QVERIFY(!dock->hasPendingProposal());
    QVERIFY(!w.undoAct_->isEnabled()); // une question ne modifie rien
}

void MainWindowTest::agentQuestionDeclinedReturnsNoChoice() {
    MainWindow w;
    auto f = oneShape();
    w.applyLoadedProject(std::move(f.project));
    auto* dock = w.companionDock();
    QTimer::singleShot(30, &w, [dock] {
        auto* group = dock->findChild<QWidget*>(QStringLiteral("group_companionProposal"));
        if (group != nullptr && group->isVisibleTo(dock)) {
            dock->findChild<QPushButton*>(QStringLiteral("button_companionReject"))->click();
        }
    });
    const std::string args =
        "{\"question\":\"Lequel ?\",\"options\":[{\"label\":\"A\"},{\"label\":\"B\"}]}";
    const std::string reply = w.liveService().call("live_ask_user", args);
    QVERIFY2(reply.find("chosen\\\":false") != std::string::npos, reply.c_str());
}

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_companion_dock.moc"
