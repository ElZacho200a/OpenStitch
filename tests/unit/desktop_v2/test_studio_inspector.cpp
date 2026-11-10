// SPDX-License-Identifier: Apache-2.0
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>

#include "inspector_controller.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "param_spec.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Same ids and order as `stitchTypeId` (document::StitchParams alternatives).
const std::vector<std::string> kAllTypes{"running",      "tatami",  "satin",        "directional",
                                         "auto_satin",   "blanket", "contour_fill", "motif_run",
                                         "motif_fill",   "radial",  "spiral",       "crosshatch",
                                         "cross_stitch", "applique"};

std::uint64_t addRect(DocumentSession& session, double cx = 0, double cy = 0, double w = 20,
                      double h = 10) {
    const auto r = session.applyBatch(batchAddRect({cx, cy}, w, h), "Rectangle");
    return r.committed ? createdObjectId(r.report).value_or(0) : 0;
}

document::StitchParams defaultsOf(const std::string& type) {
    using namespace document;
    if (type == "running")
        return RunningStitchParams{};
    if (type == "directional")
        return DirectionalFillParams{};
    if (type == "satin")
        return SatinParams{};
    if (type == "spiral")
        return SpiralFillParams{};
    if (type == "cross_stitch")
        return CrossStitchParams{};
    if (type == "applique")
        return AppliqueParams{};
    if (type == "auto_satin")
        return AutoSatinParams{};
    if (type == "blanket")
        return BlanketStitchParams{};
    if (type == "contour_fill")
        return ContourFillParams{};
    if (type == "motif_run")
        return MotifRunParams{};
    if (type == "motif_fill")
        return MotifFillParams{};
    if (type == "radial")
        return RadialFillParams{};
    if (type == "crosshatch")
        return CrosshatchFillParams{};
    return TatamiParams{};
}

void select(DocumentSession& session, std::vector<std::uint64_t> ids) {
    std::vector<Target> targets;
    for (const auto id : ids) {
        targets.push_back({TargetKind::EmbroideryObject, id});
    }
    session.selection().replace(std::move(targets));
}

const document::EmbroideryObject& object(const DocumentSession& session, std::uint64_t id) {
    return *session.project().findEmbroidery(ObjectId{id});
}

std::vector<std::uint8_t> dstOf(const DocumentSession& session) {
    const auto sequence = stitch_generation::effective_sequence(session.project());
    if (!sequence) {
        return {};
    }
    const auto bytes = formats::encode_dst(*sequence);
    return bytes ? *bytes : std::vector<std::uint8_t>{};
}

// Delegates of a Repeater are not QObject children of the inspector: walk the visual tree.
QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

QVariantMap fieldOf(InspectorController& inspector, const QString& key) {
    for (const auto& f : inspector.fields()) {
        if (f.toMap().value(QStringLiteral("key")).toString() == key) {
            return f.toMap();
        }
    }
    return {};
}

// A value different from the current one and inside the spec range.
double otherValue(const ParamSpec& spec, double current) {
    switch (spec.kind) {
    case ParamKind::Bool:
        return current >= 0.5 ? 0.0 : 1.0;
    case ParamKind::Choice: {
        const auto n = static_cast<double>(paramOptions(spec).size());
        return std::fmod(std::round(current) + 1.0, n);
    }
    case ParamKind::Integer:
        return std::abs(current - spec.min) > 0.5 ? spec.min : spec.max;
    case ParamKind::Point:
    case ParamKind::List:
        return current; // edited by their own editors (see the point / list tests)
    case ParamKind::OptionalNumber:
    case ParamKind::Number:
        break;
    }
    const double mid = spec.min + (spec.max - spec.min) * 0.5;
    return std::abs(current - mid) > spec.step ? mid : spec.min + (spec.max - spec.min) * 0.25;
}

} // namespace

class StudioInspectorTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void everySpecFieldIsExposedAndEditable();
    void listsAndPointsEditEndToEnd();
    void everyTypePersistsAndKeepsTheMachineOutput();
    void refusalsNeverCorrectSilently();
    void burstOfEditsIsOneUndoStep();
    void mixedValuesAndMultiEdit();
    void colourOfTheSelectionIncludingMixedTypes();
    void resetToDefaults();
    void stitchTypeConversion();
    void searchAndLevels();
    void machineOutputIsByteIdentical();
    void persistsInTheOspFile();
    void presetsLifecycle();
    void qmlInspectorEditsThroughTheController();
};

void StudioInspectorTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
}

// Installs the parameters of `type` on a fresh rectangle (the satin and auto-satin without rails or
// guides get their default parameters directly).
static std::uint64_t objectOfType(DocumentSession& session, const std::string& type) {
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    if (id != 0 && type != "tatami") {
        session.execute(
            std::make_unique<commands::SetStitchParamsCommand>(ObjectId{id}, defaultsOf(type)));
    }
    select(session, {id});
    return id;
}

void StudioInspectorTest::everySpecFieldIsExposedAndEditable() {
    int specFields = 0;
    int exposed = 0;
    std::set<std::string> typesWithSpecs;
    for (const auto& type : kAllTypes) {
        DocumentSession session;
        const auto id = objectOfType(session, type);
        QVERIFY(id != 0);
        StudioController studio(session);
        InspectorController inspector(studio);
        inspector.setShowAdvanced(true);
        inspector.setShowExpert(true);
        QCOMPARE(inspector.typeId().toStdString(), std::string(stitchTypeId(object(session, id))));
        const auto specs = allParamSpecs(stitchTypeId(object(session, id)));
        QVERIFY2(!specs.empty(), type.c_str());
        typesWithSpecs.insert(type);
        QCOMPARE(inspector.fields().size(), static_cast<int>(specs.size()));
        QCOMPARE(inspector.hiddenExpertCount(), 0);
        for (const auto& spec : specs) {
            ++specFields;
            const auto key = QString::fromUtf8(spec.key.data(), static_cast<int>(spec.key.size()));
            const QString where = QString::fromStdString(type) + "." + key;
            const auto field = fieldOf(inspector, key);
            QVERIFY2(!field.isEmpty(), qPrintable(where));
            ++exposed;
            const auto depth = session.undoNames().size();
            if (spec.kind == ParamKind::Point) {
                QVERIFY2(!field.value(QStringLiteral("isSet")).toBool(), qPrintable(where));
                const auto refusal = inspector.setPoint(key, 12.5, -3.25);
                QVERIFY2(refusal.isEmpty(), qPrintable(where + ": " + refusal));
                QCOMPARE(session.undoNames().size(), depth + 1);
                const auto pt = readPointMm(object(session, id), spec.key);
                QVERIFY2(pt.has_value(), qPrintable(where));
                QVERIFY(std::abs(pt->first - 12.5) < 1e-6 && std::abs(pt->second + 3.25) < 1e-6);
                QVERIFY(!inspector.setPoint(key, 9999.0, 0.0).isEmpty()); // out of range: refused
                QVERIFY(inspector.clearParam(key).isEmpty());
                QVERIFY2(!readPointMm(object(session, id), spec.key).has_value(),
                         qPrintable(where));
                QVERIFY(session.undo());
                QVERIFY(readPointMm(object(session, id), spec.key).has_value());
                QVERIFY(session.undo());
                continue;
            }
            if (spec.kind == ParamKind::List) {
                QCOMPARE(field.value(QStringLiteral("kind")).toString(), QStringLiteral("list"));
                QVERIFY(inspector.clearParam(key).isEmpty()); // already empty: no undo entry
                QCOMPARE(session.undoNames().size(), depth);
                continue;
            }
            const auto before = *readParam(object(session, id), spec.key);
            const double target = otherValue(spec, before);
            const auto refusal = inspector.setParam(key, target);
            QVERIFY2(refusal.isEmpty(), qPrintable(where + ": " + refusal));
            QCOMPARE(session.undoNames().size(), depth + 1);
            const auto after = *readParam(object(session, id), spec.key);
            QVERIFY2(std::abs(after - target) < 0.01 + spec.step * 0.01, qPrintable(where));
            QCOMPARE(fieldOf(inspector, key).value(QStringLiteral("value")).toDouble(), after);
            if (spec.kind == ParamKind::OptionalNumber) {
                QVERIFY(paramIsSet(object(session, id), spec.key));
                QVERIFY(inspector.clearParam(key).isEmpty());
                QVERIFY2(!paramIsSet(object(session, id), spec.key), qPrintable(where));
            }
        }
    }
    QVERIFY(specFields == 176); // the complete table lists every engine setting of the 14 types
    QCOMPARE(exposed, specFields);
    QCOMPARE(typesWithSpecs.size(), static_cast<std::size_t>(14));
}

