// SPDX-License-Identifier: Apache-2.0
// Dialogue « Segmenter avec l'IA » et préférences IA via le registre de backends
// (openstitch.ai.backend/1), avec un backend factice EN MÉMOIRE : aucun processus, aucun modèle.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"

namespace ai = openstitch::ai_segmentation;
using openstitch::Millimeters;
using openstitch::desktop::AiEngine;
using openstitch::desktop::AiPreferences;
using openstitch::desktop::AiPreferencesDialog;
using openstitch::desktop::AiSegmentationDialog;
using namespace std::chrono_literals;

namespace {

bool has(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

std::string id_of(const std::string& request) {
    const auto start = request.find("\"id\":\"") + 6;
    return request.substr(start, request.find('"', start) - start);
}

class Scripted final : public ai::ITransport {
public:
    using Responder = std::function<void(const std::string&, Scripted&)>;
    explicit Scripted(Responder r) : responder_(std::move(r)) {}
    void push(std::string line) {
        std::lock_guard lock(mutex_);
        out_.push_back(std::move(line));
    }
    ai::AiResult<void> write_line(std::string_view line) override {
        responder_(std::string(line), *this);
        return {};
    }
    ai::AiResult<std::optional<std::string>> read_line(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            {
                std::lock_guard lock(mutex_);
                if (!out_.empty()) {
                    auto l = std::move(out_.front());
                    out_.pop_front();
                    return std::optional<std::string>(std::move(l));
                }
                if (closed_) {
                    return std::unexpected(
                        ai::AiError{ai::AiErrorCode::WorkerCrashed, "fermé", ""});
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::optional<std::string>{};
            }
            std::this_thread::sleep_for(2ms);
        }
    }
    void terminate() override {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }

private:
    Responder responder_;
    std::mutex mutex_;
    std::deque<std::string> out_;
    bool closed_{false};
};

void writeMask(const QString& jobDir, const QString& name, const QRect& rect) {
    QDir().mkpath(jobDir + QStringLiteral("/masks"));
    QImage mask(8, 4, QImage::Format_RGB32);
    mask.fill(Qt::black);
    QPainter(&mask).fillRect(rect, Qt::white);
    mask.save(jobDir + QStringLiteral("/masks/") + name);
}

enum class Mode { Ok, Slow, Fail, Unavailable };

ai::TransportFactory fakeBackend(Mode mode, std::shared_ptr<std::atomic<int>> launches = {}) {
    return [mode,
            launches](const ai::BackendConfig&) -> ai::AiResult<std::unique_ptr<ai::ITransport>> {
        if (launches) {
            ++*launches;
        }
        auto t = std::make_unique<Scripted>([mode](const std::string& req, Scripted& self) {
            const std::string id = id_of(req);
            if (has(req, "\"type\":\"capabilities\"")) {
                self.push(
                    R"({"id":")" + id +
                    R"(","type":"capabilities","backend":{"backend_id":"fake","backend_revision":"1","display_name":"Faux","model_id":"fake-small","model_hash":"0123456789abcdef0123","prompts":["automatic"],"devices":["cpu"],"license_code":"Apache-2.0","license_weights":"Apache-2.0","experimental":false}})");
            } else if (has(req, "\"type\":\"health\"")) {
                self.push(R"({"id":")" + id + R"(","type":"health","state":")" +
                          (mode == Mode::Unavailable ? "unavailable" : "ready") +
                          R"(","detail":"poids absents"})");
            } else if (has(req, "\"type\":\"predict\"")) {
                if (mode == Mode::Fail) {
                    self.push(
                        R"({"id":")" + id +
                        R"(","type":"error","code":"INFERENCE_FAILED","message":"échec simulé","details":"d"})");
                } else if (mode == Mode::Ok) {
                    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(req));
                    const QString job = doc.object().value(QStringLiteral("job_dir")).toString();
                    writeMask(job, QStringLiteral("m0.png"), QRect(0, 0, 4, 4));
                    writeMask(job, QStringLiteral("m1.png"), QRect(4, 0, 4, 4));
                    self.push(
                        R"({"id":")" + id +
                        R"(","type":"result","image_sha256":"aa","transform":{"source_width":8,"source_height":4,"width":8,"height":4,"scale":1.0,"crop":null},"masks":[{"id":"a","file":"masks/m0.png","bbox_xywh":[0,0,4,4],"area_px":16,"confidence":0.9},{"id":"b","file":"masks/m1.png","bbox_xywh":[4,0,4,4],"area_px":16}],"elapsed_ms":3,"warnings":[]})");
                } // Slow : rien tant qu'il n'y a pas de `cancel`
            } else if (has(req, "\"type\":\"cancel\"")) {
                const auto s = req.find("\"target_id\":\"") + 13;
                const std::string target = req.substr(s, req.find('"', s) - s);
                self.push(R"({"id":")" + target + R"(","type":"cancelled","target_id":")" + target +
                          R"("})");
            } else if (has(req, "\"type\":\"shutdown\"")) {
                self.push(R"({"id":")" + id + R"(","type":"bye"})");
            }
        });
        t->push(
            R"({"type":"hello","protocol":"openstitch.ai.backend/1","backend_id":"fake","pid":1})");
        return t;
    };
}

