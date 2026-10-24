// SPDX-License-Identifier: Apache-2.0
// Opérations d'import et de lettrage : `import_svg`, `add_text`, `set_text`, `remove_text`.
// Aucun algorithme ici : lecture SVG (`formats::read_svg_file`), classification automatique
// (`autodigitize::auto_digitize_vectors`), lettrage (`lettering::build_text_objects`) et
// commandes existantes (`AddObjectBatchCommand`, `SetTextObjectCommand`).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "op_registry.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/text_commands.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/lettering/font.hpp"
#include "openstitch/lettering/lettering.hpp"
#include "openstitch/project_ops/inspect.hpp"

namespace openstitch::project_ops::detail {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

ArgSpec arg(std::string name, std::string type, std::string description, bool required = false,
            std::string unit = {}, std::string range = {}) {
    return ArgSpec{std::move(name),        std::move(type), std::move(unit),
                   std::move(description), required,        std::move(range)};
}

bool read_point(const json& v, Vec2um& out) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
        return false;
    }
    const double x = v[0].get<double>();
    const double y = v[1].get<double>();
    if (!(std::abs(x) <= 1000.0) || !(std::abs(y) <= 1000.0)) {
        return false;
    }
    out = Vec2um{to_micrometers(Millimeters{x}), to_micrometers(Millimeters{y})};
    return true;
}

OpStatus out_of_range(const std::string& message, const std::string& hint = {}) {
    return OpStatus::failure(code::kOutOfRange, message, hint);
}

// --- import_svg ---------------------------------------------------------------------------

OpStatus import_svg(OpContext& ctx) {
    const std::string file = ctx.args.at("file").get<std::string>();
    const std::string type = ctx.args.value("stitch_type", std::string("auto"));
    if (type != "auto" && type != "tatami" && type != "running") {
        return out_of_range("stitch_type « " + type + " » inconnu",
                            "valeurs : auto, tatami, running");
    }
    std::array<std::uint8_t, 3> rgb{80, 120, 200}; // comme l'import SVG du bureau
    if (ctx.args.contains("rgb")) {
        const auto parsed = rgb_from_hex(ctx.args.at("rgb").get<std::string>());
        if (!parsed) {
            return out_of_range("rgb invalide", "format attendu : #RRGGBB");
        }
        rgb = *parsed;
    }
    const std::string prefix = ctx.args.value("name", std::string("Import SVG"));
    if (prefix.empty() || prefix.size() > 128) {
        return out_of_range("name : 1 à 128 caractères attendus");
    }

    std::error_code ec;
    if (!fs::is_regular_file(fs::path(file), ec)) {
        return OpStatus::failure(code::kIo, "fichier SVG introuvable : " + file,
                                 "chemin relatif au dossier courant ou absolu");
    }
    auto imported = formats::read_svg_file(fs::path(file));
    if (!imported) {
        return OpStatus::failure(code::kRefused, "SVG illisible : " + imported.error().message);
    }
    if (imported->objects.empty()) {
        std::string why = "le SVG ne contient aucune forme importable";
        for (const auto& w : imported->warnings) {
            why += " ; " + w;
        }
        return OpStatus::failure(code::kRefused, why,
                                 "formes prises en charge : path, rect, circle, ellipse, line, "
                                 "polyline, polygon (voir docs/source/cli-project.md)");
    }
    if (imported->objects.size() > 5000) {
        return out_of_range("le SVG contient plus de 5000 formes", "simplifiez le dessin");
    }

    std::vector<document::VectorObject> vectors;
    for (auto& pathSet : imported->objects) {
        document::VectorObject v;
        v.id = ctx.project.object_ids.next();
        v.name = prefix + " " + std::to_string(vectors.size() + 1);
        v.rgb = rgb;
        v.paths.push_back(std::move(pathSet));
        vectors.push_back(std::move(v));
    }

    std::vector<document::EmbroideryObject> embroideries;
    json warnings = json::array();
    for (const auto& w : imported->warnings) {
        warnings.push_back(w);
    }
    if (type == "auto") {
        // Même classification que l'auto-numérisation : tatami pour les zones, contour pour
        // les petites formes.
        autodigitize::AutoOptions options;
        options.mm_per_px = ctx.project.mm_per_px;
        auto built = autodigitize::auto_digitize_vectors(vectors, ctx.project.object_ids, options);
        if (!built) {
            return OpStatus::failure(code::kRefused, "numérisation des formes impossible : " +
                                                         built.error().message);
        }
        embroideries = std::move(built->embroideries);
        for (const auto& w : built->warnings) {
            warnings.push_back(w);
        }
    } else {
        for (const auto& v : vectors) {
            document::EmbroideryObject e;
            e.id = ctx.project.object_ids.next();
            e.name = v.name;
            e.source_vector = v.id;
            e.rgb = rgb;
            e.intent = document::EmbroideryIntent::ForcedUserChoice;
            if (type == "running") {
                e.params = document::RunningStitchParams{};
            } else {
                e.params = document::TatamiParams{};
            }
            embroideries.push_back(std::move(e));
        }
    }
    if (embroideries.empty()) {
        return OpStatus::failure(code::kRefused, "aucun objet de broderie produit par ce SVG");
    }
    // Les couleurs du SVG ne sont pas lues (fill/stroke non repris) : une seule couleur de fil.
    for (auto& e : embroideries) {
        e.rgb = rgb;
    }
    json vectorIds = json::array();
    for (const auto& v : vectors) {
        vectorIds.push_back(v.id.value);
    }
    json created = json::array();
    for (const auto& e : embroideries) {
        created.push_back(e.id.value);
    }
    const std::uint64_t first = embroideries.front().id.value;
    ctx.stack.execute(std::make_unique<commands::AddObjectBatchCommand>(
                          std::move(vectors), std::move(embroideries), "Importer un SVG"),
                      ctx.project);
    return OpStatus::success({{"id", first},
                              {"created", created},
                              {"vector_ids", vectorIds},
                              {"shapes", created.size()},
                              {"warnings", warnings}});
}

