// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- famille I : gestes de numérisation au canevas (points individuels, axe de fondu, rails
// et barreaux de satin, guides / lignes de rupture). Adaptateur SANS logique métier : les
// contrôleurs de la famille G (PointEditController, FadeController, SatinController) portent toutes
// les mutations (services du coeur, UNE `ICommand` annulable chacune). Pendant un geste, seule la
// surcouche change (aperçu) ; le document n'est muté qu'au relâchement. Les événements du canevas
// arrivent en mm « scène » (Y vers le bas) ; la conversion vers le repère modèle est faite ici, une
// seule fois. La surcouche est de la géométrie groupée (quelques polylignes + un nuage de points),
// jamais un objet QML par point.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "fade_controller.hpp"
#include "point_edit_controller.hpp"
#include "satin_controller.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Élément de surcouche (mm scène).
struct HandleItem {
    enum class Kind { Line, Closed, Dots, Selected, Handle, Active, Ghost };
    Kind kind{Kind::Line};
    std::vector<ScenePoint> points;
};

class DigitizeGestures : public QObject {
    Q_OBJECT
    // "", "points", "fade", "satin", "guide", "break", "rungs".
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool capturesMouse READ capturesMouse NOTIFY modeChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY overlayChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY modeChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int pendingRungs READ pendingRungs NOTIFY overlayChanged)
public:
    DigitizeGestures(StudioController& studio, PointEditController& points, FadeController& fade,
                     SatinController& satin, QObject* parent = nullptr);

    [[nodiscard]] QString mode() const { return mode_; }
    void setMode(const QString& mode);
    [[nodiscard]] bool capturesMouse() const { return !mode_.isEmpty(); }
    [[nodiscard]] bool busy() const { return drag_ != Drag::None; }
    [[nodiscard]] QString hint() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] int pendingRungs() const { return static_cast<int>(rungs_.size()); }

    // --- Événements du canevas (mm scène, échelle en px/mm) --------------------------------------
    bool press(double x, double y, double pxPerMm, Qt::KeyboardModifiers mods);
    void move(double x, double y, Qt::KeyboardModifiers mods);
    void release(double x, double y, Qt::KeyboardModifiers mods);
    // Entrée valide les barreaux en attente, Suppr efface les points choisis, Échap annule.
    bool keyPress(int key);
    // Annule le geste en cours (ou les barreaux en attente) sans rien muter ; faux si rien à
    // annuler.
    Q_INVOKABLE bool cancel();
    // Crée la colonne satin des barreaux posés (mode « rungs »).
    Q_INVOKABLE bool finishRungs();

    // Surcouche : statique (points, poignées, reconstruite quand le document ou la sélection
    // change) puis dynamique (aperçu du geste, reconstruite à chaque mouvement).
    [[nodiscard]] const std::vector<HandleItem>& staticItems() const { return static_; }
    [[nodiscard]] const std::vector<HandleItem>& dynamicItems() const { return dynamic_; }
    [[nodiscard]] QString info() const { return info_; }

signals:
    void modeChanged();
    void messageChanged();
    void overlayChanged();

private:
    enum class Drag { None, Points, Rect, Fade, RailNode, RungEnd, GuideNode, Stroke, NewRung };
    struct Grip {
        enum class Type { RailNode, RungEnd, GuideNode };
        Type type{Type::RailNode};
        int side{0};  // rail: 0=A, 1=B ; guide: indice de la courbe
        int index{0}; // nœud ou barreau
        PointMm pos;
    };
    [[nodiscard]] static PointMm toModel(double x, double y) { return screenToModel(x, y); }
    [[nodiscard]] static ScenePoint toScene(PointMm p) { return {p.x, -p.y}; }
    [[nodiscard]] std::uint64_t satinId() const { return satin_.objectId().toULongLong(); }
    [[nodiscard]] std::vector<Grip> grips() const;
    [[nodiscard]] std::optional<Grip> gripAt(PointMm p) const;
    [[nodiscard]] int fadeHandleAt(PointMm p) const;
    [[nodiscard]] static PointMm fadeHandlePos(const QVariantMap& axis, int handle);
    bool insertRailNodeAt(PointMm p);
    void rebuildStatic();
    void rebuildDynamic();
    void note(const QString& text, bool ok = true);
    void finishDrag();

    StudioController& studio_;
    PointEditController& points_;
    FadeController& fade_;
    SatinController& satin_;
    QString mode_;
    bool pointsTurnedOn_{false};

    double scale_{1.0};
    Qt::KeyboardModifiers mods_{};
    Drag drag_{Drag::None};
    PointMm anchor_, current_;
    int rangeAnchor_{-1};
    int hitIndex_{-1};
    bool travelled_{false};
    bool additive_{false};
    int fadeHandle_{-1};
    std::optional<Grip> grip_;
    std::vector<PointMm> stroke_;
    std::vector<std::pair<PointMm, PointMm>> rungs_;
    QVariantMap fadePreview_;

    std::vector<HandleItem> static_;
    std::vector<HandleItem> dynamic_;
    QString message_;
    QString info_;
};

} // namespace openstitch::desktop_v2::studio
