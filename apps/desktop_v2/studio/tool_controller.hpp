// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- famille F : outils de dessin et d'édition vectorielle. Adaptateur Qt Quick SANS logique
// métier : le registre d'outils (ToolRegistry) donne identifiants, libellés et raccourcis ; les
// formes, la plume, les nœuds et le couteau passent par les services de core/ (shape_batch,
// pen_shape, node_edit, edit_batch) via DocumentSession, donc UNE étape d'annulation par geste.
// Les événements du canevas arrivent en mm « scène » (Y vers le bas) ; la conversion vers le repère
// modèle (Y vers le haut) est faite ici, une seule fois. Aucun geste ne mute avant le relâchement :
// pendant le geste, seul l'aperçu (surcouche) change.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <optional>
#include <vector>

#include "node_edit.hpp"
#include "pen_shape.hpp"
#include "studio_camera.hpp"
#include "studio_controller.hpp"
#include "tool_registry.hpp"

namespace openstitch::desktop_v2::studio {

// Un élément de surcouche d'aperçu (mm scène). Peu d'éléments, jamais un par point de couture.
struct ToolOverlayItem {
    enum class Kind { Line, Closed, Point, Selected, Tangent, Ghost };
    Kind kind{Kind::Line};
    std::vector<ScenePoint> points;
};

class ToolController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(QVariantList tools READ tools CONSTANT)
    Q_PROPERTY(QString hint READ hint NOTIFY hintChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool capturesMouse READ capturesMouse NOTIFY activeToolChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY overlayChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY optionsChanged)
    Q_PROPERTY(bool star READ star WRITE setStar NOTIFY optionsChanged)
    Q_PROPERTY(double starRatio READ starRatio WRITE setStarRatio NOTIFY optionsChanged)
    Q_PROPERTY(bool snapGrid READ snapGrid WRITE setSnapGrid NOTIFY optionsChanged)
    Q_PROPERTY(double gridMm READ gridMm WRITE setGridMm NOTIFY optionsChanged)
    Q_PROPERTY(bool snapVertices READ snapVertices WRITE setSnapVertices NOTIFY optionsChanged)
    Q_PROPERTY(int snapRadiusPx READ snapRadiusPx WRITE setSnapRadiusPx NOTIFY optionsChanged)
    Q_PROPERTY(int selectedNodeCount READ selectedNodeCount NOTIFY overlayChanged)

public:
    explicit ToolController(StudioController& studio, QObject* parent = nullptr);

    [[nodiscard]] QString activeTool() const { return tool_; }
    void setActiveTool(const QString& id);
    [[nodiscard]] QVariantList tools() const;
    [[nodiscard]] QString hint() const { return hint_; }
    [[nodiscard]] QString message() const { return message_; }
    // Vrai quand le canevas doit confier la souris à l'outil (tout sauf Sélection et Panoramique).
    [[nodiscard]] bool capturesMouse() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] int sides() const { return sides_; }
    void setSides(int value);
    [[nodiscard]] bool star() const { return star_; }
    void setStar(bool value);
    [[nodiscard]] double starRatio() const { return starRatio_; }
    void setStarRatio(double value);
    [[nodiscard]] bool snapGrid() const { return snapGrid_; }
    void setSnapGrid(bool value);
    [[nodiscard]] double gridMm() const { return gridMm_; }
    void setGridMm(double value);
    [[nodiscard]] bool snapVertices() const { return snapVertices_; }
    void setSnapVertices(bool value);
    [[nodiscard]] int snapRadiusPx() const { return snapRadiusPx_; }
    void setSnapRadiusPx(int value);
    [[nodiscard]] int selectedNodeCount() const { return static_cast<int>(nodeSel_.size()); }

    // --- Événements du canevas (mm scène, échelle en px/mm)
    // ---------------------------------------
    bool press(double x, double y, double pxPerMm, Qt::KeyboardModifiers mods);
    void move(double x, double y, Qt::KeyboardModifiers mods);
    void release(double x, double y, Qt::KeyboardModifiers mods);
    void doubleClick(double x, double y, double pxPerMm);
    // Touche traitée par l'outil (Entrée, Échap, Suppr, Retour arrière, C/S sur nœuds).
    bool keyPress(int key);
    // Annule le geste en cours sans rien muter ; faux s'il n'y en avait pas.
    Q_INVOKABLE bool cancel();
    // Valide la construction en cours (polygone, plume).
    Q_INVOKABLE bool finish();

    // --- Nœuds (outil « node.edit »)
    // --------------------------------------------------------------
    Q_INVOKABLE bool deleteSelectedNodes();
    Q_INVOKABLE bool setSelectedNodeKind(bool smooth);
    Q_INVOKABLE bool removeHole();

    [[nodiscard]] const std::vector<ToolOverlayItem>& overlay() const { return overlay_; }
    // Diagonale (mm) de la boîte des nœuds de la forme éditée ; 0 sans nœuds visibles.
    [[nodiscard]] double nodeSpanMm() const;
    [[nodiscard]] QString info() const { return info_; }
    [[nodiscard]] QString cursorName() const;

signals:
    void activeToolChanged();
    void hintChanged();
    void messageChanged();
    void optionsChanged();
    void overlayChanged();
    // Le dernier objet créé est sélectionné.
    void objectCreated(const QString& id);

private:
    enum class Drag { None, Box, Freehand, PenNode, Line, Nodes, Tangent, Segment };
    [[nodiscard]] PointMm toModel(double x, double y) const { return screenToModel(x, y); }
    [[nodiscard]] PointMm snapped(PointMm p, bool allowGrid = true) const;
    void refuse(const QString& text);
    void inform(const QString& text);
    void setHint();
    void reset();
    void rebuild();
    void refreshNodeView();
    void createdFrom(std::uint64_t id, const QString& label);
    bool commitBox(PointMm a, PointMm b, Qt::KeyboardModifiers mods);
    [[nodiscard]] std::vector<PointMm> boxPolygon(PointMm a, PointMm b,
                                                  Qt::KeyboardModifiers mods) const;
    [[nodiscard]] std::vector<std::uint64_t> selectedIds() const;
    [[nodiscard]] static ScenePoint toScene(PointMm p) { return {p.x, -p.y}; }

    StudioController& studio_;
    ToolRegistry registry_;
    QString tool_{QStringLiteral("select")};
    QString hint_;
    QString message_;
    QString info_;
    int sides_{6};
    bool star_{false};
    double starRatio_{0.45};
    bool snapGrid_{false};
    double gridMm_{5.0};
    bool snapVertices_{true};
    int snapRadiusPx_{8};
    double scale_{1.0};
    Qt::KeyboardModifiers mods_{};

    Drag drag_{Drag::None};
    PointMm anchor_;  // point du premier appui (modèle)
    PointMm current_; // pointeur courant (modèle)
    std::vector<PointMm> stroke_;
    std::vector<PenNode> penNodes_; // plume : nœuds validés
    bool penDragging_{false};
    std::vector<PointMm> polyPoints_;
    bool hover_{false};

    std::uint64_t nodeObject_{0};
    std::optional<NodeView> view_;
    std::vector<NodeRef> nodeSel_;
    NodeRef activeNode_;
    bool tangentOut_{false};
    std::optional<SegmentHit> segment_;
    std::vector<ToolOverlayItem> overlay_;
};

} // namespace openstitch::desktop_v2::studio
