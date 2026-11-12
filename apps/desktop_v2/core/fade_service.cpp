// SPDX-License-Identifier: Apache-2.0
#include "fade_service.hpp"

#include <algorithm>

#include "openstitch/project_ops/gradient_fill.hpp"

namespace openstitch::desktop_v2 {

namespace {
ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}
const document::EmbroideryObject* findObject(const document::Project& p, std::uint64_t id) {
    const auto it =
        std::ranges::find_if(p.embroidery_objects, [&](const auto& o) { return o.id.value == id; });
    return it == p.embroidery_objects.end() ? nullptr : &*it;
}
} // namespace

std::string fadeIncompatibility(const document::Project& project, std::uint64_t objectId) {
    return project_ops::gradient_incompatibility(project, ObjectId{objectId});
}

std::optional<FadeSettings> readFade(const document::Project& project, std::uint64_t objectId) {
    const auto spec = project_ops::describe_gradient_fill(project, ObjectId{objectId});
    if (!spec) {
        return std::nullopt;
    }
    return FadeSettings{spec->foreground, spec->complementary,
                        static_cast<int>(spec->gradient.texture)};
}

ServiceResult createFade(DocumentSession& session, std::uint64_t objectId,
                         const FadeSettings& settings) {
    const auto* object = findObject(session.project(), objectId);
    if (object == nullptr) {
        return refuse("Sélectionnez le remplissage qui portera le fondu.");
    }
    if (const auto why = fadeIncompatibility(session.project(), objectId); !why.empty()) {
        return refuse(why);
    }
    auto spec = project_ops::default_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return refuse("Ce remplissage ne peut pas porter un fondu (forme vide ou dégénérée).");
    }
    spec->foreground = settings.foreground;
    spec->foreground_thread.reset(); // couleur libre : pas de fil de nuancier imposé
    spec->complementary = settings.complementary;
    if (settings.texture < 0 || settings.texture > 2)
        return refuse("Technique de fondu inconnue.");
    spec->gradient.texture = static_cast<document::DensityTexture>(settings.texture);
    if (spec->foreground == spec->background) {
        return refuse("Choisissez un fil du dessus différent du fil du fond.");
    }
    const auto* shape = session.project().findObject(object->source_vector);
    if (shape == nullptr) {
        return refuse("Forme source introuvable.");
    }
    auto command = project_ops::make_create_gradient_fill_command(
        session.projectForIdAllocation(), ObjectId{objectId}, *spec, shape->paths);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult editFade(DocumentSession& session, std::uint64_t objectId,
                       const FadeSettings& settings) {
    auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return refuse("Cet objet ne porte pas de fondu.");
    }
    spec->foreground = settings.foreground;
    spec->foreground_thread.reset();
    spec->complementary = settings.complementary;
    if (settings.texture < 0 || settings.texture > 2)
        return refuse("Technique de fondu inconnue.");
    spec->gradient.texture = static_cast<document::DensityTexture>(settings.texture);
    auto command =
        project_ops::make_edit_gradient_fill_command(session.project(), ObjectId{objectId}, *spec);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult removeFade(DocumentSession& session, std::uint64_t objectId) {
    auto command =
        project_ops::make_remove_gradient_fill_command(session.project(), ObjectId{objectId});
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
