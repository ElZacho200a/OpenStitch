// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "openstitch/ai_segmentation/backend_session.hpp"

namespace openstitch::ai_segmentation {

// Lancement d'un worker local. stdin/stdout sont les canaux JSON Lines ; stderr est hérité du
// processus parent (c'est là que le worker écrit ses logs). Aucun shell n'est utilisé : les
// arguments sont passés tels quels (pas d'injection par un chemin ou un nom de modèle).
struct ProcessSpec {
    std::string executable; // nom résolu via PATH ou chemin
    std::vector<std::string> args;
    std::filesystem::path working_dir; // vide = répertoire courant
};

// Transport par processus fils (Windows : CreateProcess ; POSIX : fork/exec). Un thread lit
// stdout ; les lignes sont remises par `read_line` avec délai.
[[nodiscard]] AiResult<std::unique_ptr<ITransport>> spawn_process(const ProcessSpec& spec);

} // namespace openstitch::ai_segmentation