// --- lettrage -----------------------------------------------------------------------------

const char* align_name(document::TextAlign a) {
    switch (a) {
    case document::TextAlign::Left:
        return "left";
    case document::TextAlign::Center:
        return "center";
    case document::TextAlign::Right:
        return "right";
    case document::TextAlign::Justify:
        return "justify";
    }
    return "left";
}

// Lit les champs communs d'un texte dans `t` (mise à jour partielle) ; refus nommé sinon.
OpStatus read_text_fields(const json& args, document::TextObject& t, bool& originGiven) {
    originGiven = false;
    if (args.contains("text")) {
        const auto text = args.at("text").get<std::string>();
        if (text.empty() || text.size() > 2000) {
            return out_of_range("text : 1 à 2000 caractères attendus");
        }
        t.text = text;
    }
    const auto number = [&](const char* key, double lo, double hi, double& out) -> OpStatus {
        if (!args.contains(key)) {
            return OpStatus::success();
        }
        const double v = args.at(key).get<double>();
        if (!(v >= lo && v <= hi)) {
            return out_of_range(std::string(key) + " : " + std::to_string(v) + " hors plage [" +
                                std::to_string(lo) + " ; " + std::to_string(hi) + "]");
        }
        out = v;
        return OpStatus::success();
    };
    double v = 0.0;
    if (args.contains("cap_height_mm")) {
        if (auto s = number("cap_height_mm", 1.0, 500.0, v); !s.ok) {
            return s;
        }
        t.cap_height = to_micrometers(Millimeters{v});
    }
    if (args.contains("letter_spacing_mm")) {
        if (auto s = number("letter_spacing_mm", -20.0, 50.0, v); !s.ok) {
            return s;
        }
        t.letter_spacing = to_micrometers(Millimeters{v});
    }
    if (args.contains("word_spacing_mm")) {
        if (auto s = number("word_spacing_mm", -20.0, 100.0, v); !s.ok) {
            return s;
        }
        t.word_spacing = to_micrometers(Millimeters{v});
    }
    if (args.contains("line_spacing")) {
        if (auto s = number("line_spacing", 0.5, 5.0, v); !s.ok) {
            return s;
        }
        t.line_spacing = v;
    }
    if (args.contains("density_mm")) {
        if (auto s = number("density_mm", 0.2, 2.0, v); !s.ok) {
            return s;
        }
        t.density = to_micrometers(Millimeters{v});
    }
    if (args.contains("max_satin_width_mm")) {
        if (auto s = number("max_satin_width_mm", 1.0, 20.0, v); !s.ok) {
            return s;
        }
        t.max_satin_width = to_micrometers(Millimeters{v});
    }
    if (args.contains("rotation_deg")) {
        if (auto s = number("rotation_deg", -360.0, 360.0, v); !s.ok) {
            return s;
        }
        t.rotation = Angle{v * std::numbers::pi / 180.0};
    }
    if (args.contains("kerning")) {
        t.kerning = args.at("kerning").get<bool>();
    }
    if (args.contains("align")) {
        const auto a = args.at("align").get<std::string>();
        if (a == "left") {
            t.align = document::TextAlign::Left;
        } else if (a == "center") {
            t.align = document::TextAlign::Center;
        } else if (a == "right") {
            t.align = document::TextAlign::Right;
        } else {
            return out_of_range("align « " + a + " » inconnu", "valeurs : left, center, right");
        }
    }
    if (args.contains("fill")) {
        const auto f = args.at("fill").get<std::string>();
        if (f == "auto") {
            t.fill = document::TextFill::Auto;
        } else if (f == "satin") {
            t.fill = document::TextFill::Satin;
        } else if (f == "tatami") {
            t.fill = document::TextFill::Tatami;
        } else if (f == "contour") {
            t.fill = document::TextFill::Contour;
        } else {
            return out_of_range("fill « " + f + " » inconnu",
                                "valeurs : auto, satin, tatami, contour");
        }
    }
    if (args.contains("rgb")) {
        const auto parsed = rgb_from_hex(args.at("rgb").get<std::string>());
        if (!parsed) {
            return out_of_range("rgb invalide", "format attendu : #RRGGBB");
        }
        t.rgb = *parsed;
    }
    if (args.contains("origin_mm")) {
        Vec2um origin;
        if (!read_point(args.at("origin_mm"), origin)) {
            return out_of_range("origin_mm : [x, y] en mm (|coord| <= 1000) attendu");
        }
        t.origin = origin;
        originGiven = true;
    }
    return OpStatus::success();
}

