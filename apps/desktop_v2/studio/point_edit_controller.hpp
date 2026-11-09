// SPDX-License-Identifier: Apache-2.0
#pragma once

// Adaptateur Qt Quick de la retouche point à point (famille G). Aucune logique métier : lecture de
// `stitchPoints` (séquence effective retouchée), mutations par les services `point_edit` du coeur
// (une `ICommand` annulable chacune). Coordonnées en mm « modèle » (Y vers le haut).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <set>
#include <vector>

#include "point_edit.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class PointEditController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(QString objectId READ objectId NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed) // none clean edited dirty
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(int editedCount READ editedCount NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(QVariantList selectedPoints READ selectedPoints NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    explicit PointEditController(StudioController& studio, QObject* parent = nullptr);
    ~PointEditController() override;

    [[nodiscard]] bool active() const { return active_; }
    void setActive(bool value);
    [[nodiscard]] QString objectId() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] int pointCount() const { return static_cast<int>(view_.points.size()); }
    [[nodiscard]] int editedCount() const { return static_cast<int>(view_.editedCount); }
    [[nodiscard]] int selectedCount() const { return static_cast<int>(selected_.size()); }
    [[nodiscard]] QVariantList selectedPoints() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // Lecture : {index, x, y, jump, deleted, edited, trim, movable, selected} ; vide si inconnu.
    Q_INVOKABLE [[nodiscard]] QVariantMap pointInfo(int index) const;
    // Indice du point le plus proche de (x, y) mm modèle dans `tolMm`, sinon -1.
    Q_INVOKABLE [[nodiscard]] int pointAt(double xMm, double yMm, double tolMm) const;
    // Points d'un rectangle (mm modèle) : de quoi dessiner sans un objet QML par point.
    Q_INVOKABLE [[nodiscard]] QVariantList pointsIn(double x0, double y0, double x1, double y1,
                                                    int limit) const;

    // Lecture seule pour les gestes du canevas (aucune copie, aucune mutation).
    [[nodiscard]] const PointView& view() const { return view_; }
    [[nodiscard]] const std::set<std::size_t>& selection() const { return selected_; }
    // Sélection par rectangle (mm modèle) ; `additive` conserve la sélection actuelle.
    Q_INVOKABLE int selectRect(double x0, double y0, double x1, double y1, bool additive);
    Q_INVOKABLE bool selectPoint(int index, bool additive);
    Q_INVOKABLE bool selectRange(int fromIndex, int toIndex);
    Q_INVOKABLE int selectAllPoints();
    Q_INVOKABLE void clearPointSelection();

    Q_INVOKABLE bool moveSelected(double dxMm, double dyMm);
    Q_INVOKABLE bool movePoint(int index, double xMm, double yMm);
    Q_INVOKABLE bool insertAfter(int index, double xMm, double yMm);
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool restoreSelected();
    Q_INVOKABLE bool setJump(bool jump);
    Q_INVOKABLE bool setTrim(bool trim);
    Q_INVOKABLE bool discardEdits();
signals:
    void changed();
    void messageChanged();

private:
    void refresh();
    bool report(const ServiceResult& result, const QString& success);
    bool refuse(const QString& why);
    [[nodiscard]] std::vector<std::size_t> selectedIndices() const;

    StudioController& studio_;
    // Cache d'invalidation : le recalcul de points peut générer des milliers de points.
    std::uint64_t lastSeenRevision_{~std::uint64_t{0}};
    std::vector<Target> lastSeenSelection_;
    bool lastSeenActive_{false};
    bool active_{false};
    std::uint64_t id_{0};
    PointView view_;
    std::set<std::size_t> selected_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
