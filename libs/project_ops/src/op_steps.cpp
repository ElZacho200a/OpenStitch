// SPDX-License-Identifier: Apache-2.0
// Étapes du flux Studio exposées en opérations (agent, CLI) : Image (`image_prep`, `image_size`)
// et Vecteurs (`vectorize_segmentation`). La segmentation (`segment_image`, `seg_*`) et les
// points (`convert_vectors`, `set_stitch_type`…) existent déjà. Chaque appel est UNE commande
// annulable, comme le bouton correspondant de Studio.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "op_registry.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/project_ops/revectorize.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::project_ops {

RevectorizePlan revectorize(const document::Project& project, const RevectorizeOptions& opt) {
    RevectorizePlan out;
    if (!project.hasActiveImage() || !project.imageSource().segmentation) {
        return out;
    }
    const auto& source = project.imageSource();
    std::vector<RegionId> ids;
    for (const auto& slot : source.segmentation->region_slots) {
        if (slot) {
            ids.push_back(slot->id);
        }
    }
    const vectorization::VectorizeOptions options{
        .mm_per_px = source.mm_per_px,
        .simplify_tolerance = to_micrometers(Millimeters{opt.precision_mm}),
        .curve_tolerance =
            opt.curves ? Micrometers{static_cast<std::int32_t>(
                             std::lround(opt.curve_tolerance_px * source.mm_per_px.value * 1000.0))}
                       : Micrometers{0},
        .detail_mask = &source.detail_mask};
    const auto results = vectorization::vectorize_regions(*source.segmentation, ids, options);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (!results[i]) {
            continue;
        }
        const document::VectorObject* existing = nullptr;
        for (const auto& shape : project.vector_objects) {
            if (project.fromActiveImage(shape) && *shape.source_region == ids[i]) {
                existing = &shape;
                break;
            }
        }
        if (existing != nullptr) {
            if (existing->paths == *results[i]) {
                ++out.keep;
            } else {
                out.replace.emplace_back(existing->id, *results[i]);
            }
        } else if (!results[i]->empty()) {
            document::VectorObject shape;
            shape.name = "Région " + std::to_string(ids[i].value);
            shape.source_region = ids[i];
            shape.source_image = project.imageSourceId();
            shape.calque = !shape.source_image.valid() && !project.image_sources.empty()
                               ? project.calques.front().id
                               : shape.source_image;
            shape.rgb = source.segmentation->find(ids[i])->rgb;
            shape.paths = *results[i];
            out.add.push_back(std::move(shape));
        }
    }
    for (const auto& shape : project.vector_objects) {
        if (project.fromActiveImage(shape) &&
            source.segmentation->find(*shape.source_region) == nullptr) {
            ++out.orphan;
        }
    }
    return out;
}

