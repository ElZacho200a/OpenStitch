// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/fabric_rules.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"
#include "openstitch/stitch_analysis/text_rules.hpp"
#include "openstitch/stitch_generation/finish.hpp"

namespace openstitch::stitch_analysis {

namespace {

bool outside(Vec2um p, const stitch::BoundsUm& hoop) {
    return p.x < hoop.min.x || p.x > hoop.max.x || p.y < hoop.min.y || p.y > hoop.max.y;
}

// Points hors cadre d'un objet : nombre, premier point fautif et boîte englobante de TOUS
// ses points cousus (c'est elle qui doit rentrer dans le cadre).
struct ObjectOverflow {
    std::size_t count{0};
    Vec2um first{};
    stitch::BoundsUm box{Vec2um{Micrometers{std::numeric_limits<std::int32_t>::max()},
                                Micrometers{std::numeric_limits<std::int32_t>::max()}},
                         Vec2um{Micrometers{std::numeric_limits<std::int32_t>::min()},
                                Micrometers{std::numeric_limits<std::int32_t>::min()}}};
};

// Translation minimale (une composante) qui fait rentrer [lo, hi] dans [hoopLo, hoopHi] ;
// false si l'intervalle est plus grand que le cadre.
bool shift_into(std::int32_t lo, std::int32_t hi, std::int32_t hoopLo, std::int32_t hoopHi,
                std::int32_t& out) {
    if (static_cast<std::int64_t>(hi) - lo > static_cast<std::int64_t>(hoopHi) - hoopLo) {
        return false;
    }
    out = 0;
    if (lo < hoopLo) {
        out = hoopLo - lo;
    } else if (hi > hoopHi) {
        out = hoopHi - hi;
    }
    return true;
}

// Constats « hors-cadre » REGROUPÉS par objet (ordre d'identifiant croissant), avec la
// correction « ramener dans le cadre ». Applique le plafond par catégorie.
void group_out_of_hoop(const stitch::StitchSequence& sequence, const stitch::BoundsUm& hoop,
                       const document::Canvas& canvas, std::size_t cap, std::vector<Finding>& out,
                       std::map<std::string, std::size_t>& suppressed) {
    std::map<std::uint64_t, ObjectOverflow> perObject;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        ObjectOverflow& o = perObject[cmd.source.value];
        o.box.min.x = std::min(o.box.min.x, cmd.pos.x);
        o.box.min.y = std::min(o.box.min.y, cmd.pos.y);
        o.box.max.x = std::max(o.box.max.x, cmd.pos.x);
        o.box.max.y = std::max(o.box.max.y, cmd.pos.y);
        // Rectangle : bornes ; rond, ovale, coins arrondis : forme réelle (HP-HOOP-001).
        if (outside(cmd.pos, hoop) || !canvas.contains_usable(cmd.pos)) {
            if (o.count == 0) {
                o.first = cmd.pos;
            }
            ++o.count;
        }
    }
    std::size_t emitted = 0;
    std::size_t total = 0;
    for (const auto& [id, o] : perObject) {
        if (o.count == 0) {
            continue;
        }
        ++total;
        if (emitted >= cap) {
            continue;
        }
        ++emitted;
        Finding f;
        f.severity = Severity::Error;
        f.category = "hors-cadre";
        f.message = std::to_string(o.count) + " point(s) hors du cadre de broderie.";
        f.location = o.first;
        f.object = ObjectId{id};
        f.hint = "Déplacez l'objet dans le cadre ou agrandissez le canevas.";
        if (id != 0 && canvas.shape != document::HoopShape::Rectangle) {
            f.hint = "Déplacez l'objet dans la forme du cadre ou agrandissez le cadre.";
        } else if (id != 0) {
            std::int32_t dx = 0;
            std::int32_t dy = 0;
            if (shift_into(o.box.min.x.value, o.box.max.x.value, hoop.min.x.value, hoop.max.x.value,
                           dx) &&
                shift_into(o.box.min.y.value, o.box.max.y.value, hoop.min.y.value, hoop.max.y.value,
                           dy)) {
                f.fix = Fix{FixKind::MoveIntoHoop,
                            Vec2um{Micrometers{dx}, Micrometers{dy}},
                            {},
                            {},
                            "Ramener dans le cadre"};
            } else {
                f.hint = "Agrandissez le canevas.";
            }
        }
        out.push_back(std::move(f));
    }
    if (total > emitted) {
        suppressed["hors-cadre"] = total - emitted;
    }
}

bool has_category(const std::vector<Finding>& findings, const char* category) {
    return std::any_of(findings.begin(), findings.end(),
                       [&](const Finding& f) { return f.category == category; });
}

// « point-long » : le découpage (`finishing.split_long_stitches`) coupe un point seulement s'il
// suit directement un point cousu. Vrai si TOUS les points trop longs de la séquence sont dans
// ce cas (sinon activer le découpage ne résoudrait pas le constat).
bool all_long_stitches_splittable(const stitch::StitchSequence& sequence,
                                  const AnalysisOptions& options) {
    bool hasPrevStitch = false;
    Vec2um prevStitch{};
    const auto& cmds = sequence.commands;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const auto& cmd = cmds[i];
        if (cmd.type == stitch::CommandType::Jump) {
            hasPrevStitch = false;
            continue;
        }
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        if (hasPrevStitch && cmd.pass != stitch::StitchPass::Lock &&
            length_um(cmd.pos - prevStitch) > static_cast<double>(options.max_stitch.value) &&
            cmds[i - 1].type != stitch::CommandType::Stitch) {
            return false;
        }
        prevStitch = cmd.pos;
        hasPrevStitch = true;
    }
    return true;
}

