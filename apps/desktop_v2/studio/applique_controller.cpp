// SPDX-License-Identifier: Apache-2.0
#include "applique_controller.hpp"
#include "change_gate.hpp"

#include <filesystem>

#include "openstitch/project_ops/applique_cut.hpp"

namespace openstitch::desktop_v2::studio {

AppliqueController::AppliqueController(StudioController& studio, InspectorController& inspector,
                                       QObject* parent)
    : QObject(parent), studio_(studio), inspector_(inspector) {
    gateStamp_ = documentStamp(studio_);
    connect(&studio_, &StudioController::changed, this, [this] {
        const auto stamp = documentStamp(studio_);
        if (stamp != gateStamp_) {
            gateStamp_ = stamp;
            emit changed();
        }
    });
}

std::uint64_t AppliqueController::selectedApplique() const {
    std::uint64_t found = 0;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        const auto* object = studio_.session().project().findEmbroidery(ObjectId{target.id});
        if (object != nullptr && object->is_applique()) {
            if (found != 0) {
                return 0; // plusieurs : pas d'objet unique
            }
            found = target.id;
        }
    }
    return found;
}

bool AppliqueController::isApplique() const {
    return selectedApplique() != 0;
}

bool AppliqueController::canConvert() const {
    const auto& targets = studio_.session().selection().targets();
    if (targets.empty()) {
        return false;
    }
    for (const auto& target : targets) {
        const auto* object = target.kind == TargetKind::EmbroideryObject
                                 ? studio_.session().project().findEmbroidery(ObjectId{target.id})
                                 : nullptr;
        if (object == nullptr || object->is_applique()) {
            return false;
        }
    }
    return true;
}

int AppliqueController::appliqueCount() const {
    int count = 0;
    for (const auto& object : studio_.session().project().embroidery_objects) {
        count += object.is_applique() ? 1 : 0;
    }
    return count;
}

void AppliqueController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

bool AppliqueController::convert() {
    if (!canConvert()) {
        say(false, QStringLiteral("Sélectionnez des objets de broderie qui ne sont pas déjà des "
                                  "appliqués."));
        return false;
    }
    const auto error = inspector_.setStitchType(QStringLiteral("applique"));
    say(error.isEmpty(), error.isEmpty() ? QStringLiteral("Converti en appliqué.") : error);
    emit changed();
    return error.isEmpty();
}

QVariantMap AppliqueController::field(const QString& key) const {
    return inspector_.field(key);
}

bool AppliqueController::setField(const QString& key, double value) {
    if (!isApplique()) {
        say(false, QStringLiteral("Sélectionnez un appliqué."));
        return false;
    }
    const auto error = inspector_.setParam(key, value);
    say(error.isEmpty(),
        error.isEmpty() ? QStringLiteral("Réglage de l'appliqué modifié.") : error);
    return error.isEmpty();
}

bool AppliqueController::exportCut(const QString& path) {
    const auto id = selectedApplique();
    std::optional<ObjectId> only;
    if (id != 0) {
        only = ObjectId{id};
    }
    const auto result = project_ops::export_applique_cut(
        studio_.session().project(), std::filesystem::path(path.toStdWString()), only);
    if (!result) {
        say(false, QString::fromStdString(result.error().message));
        return false;
    }
    say(true, QStringLiteral("Contour de découpe exporté : %1 contour(s).").arg(result->paths));
    return true;
}

} // namespace openstitch::desktop_v2::studio
