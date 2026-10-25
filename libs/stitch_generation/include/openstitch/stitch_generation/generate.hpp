// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_generation {

// Assemble la séquence machine complète du projet : les objets de broderie
// dans leur ordre, un Jump vers le début de chaque contour, un ColorChange
// entre deux objets de couleurs différentes, End à la fin.
// Fonction pure : recalculable à tout moment depuis le document (ADR-014).
[[nodiscard]] Result<stitch::StitchSequence> generate_sequence(const document::Project& project);
// Variante annulable avec progression (HP-PERF-001) : même séquence octet pour octet quand elle
// n'est pas annulée ; une annulation renvoie `ErrorCategory::Cancelled` sans rien produire.
// La progression compte les unités de génération terminées (appelée depuis les fils de travail).
[[nodiscard]] Result<stitch::StitchSequence> generate_sequence(const document::Project& project,
                                                               const TaskContext& task);

// Cache de régénération par objet (HP-PERF-002) : les objets droits, tatami et directionnels
// sont mémorisés avec une clé exacte (identifiant, paramètres, géométrie source). Modifier un
// objet ne régénère que lui. La séquence produite est identique, octet pour octet, avec ou sans
// cache (test d'équivalence). Les fonctions ci-dessous servent aux tests et au banc de mesure.
struct ChunkCacheStats {
    std::size_t entries{0};
    std::size_t bytes{0};
    std::size_t hits{0};
    std::size_t misses{0};
};
[[nodiscard]] ChunkCacheStats chunk_cache_stats();
void clear_chunk_cache();
void set_chunk_cache_enabled(bool enabled);

} // namespace openstitch::stitch_generation