// Active les finitions SUR LA SÉQUENCE (les finitions sont la dernière passe de
// `effective_sequence`, donc la séquence reçue d'un projet aux finitions désactivées est
// exactement leur entrée) et vérifie qu'aucun « saut-sans-coupe » ne subsiste.
bool enabling_finishing_resolves(const document::Project& project,
                                 const stitch::StitchSequence& sequence,
                                 const stitch::BoundsUm& hoop) {
    document::Project enabled = project;
    enabled.finishing.enabled = true;
    const auto finished = stitch_generation::finish_sequence(sequence, enabled);
    AnalysisOptions options = options_from_project(enabled);
    options.hoop = hoop;
    options.max_findings_per_category = std::numeric_limits<std::size_t>::max();
    return !has_category(analyze_detailed(finished, options).findings, "saut-sans-coupe");
}

} // namespace

stitch::BoundsUm canvas_hoop(const document::Canvas& canvas) {
    const std::int32_t hw = canvas.usable_width().value / 2;
    const std::int32_t hh = canvas.usable_height().value / 2;
    return {Vec2um{Micrometers{-hw}, Micrometers{-hh}}, Vec2um{Micrometers{hw}, Micrometers{hh}}};
}

AnalysisReport analyze_project(const document::Project& project,
                               const stitch::StitchSequence& sequence) {
    const AnalysisOptions options = options_from_project(project);
    const stitch::BoundsUm hoop = canvas_hoop(project.canvas);

    // `hors-cadre` est traité à part (regroupé par objet) : l'analyse de séquence tourne sans
    // cadre. Ces constats sont les seuls Error d'une séquence non vide, donc ils passent en tête.
    AnalysisReport detailed = analyze_detailed(sequence, options);

    AnalysisReport report;
    if (!detailed.findings.empty() && detailed.findings.front().category == "vide") {
        report = std::move(detailed);
    } else {
        group_out_of_hoop(sequence, hoop, project.canvas, options.max_findings_per_category,
                          report.findings, report.suppressed);
        for (auto& f : detailed.findings) {
            report.findings.push_back(std::move(f));
        }
        for (const auto& [category, count] : detailed.suppressed) {
            report.suppressed[category] = count;
        }
    }

    // Corrections qui dépendent du projet (réglages de finitions).
    if (project.finishing.enabled && !project.finishing.split_long_stitches &&
        project.finishing.max_stitch_length.value > 0 &&
        has_category(report.findings, "point-long") &&
        all_long_stitches_splittable(sequence, options)) {
        for (Finding& f : report.findings) {
            if (f.category == "point-long") {
                f.fix = Fix{FixKind::SplitLongStitches, {}, {}, {}, "Découper les points longs"};
            }
        }
    }
    if (!project.finishing.enabled && has_category(report.findings, "saut-sans-coupe") &&
        enabling_finishing_resolves(project, sequence, hoop)) {
        for (Finding& f : report.findings) {
            if (f.category == "saut-sans-coupe") {
                f.fix = Fix{FixKind::EnableFinishing,
                            {},
                            {},
                            {},
                            "Activer les finitions (coupes et points d'arrêt)"};
            }
        }
    }

    for (Finding& f : analyze_text_objects(project)) {
        report.findings.push_back(std::move(f));
    }
    for (Finding& f : check_stitch_limits(project)) {
        report.findings.push_back(std::move(f));
    }
    for (Finding& f : check_thread_vs_fabric(project)) { // HP-THR-008
        report.findings.push_back(std::move(f));
    }
    // HP-HOOP-003 : profil machine (champ, cadres, aiguilles, points longs) ; le format
    // d'export se vérifie à l'export, via `check_machine` avec le format choisi.
    for (Finding& f : check_machine(project, sequence)) {
        report.findings.push_back(std::move(f));
    }
    return report;
}

} // namespace openstitch::stitch_analysis