void StudioInspectorTest::listsAndPointsEditEndToEnd() {
    DocumentSession session;
    const auto id = objectOfType(session, "directional");
    StudioController studio(session);
    InspectorController inspector(studio);
    inspector.setShowExpert(true);
    // Two guides and a gradient installed by the engine operation (as the canvas tool does).
    const std::string ops =
        R"({"schema":"openstitch.batch/1","operations":[{"op":"set_params","object":)" +
        std::to_string(id) +
        R"(,"params":{"guides":[[[0,0],[10,5]],[[0,2],[10,7]]],"density_gradient":)"
        R"({"from_mm":[0,0],"to_mm":[10,0],"spacing_from_mm":0.4,"spacing_to_mm":0.8}}}]})";
    QVERIFY(session.applyBatch(ops, "Guides").committed);
    QCOMPARE(fieldOf(inspector, QStringLiteral("guides")).value("value").toInt(), 2);
    QVERIFY(fieldOf(inspector, QStringLiteral("density_gradient")).value("isSet").toBool());
    const auto withGuides = dstOf(session);
    const auto depth = session.undoNames().size();
    QVERIFY(inspector.clearParam(QStringLiteral("guides")).isEmpty());
    QCOMPARE(session.undoNames().size(), depth + 1);
    QCOMPARE(fieldOf(inspector, QStringLiteral("guides")).value("value").toInt(), 0);
    // A reset-all never wipes canvas-authored lists (guides stay, the gradient too).
    QVERIFY(session.undo());
    QVERIFY(inspector.resetAll().isEmpty());
    QCOMPARE(fieldOf(inspector, QStringLiteral("guides")).value("value").toInt(), 2);
    QVERIFY(dstOf(session) == withGuides); // the undo restores the machine output byte for byte
    QVERIFY(inspector.clearParam(QStringLiteral("density_gradient")).isEmpty());
    QVERIFY(!fieldOf(inspector, QStringLiteral("density_gradient")).value("isSet").toBool());
    // The sweep angle of an auto-satin is an optional number: automatic until fixed.
    DocumentSession autoSession;
    const auto autoId = objectOfType(autoSession, "auto_satin");
    StudioController autoStudio(autoSession);
    InspectorController autoInspector(autoStudio);
    autoInspector.setShowExpert(true);
    auto sweep = fieldOf(autoInspector, QStringLiteral("sweep_angle_deg"));
    QVERIFY(!sweep.value("isSet").toBool());
    QCOMPARE(sweep.value("text").toString(), QStringLiteral("Automatique"));
    QVERIFY(autoInspector.setParam(QStringLiteral("sweep_angle_deg"), 35).isEmpty());
    QVERIFY(paramIsSet(object(autoSession, autoId), "sweep_angle_deg"));
    QVERIFY(autoInspector.resetParam(QStringLiteral("sweep_angle_deg")).isEmpty());
    QVERIFY(!paramIsSet(object(autoSession, autoId), "sweep_angle_deg"));
}

void StudioInspectorTest::refusalsNeverCorrectSilently() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    const auto revision = session.revision();
    const auto before = *readParam(object(session, id), "row_spacing_mm");
    auto refusal = inspector.setParam(QStringLiteral("row_spacing_mm"), 9.0);
    QVERIFY(refusal.contains(QStringLiteral("hors plage")));
    QVERIFY(!inspector.messageOk());
    refusal = inspector.setParamText(QStringLiteral("row_spacing_mm"), QStringLiteral("abc"));
    QVERIFY(refusal.contains(QStringLiteral("pas un nombre")));
    refusal = inspector.setParam(QStringLiteral("stagger"), 2.5);
    QVERIFY(refusal.contains(QStringLiteral("entière")));
    refusal = inspector.setParam(QStringLiteral("no_such_field"), 1.0);
    QVERIFY(!refusal.isEmpty());
    refusal = inspector.setParam(QStringLiteral("row_spacing_mm"), std::nan(""));
    QVERIFY(!refusal.isEmpty());
    QCOMPARE(session.revision(), revision); // nothing was applied, nothing clamped
    QCOMPARE(*readParam(object(session, id), "row_spacing_mm"), before);
    // Decimal comma and unit suffix are accepted as typed.
    QVERIFY(inspector.setParamText(QStringLiteral("row_spacing_mm"), QStringLiteral("0,35 mm"))
                .isEmpty());
    QVERIFY(std::abs(*readParam(object(session, id), "row_spacing_mm") - 0.35) < 1e-6);
}

