// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- inspecteur contextuel : affiche les valeurs communes / mixtes de la sélection
// (`inspect`) et applique chaque modification comme UN lot d'opérations `project_ops` via
// `DocumentSession::applyBatch` (même chemin que Claude), donc une étape d'annulation par réglage,
// quel que soit le nombre d'objets sélectionnés. Aucune logique métier ici.

#include <QColor>
#include <QStringList>
#include <QWidget>
#include <optional>

#include "document_session.hpp"
#include "inspector_model.hpp"

class QCheckBox;
class QGroupBox;
class QFormLayout;
class QWidget;
class QComboBox;
class QLabel;
class QPushButton;

namespace openstitch::desktop_v2 {

class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(DocumentSession& session, QWidget* parent = nullptr);
    ~InspectorPanel() override;

    // Actions programmatiques (les widgets les appellent ; les tests aussi). Retournent vrai si une
    // étape d'annulation a été empilée.
    bool applyColor(const QColor& color);
    bool applyStitchType(const QString& typeId);
    bool applyVisible(bool visible);
    bool applyLocked(bool locked);
    // Réglage de couture essentiel (table `paramSpecs` du type commun) ; faux si refusé / sans
    // effet.
    bool applyParam(const QString& key, double value);
    [[nodiscard]] QStringList paramKeys() const { return paramKeys_; }
    [[nodiscard]] std::optional<double> paramValue(const QString& key) const;
    [[nodiscard]] bool paramMixed(const QString& key) const;

signals:
    void refused(const QString& message); // lot refusé par le moteur : document inchangé

private:
    void refresh();
    void rebuildParams(const InspectorState& state);
    bool run(const std::string& batch, const QString& label);

    DocumentSession& session_;
    QLabel* title_;
    QComboBox* type_;
    QPushButton* colour_;
    QCheckBox* visible_;
    QCheckBox* locked_;
    QGroupBox* paramsBox_{nullptr};
    QFormLayout* paramsForm_{nullptr};
    QStringList paramKeys_;
    QString paramType_;
    DocumentSession::Token sessionToken_{0};
    SelectionModel::Token selectionToken_{0};
    bool refreshing_{false};
};

} // namespace openstitch::desktop_v2
