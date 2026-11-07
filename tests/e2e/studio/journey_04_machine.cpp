// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"
#include <QFile>

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;
using namespace openstitch::desktop_v2::studio::e2e;

class Journey04 : public QObject {
    Q_OBJECT
private slots:
    void run() {
        Harness h;
        QVERIFY(h.ready());
        QVERIFY(h.rectangles());
        const auto snapshot = h.session.snapshot();
        const auto file =
            std::filesystem::path(h.directory.filePath(QStringLiteral("input.dst")).toStdWString());
        QVERIFY(export_project_file(snapshot, file, TaskContext{}).has_value());
        h.studio.newProject();
        QVERIFY(h.settle());
        QVERIFY(h.openFile(QString::fromStdWString(file.wstring())));
        QVERIFY(h.settle());
        QVERIFY(h.session.project().imported_design.has_value());
        QVERIFY(h.prepareSavePath());
        const auto before = h.bytes();
        QVERIFY(h.click("pointEditPanelButton"));
        QCOMPARE(h.bytes(), before);
        QSKIP("BLOCKED: base #283 imports a machine sequence but exposes no selectable "
              "imported-object editing target; CX08 integration required for full DST/PES "
              "edit/export journey.");
    }
};
namespace {
[[maybe_unused]] const bool registered = registerJourney<Journey04>("04");
}
#include "journey_04_machine.moc"