void StudioInspectorTest::burstOfEditsIsOneUndoStep() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    const auto original = *readParam(object(session, id), "row_spacing_mm");
    const auto depth = session.undoNames().size();
    for (const double v : {0.30, 0.31, 0.32, 0.33, 0.34, 0.35}) { // a slider drag
        QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), v).isEmpty());
    }
    QCOMPARE(session.undoNames().size(), depth + 1);
    QVERIFY(std::abs(*readParam(object(session, id), "row_spacing_mm") - 0.35) < 1e-6);
    QVERIFY(session.undo());
    QCOMPARE(*readParam(object(session, id), "row_spacing_mm"), original);
    QVERIFY(session.redo());
    QVERIFY(std::abs(*readParam(object(session, id), "row_spacing_mm") - 0.35) < 1e-6);
}

void StudioInspectorTest::mixedValuesAndMultiEdit() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto a = addRect(session, 0, 0);
    const auto b = addRect(session, 40, 0);
    select(session, {a});
    StudioController studio(session);
    InspectorController inspector(studio);
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), 0.5).isEmpty());
    select(session, {a, b});
    QCOMPARE(inspector.count(), 2);
    auto field = fieldOf(inspector, QStringLiteral("row_spacing_mm"));
    QVERIFY(field.value(QStringLiteral("mixed")).toBool());
    QCOMPARE(field.value(QStringLiteral("text")).toString(), QStringLiteral("—"));
    QVERIFY(
        !fieldOf(inspector, QStringLiteral("inset_mm")).value(QStringLiteral("mixed")).toBool());
    const auto depth = session.undoNames().size();
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), 0.7).isEmpty());
    QCOMPARE(session.undoNames().size(), depth + 1); // one step for both objects
    QVERIFY(std::abs(*readParam(object(session, a), "row_spacing_mm") - 0.7) < 1e-6);
    QVERIFY(std::abs(*readParam(object(session, b), "row_spacing_mm") - 0.7) < 1e-6);
    QVERIFY(!fieldOf(inspector, QStringLiteral("row_spacing_mm"))
                 .value(QStringLiteral("mixed"))
                 .toBool());
    QVERIFY(session.undo());
    QVERIFY(std::abs(*readParam(object(session, a), "row_spacing_mm") - 0.5) < 1e-6);
    QVERIFY(std::abs(*readParam(object(session, b), "row_spacing_mm") - 0.7) > 0.05); // original
    // A selection mixing types exposes ONLY the settings every type has in common, and an edit
    // reaches each object through its own type, in one undo step.
    QVERIFY(session.applyBatch(batchSetStitchType({b}, "directional"), "Type").committed);
    QVERIFY(inspector.typeMixed());
    QVERIFY(inspector.fields().size() > 0);
    // A tatami-only setting is hidden; the shared ones stay.
    std::string tatamiOnly;
    for (const auto& spec : allParamSpecs("tatami")) {
        const auto other = allParamSpecs("directional");
        if (spec.kind != ParamKind::Point && spec.kind != ParamKind::List &&
            std::ranges::none_of(other, [&](const auto& o) { return o.key == spec.key; })) {
            tatamiOnly = std::string(spec.key);
            break;
        }
    }
    QVERIFY(!tatamiOnly.empty());
    QVERIFY(fieldOf(inspector, QString::fromStdString(tatamiOnly)).isEmpty());
    QVERIFY(!fieldOf(inspector, QStringLiteral("pull_compensation_mm")).isEmpty());
    const auto mixedDepth = session.undoNames().size();
    QVERIFY(inspector.setParam(QStringLiteral("pull_compensation_mm"), 0.3).isEmpty());
    QCOMPARE(session.undoNames().size(), mixedDepth + 1);
    QVERIFY(std::abs(*readParam(object(session, a), "pull_compensation_mm") - 0.3) < 1e-6);
    QVERIFY(std::abs(*readParam(object(session, b), "pull_compensation_mm") - 0.3) < 1e-6);
    QVERIFY(!fieldOf(inspector, QStringLiteral("pull_compensation_mm"))
                 .value(QStringLiteral("mixed"))
                 .toBool());
    // Reset sends each type back to its own default.
    QVERIFY(inspector.resetParam(QStringLiteral("pull_compensation_mm")).isEmpty());
    QVERIFY(fieldOf(inspector, QStringLiteral("pull_compensation_mm"))
                .value(QStringLiteral("isDefault"))
                .toBool());
    QVERIFY(inspector.resetAll().isEmpty());
    // A setting only one type has is still refused.
    QVERIFY(!inspector.setParam(QString::fromStdString(tatamiOnly), 0.4).isEmpty());
}

