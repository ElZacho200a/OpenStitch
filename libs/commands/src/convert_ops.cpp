// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/convert_ops.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/border_satin.hpp"

namespace openstitch::commands {

namespace {

constexpr Micrometers kFlattenTolerance{50};
constexpr std::size_t kMinSamples = 8;
constexpr std::size_t kMaxSamples = 512;

// Remplacement atomique d'un objet de broderie (+ sections soeurs retirées, couches ajoutées
// juste après, forme source ajoutée ou remplacée). Tout est restitué exactement à l'annulation.
class ConvertEmbroideryCommand final : public ICommand {
public:
    struct Spec {
        document::EmbroideryObject replacement;                // même id que l'objet converti
        std::vector<document::EmbroideryObject> extras;        // insérés juste après
        std::vector<ObjectId> removed;                         // sections soeurs retirées
        std::optional<document::VectorObject> added_vector;    // nouvelle forme source
        std::optional<document::VectorObject> replaced_vector; // forme source remplacée (même id)
        std::string label;
    };

    explicit ConvertEmbroideryCommand(Spec spec) : spec_(std::move(spec)) {}

    void apply(document::Project& project) override {
        applied_ = false;
        removedAt_.clear();
        auto& embs = project.embroidery_objects;
        const auto it = std::find_if(embs.begin(), embs.end(),
                                     [&](const auto& e) { return e.id == spec_.replacement.id; });
        if (it == embs.end()) {
            return;
        }
        previous_ = *it;
        *it = spec_.replacement;

        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < embs.size(); ++i) {
            if (std::find(spec_.removed.begin(), spec_.removed.end(), embs[i].id) !=
                spec_.removed.end()) {
                indices.push_back(i);
            }
        }
        for (auto rit = indices.rbegin(); rit != indices.rend(); ++rit) {
            removedAt_.emplace_back(*rit, embs[*rit]);
            embs.erase(embs.begin() + static_cast<std::ptrdiff_t>(*rit));
        }
        std::reverse(removedAt_.begin(), removedAt_.end()); // ordre croissant d'index d'origine

        const auto anchor = std::find_if(
            embs.begin(), embs.end(), [&](const auto& e) { return e.id == spec_.replacement.id; });
        embs.insert(anchor + 1, spec_.extras.begin(), spec_.extras.end());

        previousVector_.reset();
        if (spec_.added_vector) {
            project.vector_objects.push_back(*spec_.added_vector);
        }
        if (spec_.replaced_vector) {
            if (auto* vec = project.findObject(spec_.replaced_vector->id)) {
                previousVector_ = *vec;
                *vec = *spec_.replaced_vector;
            }
        }
        applied_ = true;
    }

    void revert(document::Project& project) override {
        if (!applied_) {
            return;
        }
        auto& embs = project.embroidery_objects;
        for (const auto& extra : spec_.extras) {
            embs.erase(std::remove_if(embs.begin(), embs.end(),
                                      [&](const auto& e) { return e.id == extra.id; }),
                       embs.end());
        }
        for (const auto& [index, emb] : removedAt_) {
            const std::size_t pos = std::min(index, embs.size());
            embs.insert(embs.begin() + static_cast<std::ptrdiff_t>(pos), emb);
        }
        removedAt_.clear();
        for (auto& e : embs) {
            if (e.id == previous_.id) {
                e = previous_;
                break;
            }
        }
        if (previousVector_) {
            if (auto* vec = project.findObject(previousVector_->id)) {
                *vec = *previousVector_;
            }
        }
        if (spec_.added_vector) {
            auto& vecs = project.vector_objects;
            vecs.erase(
                std::remove_if(vecs.begin(), vecs.end(),
                               [&](const auto& v) { return v.id == spec_.added_vector->id; }),
                vecs.end());
        }
        applied_ = false;
    }