OpStatus load_font(const std::string& file, int faceIndex, std::optional<lettering::Font>& out) {
    std::error_code ec;
    if (file.empty()) {
        return OpStatus::failure(
            code::kUsage, "font_file obligatoire : chemin d'un fichier de police TrueType/OpenType",
            "les polices intégrées de l'application ne sont pas accessibles en ligne de commande");
    }
    if (!fs::is_regular_file(fs::path(file), ec)) {
        return OpStatus::failure(code::kIo, "fichier de police introuvable : " + file);
    }
    auto font = lettering::Font::from_file(fs::path(file), faceIndex);
    if (!font) {
        return OpStatus::failure(code::kRefused, "police illisible : " + font.error().message,
                                 "attendu : un fichier TrueType ou OpenType valide");
    }
    out.emplace(std::move(*font));
    return OpStatus::success();
}

json warnings_json(const std::vector<lettering::TextWarning>& warnings) {
    json out = json::array();
    for (const auto& w : warnings) {
        out.push_back({{"code", w.code}, {"message", w.message}});
    }
    return out;
}

// Construit les lettres et applique le texte (création ou remplacement).
OpStatus apply_text(OpContext& ctx, document::TextObject text, const lettering::Font& font,
                    bool isNew) {
    lettering::BuildOptions buildOptions;
    buildOptions.layout = lettering::layout_options_for(text);
    auto built = lettering::build_text_objects(font, text, ctx.project.object_ids, buildOptions);
    if (!built) {
        return OpStatus::failure(code::kRefused, "lettrage impossible : " + built.error().message);
    }
    if (built->vectors.empty()) {
        return OpStatus::failure(
            code::kRefused,
            "aucune lettre à broder : le texte est vide ou la police n'en contient aucune");
    }
    json letters = json::array();
    for (const auto& e : built->embroideries) {
        letters.push_back(e.id.value);
    }
    json vectorIds = json::array();
    for (const auto& v : built->vectors) {
        vectorIds.push_back(v.id.value);
    }
    const std::size_t count = built->layout.glyphs.size();
    json warnings = warnings_json(built->warnings);
    ctx.stack.execute(std::make_unique<commands::SetTextObjectCommand>(
                          text, std::move(built->vectors), std::move(built->embroideries),
                          isNew ? "Créer un texte" : "Modifier le texte"),
                      ctx.project);
    return OpStatus::success({{"id", text.id.value},
                              {"letters", count},
                              {"embroidery_ids", letters},
                              {"vector_ids", vectorIds},
                              {"warnings", warnings}});
}

