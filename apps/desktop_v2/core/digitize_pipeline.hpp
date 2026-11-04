// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- chaîne « image -> broderie -> DST » (premier objectif de la mission). Service pur,
// sans Qt, qui s'exécute SUR UN FIL DE TRAVAIL via TaskManager : il ne touche jamais la session
// vivante, il rend un projet que l'appelant installe ensuite par `DocumentSession::replaceProject`.
// Tout passe par les mêmes fonctions que la CLI (`project_ops::digitize_image_into`,
// `stitch_generation::effective_sequence`), donc mêmes octets en sortie.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::desktop_v2 {

struct DigitizeRequest {
    std::filesystem::path image;
    // > 0 : hauteur physique visée du motif (mm), convertie en résolution d'import (dpi) ; 0 =
    // `settings.dpi` tel quel.
    double target_height_mm{0.0};
    project_ops::DigitizeSettings settings;
};

struct DigitizeOutput {
    document::Project project;
    stitch::StitchSequence sequence;
    stitch::StitchStats stats;
    project_ops::DigitizeReport report;
    double width_mm{0.0};
    double height_mm{0.0};
    std::vector<std::string> log; // journal d'état de la chaîne (affichable tel quel)
};

// Étapes de progression rapportées : 0 import, 1 numérisation, 2 génération des points.
inline constexpr std::size_t kDigitizeSteps = 3;

[[nodiscard]] Result<DigitizeOutput> run_digitize(const DigitizeRequest& request,
                                                  const TaskContext& task);

// Écrit la séquence (DST). Séparé de `run_digitize` : l'export est une action utilisateur.
[[nodiscard]] Result<void> export_dst(const stitch::StitchSequence& sequence,
                                      const std::filesystem::path& path);

// Séquence effective du projet (retouches manuelles comprises) écrite en DST : c'est l'export
// utilisateur. Opère sur un instantané immuable, annulable.
[[nodiscard]] Result<stitch::StitchStats>
export_project_dst(std::shared_ptr<const document::Project> project,
                   const std::filesystem::path& path, const TaskContext& task);

// Formats machine inscrits dans `formats::registered_formats()` qui savent ÉCRIRE : (extension sans
// point, libellé), dans l'ordre du registre. Source unique pour le sélecteur de fichier et les
// tests.
struct ExportFormat {
    std::string extension;
    std::string label;
    bool carriesColors{false};
};
[[nodiscard]] std::vector<ExportFormat> exportFormats();

// Export machine selon l'extension de `path` (dst, pes, jef, exp, vp3…) : séquence EFFECTIVE du
// projet, couleurs des blocs dérivées du projet (`stitch_analysis::color_blocks`), nom de motif
// tiré du nom de fichier (ASCII, 16 caractères), écriture atomique. Instantané immuable, annulable.
[[nodiscard]] Result<stitch::StitchStats>
export_project_file(std::shared_ptr<const document::Project> project,
                    const std::filesystem::path& path, const TaskContext& task);

} // namespace openstitch::desktop_v2