openstitch::image::Image twoColorImage() {
    openstitch::image::Image image;
    image.width = 8;
    image.height = 4;
    image.rgba.resize(8 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 8; ++x) {
            auto* p = &image.rgba[static_cast<std::size_t>(y * 8 + x) * 4];
            p[0] = x < 4 ? 220 : 20;
            p[1] = 40;
            p[2] = x < 4 ? 30 : 200;
            p[3] = 255;
        }
    }
    return image;
}

QPushButton* buttonNamed(QWidget& w, const QString& text) {
    for (auto* b : w.findChildren<QPushButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

} // namespace

class AiBackendDialogTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;
    QTemporaryDir fileDir_;

    QString writeBackendsFile() {
        const QString path = fileDir_.filePath(QStringLiteral("backends.json"));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return {};
        }
        f.write(R"({"schema":"openstitch.ai.backends/1","preferred":"fake","backends":[
            {"id":"fake","display_name":"Faux SAM","command":"unused","args":[]}]})");
        return path;
    }
    AiPreferences backendPrefs() {
        AiPreferences prefs;
        prefs.backendsFile = writeBackendsFile();
        prefs.engine = QStringLiteral("backend:fake");
        return prefs;
    }

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchTest"));
        QCoreApplication::setApplicationName(QStringLiteral("ai-backend-dialog-test"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void engineRoundTripsThroughText() {
        for (const auto& e :
             {AiEngine{AiEngine::Kind::Legacy, {}}, AiEngine{AiEngine::Kind::Classical, {}},
              AiEngine{AiEngine::Kind::Backend, QStringLiteral("sam2")}}) {
            QCOMPARE(AiEngine::fromString(e.toString()), e);
        }
        // Texte inconnu ou vide : comportement historique, jamais un backend inventé.
        QCOMPARE(AiEngine::fromString(QStringLiteral("backend:")).kind, AiEngine::Kind::Legacy);
        QCOMPARE(AiEngine::fromString(QString()).kind, AiEngine::Kind::Legacy);
    }

    void preferencesPersistBackendsFileAndEngine() {
        AiPreferences prefs;
        prefs.backendsFile = QStringLiteral("C:/x/backends.json");
        prefs.engine = QStringLiteral("backend:sam2");
        openstitch::desktop::saveAiPreferences(prefs);
        const auto loaded = openstitch::desktop::loadAiPreferences();
        QCOMPARE(loaded.backendsFile, prefs.backendsFile);
        QCOMPARE(loaded.engine, prefs.engine);
        QCOMPARE(AiPreferences{}.engine, QStringLiteral("legacy")); // SAM historique par défaut
    }

    void dialogListsDeclaredBackendsAndSelectsThePreferredOne() {
        AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, backendPrefs());
        const auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("aiEngineCombo"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3); // historique, « Faux SAM », couleur
        QCOMPARE(dialog.engine(), (AiEngine{AiEngine::Kind::Backend, QStringLiteral("fake")}));
    }

    void backendMasksFillTheReviewTableAndValidate() {
        AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, backendPrefs());
        dialog.setTransportFactory(fakeBackend(Mode::Ok));
        buttonNamed(dialog, QStringLiteral("Analyser"))->click();

        auto* table = dialog.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);
        QVERIFY(!dialog.usedClassicalFallback());
        // Score rapporté affiché, score absent jamais inventé.
        QCOMPARE(table->item(0, 3)->text(), QStringLiteral("0.90"));
        QCOMPARE(table->item(1, 3)->text(), QStringLiteral("—"));
        QCOMPARE(table->item(0, 4)->text(), QStringLiteral("—"));
        const auto statuses = dialog.findChildren<QLabel*>();
        bool traced = false;
        for (const auto* l : statuses) {
            traced = traced || (l->text().contains(QStringLiteral("fake-small")) &&
                                l->text().contains(QStringLiteral("0123456789ab")));
        }
        QVERIFY2(traced, "le modèle et l'empreinte qui ont répondu doivent être affichés");

        auto* validate = buttonNamed(dialog, QStringLiteral("Valider"));
        QVERIFY(validate->isEnabled());
        validate->click();
        QVERIFY(dialog.hasResult());
        auto seg = dialog.takeSegmentation();
        QVERIFY(seg.has_value());
        QCOMPARE(seg->width, 8);
        QVERIFY(seg->region_count() >= 2);
    }

    void failingBackendFallsBackToColorSegmentationExplicitly() {
        for (const Mode mode : {Mode::Fail, Mode::Unavailable}) {
            AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, backendPrefs());
            dialog.setTransportFactory(fakeBackend(mode));
            buttonNamed(dialog, QStringLiteral("Analyser"))->click();

            QVERIFY(dialog.usedClassicalFallback());
            QString shown;
            for (const auto* l : dialog.findChildren<QLabel*>()) {
                shown += l->text();
            }
            QVERIFY2(shown.contains(QStringLiteral("CIELAB")), qPrintable(shown));
            QVERIFY2(shown.contains(QStringLiteral("Faux SAM")), qPrintable(shown));
            auto* validate = buttonNamed(dialog, QStringLiteral("Valider"));
            QVERIFY(validate->isEnabled());
            validate->click();
            QVERIFY(dialog.hasResult());
            const auto seg = dialog.takeSegmentation();
            QVERIFY(seg && seg->region_count() >= 2); // deux couleurs -> deux régions
        }
    }

    void missingBackendFileAlsoFallsBack() {
        AiPreferences prefs;
        prefs.backendsFile = fileDir_.filePath(QStringLiteral("absent.json"));
        prefs.engine = QStringLiteral("backend:fake");
        AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, prefs);
        // Backend introuvable : retour au worker historique dans le sélecteur, jamais d'invention.
        QCOMPARE(dialog.engine().kind, AiEngine::Kind::Legacy);
    }

    void chosenColorEngineNeverLaunchesABackend() {
        auto launches = std::make_shared<std::atomic<int>>(0);
        AiPreferences prefs = backendPrefs();
        prefs.engine = QStringLiteral("classical");
        AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, prefs);
        dialog.setTransportFactory(fakeBackend(Mode::Ok, launches));
        buttonNamed(dialog, QStringLiteral("Analyser"))->click();
        QCOMPARE(launches->load(), 0);
        QVERIFY(dialog.usedClassicalFallback());
        buttonNamed(dialog, QStringLiteral("Valider"))->click();
        QVERIFY(dialog.hasResult());
    }

    void cancellingDuringAnalysisKeepsTheDialogClean() {
        AiSegmentationDialog dialog(twoColorImage(), Millimeters{0.2}, backendPrefs());
        dialog.setTransportFactory(fakeBackend(Mode::Slow));
        QTimer::singleShot(250, [] {
            for (auto* w : QApplication::topLevelWidgets()) {
                if (auto* p = qobject_cast<QProgressDialog*>(w); p != nullptr && p->isVisible()) {
                    // `cancel()` n'émet pas `canceled` : on clique le vrai bouton « Annuler ».
                    if (auto* cancelButton = p->findChild<QPushButton*>()) {
                        cancelButton->click();
                    }
                }
            }
        });
        buttonNamed(dialog, QStringLiteral("Analyser"))->click();
        QVERIFY(!dialog.usedClassicalFallback()); // annuler n'est pas un échec : pas de repli
        QVERIFY(!dialog.hasResult());
        QCOMPARE(dialog.findChild<QTableWidget*>()->rowCount(), 0);
        QVERIFY(buttonNamed(dialog, QStringLiteral("Analyser"))->isEnabled());
        QVERIFY(!buttonNamed(dialog, QStringLiteral("Valider"))->isEnabled());
    }

    void preferencesProbeReportsStateModelAndLicences() {
        AiPreferencesDialog dialog;
        dialog.setTransportFactory(fakeBackend(Mode::Ok));
        dialog.setPreferences(backendPrefs());
        auto* probe = dialog.findChild<QPushButton*>(QStringLiteral("aiProbeBackendsButton"));
        QVERIFY(probe != nullptr);
        probe->click();
        const QString log = dialog.findChild<QPlainTextEdit*>()->toPlainText();
        QVERIFY2(log.contains(QStringLiteral("Faux SAM")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("fake-small")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("0123456789ab")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("Apache-2.0")), qPrintable(log));
        const auto prefs = dialog.preferences();
        QCOMPARE(prefs.engine, QStringLiteral("backend:fake"));
        QCOMPARE(prefs.backendsFile, backendPrefs().backendsFile);
    }

    void preferencesProbeExplainsAnUnavailableBackend() {
        AiPreferencesDialog dialog;
        dialog.setTransportFactory(fakeBackend(Mode::Unavailable));
        dialog.setPreferences(backendPrefs());
        dialog.findChild<QPushButton*>(QStringLiteral("aiProbeBackendsButton"))->click();
        const QString log = dialog.findChild<QPlainTextEdit*>()->toPlainText();
        QVERIFY2(log.contains(QStringLiteral("poids absents")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("segmentation classique")), qPrintable(log));
    }
};

QTEST_MAIN(AiBackendDialogTest)
#include "test_ai_backend_dialog.moc"