namespace detail {

namespace {

using json = nlohmann::json;

OpStatus refused(const std::string& message, const std::string& hint = {}) {
    return OpStatus::failure(code::kRefused, message, hint);
}

// Nombre facultatif dans [lo ; hi] ; absent => `value` inchangé.
OpStatus number_arg(const OpContext& ctx, const char* key, double lo, double hi, double& value) {
    const auto it = ctx.args.find(key);
    if (it == ctx.args.end() || it->is_null()) {
        return OpStatus::success();
    }
    if (!it->is_number()) {
        return OpStatus::failure(code::kUsage,
                                 std::string("argument « ") + key + " » : nombre attendu");
    }
    value = it->get<double>();
    if (!std::isfinite(value) || value < lo || value > hi) {
        return OpStatus::failure(code::kOutOfRange,
                                 std::string("argument « ") + key + " » hors plage",
                                 "[" + std::to_string(lo) + " ; " + std::to_string(hi) + "]");
    }
    return OpStatus::success();
}

// Taille de l'image de travail (original + pile de prétraitements), (0, 0) si illisible.
std::pair<int, int> working_size(const document::Project& project) {
    auto working = image::apply_pipeline(project.imageSource().original, project.imageSource().ops);
    if (!working) {
        return {0, 0};
    }
    return {working->width, working->height};
}

// --- image_prep ----------------------------------------------------------------------------

OpStatus op_image_prep(OpContext& ctx) {
    if (!ctx.project.hasActiveImage()) {
        return refused("le projet n'a pas d'image source", "commencer par set_source_image");
    }
    const auto it = ctx.args.find("kind");
    if (it == ctx.args.end() || !it->is_string()) {
        return OpStatus::failure(code::kUsage, "argument « kind » obligatoire (texte)");
    }
    const std::string kind = it->get<std::string>();
    image::ImageOp op;
    if (kind == "brightness_contrast") {
        double brightness = 0.0;
        double contrast = 0.0;
        if (auto st = number_arg(ctx, "brightness", -100, 100, brightness); !st.ok) {
            return st;
        }
        if (auto st = number_arg(ctx, "contrast", -100, 100, contrast); !st.ok) {
            return st;
        }
        if (std::abs(brightness) < 1e-9 && std::abs(contrast) < 1e-9) {
            return refused("luminosité et contraste à zéro : rien à appliquer");
        }
        op = image::BrightnessContrastOp{brightness, contrast};
    } else if (kind == "grayscale") {
        op = image::GrayscaleOp{};
    } else if (kind == "flip_h" || kind == "flip_v") {
        op = image::FlipOp{kind == "flip_h"};
    } else if (kind == "rotate_right" || kind == "rotate_180" || kind == "rotate_left") {
        op = image::Rotate90Op{kind == "rotate_right" ? 1 : kind == "rotate_180" ? 2 : 3};
    } else if (kind == "sharpen") {
        double amount = 1.0;
        if (auto st = number_arg(ctx, "amount", 0.05, 3.0, amount); !st.ok) {
            return st;
        }
        op = image::SharpenOp{amount};
    } else if (kind == "clahe") {
        double clip = 2.0;
        if (auto st = number_arg(ctx, "clip", 1.0, 8.0, clip); !st.ok) {
            return st;
        }
        op = image::ClaheOp{clip};
    } else if (kind == "median" || kind == "bilateral" || kind == "flatten") {
        double strength = 1.0;
        if (auto st = number_arg(ctx, "strength", 1, kind == "median" ? 2 : 3, strength); !st.ok) {
            return st;
        }
        const int level = static_cast<int>(std::lround(strength));
        op = kind == "median"      ? image::ImageOp{image::MedianDenoiseOp{level}}
             : kind == "bilateral" ? image::ImageOp{image::BilateralDenoiseOp{level}}
                                   : image::ImageOp{image::FlattenOp{level}};
    } else if (kind == "quantize") {
        double colors = 8.0;
        if (auto st = number_arg(ctx, "colors", 2, 64, colors); !st.ok) {
            return st;
        }
        op = image::QuantizeOp{static_cast<int>(std::lround(colors))};
    } else if (kind == "crop") {
        const auto [w, h] = working_size(ctx.project);
        if (w == 0) {
            return refused("l'image de travail est illisible");
        }
        double x = 0;
        double y = 0;
        double cw = w;
        double ch = h;
        for (auto [key, ref, hi] : {std::tuple{"x", &x, w}, std::tuple{"y", &y, h},
                                    std::tuple{"w", &cw, w}, std::tuple{"h", &ch, h}}) {
            if (auto st = number_arg(ctx, key, 0, hi, *ref); !st.ok) {
                return st;
            }
        }
        const int ix = static_cast<int>(std::lround(x));
        const int iy = static_cast<int>(std::lround(y));
        const int iw = static_cast<int>(std::lround(cw));
        const int ih = static_cast<int>(std::lround(ch));
        if (iw < 8 || ih < 8 || ix + iw > w || iy + ih > h) {
            return refused("le recadrage doit rester dans l'image (" + std::to_string(w) + " × " +
                           std::to_string(h) + " px) et mesurer 8 px au moins");
        }
        if (ix == 0 && iy == 0 && iw == w && ih == h) {
            return refused("le recadrage couvre toute l'image : rien à faire");
        }
        op = image::CropOp{ix, iy, iw, ih};
    } else {
        return OpStatus::failure(code::kOutOfRange, "argument « kind » = « " + kind + " » inconnu",
                                 "brightness_contrast | grayscale | flip_h | flip_v | rotate_right "
                                 "| rotate_left | rotate_180 | sharpen | clahe | median | "
                                 "bilateral | flatten | quantize | crop");
    }
    ctx.stack.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)), ctx.project);
    const auto [w, h] = working_size(ctx.project);
    const double mm = ctx.project.imageSource().mm_per_px.value;
    return OpStatus::success({{"kind", kind},
                              {"steps", ctx.project.imageSource().ops.size()},
                              {"width_px", w},
                              {"height_px", h},
                              {"width_mm", w * mm},
                              {"height_mm", h * mm},
                              {"segmentation", "à recalculer (segment_image)"}});
}

// --- image_size ----------------------------------------------------------------------------

