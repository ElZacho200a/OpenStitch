// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QRectF>
#include <QString>

#include <variant>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop {

// Information purement visuelle : aucun calcul de points ni mutation au survol.
[[nodiscard]] inline QString vectorTooltip(const document::VectorObject& vector,
                                           const QRectF& bounds,
                                           const document::EmbroideryObject* embroidery) {
    QString type = QStringLiteral("Aucune broderie associée");
    QString color =
        QStringLiteral("RVB %1, %2, %3").arg(vector.rgb[0]).arg(vector.rgb[1]).arg(vector.rgb[2]);
    if (embroidery != nullptr) {
        if (std::holds_alternative<document::RunningStitchParams>(embroidery->params)) {
            type = QStringLiteral("Point droit");
        } else if (std::holds_alternative<document::TatamiParams>(embroidery->params)) {
            type = QStringLiteral("Tatami");
        } else if (std::holds_alternative<document::SatinParams>(embroidery->params)) {
            type = QStringLiteral("Satin");
        } else if (std::holds_alternative<document::DirectionalFillParams>(embroidery->params)) {
            type = QStringLiteral("Remplissage directionnel");
        } else {
            type = QStringLiteral("Satin automatique");
        }
        if (embroidery->thread) {
            color = QStringLiteral("Fil : %1 / %2")
                        .arg(QString::fromStdString(embroidery->thread->chart_id).toHtmlEscaped(),
                             QString::fromStdString(embroidery->thread->code).toHtmlEscaped());
        } else {
            color = QStringLiteral("RVB %1, %2, %3")
                        .arg(embroidery->rgb[0])
                        .arg(embroidery->rgb[1])
                        .arg(embroidery->rgb[2]);
        }
    }
    const QString name = QString::fromStdString(vector.name).toHtmlEscaped();
    return QStringLiteral("<qt><b>%1</b><br/>%2 × %3 mm<br/>%4<br/>%5</qt>")
        .arg(name.isEmpty() ? QStringLiteral("Sans nom") : name)
        .arg(QString::number(bounds.width(), 'f', 1))
        .arg(QString::number(bounds.height(), 'f', 1))
        .arg(type, color);
}

} // namespace openstitch::desktop
