// SPDX-License-Identifier: Apache-2.0
#include "journey_harness.hpp"

#include <QFileInfo>
#include <QQuickStyle>

namespace openstitch::desktop_v2::studio::e2e {

Harness::Harness(const QString& configDir)
    : files(studio, &actions, configDir.isEmpty() ? directory.path() : configDir) {
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [this](const QList<QQmlError>& errors) {
                         for (const auto& error : errors) {
                             warnings.push_back(error.toString());
                         }
                     });
    const std::vector<std::pair<const char*, QObject*>> contexts{
        {"studio", &studio},     {"actions", &actions},     {"editor", &editor},
        {"files", &files},       {"inspector", &inspector}, {"tools", &tools},
        {"vectors", &vectors},   {"view", &view},           {"threads", &threads},
        {"analysis", &analysis}, {"imageTools", &images},   {"regionTools", &regions},
        {"satin", &satin},       {"fade", &fade},           {"pointEdit", &points}};
    for (const auto& [name, object] : contexts) {
        engine.rootContext()->setContextProperty(QString::fromLatin1(name), object);
    }
    engine.addImageProvider(QStringLiteral("prepare"), images.createProvider());
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return;
    }
    window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    if (!window) {
        return;
    }
    window->resize(1500, 1100);
    window->show();
    window->requestActivate();
    canvas = window->findChild<GpuCanvas*>(QStringLiteral("studioCanvas"));
}

bool Harness::ready() const {
    return window && canvas && directory.isValid() && warnings.isEmpty();
}

bool Harness::settle(int timeout) {
    return QTest::qWaitFor(
        [this] {
            return !studio.busy() && !files.busy() && !images.busy() && !regions.busy() &&
                   !analysis.busy();
        },
        timeout);
}

QQuickItem* Harness::find(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* result = find(child, name)) {
            return result;
        }
    }
    return nullptr;
}

QQuickItem* Harness::item(const char* name) const {
    return window ? find(window->contentItem(), QString::fromLatin1(name)) : nullptr;
}

void Harness::reveal(QQuickItem* item) {
    for (auto* ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem()) {
        if (!ancestor->property("contentY").isValid()) {
            continue;
        }
        const auto position = item->mapToItem(ancestor, QPointF(0, 0));
        double delta = 0;
        if (position.y() < 0) {
            delta = position.y();
        } else if (position.y() + item->height() > ancestor->height()) {
            delta = position.y() + item->height() - ancestor->height();
        }
        ancestor->setProperty("contentY", ancestor->property("contentY").toDouble() + delta);
    }
    QCoreApplication::processEvents();
}

bool Harness::click(const char* name) {
    auto* target = item(name);
    if (!target || !target->isVisible() || !target->isEnabled()) {
        return false;
    }
    reveal(target);
    const auto position = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    if (!QRectF(0, 0, window->width(), window->height()).contains(position)) {
        return false;
    }
    QTest::mouseClick(window, Qt::LeftButton, {}, position.toPoint());
    QCoreApplication::processEvents();
    return true;
}

bool Harness::type(const char* name, const QString& text) {
    if (!click(name)) {
        return false;
    }
    QGuiApplication::clipboard()->setText(text);
    key(Qt::Key_A, Qt::ControlModifier);
    key(Qt::Key_V, Qt::ControlModifier);
    key(Qt::Key_Return);
    return true;
}

void Harness::key(Qt::Key value, Qt::KeyboardModifiers modifiers) {
    QTest::keyClick(window, value, modifiers);
    QCoreApplication::processEvents();
}

bool Harness::openFile(const QString& path) {
    auto* chooser = window->findChild<QObject*>(QStringLiteral("openAnyDialog"));
    if (!chooser) {
        return false;
    }
    // Deterministic chooser starting location/selection, then real user acceptance. The file
    // still traverses FilePanel.onAccepted and the production FileController import guard.
    chooser->setProperty("options", 0x00000008); // QFileDialogOptions::DontUseNativeDialog
    chooser->setProperty("currentFolder", QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    chooser->setProperty("selectedFile", QUrl::fromLocalFile(path));
    key(Qt::Key_O, Qt::ControlModifier);
    if (!QTest::qWaitFor([chooser] { return chooser->property("visible").toBool(); })) {
        return false;
    }
    key(Qt::Key_Return);
    return QTest::qWaitFor([chooser] { return !chooser->property("visible").toBool(); });
}

QPoint Harness::at(double xMm, double yModelMm) const {
    const auto point = canvas->camera().worldToScreen({xMm, -yModelMm});
    return canvas->mapToScene(QPointF(point.x, point.y)).toPoint();
}

void Harness::drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers) {
    QTest::mousePress(window, Qt::LeftButton, modifiers, from);
    for (int step = 1; step <= 6; ++step) {
        QTest::mouseMove(window, from + (to - from) * step / 6);
    }
    QTest::mouseRelease(window, Qt::LeftButton, modifiers, to);
    QCoreApplication::processEvents();
}

bool Harness::drawRectangle(double x, double y, double width, double height) {
    const auto before = steps();
    if (!click("tool_draw.rectangle")) {
        return false;
    }
    drag(at(x, y), at(x + width, y + height));
    return settle() && steps() == before + 1;
}

bool Harness::useFixture(document::Project project) {
    studio.adoptProject(std::move(project), {}, QStringLiteral("E2E fixture"), true);
    return settle() && ready();
}

bool Harness::rectangles() {
    auto empty = project_ops::create_empty_project({});
    if (!empty) {
        return false;
    }
    DocumentSession fixtureSession;
    fixtureSession.replaceProject(std::move(*empty), true);
    for (const auto center : {PointMm{-20, 0}, PointMm{20, 0}}) {
        if (!fixtureSession.applyBatch(batchAddRect(center, 15, 10), "Fixture rectangle")
                 .committed) {
            return false;
        }
    }
    return useFixture(fixtureSession.project());
}

std::filesystem::path Harness::fixture(const char* name) {
    return std::filesystem::path(OPENSTITCH_FIXTURES_DIR) / name;
}

bool Harness::tentabrode() {
    DigitizeRequest request;
    request.image = fixture("tentabrode.png");
    request.target_height_mm = 300;
    auto result = run_digitize(request, TaskContext{});
    return result && useFixture(std::move(result->project));
}

std::vector<std::uint8_t> Harness::bytes(const document::Project& project) {
    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return {};
    }
    const auto encoded = formats::encode_dst(*sequence);
    return encoded ? *encoded : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> Harness::bytes() const {
    return bytes(session.project());
}

bool Harness::prepareSavePath(const QString& name) {
    return studio.saveProjectAs(QUrl::fromLocalFile(directory.filePath(name)));
}

bool Harness::saveRoundTrip() {
    const auto expected = bytes();
    key(Qt::Key_S, Qt::ControlModifier);
    if (!settle() || session.dirty()) {
        return false;
    }
    auto loaded = project_io::load_project(std::filesystem::path(studio.path().toStdWString()));
    return loaded && !expected.empty() && bytes(*loaded) == expected;
}

bool Harness::undoToBytes(const std::vector<std::uint8_t>& expected) {
    key(Qt::Key_Z, Qt::ControlModifier);
    return settle() && bytes() == expected;
}

} // namespace openstitch::desktop_v2::studio::e2e
