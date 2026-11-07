// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- colonnes de satin manuelles : deux rails reliés par des barreaux (une paire de
// points par barreau), puis remodelage des rails (déplacer / insérer un nœud). Chaque action est
// UNE `ICommand` annulable. Sans Qt.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

struct SatinCreateResult {
    bool ok{false};
    std::string message;
    std::uint64_t objectId{0};
    bool railsCross{false}; // créée quand même, mais la couture sera probablement incohérente
};

enum class RailSide { A, B };

// `rungs` : (point du rail A, point du rail B) en mm modèle. Au moins 2 barreaux, A != B pour
// chacun.
SatinCreateResult createSatinColumn(DocumentSession& session,
                                    const std::vector<std::pair<PointMm, PointMm>>& rungs);

// Nombre de nœuds du rail, ou -1 si l'objet n'est pas une colonne satin à rails.
[[nodiscard]] int railNodeCount(const document::Project& project, std::uint64_t objectId,
                                RailSide side);

// Nœuds du rail en mm modèle (lecture seule, pour poser des poignées) ; vide si pas un satin.
[[nodiscard]] std::vector<PointMm> satinRailNodes(const document::Project& project,
                                                  std::uint64_t objectId, RailSide side);

ServiceResult moveSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                std::size_t node, PointMm to);
// Insère un nœud au paramètre t (0..1, exclu) du segment `segment`.
ServiceResult insertSatinRailNode(DocumentSession& session, std::uint64_t objectId, RailSide side,
                                  std::size_t segment, double t);

// --- Barreaux : édition et diagnostic ---------------------------------------------------------

// Nombre de barreaux, ou -1 si l'objet n'est pas une colonne satin à rails.
[[nodiscard]] int satinRungCount(const document::Project& project, std::uint64_t objectId);

// Barreaux (rail A, rail B) en mm modèle ; vide si ce n'est pas une colonne satin.
[[nodiscard]] std::vector<std::pair<PointMm, PointMm>> satinRungs(const document::Project& project,
                                                                  std::uint64_t objectId);

// Ajoute un barreau À LA POSITION `index` (0..n) : (a, b) en mm modèle. Refus si les deux
// extrémités sont confondues, hors limites, ou si le total dépasserait 500 barreaux. Une commande
// annulable.
ServiceResult addSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index,
                           PointMm a, PointMm b);
// Insère un barreau au milieu des barreaux `index` et `index + 1` (moyenne de leurs extrémités).
ServiceResult insertSatinRungBetween(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t index);
// Déplace les deux extrémités du barreau `index`.
ServiceResult moveSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index,
                            PointMm a, PointMm b);
// Retire le barreau `index` ; refus s'il n'en resterait que un.
ServiceResult removeSatinRung(DocumentSession& session, std::uint64_t objectId, std::size_t index);

// Indice i du plus grand écart entre les barreaux i et i+1 (là où un barreau manque le plus).
[[nodiscard]] std::optional<std::size_t> widestRungGap(const document::Project& project,
                                                       std::uint64_t objectId);
// Indice du barreau intérieur dont le retrait écarte le moins ses voisins (le plus redondant).
[[nodiscard]] std::optional<std::size_t> redundantRung(const document::Project& project,
                                                       std::uint64_t objectId);

struct SatinDiagnosis {
    bool isSatin{false};
    int rungCount{0};
    bool railsCross{false};            // un rail traverse l'autre
    std::vector<std::size_t> crossing; // barreaux i tels que i et i+1 se croisent (nœud papillon)
    std::vector<std::size_t> tooWide;  // barreaux plus longs que la largeur maximale du satin
    double widestMm{0.0};
    [[nodiscard]] bool clean() const {
        return isSatin && !railsCross && crossing.empty() && tooWide.empty();
    }
};

// Analyse purement géométrique (aucune mutation).
[[nodiscard]] SatinDiagnosis diagnoseSatin(const document::Project& project,
                                           std::uint64_t objectId);
// Phrase lisible du diagnostic (« Rails sains… » ou la liste des défauts).
[[nodiscard]] std::string satinDiagnosisSummary(const SatinDiagnosis& diagnosis);

} // namespace openstitch::desktop_v2
