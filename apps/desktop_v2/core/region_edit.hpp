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
#include <utility>
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

// Même carte, avec PLUSIEURS régions éclaircies (sélection multiple).
[[nodiscard]] RegionMapImage renderRegionMap(const document::Project& project,
                                             const std::vector<std::uint64_t>& highlights);

// --- Sélection multiple (le modèle de sélection partagé porte ces identifiants) ----------------
// Toutes les régions vivantes (plus grandes d'abord).
[[nodiscard]] std::vector<std::uint64_t> allRegionIds(const document::Project& project);
// Régions de même couleur que `id` (`id` comprise, ordre de `listRegions`).
[[nodiscard]] std::vector<std::uint64_t> sameColorRegions(const document::Project& project,
                                                          std::uint64_t id);
// Voisines de l'ensemble, hors ensemble (frontière la plus longue d'abord).
[[nodiscard]] std::vector<std::uint64_t> neighboursOfSet(const document::Project& project,
                                                         const std::vector<std::uint64_t>& ids);
// Régions vivantes qui ne sont PAS dans l'ensemble.
[[nodiscard]] std::vector<std::uint64_t> invertRegionSet(const document::Project& project,
                                                         const std::vector<std::uint64_t>& ids);

// --- Lien région <-> objets ----------------------------------------------------------------------
// Objets de broderie issus de ces régions (vecteur `source_region` -> broderie `source_vector`).
[[nodiscard]] std::vector<std::uint64_t>
objectsOfRegions(const document::Project& project, const std::vector<std::uint64_t>& regionIds);
// Régions d'où viennent ces objets de broderie (sans doublon, ordre d'apparition).
[[nodiscard]] std::vector<std::uint64_t>
regionsOfObjects(const document::Project& project, const std::vector<std::uint64_t>& objectIds);

// --- Corrections sur un ensemble : UNE étape d'annulation pour tout l'ensemble -------------------
// Fusionne tout l'ensemble dans `into` (défaut : la plus grande région de l'ensemble).
ServiceResult mergeRegionSet(DocumentSession& session, const std::vector<std::uint64_t>& ids,
                             std::optional<std::uint64_t> into = std::nullopt);
// Supprime (absorbe dans les voisines) ; refuse s'il ne resterait aucune région.
ServiceResult deleteRegionSet(DocumentSession& session, const std::vector<std::uint64_t>& ids);
ServiceResult recolorRegionSet(DocumentSession& session, const std::vector<std::uint64_t>& ids,
                               std::array<std::uint8_t, 3> rgb);

// Corrections (une commande annulable chacune ; refus clair sinon).
ServiceResult mergeRegionInto(DocumentSession& session, std::uint64_t keep, std::uint64_t absorb);
ServiceResult deleteRegion(DocumentSession& session, std::uint64_t id);
// Recolore une seule région sans changer ses pixels ; refuse une couleur inchangée.
ServiceResult recolorRegion(DocumentSession& session, std::uint64_t id,
                            std::array<std::uint8_t, 3> rgb);
// Fusionne toutes les régions de la couleur source dans la couleur de la région cible.
// Les régions adjacentes de couleur identique sont réunies en une commande annulable.
ServiceResult mergeRegionColors(DocumentSession& session, std::uint64_t source,
                                std::uint64_t target);
// Retouche d'une frontière au pinceau : peint la région `regionId` sur tous les pixels à moins de
// `radiusPx` d'un des centres (pixels de l'image de travail), quel que soit ce qui s'y trouvait
// (région voisine ou fond). `erase` : rend au fond les seuls pixels de cette région. Refus : région
// inconnue, rayon hors [0,5 ; 200], centre non fini, rien ne change, ou une région voisine serait
// entièrement effacée (utiliser « Supprimer »). UNE commande annulable ; ni les objets ni les
// couleurs ne sont touchés (la régénération les refera).
ServiceResult paintRegionStroke(DocumentSession& session, std::uint64_t regionId,
                                const std::vector<std::pair<double, double>>& centresPx,
                                double radiusPx, bool erase);
// Fusionne chaque région de moins de `minPixels` dans sa voisine de couleur la plus proche.
ServiceResult cleanSmallIslands(DocumentSession& session, std::size_t minPixels);
// Rebouche uniquement les composants transparents enclavés de taille <= maxPixels.
ServiceResult fillSmallHoles(DocumentSession& session, std::size_t maxPixels);

// Régénération des objets depuis la segmentation courante. `working` : image de travail que la
// segmentation a lue ; `settings` : réglages de numérisation (types de points, fond, lissage…).
struct Regeneration {
    std::shared_ptr<const document::Project> after; // projet avec les nouveaux objets
    std::size_t objectsBefore{0};
    std::size_t objectsAfter{0};
    std::uint64_t revision{0}; // révision du document sur laquelle c'est calculé
    std::size_t preserved{0};  // objets dessinés à la main, CONSERVÉS tels quels
    std::size_t lostRetouches{
        0}; // objets issus de régions dont des retouches de points sont remplacées
};

// Ce qu'une régénération touche : `retouchedObjects` = objets issus de la segmentation qui portent
// des retouches de points (remplacées) ; `handDrawnObjects` = objets sans région d'origine (formes,
// SVG, texte converti…), toujours CONSERVÉS. Sert à prévenir avant d'écraser, jamais à bloquer.
struct HandEdits {
    std::size_t retouchedObjects{0};
    std::size_t handDrawnObjects{0};
    [[nodiscard]] bool any() const { return retouchedObjects > 0; }
};
[[nodiscard]] HandEdits countHandEdits(const document::Project& project);

[[nodiscard]] Result<Regeneration>
regenerateObjects(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                  const image::Image& working, const project_ops::DigitizeSettings& settings,
                  const TaskContext& task);

// RE-SEGMENTE l'image de travail avec de nouveaux réglages (couleurs, plus petite région, lissage),
// puis régénère les objets depuis cette nouvelle segmentation. Calculé sur une copie ; les
// corrections de régions et retouches d'objets faites jusque-là sont remplacées, mais l'ensemble
// s'annule en UN pas (`applyRegeneration`).
[[nodiscard]] Result<Regeneration>
resegmentObjects(std::shared_ptr<const document::Project> project, std::uint64_t revision,
                 const image::Image& working, const project_ops::DigitizeSettings& settings,
                 const TaskContext& task);

// --- Réduction de la palette (nombre de couleurs, distance) avec aperçu chiffré -----------------
struct ColorReductionPreview {
    std::size_t colorsBefore{0};
    std::size_t colorsAfter{0};
    std::size_t recolors{0}; // régions qui changent de couleur
    std::size_t merges{0};   // régions voisines fusionnées ensuite
};
// `maxColors` 0 = pas de limite ; `maxDistance` (CIEDE2000) 0 = pas de seuil.
[[nodiscard]] ColorReductionPreview previewColorReduction(const document::Project& project,
                                                          std::size_t maxColors, double maxDistance,
                                                          bool mergeAdjacent);
// Applique la réduction : UNE commande annulable ; refus clair si rien ne change.
ServiceResult reduceColors(DocumentSession& session, std::size_t maxColors, double maxDistance,
                           bool mergeAdjacent);

// Installe la régénération en UN pas d'annulation. Refus si le document a changé depuis le calcul.
ServiceResult applyRegeneration(DocumentSession& session, const Regeneration& regeneration);

} // namespace openstitch::desktop_v2
