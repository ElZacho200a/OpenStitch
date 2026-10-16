// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

// Service Qt-free équivalent de ce que fait le bureau (`MainWindow::stitchParamsForType`,
// type « satin ») : même contour source (`pristine` = contour brut de la région quand il
// existe), même moteur (`auto_satin::generate_satin` avec ses diagnostics), mêmes
// refus nommés. AUCUNE seconde implémentation, aucun repli silencieux sur une géométrie de
// moindre qualité : si le moteur ne construit aucune colonne, le plan est refusé avec ses
// messages.
struct AutoSatinPlanRequest {
    std::optional<Micrometers> spacing; // défaut : celui d'`AutoSatinParams` (0,4 mm)
    bool compact_axis{false};           // formes compactes : axe synthétique (cf. moteur)
    document::AutoSatinStrategy strategy{document::AutoSatinStrategy::Skeleton}; // méthode
    std::optional<Angle> sweep_angle; // balayage parallèle : angle explicite (sinon auto)
    bool use_pristine_contour{true};  // contour brut de la région source si disponible
    std::vector<document::AutoSatinGuide> guides;
};

struct AutoSatinPlan {
    bool accepted{false};             // faux : `refusal` dit pourquoi, `params` inutilisable
    document::AutoSatinParams params; // à poser avec ConvertFillGroupCommand / SetStitchParams
    // Contour à restaurer avant conversion (comme le bureau) ; absent = contour source inchangé.
    std::optional<std::vector<geometry::PathSet>> restored_contour;
    // Diagnostics du moteur et de la satinabilité (`auto_satin::evaluate_satinability`).
    std::string satinability_status; // « Suitable », « RequiresDecomposition », …
    std::size_t columns{0};
    double coverage_ratio{0.0}; // 0 si non mesurée
    bool coverage_measured{false};
    std::vector<std::string> messages; // refus et avertissements nommés du moteur
    std::string refusal;               // phrase de refus (vide si accepté)
};

// `vector_object` = objet VECTORIEL source de l'objet de broderie visé. Erreur
// (`Result`) seulement pour un défaut d'appel (objet inconnu) ; un refus métier est un plan
// `accepted == false`.
[[nodiscard]] Result<AutoSatinPlan> plan_auto_satin(const document::Project& project,
                                                    ObjectId vector_object,
                                                    const AutoSatinPlanRequest& request = {});

} // namespace openstitch::project_ops
