// SPDX-License-Identifier: Apache-2.0
// CX09 : décalage entre couleurs et DST compensé dans Studio. Vrai moteur (document numérisé depuis
// une vraie image), vrai PullController, vrai PullPanel.qml. La séquence compensée n'est JAMAIS
// stockée dans le document : seul le fichier exporté la contient.
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

#include "digitize_pipeline.hpp"
#include "inspector_controller.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "pull_controller.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

std::vector<std::uint8_t> dstBytes(const DocumentSession& session) {
    auto sequence = stitch_generation::effective_sequence(session.project());
    if (!sequence) {
        return {};
    }
    auto bytes = formats::encode_dst(*sequence);
    return bytes ? *bytes : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto data = file.readAll();
    return {reinterpret_cast<const std::uint8_t*>(data.constData()),
            reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size()};
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    InspectorController inspector{studio};
    PullController pull{studio, &inspector};
    QTemporaryDir dir;

    void load() {
        DigitizeRequest request;
        request.image = std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / "tentabrode.png";
        request.target_height_mm = 80.0;
        request.settings.max_colors = 4;
        auto result = run_digitize(request, TaskContext{});
        QVERIFY(result.has_value());
        session.replaceProject(std::move(result->project), true);
    }
    void predict(double strength) {
        pull.predictRegistration(strength);
        QTRY_VERIFY_WITH_TIMEOUT(!pull.busy(), 60000);
    }
};

QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisual(child, name)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

class StudioRegistrationTest : public QObject {
    Q_OBJECT
private slots:
    void compensatedExportDiffersFromTheDocumentAndNeverTouchesIt() {
        Rig rig;
        rig.load();
        const auto plain = dstBytes(rig.session);
        QVERIFY(!plain.empty());
        const auto revision = rig.session.revision();
        const auto steps = rig.session.historySize();
        rig.predict(1.0);
        QVERIFY(rig.pull.registration().value(QStringLiteral("compensated")).toBool());
        QVERIFY(rig.pull.canExportCompensated());
        const auto path = rig.dir.filePath(QStringLiteral("compense.dst"));
        QVERIFY(rig.pull.exportCompensatedDst(path));
        const auto written = readFile(path);
        QCOMPARE(static_cast<int>(written.size()),
                 rig.pull.registration().value(QStringLiteral("dstBytes")).toInt());
        QVERIFY(!written.empty());
        QVERIFY(written != plain); // la contre-compensation décale réellement des points
        // Le document, sa révision, son historique et son DST sont strictement inchangés.
        QCOMPARE(rig.session.revision(), revision);
        QCOMPARE(rig.session.historySize(), steps);
        QVERIFY(dstBytes(rig.session) == plain);
    }

    void withoutCompensationNothingIsExportable() {
        Rig rig;
        rig.load();
        rig.predict(0.0);
        QVERIFY(!rig.pull.registration().isEmpty());
        QVERIFY(!rig.pull.canExportCompensated());
        const auto path = rig.dir.filePath(QStringLiteral("rien.dst"));
        QVERIFY(!rig.pull.exportCompensatedDst(path));
        QVERIFY(!QFile::exists(path));
    }

    void staleRegistrationRefusesTheExport() {
        Rig rig;
        rig.load();
        rig.predict(1.0);
        QVERIFY(rig.pull.canExportCompensated());
        auto finishing = rig.session.project().finishing;
        finishing.enabled = !finishing.enabled;
        rig.session.execute(std::make_unique<commands::SetFinishingCommand>(finishing));
        QVERIFY(rig.pull.registrationStale());
        QVERIFY(!rig.pull.canExportCompensated());
        const auto path = rig.dir.filePath(QStringLiteral("perime.dst"));
        QVERIFY(!rig.pull.exportCompensatedDst(path));
        QVERIFY(!rig.pull.lastOk());
        QVERIFY(!QFile::exists(path));
        // Relancer la prédiction redonne un export valable.
        rig.predict(1.0);
        QVERIFY(!rig.pull.registrationStale());
        QVERIFY(rig.pull.exportCompensatedDst(path));
        QVERIFY(QFile::exists(path));
    }

    void panelOffersCompensationAndExport() {
        Rig rig;
        rig.load();
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/studio/qml/PullPanel.qml")));
        QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue(&rig.pull)}}));
        QVERIFY(object);
        auto* panel = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(panel);
        panel->setSize({420, 900});
        auto* compensate = findVisual(panel, QStringLiteral("pullRegistrationCompensate"));
        auto* exportButton = findVisual(panel, QStringLiteral("pullExportCompensated"));
        auto* pathField = findVisual(panel, QStringLiteral("pullCompensatedPath"));
        QVERIFY(compensate && exportButton && pathField);
        QVERIFY(!exportButton->isEnabled()); // rien à exporter avant la prédiction
        QVERIFY(QMetaObject::invokeMethod(compensate, "click"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.pull.canExportCompensated(), 60000);
        const auto path = rig.dir.filePath(QStringLiteral("panneau.dst"));
        pathField->setProperty("text", path);
        QTRY_VERIFY(exportButton->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(exportButton, "click"));
        QVERIFY(QFile::exists(path));
        QVERIFY(QFileInfo(path).size() > 0);
    }
};

QTEST_MAIN(StudioRegistrationTest)
#include "test_studio_registration.moc"