// Couleur de la sélection : commune ou « différentes », changée d'un coup pour des types de points
// différents (une étape d'annulation), refus d'une couleur invalide ou d'une sélection vide.
void StudioInspectorTest::colourOfTheSelectionIncludingMixedTypes() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto a = addRect(session, 0, 0);
    const auto b = addRect(session, 40, 0);
    StudioController studio(session);
    InspectorController inspector(studio);
    select(session, {});
    QVERIFY(!inspector.setColour(QStringLiteral("#ff0000")).isEmpty()); // rien de sélectionné
    QVERIFY(session.applyBatch(batchSetStitchType({b}, "directional"), "Type").committed);
    select(session, {a, b});
    QVERIFY(inspector.typeMixed());
    const auto depth = session.undoNames().size();
    QVERIFY(!inspector.setColour(QStringLiteral("pas-une-couleur")).isEmpty());
    QCOMPARE(session.undoNames().size(), depth);

    QVERIFY(inspector.setColour(QStringLiteral("#ff0000")).isEmpty());
    QCOMPARE(session.undoNames().size(), depth + 1);
    QCOMPARE(object(session, a).rgb, (std::array<std::uint8_t, 3>{255, 0, 0}));
    QCOMPARE(object(session, b).rgb, (std::array<std::uint8_t, 3>{255, 0, 0}));
    QCOMPARE(inspector.colour(), QStringLiteral("#ff0000"));
    QVERIFY(!inspector.colourMixed());

    QVERIFY(inspector.setColour(QStringLiteral("#00ff00")).isEmpty());
    select(session, {a});
    QVERIFY(inspector.setColour(QStringLiteral("#0000ff")).isEmpty());
    select(session, {a, b});
    QVERIFY(inspector.colour().isEmpty());
    QVERIFY(inspector.colourMixed());
    QVERIFY(session.undo());
    QVERIFY(!inspector.colourMixed());
    QCOMPARE(inspector.colour(), QStringLiteral("#00ff00"));
}

void StudioInspectorTest::resetToDefaults() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    inspector.setShowAdvanced(true);
    const document::TatamiParams defaults;
    QVERIFY(inspector.setParam(QStringLiteral("angle_deg"), 45).isEmpty());
    QVERIFY(inspector.setParam(QStringLiteral("stagger"), 3).isEmpty());
    QVERIFY(!fieldOf(inspector, QStringLiteral("angle_deg"))
                 .value(QStringLiteral("isDefault"))
                 .toBool());
    QVERIFY(inspector.resetParam(QStringLiteral("angle_deg")).isEmpty());
    QVERIFY(fieldOf(inspector, QStringLiteral("angle_deg"))
                .value(QStringLiteral("isDefault"))
                .toBool());
    QVERIFY(inspector.resetAll().isEmpty());
    const auto& tatami = std::get<document::TatamiParams>(object(session, id).params);
    QVERIFY(tatami == defaults);
}

void StudioInspectorTest::stitchTypeConversion() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    QCOMPARE(inspector.typeId(), QStringLiteral("tatami"));
    const auto depth = session.undoNames().size();
    QVERIFY(inspector.setStitchType(QStringLiteral("running")).isEmpty());
    QCOMPARE(inspector.typeId(), QStringLiteral("running"));
    QCOMPARE(session.undoNames().size(), depth + 1);
    QVERIFY(fieldOf(inspector, QStringLiteral("stitch_length_mm")).contains(QStringLiteral("key")));
    QVERIFY(session.undo());
    QCOMPARE(inspector.typeId(), QStringLiteral("tatami"));
    QVERIFY(!inspector.setStitchType(QStringLiteral("no_such_type")).isEmpty());
    QVERIFY(inspector.setStitchType(QStringLiteral("spiral_fill")).isEmpty());
    QCOMPARE(inspector.typeId(), QStringLiteral("spiral"));
    QVERIFY(inspector.setStitchType(QStringLiteral("directional")).isEmpty());
    QCOMPARE(inspector.typeId(), QStringLiteral("directional"));
    QVERIFY(inspector.setStitchType(QStringLiteral("contour_fill")).isEmpty());
    QCOMPARE(inspector.typeId(), QStringLiteral("contour_fill"));
    QVERIFY(inspector.fields().size() > 0); // concentric: its table is complete since family J
}

