// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QClipboard>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "fade_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "inspector_controller.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "point_edit_controller.hpp"
#include "region_controller.hpp"
#include "satin_controller.hpp"
#include "shape_batch.hpp"
#include "thread_controller.hpp"
#include "tool_controller.hpp"
#include "vector_controller.hpp"
#include "view_controller.hpp"

namespace openstitch::desktop_v2::studio::e2e {

struct Journey {
    const char* code;
    std::function<std::unique_ptr<QObject>()> create;
};
inline std::vector<Journey>& journeys() {
    static std::vector<Journey> result;
    return result;
}
template <typename Test> bool registerJourney(const char* code) {
    journeys().push_back({code, [] { return std::make_unique<Test>(); }});
    return true;
}

// One harness = one live document and the real production QML/adapters. Fixtures can be set up
// via core APIs; the tested action always enters the scene through QTest mouse/keyboard events.
class Harness {
public:
    QTemporaryDir directory;
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    FileController files{studio, &actions, directory.path()};
    InspectorController inspector{studio};
    ToolController tools{studio};
    VectorController vectors{studio};
    ViewController view{studio, std::filesystem::path(directory.path().toStdWString())};
    ThreadController threads{studio, std::filesystem::path(directory.path().toStdWString())};
    AnalysisController analysis{studio, view,
                                std::filesystem::path(directory.path().toStdWString())};
    ImageController images{studio};
    RegionController regions{studio, images};
    SatinController satin{studio};
    FadeController fade{studio};
    PointEditController points{studio};
    QStringList warnings;
    QQmlApplicationEngine engine;
    QQuickWindow* window{};
    GpuCanvas* canvas{};

    explicit Harness(const QString& configDir = {});
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool settle(int timeout = 30000);
    [[nodiscard]] QQuickItem* item(const char* name) const;
    [[nodiscard]] bool click(const char* name);
    [[nodiscard]] bool type(const char* name, const QString& text);
    [[nodiscard]] bool openFile(const QString& path);
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = {});
    [[nodiscard]] QPoint at(double xMm, double yModelMm) const;
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = {});
    [[nodiscard]] bool drawRectangle(double x, double y, double width, double height);
    [[nodiscard]] bool useFixture(document::Project project);
    [[nodiscard]] bool rectangles();
    [[nodiscard]] bool tentabrode();
    [[nodiscard]] bool prepareSavePath(const QString& name = QStringLiteral("journey.osp"));
    [[nodiscard]] bool saveRoundTrip();
    [[nodiscard]] std::vector<std::uint8_t> bytes() const;
    [[nodiscard]] static std::vector<std::uint8_t> bytes(const document::Project& project);
    [[nodiscard]] bool undoToBytes(const std::vector<std::uint8_t>& expected);
    [[nodiscard]] std::size_t steps() const { return session.undoNames().size(); }
    [[nodiscard]] static std::filesystem::path fixture(const char* name);

private:
    static QQuickItem* find(QQuickItem* root, const QString& name);
    static void reveal(QQuickItem* item);
};

} // namespace openstitch::desktop_v2::studio::e2e
