// SPDX-License-Identifier: Apache-2.0
#include "points_space_controller.hpp"

#include <string>
#include <utility>

#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2::studio {

PointsSpaceController::PointsSpaceController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &PointsSpaceController::refresh);
    connect(&studio_, &StudioController::documentReplaced, this,
            &PointsSpaceController::clearCamera);
    refresh();
}

void PointsSpaceController::refresh() {
    const auto& project = studio_.session().project();
    QVariantMap sources;
    int orphans = 0;
    int linked = 0;
    for (const auto& object : project.embroidery_objects) {
        QVariantMap entry;
        if (!object.source_vector.valid()) {
            // Objet issu d'un import machine ou d'un réglage direct : pas de forme source.
            entry = {{QStringLiteral("vectorId"), QString()},
                     {QStringLiteral("label"), QString()},
                     {QStringLiteral("found"), true}};
        } else {
            const auto* shape = project.findObject(object.source_vector);
            const bool found = shape != nullptr;
            if (!found) {
                ++orphans;
            } else {
                ++linked;
            }
            const QString label =
                !found ? QString()
                       : QString::fromStdString(shape->name.empty()
                                                    ? "Forme " + std::to_string(shape->id.value)
                                                    : shape->name);
            entry = {{QStringLiteral("vectorId"), QString::number(object.source_vector.value)},
                     {QStringLiteral("label"), label},
                     {QStringLiteral("found"), found}};
        }
        sources.insert(QString::number(object.id.value), entry);
    }
    const int count = static_cast<int>(project.embroidery_objects.size());
    QString state;
    QString summary;
    if (count == 0) {
        state = QStringLiteral("empty");
        summary = QStringLiteral("Aucun objet de broderie : les points apparaîtront avec les "
                                 "premières formes brodées.");
    } else if (orphans > 0) {
        state = QStringLiteral("stale");
        summary = QStringLiteral("%1 objet(s) de broderie ont perdu leur forme source : "
                                 "supprimez-les ou rétablissez la forme.")
                      .arg(orphans);
    } else {
        state = QStringLiteral("upToDate");
        summary = QStringLiteral("Points à jour (recalculés depuis les formes) : %1 objet(s), %2 "
                                 "relié(s) à une forme.")
                      .arg(count)
                      .arg(linked);
    }
    const bool same = objectCount_ == count && orphanCount_ == orphans && state_ == state &&
                      summary_ == summary && sources_ == sources;
    objectCount_ = count;
    orphanCount_ = orphans;
    state_ = state;
    summary_ = summary;
    sources_ = std::move(sources);
    if (!same) {
        emit changed();
    }
}

void PointsSpaceController::rememberCamera(const QVariantMap& view) {
    if (view.isEmpty() || camera_ == view) {
        return;
    }
    camera_ = view;
    emit cameraChanged();
}

void PointsSpaceController::clearCamera() {
    if (!camera_.isEmpty()) {
        camera_.clear();
        emit cameraChanged();
    }
}

bool PointsSpaceController::hasSource(const QString& objectId) const {
    const auto entry = sources_.value(objectId).toMap();
    return !entry.isEmpty() && entry.value(QStringLiteral("found")).toBool() &&
           !entry.value(QStringLiteral("vectorId")).toString().isEmpty();
}

} // namespace openstitch::desktop_v2::studio
