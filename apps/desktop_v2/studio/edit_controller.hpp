// SPDX-License-Identifier: Apache-2.0
#pragma once

// Adaptateur Qt Quick de la famille « sélection, transformations, calques, édition d'objets ».
// Aucune logique métier : les sélections lisent le projet vivant, toute mutation passe par
// `DocumentSession` (lots `project_ops` de core/ ou services à `ICommand`) en UNE étape
// d'annulation. Repère des paramètres : mm « scène » (celui du canevas, Y vers le bas) ; la
// conversion vers le repère modèle (Y vers le haut) est faite ici, une seule fois.

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "snap.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class EditController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tree READ tree NOTIFY treeChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY treeChanged)
    Q_PROPERTY(QStringList savedSelections READ savedSelections NOTIFY savedSelectionsChanged)
    Q_PROPERTY(QStringList stitchTypes READ stitchTypes NOTIFY treeChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapChanged)
    Q_PROPERTY(bool eyedropper READ eyedropper WRITE setEyedropper NOTIFY eyedropperChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY treeChanged)
    // Boîte de la sélection (mm scène) : x, y, width, height, cx, cy ; vide sans sélection.
    Q_PROPERTY(QVariantMap selectionBox READ selectionBox NOTIFY treeChanged)
public:
    explicit EditController(StudioController& studio, QObject* parent = nullptr);
    ~EditController() override;

    [[nodiscard]] QVariantList tree() const;
    [[nodiscard]] QVariantList layers() const;
    [[nodiscard]] QStringList savedSelections() const;
    [[nodiscard]] QStringList stitchTypes() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool snapEnabled() const { return snap_; }
    void setSnapEnabled(bool value);
    [[nodiscard]] bool eyedropper() const { return eyedropper_; }
    void setEyedropper(bool value);
    [[nodiscard]] int selectedCount() const;
    [[nodiscard]] QVariantMap selectionBox() const;
    // Union des boîtes de la sélection, en mm scène ; vide sans sélection ni scène.
    [[nodiscard]] std::optional<stitch_render::RectMm> selectionBounds() const;

    // --- Sélection ---------------------------------------------------------------------------
    Q_INVOKABLE int selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE int invertSelection();
    Q_INVOKABLE int selectSameColor();
    Q_INVOKABLE int selectNeighbors();
    Q_INVOKABLE int selectByType(const QString& typeId);
    Q_INVOKABLE int selectByLayer(const QString& calqueId);
    Q_INVOKABLE int selectGroup(const QString& groupId);
    // Rectangle en mm scène. `crossing` : tout objet touché ; sinon seulement ceux entièrement
    // contenus. `additive` : s'ajoute à la sélection courante.
    Q_INVOKABLE int selectRect(double x0, double y0, double x1, double y1, bool crossing,
                               bool additive);
    Q_INVOKABLE bool saveSelection(const QString& name);
    Q_INVOKABLE bool recallSelection(const QString& name);
    Q_INVOKABLE bool deleteSavedSelection(const QString& name);

    // --- Transformations (une étape d'annulation chacune) -------------------------------------
    Q_INVOKABLE bool rotate(double degrees); // anti-horaire, autour du centre de la sélection
    Q_INVOKABLE bool rotate90(bool clockwise);
    Q_INVOKABLE bool mirror(bool horizontal);
    Q_INVOKABLE bool scale(double factor, double anchorXmm, double anchorYmm); // ancre en mm scène
    Q_INVOKABLE bool duplicate(double offsetMm = 5.0);
    Q_INVOKABLE bool deleteSelection();
    Q_INVOKABLE bool align(const QString& mode); // left centerH right top centerV bottom
    Q_INVOKABLE bool distribute(bool horizontal);
    // Placement numérique de la boîte (centre en mm modèle ; NaN = inchangé). Un seul objet :
    // `place` ; plusieurs : translation / échelle uniforme / rotation de l'ensemble.
    Q_INVOKABLE bool placeSelection(double xMm, double yMm, double widthMm, double heightMm,
                                    double rotateDeg);
    Q_INVOKABLE bool stack(const QString& move); // front back forward backward
    Q_INVOKABLE bool group(const QString& name = {});
    Q_INVOKABLE bool ungroup();
    Q_INVOKABLE bool renameObject(const QString& id, const QString& name);
    Q_INVOKABLE bool setObjectVisible(const QString& id, bool visible);
    Q_INVOKABLE bool setObjectLocked(const QString& id, bool locked);
    // Pipette : applique la couleur du fil de l'objet `sourceId` à la sélection.
    Q_INVOKABLE bool applyColorFrom(const QString& sourceId);

    // --- Déplacement à la souris (aperçu sans mutation, validation en UNE commande) ------------
    // `previewMove` rend le décalage corrigé (mm scène) et les repères (mm scène).
    Q_INVOKABLE bool beginMove();
    Q_INVOKABLE QVariantMap previewMove(double dxMm, double dyMm, double thresholdMm, bool snap);
    Q_INVOKABLE bool commitMove(double dxMm, double dyMm);
    Q_INVOKABLE void cancelMove();

    // --- Calques -----------------------------------------------------------------------------
    Q_INVOKABLE bool addLayer(const QString& name);
    Q_INVOKABLE bool renameLayer(const QString& id, const QString& name);
    Q_INVOKABLE bool removeLayer(const QString& id);
    Q_INVOKABLE bool setLayerVisible(const QString& id, bool visible);
    Q_INVOKABLE bool setLayerLocked(const QString& id, bool locked);
    Q_INVOKABLE bool moveLayer(const QString& id, int direction);
    Q_INVOKABLE bool moveSelectionToLayer(const QString& id);
signals:
    void treeChanged();
    void messageChanged();
    void savedSelectionsChanged();
    void snapChanged();
    void eyedropperChanged();

private:
    [[nodiscard]] std::vector<std::uint64_t> selectedIds() const;
    [[nodiscard]] bool selectIds(const std::vector<std::uint64_t>& ids, bool additive = false);
    [[nodiscard]] bool visibleObject(std::uint64_t id) const;
    bool applyBatch(const std::string& batch, const QString& label, const QString& done);
    bool refuse(const QString& why);
    void say(const QString& text);
    [[nodiscard]] static std::string mergeBatches(const std::vector<std::string>& batches);
    [[nodiscard]] PointMm selectionCentreModel() const;

    StudioController& studio_;
    DocumentSession::Token token_{};
    QString message_;
    bool snap_{true};
    bool eyedropper_{false};
    std::vector<std::pair<QString, std::vector<std::uint64_t>>> saved_;
    struct MoveState {
        SnapBox moving;
        std::vector<SnapBox> targets;
        std::vector<std::uint64_t> ids;
    };
    std::optional<MoveState> move_;
};

} // namespace openstitch::desktop_v2::studio
