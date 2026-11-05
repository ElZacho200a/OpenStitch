// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- contrôleur d'édition directe au canevas : poignées de nœuds du contour, rails de
// satin, remodelage d'un segment de courbe et axe d'un fondu. Il possède l'état de ces gestes
// (nœuds choisis, segment saisi, poignée du fondu) et n'agit que par les services du cœur
// (`node_edit`, `fade_axis`) : chaque geste validé est UNE commande annulable. `ShellWindow` ne
// fait que le construire et lui transmettre le contexte.

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QObject>
#include <QPointF>
#include <QString>

#include "document_session.hpp"
#include "fade_axis.hpp"
#include "node_edit.hpp"
#include "preview_service.hpp"

namespace openstitch::desktop_v2 {

class PreviewCanvas;

class NodeEditController final : public QObject {
    Q_OBJECT

public:
    // Contexte fourni par la fenêtre : tout ce que le contrôleur lit ou appelle.
    struct Host {
        DocumentSession& session;
        PreviewCanvas& canvas;
        const PreviewImage& frame;                                  // repère mm / pixels courant
        std::function<QString()> activeTool;                        // identifiant de l'outil actif
        std::function<bool()> documentOpen;                         // un document est-il ouvert ?
        std::function<std::optional<std::uint64_t>()> singleTarget; // l'unique objet sélectionné
        std::function<void(const QString&, bool)> notice;           // message d'état (erreur ?)
    };

    explicit NodeEditController(Host host, QObject* parent = nullptr);

    // Branche les signaux du canevas (poignées, segment, insertion, annulation d'un glisser).
    void connectCanvas();
    // Recalcule poignées et repères depuis le document et l'outil actif.
    void refresh();
    // Retire la sélection de nœuds (clic sur le fond).
    void clearSelection();
    // Supprime les nœuds choisis (touche Suppr) ; faux si rien n'est choisi ou si c'est refusé.
    bool deleteSelected();

    [[nodiscard]] int handleCount() const;
    [[nodiscard]] int selectedCount() const { return static_cast<int>(nodeSel_.size()); }
    [[nodiscard]] QPointF handlePx(int index) const;

private:
    void onNodePressed(int index, Qt::KeyboardModifiers modifiers);
    void onNodeDragMoved(QPointF deltaPx);
    void onNodeDragFinished(QPointF deltaPx);
    void onNodeInsert(QPointF scenePx);

    Host host_;
    NodeView nodeView_;
    std::optional<SegmentHit> segmentDrag_; // segment de courbe en cours de remodelage
    std::optional<FadeAxisView> fadeView_;  // outil « fade.axis » : axe du fondu sélectionné
    std::uint64_t fadeObject_{0};
    int fadeHandle_{-1};
    std::vector<int> nodeSel_; // indices dans nodeView_.nodes (tous dans le même contour)
    std::uint64_t nodeObject_{0};
};

} // namespace openstitch::desktop_v2