std::vector<ArgSpec> text_style_args() {
    return {
        arg("text", "string", "texte UTF-8 ('\\n' sépare les lignes)", false, {},
            "1 à 2000 caractères"),
        arg("cap_height_mm", "number", "hauteur de capitale (défaut 10)", false, "mm", "[1 ; 500]"),
        arg("letter_spacing_mm", "number", "espacement ajouté entre lettres", false, "mm",
            "[-20 ; 50]"),
        arg("word_spacing_mm", "number", "espacement ajouté aux espaces", false, "mm",
            "[-20 ; 100]"),
        arg("line_spacing", "number", "interligne en multiple de la capitale (défaut 1.5)", false,
            {}, "[0.5 ; 5]"),
        arg("kerning", "boolean", "crénage de la police (défaut true)"),
        arg("align", "string", "left | center | right", false, {}, "left | center | right"),
        arg("origin_mm", "array", "point de la ligne de base [x,y] (défaut [0,0])", false, "mm",
            "|coord| <= 1000"),
        arg("rotation_deg", "number", "rotation du texte autour de l'origine", false, "deg",
            "[-360 ; 360]"),
        arg("rgb", "string", "couleur de fil #RRGGBB (défaut #000000)"),
        arg("fill", "string", "auto | satin | tatami | contour (défaut auto)", false, {},
            "auto | satin | tatami | contour"),
        arg("density_mm", "number", "écart des traversées satin / rangées tatami (défaut 0.4)",
            false, "mm", "[0.2 ; 2]"),
        arg("max_satin_width_mm", "number", "au-delà, la lettre passe en tatami (défaut 6)", false,
            "mm", "[1 ; 20]")};
}

OpSpec op_add_text() {
    OpSpec s;
    s.name = "add_text";
    s.summary = "Crée un texte brodé (lettrage) : une lettre = un objet vectoriel + un objet de "
                "broderie éditables. `id` du résultat = identifiant de l'objet TEXTE.";
    s.args = text_style_args();
    s.args.push_back(arg("font_file", "string", "fichier de police TrueType/OpenType", true));
    s.args.push_back(arg("face_index", "integer", "visage d'une collection .ttc (défaut 0)"));
    for (auto& a : s.args) {
        if (a.name == "text") {
            a.required = true;
        }
    }
    s.example =
        R"({"op":"add_text","as":"titre","text":"Bonjour","font_file":"resources/fonts/VeraBd.ttf",)"
        R"("cap_height_mm":12,"align":"center","rgb":"#204080"})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        document::TextObject text;
        text.rgb = {0, 0, 0};
        bool originGiven = false;
        if (auto st = read_text_fields(ctx.args, text, originGiven); !st.ok) {
            return st;
        }
        const std::string file = ctx.args.at("font_file").get<std::string>();
        const int face = ctx.args.value("face_index", 0);
        std::optional<lettering::Font> font;
        if (auto st = load_font(file, face, font); !st.ok) {
            return st;
        }
        text.id = ctx.project.object_ids.next();
        text.font.family = font->family_name();
        text.font.file = fs::absolute(fs::path(file)).string();
        text.font.face_index = face;
        return apply_text(ctx, std::move(text), *font, true);
    };
    return s;
}

