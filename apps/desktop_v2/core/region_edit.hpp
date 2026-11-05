// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- correction de la segmentation après numérisation : lister, fusionner, supprimer des
// régions, nettoyer les îlots, puis RÉGÉNÉRER les objets de broderie à partir de la segmentation
// corrigée. Chaque correction de région est UNE commande annulable du moteur ; la régénération est
// calculée sur une copie (tâche annulable) puis installée en UN pas d'annulation, et refusée si le
// document a changé entre-temps. Attention : régénérer REMPLACE les objets vectoriels et de
// broderie (les retouches faites sur ces objets sont perdues, mais annulables). Sans Qt.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_ops/factory.hpp"

namespace openstitch::desktop_v2 {

struct RegionInfo {
    std::uint64_t id{0};
    std::array<std::uint8_t, 3> rgb{};
    std::size_t pixels{0};
};

// Régions vivantes de la segmentation du projet, les plus grandes d'abord (à taille égale : par
// identifiant). Vide si le projet n'a pas de segmentation.
[[nodiscard]] std::vector<RegionInfo> listRegions(const document::Project& project);

// Région au pixel (x, y) de l'image de travail ; rien pour le fond ou hors image.
[[nodiscard]] std::optional<std::uint64_t> regionAtPixel(const document::Project& project, int x,
                                                         int y);

// Régions voisines de `id` (frontière commune), la plus longue frontière d'abord.
[[nodiscard]] std::vector<std::uint64_t> neighbourRegions(const document::Project& project,
                                                          std::uint64_t id);

// Carte des régions en RGBA (fond transparent) ; `highlight` est éclaircie pour montrer le choix.
struct RegionMapImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};
[[nodiscard]] RegionMapImage renderRegionMap(const document::Project& project,
                                             std::optional<std::uint64_t> highlight);

// Corrections (une commande annulable chacune ; refus clair sinon).
ServiceResult mergeRegionInto(DocumentSession& session, std::uint64_t keep, std::uint64_t absorb);
ServiceResult deleteRegion(DocumentSession& session, std::uint64_t id);
// Fusionne chaque région de moins de `minPixels` dans sa voisine de couleur la plus proche.
ServiceResult cleanSmallIslands(DocumentSession& session, std::size_t minPixels);

// Régénération des objets depuis la segmentation courante. `working` : image de travail que la
// segmentation a lue ; `settings` : réglages de numérisation (types de points, fond, lissage…).
struct Regeneration {
    std::shared_ptr<const document::Project> after; // projet avec les nouveaux objets
    std::size_t objectsBefore{0};
    std::size_t objectsAfter{0};
    std::uint64_t revision{0}; // révision du document sur laquelle c'est calculé
};

[[nodiscard]] Result<Regeneration>
regenerateObjects(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                  const image::Image& working, const project_ops::DigitizeSettings& settings,
                  const TaskContext& task);

// Installe la régénération en UN pas d'annulation. Refus si le document a changé depuis le calcul.
ServiceResult applyRegeneration(DocumentSession& session, const Regeneration& regeneration);

} // namespace openstitch::desktop_v2
