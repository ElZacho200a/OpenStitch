// SPDX-License-Identifier: Apache-2.0
#include "vector_controller.hpp"

#include "document_services.hpp"

#include <algorithm>
#include <cmath>

#include "edit_batch.hpp"
#include "node_edit.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

std::string wrap(const std::string& ops) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

std::string idList(const std::vector<std::uint64_t>& ids) {
    std::string out = "[";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        out += (i == 0 ? "" : ",") + std::to_string(ids[i]);
    }
    return out + "]";
}

std::string num(double v) {
    return std::to_string(std::round(v * 1e6) / 1e6);
}

} // namespace

VectorController::VectorController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::selectionChanged, this,
            &VectorController::selectionCountChanged);
}

std::vector<std::uint64_t> VectorController::ids() const {
    std::vector<std::uint64_t> out;
    for (const auto& t : studio_.session().selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject) {
            out.push_back(t.id);
        }
    }
    return out; // ordre de sélection conservé : le dernier est l'objet principal
}

int VectorController::selectedCount() const {
    return static_cast<int>(ids().size());
}

void VectorController::say(const QString& text) {
    message_ = text;
    emit messageChanged();
}

bool VectorController::apply(const std::string& batch, const QString& label, bool selectCreated) {
    const auto result = studio_.session().applyBatch(batch, label.toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        say(label + QStringLiteral(" refusé : ") +
            (outcomes.empty() ? QStringLiteral("aucun effet")
                              : QString::fromStdString(outcomes.back().message)));
        return false;
    }
    if (selectCreated) {
        std::vector<Target> created;
        for (const auto id : createdObjectIds(result.report)) {
            created.push_back({TargetKind::EmbroideryObject, id});
        }
        if (!created.empty()) {
            studio_.session().selection().replace(std::move(created));
        }
    }
    say(label);
    return true;
}

bool VectorController::booleanOp(const QString& mode) {
    static const char* const kModes[] = {"union", "subtract", "intersect", "exclude"};
    static const QString kLabels[] = {QStringLiteral("Union"), QStringLiteral("Soustraction"),
                                      QStringLiteral("Intersection"), QStringLiteral("Exclusion")};
    const auto m = mode.toStdString();
    std::size_t index = 4;
    for (std::size_t i = 0; i < 4; ++i) {
        if (m == kModes[i]) {
            index = i;
        }
    }
    if (index == 4) {
        say(QStringLiteral("Opération booléenne inconnue : %1").arg(mode));
        return false;
    }
    const auto selected = ids();
    if (selected.size() < 2) {
        say(QStringLiteral("Sélectionnez au moins deux formes (la dernière est la principale)"));
        return false;
    }
    return apply(
        wrap(R"({"op":"merge","objects":)" + idList(selected) + R"(,"mode":")" + m + "\"}"),
        kLabels[index], true);
}

bool VectorController::breakApart() {
    const auto selected = ids();
    if (selected.empty()) {
        say(QStringLiteral("Séparer : sélectionnez une forme"));
        return false;
    }
    std::string ops;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":"break_apart","object":)" +
               std::to_string(selected[i]) + "}";
    }
    return apply(wrap(ops), QStringLiteral("Séparer les morceaux"), true);
}

bool VectorController::offsetShapes(double offsetMm) {
    const auto result = openstitch::desktop_v2::offsetShapes(studio_.session(), ids(), offsetMm);
    say(result.ok ? QStringLiteral("Forme décalée créée.")
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool VectorController::outlineShapes(double widthMm) {
    const auto result = openstitch::desktop_v2::outlineShapes(studio_.session(), ids(), widthMm);
    say(result.ok ? QStringLiteral("Contour créé autour de la sélection.")
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool VectorController::simplify(double toleranceMm) {
    const auto selected = ids();
    if (selected.empty() || !(toleranceMm > 0.0)) {
        say(QStringLiteral("Simplifier : sélection ou tolérance invalide"));
        return false;
    }
    return apply(wrap(R"({"op":"simplify_shapes","objects":)" + idList(selected) +
                      R"(,"tolerance_mm":)" + num(toleranceMm) + "}"),
                 QStringLiteral("Simplifier les contours"), false);
}

bool VectorController::smooth(double toleranceMm) {
    const auto selected = ids();
    if (selected.empty() || !(toleranceMm > 0.0)) {
        say(QStringLiteral("Lisser : sélection ou tolérance invalide"));
        return false;
    }
    return apply(wrap(R"({"op":"smooth_shapes","objects":)" + idList(selected) +
                      R"(,"tolerance_mm":)" + num(toleranceMm) + "}"),
                 QStringLiteral("Lisser les contours"), false);
}

bool VectorController::fillHoles(double maxAreaMm2) {
    const auto selected = ids();
    if (selected.empty() || !(maxAreaMm2 > 0.0)) {
        say(QStringLiteral("Combler : sélection ou aire invalide"));
        return false;
    }
    return apply(wrap(R"({"op":"fill_small_holes","objects":)" + idList(selected) +
                      R"(,"max_area_mm2":)" + num(maxAreaMm2) + "}"),
                 QStringLiteral("Combler les trous"), false);
}

bool VectorController::convertToCurves() {
    const auto r = desktop_v2::convertToCurves(studio_.session(), ids());
    say(r.ok ? QStringLiteral("Converti en courbes") : QString::fromStdString(r.message));
    return r.ok;
}

bool VectorController::smoothContours() {
    const auto r = desktop_v2::smoothContours(studio_.session(), ids());
    say(r.ok ? QStringLiteral("Contours lissés") : QString::fromStdString(r.message));
    return r.ok;
}

} // namespace openstitch::desktop_v2::studio