OpSpec op_set_text() {
    OpSpec s;
    s.name = "set_text";
    s.summary =
        "Modifie un texte existant (mise à jour partielle) : les lettres sont régénérées "
        "en un seul pas d'annulation, depuis leur position visible si origin_mm est absent.";
    s.args = text_style_args();
    s.args.insert(s.args.begin(),
                  arg("text_object", "object_id", "identifiant de l'objet texte", true));
    s.args.push_back(
        arg("font_file", "string", "autre fichier de police (défaut : celui du texte)"));
    s.args.push_back(arg("face_index", "integer", "visage d'une collection .ttc"));
    s.example = R"({"op":"set_text","text_object":12,"text":"Salut","cap_height_mm":15})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        const ObjectId id{ctx.args.at("text_object").get<std::uint64_t>()};
        const document::TextObject* existing = ctx.project.findText(id);
        if (existing == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "objet texte " + std::to_string(id.value) + " introuvable",
                                     "voir `project inspect --json` (text_objects)");
        }
        const document::TextObject before = *existing;
        document::TextObject text = before;
        bool originGiven = false;
        if (auto st = read_text_fields(ctx.args, text, originGiven); !st.ok) {
            return st;
        }
        std::string file = before.font.file;
        int face = before.font.face_index;
        if (ctx.args.contains("font_file")) {
            file = ctx.args.at("font_file").get<std::string>();
        }
        if (ctx.args.contains("face_index")) {
            face = ctx.args.at("face_index").get<int>();
        }
        std::optional<lettering::Font> font;
        if (auto st = load_font(file, face, font); !st.ok) {
            if (file.empty()) {
                return OpStatus::failure(
                    code::kUsage, "ce texte utilise une police intégrée : fournissez font_file",
                    "chemin d'un fichier TrueType/OpenType");
            }
            return st;
        }
        if (ctx.args.contains("font_file") || ctx.args.contains("face_index")) {
            text.font.family = font->family_name();
            text.font.file = fs::absolute(fs::path(file)).string();
            text.font.builtin.clear();
            text.font.face_index = face;
        }
        if (!originGiven) {
            // Les lettres ont pu être déplacées : on repart de leur position visible.
            if (const auto moved = lettering::text_displacement(
                    ctx.project, before, *font, lettering::layout_options_for(before))) {
                text.origin = text.origin + *moved;
            }
        }
        return apply_text(ctx, std::move(text), *font, false);
    };
    return s;
}

OpSpec op_remove_text() {
    OpSpec s;
    s.name = "remove_text";
    s.summary = "Supprime un texte et toutes ses lettres (vecteurs et broderies).";
    s.args = {arg("text_object", "object_id", "identifiant de l'objet texte", true)};
    s.example = R"({"op":"remove_text","text_object":12})";
    s.apply = [](OpContext& ctx) -> OpStatus {
        const ObjectId id{ctx.args.at("text_object").get<std::uint64_t>()};
        if (ctx.project.findText(id) == nullptr) {
            return OpStatus::failure(code::kUnknownObject,
                                     "objet texte " + std::to_string(id.value) + " introuvable",
                                     "voir `project inspect --json` (text_objects)");
        }
        ctx.stack.execute(std::make_unique<commands::RemoveTextObjectCommand>(id), ctx.project);
        return OpStatus::success({{"id", id.value}});
    };
    return s;
}

// --- set_source_image ---------------------------------------------------------------------

// Remplace l'image source et la résolution de travail, rien d'autre : objets, vecteurs et
// identifiants ne sont pas touchés. Annulable en un pas (état précédent restitué à l'identique).
class SetSourceImageCommand final : public commands::ICommand {
public:
    SetSourceImageCommand(image::Image image, double mm_per_px)
        : image_(std::move(image)), mm_per_px_(mm_per_px) {}

    void apply(document::Project& project) override {
        previous_image_ = std::move(project.original);
        previous_mm_per_px_ = project.mm_per_px;
        project.original = image_;
        project.mm_per_px = Millimeters{mm_per_px_};
    }
    void revert(document::Project& project) override {
        project.original = std::move(previous_image_);
        project.mm_per_px = previous_mm_per_px_;
        previous_image_ = image::Image{};
    }
    [[nodiscard]] std::string name() const override { return "Définir l'image source"; }

private:
    image::Image image_;
    double mm_per_px_;
    image::Image previous_image_;
    Millimeters previous_mm_per_px_{};
};

} // namespace

