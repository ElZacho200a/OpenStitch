// SPDX-License-Identifier: Apache-2.0
#pragma once

// Studio -- famille F : opérations vectorielles sur la sélection (booléennes, nettoyage,
// simplification, lissage, séparation, conversion en courbes). Adaptateur sans logique métier :
// chaque opération est UN lot `project_ops` (ou un service de core/) appliqué par DocumentSession,
// donc UNE étape d'annulation.

#include <QObject>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>

#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class VectorController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionCountChanged)
public:
    explicit VectorController(StudioController& studio, QObject* parent = nullptr);
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] int selectedCount() const;

    // mode : union | subtract | intersect | exclude ; la DERNIÈRE forme sélectionnée est la
    // principale.
    Q_INVOKABLE bool booleanOp(const QString& mode);
    Q_INVOKABLE bool mergeObjects() { return booleanOp(QStringLiteral("union")); }
    Q_INVOKABLE bool breakApart();
    Q_INVOKABLE bool simplify(double toleranceMm);
    Q_INVOKABLE bool smooth(double toleranceMm);
    Q_INVOKABLE bool fillHoles(double maxAreaMm2);
    Q_INVOKABLE bool convertToCurves();
    Q_INVOKABLE bool smoothContours();

signals:
    void messageChanged();
    void selectionCountChanged();

private:
    [[nodiscard]] std::vector<std::uint64_t> ids() const;
    bool apply(const std::string& batch, const QString& label, bool selectCreated);
    void say(const QString& text);
    StudioController& studio_;
    QString message_;
};

} // namespace openstitch::desktop_v2::studio
