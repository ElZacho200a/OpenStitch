// SPDX-License-Identifier: Apache-2.0
// Studio : action « Fondu depuis l'image » du panneau de fondu (controleur, calcul hors du fil
// d'interface, une seule etape d'annulation). Headless : aucun pixel, aucune attente fixe.
#include <QGuiApplication>
#include <QtTest/QtTest>

#include "edit_batch.hpp"
#include "fade_controller.hpp"
#include "openstitch/document/multi_blend.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Image 60 x 40 px a 0,5 mm/px (30 x 20 mm, centree sur l'origine) : rouge a gauche, bleu a droite.
void paintGradient(document::Project& project) {
    auto& img = project.original;
    img.width = 60;
    img.height = 40;
    img.rgba.assign(60u * 40u * 4u, 255);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 60; ++x) {
            auto* p = &img.rgba[(static_cast<std::size_t>(y) * 60 + x) * 4];
            p[0] = static_cast<std::uint8_t>(255 - x * 255 / 59);
            p[1] = 20;
            p[2] = static_cast<std::uint8_t>(x * 255 / 59);
            p[3] = 255;
        }
    }
    project.mm_per_px = Millimeters{0.5};
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    FadeController fade{studio};
    std::uint64_t id{0};

    void open(bool withImage) {
        auto project = project_ops::create_empty_project({});
        QVERIFY(project.has_value());
        if (withImage) {
            paintGradient(*project);
        }
        session.replaceProject(std::move(*project), true);
        const auto created =
            createdObjectId(session.applyBatch(batchAddRect({0, 0}, 30, 20), "R").report);
        QVERIFY(created.has_value());
        id = *created;
        studio.selectObject(QString::number(id));
    }
};

} // namespace

class StudioFadeImageTest : public QObject {
    Q_OBJECT
private slots:
    void createsAFadeInOneUndoStep();
    void disabledWithoutImage();
    void disabledForIncompatibleObject();
};

void StudioFadeImageTest::createsAFadeInOneUndoStep() {
    Rig rig;
    rig.open(true);
    QVERIFY(rig.fade.canImageFade());
    QVERIFY(rig.fade.imageFadeReason().isEmpty());
    QVERIFY(!rig.fade.hasFade());
    const auto objects = rig.session.project().embroidery_objects.size();
    const auto depth = rig.session.undoNames().size();

    QVERIFY(rig.fade.fadeFromImage(4, 3.0, true));
    QTRY_VERIFY(!rig.fade.imageFadeBusy());
    QVERIFY2(rig.fade.lastOk(), qPrintable(rig.fade.message()));
    QVERIFY(rig.fade.hasFade());
    QVERIFY(rig.fade.colors().size() >= 2);
    QVERIFY(rig.session.project().embroidery_objects.size() > objects);
    const auto group = document::find_gradient_group(rig.session.project(), ObjectId{rig.id});
    QVERIFY(group.has_value());
    QVERIFY(group->layers.size() >= 2);
    QCOMPARE(rig.session.undoNames().size(), depth + 1); // UNE etape d'annulation

    QVERIFY(rig.session.undo());
    QCOMPARE(rig.session.project().embroidery_objects.size(), objects);
    QVERIFY(!document::find_gradient_group(rig.session.project(), ObjectId{rig.id}).has_value());
    QVERIFY(!rig.fade.hasFade());
}

void StudioFadeImageTest::disabledWithoutImage() {
    Rig rig;
    rig.open(false);
    QVERIFY(!rig.fade.canImageFade());
    QVERIFY(rig.fade.imageFadeReason().contains(QStringLiteral("image")));
    const auto depth = rig.session.undoNames().size();
    QVERIFY(!rig.fade.fadeFromImage(6, 3.0, true));
    QVERIFY(!rig.fade.lastOk());
    QCOMPARE(rig.session.undoNames().size(), depth);
}

void StudioFadeImageTest::disabledForIncompatibleObject() {
    Rig rig;
    rig.open(true);
    rig.studio.clearSelection();
    QVERIFY(!rig.fade.canImageFade());
    QVERIFY(!rig.fade.imageFadeReason().isEmpty());
    QVERIFY(!rig.fade.fadeFromImage(6, 3.0, true));
    rig.studio.selectObject(QString::number(rig.id));
    QVERIFY(!rig.fade.fadeFromImage(1, 3.0, true)); // 1 fil : refuse
    QVERIFY(!rig.fade.fadeFromImage(6, 9.0, true)); // case hors plage : refuse
}

QTEST_MAIN(StudioFadeImageTest)
#include "test_studio_fade_image.moc"