OpStatus load_source_image(const document::Project& project, const std::string& path,
                           std::optional<double> mm_per_px, SourceImagePlan& out) {
    if (path.empty()) {
        return OpStatus::failure(code::kUsage, "chemin de l'image source vide");
    }
    const fs::path file(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return OpStatus::failure(code::kIo, "image source introuvable : " + path,
                                 "chemin relatif au dossier courant ou absolu (PNG, JPEG, BMP, "
                                 "TIFF)");
    }
    auto loaded = image::load_image(file);
    if (!loaded || loaded->empty()) {
        return OpStatus::failure(code::kRefused,
                                 "image source illisible : " +
                                     (loaded ? std::string("image vide") : loaded.error().message),
                                 "formats pris en charge : PNG, JPEG, BMP, TIFF");
    }
    double scale = 0.0;
    if (mm_per_px) {
        scale = *mm_per_px;
        if (!std::isfinite(scale) || scale < 0.001 || scale > 100.0) {
            return out_of_range("mm_per_px = " + std::to_string(scale) +
                                " hors plage [0.001 ; 100]");
        }
    } else {
        // L'image remplit le cadre sans déformation : plus grande échelle telle que l'image tienne
        // dans les deux dimensions. Comparaison en entiers (µm x px), une seule division finale.
        const std::int64_t cw = project.canvas.width.value;
        const std::int64_t ch = project.canvas.height.value;
        const std::int64_t w = loaded->width;
        const std::int64_t h = loaded->height;
        if (cw <= 0 || ch <= 0) {
            return OpStatus::failure(code::kRefused, "le cadre du projet est de taille nulle",
                                     "définir le cadre par set_canvas ou passer mm_per_px");
        }
        scale = cw * h <= ch * w ? static_cast<double>(cw) / (static_cast<double>(w) * 1000.0)
                                 : static_cast<double>(ch) / (static_cast<double>(h) * 1000.0);
    }
    out.image = std::move(*loaded);
    out.mm_per_px = scale;
    return OpStatus::success();
}

OpStatus apply_source_image(OpContext& ctx, SourceImagePlan&& plan) {
    json result{{"width_px", plan.image.width},
                {"height_px", plan.image.height},
                {"mm_per_px", plan.mm_per_px},
                {"width_mm", plan.image.width * plan.mm_per_px},
                {"height_mm", plan.image.height * plan.mm_per_px}};
    ctx.stack.execute(
        std::make_unique<SetSourceImageCommand>(std::move(plan.image), plan.mm_per_px),
        ctx.project);
    return OpStatus::success(std::move(result));
}

namespace {

OpStatus set_source_image(OpContext& ctx) {
    const auto& path = ctx.args.at("path");
    if (!path.is_string()) {
        return OpStatus::failure(code::kUsage, "argument « path » : chaîne attendue");
    }
    std::optional<double> mm_per_px;
    if (const auto it = ctx.args.find("mm_per_px"); it != ctx.args.end() && !it->is_null()) {
        if (!it->is_number()) {
            return OpStatus::failure(code::kUsage,
                                     "argument « mm_per_px » : un nombre est attendu");
        }
        mm_per_px = it->get<double>();
    }
    SourceImagePlan plan;
    if (auto st = load_source_image(ctx.project, path.get<std::string>(), mm_per_px, plan);
        !st.ok) {
        return st;
    }
    return apply_source_image(ctx, std::move(plan));
}

} // namespace

void register_import_ops(std::vector<OpSpec>& out) {
    OpSpec src;
    src.name = "set_source_image";
    src.summary =
        "Définit l'image source du projet (et sa résolution) sans toucher aux objets : permet de "
        "mesurer la fidélité d'un projet construit à la main (import_svg, semantic_import). "
        "Annulable.";
    src.args = {arg("path", "string", "image PNG, JPEG, BMP ou TIFF", true),
                arg("mm_per_px", "number",
                    "taille d'un pixel (défaut : l'image remplit le cadre du projet sans "
                    "déformation)",
                    false, "mm", "[0.001 ; 100]")};
    src.example = R"({"op":"set_source_image","path":"source.png"})";
    src.apply = set_source_image;
    out.push_back(std::move(src));

    OpSpec svg;
    svg.name = "import_svg";
    svg.summary = "Importe les formes d'un fichier SVG comme objets vectoriels et de broderie "
                  "éditables (tracé direct, sans image ni segmentation).";
    svg.args = {
        arg("file", "string", "fichier .svg (path, rect, circle, ellipse, line, polyline, polygon)",
            true),
        arg("stitch_type", "string", "auto (tatami/contour selon la taille) | tatami | running",
            false, {}, "auto | tatami | running"),
        arg("rgb", "string",
            "couleur de fil #RRGGBB (défaut #5078C8) ; les couleurs du SVG ne sont pas reprises"),
        arg("name", "string", "préfixe du nom des objets (défaut « Import SVG »)", false, {},
            "1 à 128 caractères")};
    svg.example = R"({"op":"import_svg","file":"dessin.svg","stitch_type":"auto","rgb":"#336699"})";
    svg.apply = import_svg;
    out.push_back(std::move(svg));
    out.push_back(op_add_text());
    out.push_back(op_set_text());
    out.push_back(op_remove_text());
}

} // namespace openstitch::project_ops::detail
