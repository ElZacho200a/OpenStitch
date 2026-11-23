// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- segmentation assistée par IA de l'image source, sans Qt. Un backend déclaré dans
// les réglages IA (`openstitch.ai.backends/1`) propose des masques par FORME ; chaque forme est
// ensuite subdivisée par COULEUR (quantification CIELAB, le même algorithme que la segmentation
// classique). Aucun échec n'est silencieux : backend absent, en échec ou sans masque => repli
// EXPLICITE sur la segmentation couleur classique (CIELAB), avec la raison dans `notice`.

#include <filesystem>
#include <string>

#include "openstitch/ai_segmentation/backend_registry.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop_v2 {

// Moteur « sans IA » : segmentation couleur CIELAB seule.
inline constexpr const char* kAiEngineClassical = "classical";

struct AiSegmentationRequest {
    // "" = backend préféré des réglages ; kAiEngineClassical = CIELAB sans IA ; sinon un `id`.
    std::string engine;
    // Fichier `openstitch.ai.backends/1` ; vide = variable OPENSTITCH_AI_BACKENDS.
    std::filesystem::path settings_file;
    // Dossier de travail des tâches ; vide = dossier temporaire du système.
    std::filesystem::path job_root;
    // Subdivise chaque forme retenue par couleur (CIELAB). Sans cela : formes IA telles quelles.
    bool refine_by_color{true};
    int max_colors{8};
    int min_region_px{16};
    // Nettoyage topologique (aires physiques en mm²).
    double min_island_mm2{0.3};
    double min_hole_mm2{0.3};
    // Options transmises au backend (vide = non transmis).
    std::string profile;
    int max_resolution{0};
};

struct AiSegmentationOutcome {
    segmentation::Segmentation segmentation;
    bool used_backend{false}; // faux = segmentation couleur classique
    std::string engine_label; // moteur qui a réellement répondu
    std::string notice;       // repli, découpe couleur impossible, modèle utilisé… (français)
    std::size_t mask_count{0};
};

// `source` : image source + pile d'opérations (l'image de travail est celle envoyée au backend).
// `factory` : fabrique de transport (processus réels par défaut ; faux dans les tests).
[[nodiscard]] Result<AiSegmentationOutcome>
segmentWithAi(const document::ImageSource& source, const AiSegmentationRequest& request,
              const TaskContext& task,
              const ai_segmentation::TransportFactory& factory =
                  ai_segmentation::process_transport_factory());

} // namespace openstitch::desktop_v2