void StudioInspectorTest::searchAndLevels() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    const int essentials = inspector.fields().size();
    QVERIFY(inspector.hiddenAdvancedCount() > 0);
    QVERIFY(fieldOf(inspector, QStringLiteral("stagger")).isEmpty());
    inspector.setShowAdvanced(true);
    QVERIFY(inspector.hiddenExpertCount() > 0); // the expert fields wait for the expert level
    QCOMPARE(inspector.fields().size(),
             inspector.totalFieldCount() - inspector.hiddenExpertCount());
    QVERIFY(inspector.fields().size() > essentials);
    QVERIFY(fieldOf(inspector, QStringLiteral("entry_point_mm")).isEmpty());
    inspector.setShowExpert(true);
    QCOMPARE(inspector.fields().size(), inspector.totalFieldCount());
    QVERIFY(!fieldOf(inspector, QStringLiteral("entry_point_mm")).isEmpty());
    // A search reaches an expert field without switching level.
    inspector.setShowExpert(false);
    inspector.setShowAdvanced(false);
    inspector.setQuery(QStringLiteral("depart du remplissage"));
    QCOMPARE(inspector.fields().size(), 1);
    inspector.setQuery({});
    inspector.setShowAdvanced(false);
    inspector.setQuery(QStringLiteral("quinconce")); // an advanced field found by its label
    QCOMPARE(inspector.fields().size(), 1);
    inspector.setQuery(QStringLiteral("ECART")); // accent- and case-insensitive
    QVERIFY(!fieldOf(inspector, QStringLiteral("row_spacing_mm")).isEmpty());
    inspector.setQuery(QStringLiteral("zzzz"));
    QCOMPARE(inspector.fields().size(), 0);
}

void StudioInspectorTest::machineOutputIsByteIdentical() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session, 0, 0, 24, 14);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    const auto baseline = dstOf(session);
    QVERIFY(!baseline.empty());
    // Same value: no undo entry, same bytes.
    const auto current = *readParam(object(session, id), "row_spacing_mm");
    const auto depth = session.undoNames().size();
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), current).isEmpty());
    QCOMPARE(session.undoNames().size(), depth);
    QVERIFY(dstOf(session) == baseline);
    // A real change alters the machine output; coming back restores it byte for byte.
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), 0.9).isEmpty());
    QVERIFY(dstOf(session) != baseline);
    QVERIFY(inspector.resetParam(QStringLiteral("row_spacing_mm")).isEmpty());
    QVERIFY(dstOf(session) == baseline);
    QVERIFY(inspector.setParam(QStringLiteral("angle_deg"), 33).isEmpty());
    QVERIFY(dstOf(session) != baseline);
    QVERIFY(session.undo());
    QVERIFY(dstOf(session) == baseline);
}

void StudioInspectorTest::persistsInTheOspFile() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    select(session, {id});
    StudioController studio(session);
    InspectorController inspector(studio);
    inspector.setShowAdvanced(true);
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), 0.55).isEmpty());
    QVERIFY(inspector.setParam(QStringLiteral("pull_mode"), 2).isEmpty());
    QVERIFY(inspector.setParam(QStringLiteral("underlay_zigzag"), 1).isEmpty());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = std::filesystem::path(dir.path().toStdWString()) / "inspector.osp";
    const auto saved = project_io::save_project(path, session.project());
    QVERIFY(saved.has_value());
    const auto loaded = project_io::load_project(path);
    QVERIFY(loaded.has_value());
    const auto& reloaded = loaded->embroidery_objects.front();
    QVERIFY(std::abs(*readParam(reloaded, "row_spacing_mm") - 0.55) < 1e-6);
    QCOMPARE(*readParam(reloaded, "pull_mode"), 2.0);
    QCOMPARE(*readParam(reloaded, "underlay_zigzag"), 1.0);
}

