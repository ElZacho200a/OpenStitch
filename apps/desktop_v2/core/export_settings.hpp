// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- options machine de l'export : coupes, points d'arrêt, points courts / longs,
// départ et arrivée du motif, cadre (bibliothèque de cadres génériques) et profil de machine.
// Le moteur sait déjà tout faire (`SequenceFinishing`, `Canvas`, `machine_id`) ; ce service lit ces
// réglages en millimètres, les valide et les applique en UNE commande annulable. Sans Qt.

#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Point d'arrêt (verrou) aux entrées / sorties et autour des coupes.
enum class LockKind { None, BackAndForth, Triangle, MicroZigzag };

struct ExportOptions {
    bool finishingEnabled{true};
    double trimThresholdMm{3.0};
    bool trimBeforeColorChange{true};
    LockKind lockKind{LockKind::BackAndForth};
    double lockLengthMm{0.8};
    int lockPasses{2};
    bool filterShortStitches{true};
    double minStitchMm{0.5};
    bool splitLongStitches{false};
    double maxStitchMm{7.0};
    bool autoJoin{false};
    std::optional<PointMm> designStart;
    std::optional<PointMm> designEnd;
};

[[nodiscard]] ExportOptions exportOptionsOf(const document::Project& project);
// Refus clair (message) si une valeur sort des bornes ; sinon chaîne vide.
[[nodiscard]] std::string validateExportOptions(const ExportOptions& options);
// UNE commande « Modifier les options de génération ». An export may accept unchanged valid
// settings without creating an empty history step; interactive edits keep the historical refusal.
ServiceResult setExportOptions(DocumentSession& session, const ExportOptions& options,
                               bool allowUnchanged = false);

[[nodiscard]] std::string lockKindLabel(LockKind kind);

// --- Cadres et machines -------------------------------------------------------------------------
struct HoopChoice {
    std::string id;
    std::string family;
    std::string name;
    double widthMm{0.0};
    double heightMm{0.0};
};
struct MachineChoice {
    std::string id;
    std::string name;
    std::string preferredFormat;
    double fieldWidthMm{0.0};
    double fieldHeightMm{0.0};
    bool autoTrim{false};
    int needles{1};
    std::vector<std::string> hoopIds; // cadres recommandés pour cette machine
};

[[nodiscard]] std::vector<HoopChoice> hoopChoices();
[[nodiscard]] std::vector<MachineChoice> machineChoices();

// Cadre actuel du projet : identifiant de la bibliothèque s'il en est un (forme et dimensions
// identiques), sinon vide (cadre personnalisé).
[[nodiscard]] std::string currentHoopId(const document::Project& project);
[[nodiscard]] double currentMarginMm(const document::Project& project);

// Applique le cadre de la bibliothèque (forme, dimensions, coins) et la marge de sécurité, en UNE
// commande. Refus : cadre inconnu, marge négative ou trop grande, rien à changer.
ServiceResult applyHoop(DocumentSession& session, const std::string& hoopId, double marginMm);
// Cadre personnalisé rectangulaire.
ServiceResult applyCustomHoop(DocumentSession& session, double widthMm, double heightMm,
                              double marginMm);
// Choisit le profil de machine ("" = aucune) ; refus si inconnu ou inchangé.
ServiceResult applyMachine(DocumentSession& session, const std::string& machineId);

// --- Contrôle avant export ----------------------------------------------------------------------
struct ExportCheck {
    bool ok{false}; // le calcul a eu lieu (faux : message explique pourquoi)
    std::string message;
    std::size_t stitches{0};
    std::size_t outsideHoop{0}; // points cousus hors de la zone brodable
    bool fitsHoop{true};
    bool fitsMachineField{true}; // sans machine : vrai
    bool formatAccepted{true}; // le format choisi est accepté par la machine ; sans machine : vrai
    [[nodiscard]] std::string summary() const;
};

// `format` : identifiant de format (« dst », « pes »…) ou vide. Lecture seule, séquence EFFECTIVE.
[[nodiscard]] ExportCheck checkExport(const document::Project& project,
                                      const std::string& format = {});
// Même contrôle sur la séquence réellement destinée au fichier (optimisation/compensation).
[[nodiscard]] ExportCheck checkExportSequence(const document::Project& project,
                                              const stitch::StitchSequence& sequence,
                                              const std::string& format = {});

} // namespace openstitch::desktop_v2
