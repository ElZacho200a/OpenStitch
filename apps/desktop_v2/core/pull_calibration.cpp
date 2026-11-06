// SPDX-License-Identifier: Apache-2.0
#include "pull_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/pull_map.hpp"

namespace openstitch::desktop_v2 {

namespace sg = openstitch::stitch_generation;

namespace {
bool known(const std::string& name) {
    const auto& names = sg::fabric_preset_names();
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::string format(const char* fmt, double a, double b, double c, double d, double e) {
    char buffer[400];
    std::snprintf(buffer, sizeof buffer, fmt, a, b, c, d, e);
    return buffer;
}
} // namespace

PullCalibration::PullCalibration(std::string fabric)
    : piece_(sg::build_pull_test_piece()), fabric_(known(fabric) ? std::move(fabric) : "default") {
    refresh();
}

std::vector<std::string> PullCalibration::fabricNames() {
    std::vector<std::string> out;
    for (const auto name : sg::fabric_preset_names()) {
        out.emplace_back(name);
    }
    return out;
}

void PullCalibration::refresh() {
    auto fresh = sg::predict_pull_metrics(piece_, sg::fabric_preset(fabric_));
    for (auto& row : fresh) {
        for (const auto& old : results_) {
            if (old.metric.id == row.metric.id && old.has_measure) {
                row.has_measure = true;
                row.measured_mm = old.measured_mm;
                row.error_mm = row.predicted_mm - row.measured_mm;
            }
        }
    }
    results_ = std::move(fresh);
}

bool PullCalibration::setFabric(const std::string& name) {
    if (!known(name)) {
        return false;
    }
    fabric_ = name;
    refresh();
    return true;
}

std::size_t PullCalibration::applyCsv(const std::string& csv) {
    const std::size_t kept = sg::apply_measurements_csv(results_, csv);
    return kept;
}

bool PullCalibration::setMeasure(const std::string& id, double millimeters) {
    if (!std::isfinite(millimeters) || millimeters < 0.0) {
        return false;
    }
    for (auto& row : results_) {
        if (row.metric.id == id) {
            row.has_measure = true;
            row.measured_mm = millimeters;
            row.error_mm = row.predicted_mm - millimeters;
            return true;
        }
    }
    return false;
}

bool PullCalibration::clearMeasure(const std::string& id) {
    for (auto& row : results_) {
        if (row.metric.id == id) {
            row.has_measure = false;
            row.measured_mm = 0.0;
            row.error_mm = 0.0;
            return true;
        }
    }
    return false;
}

void PullCalibration::clearMeasures() {
    for (auto& row : results_) {
        row.has_measure = false;
        row.measured_mm = 0.0;
        row.error_mm = 0.0;
    }
}

std::vector<CalibrationRow> PullCalibration::rows() const {
    std::vector<CalibrationRow> out;
    out.reserve(results_.size());
    for (const auto& r : results_) {
        out.push_back({r.metric.id, r.metric.description, r.metric.variable, r.metric.nominal_mm,
                       r.predicted_mm, r.has_measure, r.measured_mm, r.error_mm});
    }
    return out;
}

std::size_t PullCalibration::measuredCount() const {
    return static_cast<std::size_t>(std::count_if(results_.begin(), results_.end(),
                                                  [](const auto& r) { return r.has_measure; }));
}

double PullCalibration::rmsMm() const {
    return sg::pull_metrics_rms(results_);
}

std::string PullCalibration::csv() const {
    return sg::pull_metrics_csv(results_);
}

CalibrationFit PullCalibration::fit() const {
    CalibrationFit out;
    const auto result = sg::fit_pull_response(piece_, results_, sg::fabric_preset(fabric_));
    out.enoughData = result.enough_data;
    out.used = result.used;
    out.rmsBeforeMm = result.rms_before_mm;
    out.rmsAfterMm = result.rms_after_mm;
    out.gain = result.response.gain;
    out.compression = result.response.compression;
    out.weftSoftness = result.response.grain_anisotropy;
    if (!out.enoughData) {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer,
                      "Mesures insuffisantes : %zu saisie(s), au moins 6 sont nécessaires pour un "
                      "ajustement fiable.",
                      out.used);
        out.message = buffer;
        return out;
    }
    out.message =
        format("Ajustement proposé : gain %.3f, tassement %.2f, souplesse de trame %.2f ; "
               "écart quadratique %.3f mm -> %.3f mm. Le modèle reste marqué « non "
               "calibré » tant que vous n'adoptez pas ces coefficients.",
               out.gain, out.compression, out.weftSoftness, out.rmsBeforeMm, out.rmsAfterMm);
    return out;
}

Result<void> PullCalibration::writeTestPiece(const std::string& path) const {
    return formats::write_dst_file(std::filesystem::path(path), piece_.sequence);
}

std::string PullComparison::summary() const {
    if (!ok) {
        return message;
    }
    char buffer[420];
    std::snprintf(buffer, sizeof buffer,
                  "Tissu « %s » : sur %zu points, déplacement prévu moyen %.2f mm, maximal %.2f "
                  "mm ; emprise nominale %.1f x %.1f mm -> prédite %.1f x %.1f mm.%s",
                  fabric.c_str(), points, meanShiftMm, maxShiftMm, nominalWidthMm, nominalHeightMm,
                  predictedWidthMm, predictedHeightMm,
                  calibrated ? ""
                             : " Estimation NON calibrée : utile pour comparer, pas comme mesure.");
    return buffer;
}

PullComparison compareProjectPull(const document::Project& project, const std::string& fabric) {
    PullComparison out;
    out.fabric = known(fabric) ? fabric : "default";
    const auto response = sg::fabric_preset(out.fabric);
    out.calibrated = response.calibrated;
    const auto nominal = sg::effective_sequence(project);
    if (!nominal) {
        out.message = nominal.error().message;
        return out;
    }
    const auto predicted = sg::simulate_fabric_pull(*nominal, response);
    if (predicted.commands.size() != nominal->commands.size()) {
        out.message =
            "La simulation du tirage a changé le nombre de commandes : comparaison refusée.";
        return out;
    }
    double sum = 0.0;
    std::int32_t nx0 = 0, nx1 = 0, ny0 = 0, ny1 = 0, px0 = 0, px1 = 0, py0 = 0, py1 = 0;
    bool first = true;
    for (std::size_t i = 0; i < nominal->commands.size(); ++i) {
        const auto& a = nominal->commands[i];
        if (a.type != stitch::CommandType::Stitch) {
            continue;
        }
        const auto& b = predicted.commands[i];
        const double shift = length_um(b.pos - a.pos) / 1000.0;
        sum += shift;
        out.maxShiftMm = std::max(out.maxShiftMm, shift);
        ++out.points;
        if (first) {
            nx0 = nx1 = a.pos.x.value;
            ny0 = ny1 = a.pos.y.value;
            px0 = px1 = b.pos.x.value;
            py0 = py1 = b.pos.y.value;
            first = false;
        } else {
            nx0 = std::min(nx0, a.pos.x.value);
            nx1 = std::max(nx1, a.pos.x.value);
            ny0 = std::min(ny0, a.pos.y.value);
            ny1 = std::max(ny1, a.pos.y.value);
            px0 = std::min(px0, b.pos.x.value);
            px1 = std::max(px1, b.pos.x.value);
            py0 = std::min(py0, b.pos.y.value);
            py1 = std::max(py1, b.pos.y.value);
        }
    }
    if (out.points == 0) {
        out.message = "Aucun point cousu à comparer : ajoutez ou numérisez des objets d'abord.";
        return out;
    }
    out.meanShiftMm = sum / static_cast<double>(out.points);
    out.nominalWidthMm = (static_cast<double>(nx1) - nx0) / 1000.0;
    out.nominalHeightMm = (static_cast<double>(ny1) - ny0) / 1000.0;
    out.predictedWidthMm = (static_cast<double>(px1) - px0) / 1000.0;
    out.predictedHeightMm = (static_cast<double>(py1) - py0) / 1000.0;
    out.ok = true;
    return out;
}

} // namespace openstitch::desktop_v2