void StudioInspectorTest::everyTypePersistsAndKeepsTheMachineOutput() {
    for (const auto& type : kAllTypes) {
        DocumentSession session;
        const auto id = objectOfType(session, type);
        StudioController studio(session);
        InspectorController inspector(studio);
        inspector.setShowAdvanced(true);
        inspector.setShowExpert(true);
        const auto specs = allParamSpecs(stitchTypeId(object(session, id)));
        // Identical parameters: no undo entry, byte-identical machine output.
        const auto baseline = dstOf(session);
        const auto depth = session.undoNames().size();
        for (const auto& spec : specs) {
            if (!isScalarKind(spec.kind)) {
                continue;
            }
            const auto key = QString::fromUtf8(spec.key.data(), static_cast<int>(spec.key.size()));
            const auto current = *readParam(object(session, id), spec.key);
            QVERIFY2(inspector.setParam(key, current).isEmpty(),
                     qPrintable(QString::fromStdString(type) + "." + key));
        }
        QCOMPARE(session.undoNames().size(), depth);
        QVERIFY2(dstOf(session) == baseline, type.c_str());
        // Every field changed, then the file saved and reloaded: the values come back.
        std::vector<std::pair<std::string, double>> expected;
        for (const auto& spec : specs) {
            if (spec.kind == ParamKind::Point || spec.kind == ParamKind::List) {
                continue;
            }
            const auto key = QString::fromUtf8(spec.key.data(), static_cast<int>(spec.key.size()));
            const double target = otherValue(spec, *readParam(object(session, id), spec.key));
            QVERIFY2(inspector.setParam(key, target).isEmpty(),
                     qPrintable(QString::fromStdString(type) + "." + key));
            expected.emplace_back(std::string(spec.key), *readParam(object(session, id), spec.key));
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = std::filesystem::path(dir.path().toStdWString()) / "every.osp";
        QVERIFY(project_io::save_project(path, session.project()).has_value());
        const auto loaded = project_io::load_project(path);
        QVERIFY(loaded.has_value());
        const auto& reloaded = loaded->embroidery_objects.front();
        QVERIFY(reloaded.params == object(session, id).params);
        for (const auto& [key, value] : expected) {
            const auto v = readParam(reloaded, key);
            QVERIFY2(v.has_value() && std::abs(*v - value) < 1e-6,
                     qPrintable(QString::fromStdString(type + "." + key)));
        }
    }
}

void StudioInspectorTest::presetsLifecycle() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto a = addRect(session, 0, 0);
    const auto b = addRect(session, 40, 0);
    select(session, {a});
    StudioController studio(session);
    InspectorController inspector(studio);
    QVERIFY(inspector.setParam(QStringLiteral("row_spacing_mm"), 0.6).isEmpty());
    QVERIFY(!inspector.savePreset(QStringLiteral("  ")).isEmpty()); // empty name refused
    QVERIFY(inspector.savePreset(QStringLiteral("Dense")).isEmpty());
    QCOMPARE(inspector.presets().size(), 1);
    select(session, {b});
    QVERIFY(inspector.applyPreset(0).isEmpty());
    QVERIFY(std::abs(*readParam(object(session, b), "row_spacing_mm") - 0.6) < 1e-6);
    QVERIFY(session.undo()); // applying is one undo step
    QVERIFY(std::abs(*readParam(object(session, b), "row_spacing_mm") - 0.6) > 0.05);
    // By type: a preset of another type is listed only with "all types".
    QVERIFY(inspector.setStitchType(QStringLiteral("running")).isEmpty());
    QCOMPARE(inspector.presets().size(), 0);
    inspector.setAllPresets(true);
    QCOMPARE(inspector.presets().size(), 1);
    QVERIFY(!inspector.presets().front().toMap().value(QStringLiteral("matches")).toBool());
    // Rename = one undo step, duplicate names are refused.
    QVERIFY(inspector.savePreset(QStringLiteral("Other")).isEmpty());
    QVERIFY(!inspector.renamePreset(0, QStringLiteral("Other")).isEmpty());
    const auto depth = session.undoNames().size();
    QVERIFY(inspector.renamePreset(0, QStringLiteral("Dense 2")).isEmpty());
    QCOMPARE(session.undoNames().size(), depth + 1);
    QCOMPARE(session.project().stitch_presets.size(), static_cast<std::size_t>(2));
    // The renamed preset keeps its slot (no reordering): « Dense 2 » first, « Other » second.
    QCOMPARE(QString::fromStdString(session.project().stitch_presets.front().name),
             QStringLiteral("Dense 2"));
    QCOMPARE(QString::fromStdString(session.project().stitch_presets.back().name),
             QStringLiteral("Other"));
    QVERIFY(session.undo());
    QCOMPARE(QString::fromStdString(session.project().stitch_presets.front().name),
             QStringLiteral("Dense"));
    QCOMPARE(QString::fromStdString(session.project().stitch_presets.back().name),
             QStringLiteral("Other"));
    QVERIFY(inspector.removePreset(0).isEmpty());
    QCOMPARE(session.project().stitch_presets.size(), static_cast<std::size_t>(1));
    QVERIFY(!inspector.removePreset(7).isEmpty());
}

