// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- surimpressions d'analyse sur l'aperçu : carte de densité de piqûres et risque de
// plissement du tissu. Chaque carte se calcule en tâche annulable sur un instantané, ne s'affiche
// que si elle correspond à la révision courante, et se recalcule seule après une modification.
// Aucune écriture dans le document : ce sont des lectures. `ShellWindow` ne fait que fournir le
// contexte et prévenir de ses changements.

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QString>

#include "density_service.hpp"
#include "document_session.hpp"
#include "fabric_risk.hpp"
#include "preview_service.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2 {

class PreviewCanvas;

class AnalysisOverlays {
public:
    struct Host {
        DocumentSession& session;
        PreviewCanvas& canvas;
        const PreviewImage& frame;
        TaskManager& tasks;
        std::function<void(const QString&, bool)> notice;
    };

    explicit AnalysisOverlays(Host host) : host_(std::move(host)) {}

    // Carte de densité.
    void toggleDensity();
    [[nodiscard]] bool densityActive() const { return densityActive_; }
    [[nodiscard]] bool densityShown() const;
    [[nodiscard]] const std::vector<DensityHotspotInfo>& densityHotspots() const {
        return densityHotspots_;
    }

    // Risque de plissement du tissu.
    void toggleFabricRisk();
    [[nodiscard]] bool fabricActive() const { return fabricActive_; }
    [[nodiscard]] bool fabricShown() const;
    [[nodiscard]] const std::optional<FabricRisk>& fabricRisk() const { return fabricRisk_; }

    // Le document a changé : recalcule les cartes affichées devenues périmées.
    void onDocumentChanged();
    // Un nouvel aperçu est prêt : (re)pose la carte de densité dans son repère.
    void onPreviewReady();

private:
    void requestDensity();
    void applyDensity(const DensityOverlay& overlay);
    void hideDensity();
    void requestFabricRisk();
    void applyFabricRisk(const FabricRisk& risk);
    void hideFabricRisk();

    Host host_;
    bool densityActive_{false};
    std::uint64_t densityRevision_{0}; // révision de la carte affichée ; 0 = aucune
    std::uint64_t densityTask_{0};
    std::vector<DensityHotspotInfo> densityHotspots_;
    bool fabricActive_{false};
    std::uint64_t fabricTask_{0};
    std::optional<FabricRisk> fabricRisk_;
};

} // namespace openstitch::desktop_v2