    [[nodiscard]] std::string name() const override { return spec_.label; }

private:
    Spec spec_;
    document::EmbroideryObject previous_{};
    std::vector<std::pair<std::size_t, document::EmbroideryObject>> removedAt_;
    std::optional<document::VectorObject> previousVector_;
    bool applied_{false};
};

bool is_contour_type(const document::EmbroideryObject& e) {
    return std::holds_alternative<document::RunningStitchParams>(e.params) || e.is_blanket() ||
           e.is_motif_run();
}

bool is_fill_type(const document::EmbroideryObject& e) {
    return e.is_tatami() || e.is_directional() || e.is_contour_fill() || e.is_motif_fill() ||
           e.is_radial_fill() || e.is_spiral_fill() ||
           std::holds_alternative<document::CrosshatchFillParams>(e.params);
}

// Rail à >= 2 nœuds.
bool usable_satin(const document::SatinParams& p) {
    return p.rail_a.nodes.size() >= 2 && p.rail_b.nodes.size() >= 2;
}

geometry::Path polygon_path(const std::vector<Vec2um>& points, bool closed) {
    geometry::Path path;
    path.closed = closed;
    path.nodes.reserve(points.size());
    for (const Vec2um p : points) {
        path.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return path;
}

// Ligne centrale : milieu des points homologues (même fraction de longueur d'arc) des rails.
geometry::Path center_line(const document::SatinParams& p) {
    const bool closed = p.rail_a.closed && p.rail_b.closed;
    const auto a = geometry::flatten(p.rail_a, kFlattenTolerance).points;
    const auto b = geometry::flatten(p.rail_b, kFlattenTolerance).points;
    const auto ca = geometry::cumulative_lengths(a);
    const auto cb = geometry::cumulative_lengths(b);
    const double la = ca.empty() ? 0.0 : ca.back();
    const double lb = cb.empty() ? 0.0 : cb.back();
    const std::size_t n = std::clamp(std::max(a.size(), b.size()), kMinSamples, kMaxSamples);
    std::vector<Vec2um> mid;
    const std::size_t count = closed ? n : n + 1;
    for (std::size_t i = 0; i < count; ++i) {
        const double f = static_cast<double>(i) / static_cast<double>(n);
        const Vec2um pa = geometry::point_at_length(a, ca, f * la);
        const Vec2um pb = geometry::point_at_length(b, cb, f * lb);
        mid.push_back(Vec2um{Micrometers{static_cast<std::int32_t>(std::llround(
                                 (static_cast<double>(pa.x.value) + pb.x.value) / 2.0))},
                             Micrometers{static_cast<std::int32_t>(std::llround(
                                 (static_cast<double>(pa.y.value) + pb.y.value) / 2.0))}});
    }
    return polygon_path(mid, closed);
}

// Contour extérieur de la colonne : anneau + trou si les deux rails sont fermés, sinon A puis B
// inversé (polygone fermé).
geometry::PathSet outline_of(const document::SatinParams& p) {
    geometry::PathSet set;
    if (p.rail_a.closed && p.rail_b.closed) {
        const bool aOuter = std::abs(geometry::signed_area_um2(p.rail_a)) >=
                            std::abs(geometry::signed_area_um2(p.rail_b));
        set.outer = aOuter ? p.rail_a : p.rail_b;
        set.holes.push_back(aOuter ? p.rail_b : p.rail_a);
        for (auto* path : {&set.outer, &set.holes.front()}) {
            for (auto& node : path->nodes) {
                node.tan_in.reset();
                node.tan_out.reset();
            }
            path->closed = true;
        }
        return set;
    }
    std::vector<Vec2um> pts = geometry::flatten(p.rail_a, kFlattenTolerance).points;
    auto back = geometry::flatten(p.rail_b, kFlattenTolerance).points;
    pts.insert(pts.end(), back.rbegin(), back.rend());
    set.outer = polygon_path(pts, true);
    return set;
}

// Sections satin de la même forme source que `anchor`, ancre d'abord (ordre du document).
std::vector<const document::EmbroideryObject*>
satin_sections(const document::Project& project, const document::EmbroideryObject& anchor) {
    std::vector<const document::EmbroideryObject*> out{&anchor};
    for (const auto& e : project.embroidery_objects) {
        if (e.id != anchor.id && e.source_vector == anchor.source_vector && e.is_satin()) {
            out.push_back(&e);
        }
    }
    return out;
}

void reset_edits(document::EmbroideryObject& e) {
    e.overrides.clear();
    e.edited_fingerprint = 0;
    e.edited_point_count = 0;
    e.intent = document::EmbroideryIntent::ForcedUserChoice;
}

ConversionPlan failure(std::string message) {
    return ConversionPlan{nullptr, std::move(message)};
}

ConversionPlan convert_satin_geometry(document::Project& project,
                                      const document::EmbroideryObject& emb, bool centerline) {
    const auto sections = satin_sections(project, emb);
    std::vector<geometry::PathSet> paths;
    std::vector<ObjectId> siblings;
    std::optional<Vec2um> entry;
    for (const auto* section : sections) {
        const auto& satin = std::get<document::SatinParams>(section->params);
        if (!usable_satin(satin)) {
            if (section == &emb) {
                return failure("Cette colonne satin n'a pas de rails exploitables.");
            }
            continue;
        }
        if (section == &emb) {
            entry = satin.entry_point;
        } else {
            siblings.push_back(section->id);
        }
        if (centerline) {
            geometry::PathSet set;
            set.outer = center_line(satin);
            paths.push_back(std::move(set));
        } else {
            paths.push_back(outline_of(satin));
        }
    }
    if (paths.empty()) {
        return failure("Cette colonne satin n'a pas de rails exploitables.");
    }
    const auto* source = project.findObject(emb.source_vector);
    ConvertEmbroideryCommand::Spec spec;
    spec.replacement = emb;
    reset_edits(spec.replacement);
    if (centerline) {
        spec.replacement.params = document::RunningStitchParams{};
        spec.label = "Satin vers contour (ligne centrale)";
    } else {
        document::TatamiParams tatami;
        tatami.entry_point = entry;
        spec.replacement.params = tatami;
        spec.label = "Satin vers forme remplie";
    }
    spec.removed = std::move(siblings);
    const bool proxy = source != nullptr && !source->visible;
    if (proxy) {
        // Forme source cachée (colonne manuelle) : elle devient la forme visible du résultat.
        document::VectorObject vec = *source;
        vec.paths = std::move(paths);
        vec.visible = true;
        spec.replaced_vector = std::move(vec);
    } else {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = (centerline ? "Ligne centrale de " : "Forme de ") +
                   (source != nullptr ? source->name : emb.name);
        vec.rgb = emb.rgb;
        vec.paths = std::move(paths);
        if (source != nullptr) {
            vec.calque = source->calque;
        }
        spec.replacement.source_vector = vec.id;
        spec.added_vector = std::move(vec);
    }
    return ConversionPlan{std::make_unique<ConvertEmbroideryCommand>(std::move(spec)), {}};
}

ConversionPlan convert_to_border_satin(document::Project& project,
                                       const document::EmbroideryObject& emb,
                                       Micrometers border_width) {
    const auto* source = project.findObject(emb.source_vector);
    if (source == nullptr) {
        return failure("Aucun contour source pour construire la bordure.");
    }
    document::BorderSatinSpec border;
    border.width = stitch_generation::clamp_border_width(border_width);
    auto satins = stitch_generation::border_satin_from_paths(source->paths, border);
    if (satins.empty()) {
        return failure("Ce contour est trop petit ou dégénéré pour un satin de bordure.");
    }
    ConvertEmbroideryCommand::Spec spec;
    spec.replacement = emb;
    reset_edits(spec.replacement);
    spec.replacement.params = std::move(satins.front());
    for (std::size_t i = 1; i < satins.size(); ++i) {
        document::EmbroideryObject extra = emb;
        reset_edits(extra);
        extra.id = project.object_ids.next();
        extra.params = std::move(satins[i]);
        spec.extras.push_back(std::move(extra));
    }
    spec.label = "Contour vers satin de bordure";
    return ConversionPlan{std::make_unique<ConvertEmbroideryCommand>(std::move(spec)), {}};
}

} // namespace

bool conversion_applicable(const document::EmbroideryObject& object, ConversionKind kind) {
    switch (kind) {
    case ConversionKind::SatinToCenterline:
    case ConversionKind::SatinToFilledShape:
        return object.is_satin();
    case ConversionKind::ContourToBorderSatin:
        return is_contour_type(object);
    case ConversionKind::FillToContour:
        return is_fill_type(object);
    }
    return false;
}

ConversionPlan make_conversion_command(document::Project& project, ObjectId embroidery_id,
                                       ConversionKind kind, Micrometers border_width) {
    const auto* emb = project.findEmbroidery(embroidery_id);
    if (emb == nullptr) {
        return failure("Objet de broderie introuvable.");
    }
    if (!conversion_applicable(*emb, kind)) {
        return failure("Cette conversion ne s'applique pas au type de points de l'objet.");
    }
    switch (kind) {
    case ConversionKind::SatinToCenterline:
        return convert_satin_geometry(project, *emb, true);
    case ConversionKind::SatinToFilledShape:
        return convert_satin_geometry(project, *emb, false);
    case ConversionKind::ContourToBorderSatin:
        return convert_to_border_satin(project, *emb, border_width);
    case ConversionKind::FillToContour: {
        ConvertEmbroideryCommand::Spec spec;
        spec.replacement = *emb;
        reset_edits(spec.replacement);
        spec.replacement.params = document::RunningStitchParams{};
        spec.label = "Remplissage vers contour";
        return ConversionPlan{std::make_unique<ConvertEmbroideryCommand>(std::move(spec)), {}};
    }
    }
    return failure("Conversion inconnue.");
}

} // namespace openstitch::commands