// Change l'échelle physique de l'image (mm par pixel) ; pixels, pile et segmentation intacts.
class SetMmPerPxCommand final : public commands::ICommand {
public:
    explicit SetMmPerPxCommand(double mm_per_px) : next_(mm_per_px) {}
    void apply(document::Project& project) override {
        previous_ = project.imageSource().mm_per_px.value;
        project.imageSource().mm_per_px.value = next_;
    }
    void revert(document::Project& project) override {
        project.imageSource().mm_per_px.value = previous_;
    }
    [[nodiscard]] std::string name() const override { return "Redimensionner l'image"; }

private:
    double next_;
    double previous_{0.0};
};

OpStatus op_image_size(OpContext& ctx) {
    if (!ctx.project.hasActiveImage()) {
        return refused("le projet n'a pas d'image source", "commencer par set_source_image");
    }
    const auto [w, h] = working_size(ctx.project);
    if (w == 0 || h == 0) {
        return refused("l'image de travail est illisible");
    }
    double width_mm = 0;
    double height_mm = 0;
    double mm_per_px = 0;
    if (auto st = number_arg(ctx, "width_mm", 1, 2000, width_mm); !st.ok) {
        return st;
    }
    if (auto st = number_arg(ctx, "height_mm", 1, 2000, height_mm); !st.ok) {
        return st;
    }
    if (auto st = number_arg(ctx, "mm_per_px", 0.001, 100, mm_per_px); !st.ok) {
        return st;
    }
    const int given = (width_mm > 0) + (height_mm > 0) + (mm_per_px > 0);
    if (given != 1) {
        return OpStatus::failure(code::kUsage,
                                 "donner exactement un argument : width_mm, height_mm ou mm_per_px",
                                 "les proportions sont toujours conservées");
    }
    const double scale = width_mm > 0 ? width_mm / w : height_mm > 0 ? height_mm / h : mm_per_px;
    ctx.stack.execute(std::make_unique<SetMmPerPxCommand>(scale), ctx.project);
    return OpStatus::success(
        {{"mm_per_px", scale}, {"width_mm", w * scale}, {"height_mm", h * scale}});
}

// --- vectorize_segmentation ----------------------------------------------------------------

OpStatus op_vectorize_segmentation(OpContext& ctx) {
    if (!ctx.project.hasActiveImage() || !ctx.project.imageSource().segmentation) {
        return refused("aucune segmentation : rien à vectoriser", "lancer segment_image d'abord");
    }
    RevectorizeOptions options;
    if (auto st = number_arg(ctx, "precision_mm", 0.01, 2.0, options.precision_mm); !st.ok) {
        return st;
    }
    if (auto st = number_arg(ctx, "curve_tolerance_px", 0.1, 10.0, options.curve_tolerance_px);
        !st.ok) {
        return st;
    }
    if (const auto it = ctx.args.find("curves"); it != ctx.args.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return OpStatus::failure(code::kUsage, "argument « curves » : booléen attendu");
        }
        options.curves = it->get<bool>();
    }
    bool prune = false;
    if (const auto it = ctx.args.find("prune_orphans"); it != ctx.args.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return OpStatus::failure(code::kUsage, "argument « prune_orphans » : booléen attendu");
        }
        prune = it->get<bool>();
    }
    auto plan = revectorize(ctx.project, options);
    // Formes orphelines (leur région a disparu après une nouvelle segmentation) et leurs objets
    // de broderie : retirées dans la MÊME commande, au lieu d'un remove_object par objet.
    std::vector<ObjectId> orphan_shapes;
    std::vector<ObjectId> orphan_stitches;
    int kept_locked = 0;
    if (prune) {
        const auto& seg = *ctx.project.imageSource().segmentation;
        for (const auto& shape : ctx.project.vector_objects) {
            if (!ctx.project.fromActiveImage(shape) || seg.find(*shape.source_region) != nullptr) {
                continue;
            }
            const bool locked =
                shape.locked || std::any_of(ctx.project.embroidery_objects.begin(),
                                            ctx.project.embroidery_objects.end(),
                                            [&](const document::EmbroideryObject& e) {
                                                return e.source_vector == shape.id && e.locked;
                                            });
            if (locked) {
                ++kept_locked;
                continue;
            }
            orphan_shapes.push_back(shape.id);
            for (const auto& e : ctx.project.embroidery_objects) {
                if (e.source_vector == shape.id) {
                    orphan_stitches.push_back(e.id);
                }
            }
        }
    }
    json result{{"replaced", plan.replace.size()},
                {"added", plan.add.size()},
                {"unchanged", plan.keep},
                {"orphans", plan.orphan}};
    if (prune) {
        result["pruned_shapes"] = orphan_shapes.size();
        result["pruned_stitch_objects"] = orphan_stitches.size();
        result["orphans_kept_locked"] = kept_locked;
    }
    if (plan.replace.empty() && plan.add.empty() && orphan_shapes.empty()) {
        result["note"] = "les formes suivent déjà la segmentation";
        return OpStatus::success(std::move(result));
    }
    auto group = std::make_unique<commands::CompositeCommand>(
        "Mettre à jour les formes depuis la segmentation");
    for (const auto id : orphan_stitches) {
        group->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(id));
    }
    for (const auto id : orphan_shapes) {
        group->add(std::make_unique<commands::RemoveVectorObjectCommand>(id));
    }
    for (auto& [id, paths] : plan.replace) {
        group->add(std::make_unique<commands::SetVectorPathsCommand>(id, std::move(paths),
                                                                     "Mettre à jour une forme"));
    }
    json created = json::array();
    for (auto& shape : plan.add) {
        shape.id = ctx.project.object_ids.next();
        created.push_back(shape.id.value);
        group->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(shape)));
    }
    ctx.stack.execute(std::move(group), ctx.project);
    result["created"] = std::move(created);
    return OpStatus::success(std::move(result));
}

} // namespace