void StudioInspectorTest::qmlInspectorEditsThroughTheController() {
    DocumentSession session;
    auto created = project_ops::create_empty_project({});
    session.replaceProject(std::move(*created), true);
    const auto id = addRect(session);
    StudioController studio(session);
    InspectorController inspector(studio);
    QQmlEngine engine;
    QStringList qmlWarnings;
    connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
        for (const auto& e : errors) {
            qmlWarnings.push_back(e.toString());
        }
    });
    engine.rootContext()->setContextProperty(QStringLiteral("inspector"), &inspector);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/studio/qml/Inspector.qml")));
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(320, 900);
    std::unique_ptr<QObject> created_item(component.create());
    QVERIFY2(created_item != nullptr, qPrintable(component.errorString()));
    auto* root = qobject_cast<QQuickItem*>(created_item.get());
    QVERIFY(root != nullptr);
    root->setParentItem(window.contentItem());
    root->setSize({320, 900});
    QVERIFY(!root->isVisible()); // nothing selected: the inspector stays out of the way
    select(session, {id});
    QTRY_VERIFY(root->isVisible());
    const auto find = [&](const char* name) { return findItem(root, QString::fromLatin1(name)); };
    QTRY_VERIFY2(find("entry_row_spacing_mm") != nullptr,
                 qPrintable(qmlWarnings.join(QLatin1Char(10))));
    auto* entry = find("entry_row_spacing_mm");
    // Typed refusal: the text stays, the message appears, the document is untouched.
    const auto revision = session.revision();
    entry->setProperty("text", QStringLiteral("9"));
    QMetaObject::invokeMethod(entry, "editingFinished");
    auto* error = find("error_row_spacing_mm");
    QVERIFY(error != nullptr);
    QVERIFY(error->isVisible());
    QCOMPARE(session.revision(), revision);
    QCOMPARE(entry->property("text").toString(), QStringLiteral("9"));
    // Accepted entry (decimal comma) goes through the controller.
    entry->setProperty("text", QStringLiteral("0,45"));
    QMetaObject::invokeMethod(entry, "editingFinished");
    QVERIFY(std::abs(*readParam(object(session, id), "row_spacing_mm") - 0.45) < 1e-6);
    QTRY_VERIFY(!error->isVisible());
    // Switch, combo, search and advanced toggle are the controller's own state.
    auto* toggle = find("switch_underlay_edge");
    QVERIFY(toggle != nullptr);
    const auto before = *readParam(object(session, id), "underlay_edge");
    toggle->setProperty("checked", before < 0.5); // what a click does, then the user signal
    QMetaObject::invokeMethod(toggle, "toggled");
    QVERIFY(*readParam(object(session, id), "underlay_edge") != before);
    auto* combo = find("combo_pull_mode");
    QVERIFY(combo != nullptr);
    QCOMPARE(find("advancedSwitch") != nullptr, true);
    inspector.setShowAdvanced(true);
    QTRY_VERIFY(find("param_stagger") != nullptr);
    auto* reset = find("reset_row_spacing_mm");
    QVERIFY(reset != nullptr);
    QMetaObject::invokeMethod(reset, "clicked");
    QTRY_VERIFY(std::abs(*readParam(object(session, id), "row_spacing_mm") -
                         document::TatamiParams{}.row_spacing.value / 1000.0) < 1e-6);
}

QTEST_MAIN(StudioInspectorTest)
#include "test_studio_inspector.moc"