void register_step_ops(std::vector<OpSpec>& out) {
    out.push_back(OpSpec{
        "image_prep",
        "Étape Image : ajoute un prétraitement à la pile de l'image source (annulable) ; la "
        "segmentation est à recalculer ensuite.",
        {{"kind", "string", "",
          "brightness_contrast | grayscale | flip_h | flip_v | rotate_right | rotate_left | "
          "rotate_180 | sharpen | clahe | median | bilateral | flatten | quantize | crop",
          true, ""},
         {"brightness", "number", "", "brightness_contrast : -100..100", false, "[-100 ; 100]"},
         {"contrast", "number", "", "brightness_contrast : -100..100", false, "[-100 ; 100]"},
         {"amount", "number", "", "sharpen : 0,05..3 (défaut 1)", false, "[0.05 ; 3]"},
         {"clip", "number", "", "clahe : 1..8 (défaut 2)", false, "[1 ; 8]"},
         {"strength", "integer", "", "median 1..2, bilateral/flatten 1..3 (défaut 1)", false,
          "[1 ; 3]"},
         {"colors", "integer", "", "quantize : 2..64 (défaut 8)", false, "[2 ; 64]"},
         {"x", "integer", "px", "crop : coin gauche dans l'image de travail", false, ""},
         {"y", "integer", "px", "crop : coin haut", false, ""},
         {"w", "integer", "px", "crop : largeur (8 px au moins)", false, ""},
         {"h", "integer", "px", "crop : hauteur (8 px au moins)", false, ""}},
        R"({"op":"image_prep","kind":"flatten","strength":2})",
        op_image_prep});
    out.push_back(OpSpec{
        "image_size",
        "Étape Image : fixe la taille physique de l'image (proportions conservées) ; un seul "
        "argument parmi width_mm, height_mm, mm_per_px.",
        {{"width_mm", "number", "mm", "largeur voulue", false, "[1 ; 2000]"},
         {"height_mm", "number", "mm", "hauteur voulue", false, "[1 ; 2000]"},
         {"mm_per_px", "number", "mm", "échelle directe", false, "[0.001 ; 100]"}},
        R"({"op":"image_size","width_mm":120})",
        op_image_size});
    out.push_back(OpSpec{
        "vectorize_segmentation",
        "Étape Vecteurs : crée ou met à jour une forme vectorielle par région de la "
        "segmentation (comme « Mettre à jour depuis la segmentation » dans Studio).",
        {{"precision_mm", "number", "mm", "simplification des contours (défaut 0,2)", false,
          "[0.01 ; 2]"},
         {"curves", "boolean", "", "ajuster des courbes (défaut faux)", false, ""},
         {"curve_tolerance_px", "number", "px", "tolérance des courbes (défaut 1,5)", false,
          "[0.1 ; 10]"},
         {"prune_orphans", "boolean", "",
          "retire aussi les formes dont la région a disparu et leurs objets de broderie (après "
          "une nouvelle segmentation ; verrouillés gardés ; défaut faux)",
          false, ""}},
        R"({"op":"vectorize_segmentation","precision_mm":0.2})",
        op_vectorize_segmentation});
}

} // namespace detail

} // namespace openstitch::project_ops
