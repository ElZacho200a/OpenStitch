// SPDX-License-Identifier: Apache-2.0
#include "action_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>

#include "fade_service.hpp"
#include "openstitch/document/machine_profile.hpp"

namespace openstitch::desktop_v2 {

namespace {

// --- Petites briques JSON (texte libre échappé, nombres à précision fixe) --------------------
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

std::string wrap(const std::string& ops) {
    return R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}";
}

double number(const Values& v, const char* key) {
    return std::strtod(v.at(key).c_str(), nullptr);
}

bool flag(const Values& v, const char* key) {
    return v.at(key) == "1" || v.at(key) == "true";
}

std::string perObject(const ActionContext& c, const std::string& op, const std::string& extra) {
    std::string ops;
    for (std::size_t i = 0; i < c.ids.size(); ++i) {
        ops += std::string(i == 0 ? "" : ",") + R"({"op":")" + op + R"(","object":)" +
               std::to_string(c.ids[i]) + extra + "}";
    }
    return wrap(ops);
}

std::string idList(const ActionContext& c) {
    std::string out = "[";
    for (std::size_t i = 0; i < c.ids.size(); ++i) {
        out += (i == 0 ? "" : ",") + std::to_string(c.ids[i]);
    }
    return out + "]";
}

FieldDef numberField(std::string key, std::string label, double min, double max, std::string def,
                     std::string unit = "") {
    FieldDef f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.kind = FieldKind::Number;
    f.min = min;
    f.max = max;
    f.def = std::move(def);
    f.unit = std::move(unit);
    return f;
}

FieldDef intField(std::string key, std::string label, int min, int max, std::string def) {
    FieldDef f = numberField(std::move(key), std::move(label), min, max, std::move(def));
    f.kind = FieldKind::Integer;
    return f;
}

FieldDef boolField(std::string key, std::string label, bool def) {
    FieldDef f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.kind = FieldKind::Bool;
    f.def = def ? "1" : "0";
    return f;
}

FieldDef choiceField(std::string key, std::string label, std::vector<Choice> choices) {
    FieldDef f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.kind = FieldKind::Choice;
    f.def = choices.front().value;
    f.choices = std::move(choices);
    return f;
}

FieldDef textField(std::string key, std::string label, std::string def, bool required) {
    FieldDef f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.kind = FieldKind::Text;
    f.def = std::move(def);
    f.required = required;
    return f;
}

FieldDef fileField(std::string key, std::string label, std::string def) {
    FieldDef f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.kind = FieldKind::File;
    f.def = std::move(def);
    f.required = true;
    return f;
}

// Réglages typographiques communs à « Ajouter du texte » et « Modifier le texte » : mêmes champs,
// mêmes bornes que les arguments des opérations `add_text` / `set_text` du moteur.
std::vector<FieldDef> textStyleFields() {
    return {
        numberField("letter_spacing_mm", "Espacement des lettres", -20, 50, "0", "mm"),
        numberField("word_spacing_mm", "Espacement des mots", -20, 100, "0", "mm"),
        numberField("line_spacing", "Interligne (× hauteur de capitale)", 0.5, 5, "1.5"),
        boolField("kerning", "Crénage de la police", true),
        numberField("density_mm", "Densité (écart des traversées)", 0.2, 2, "0.4", "mm"),
        numberField("max_satin_width_mm", "Largeur satin maximale", 1, 20, "6", "mm"),
        choiceField("base", "Base du texte",
                    {{"line", "Ligne droite"}, {"arc", "Arc de cercle"}, {"circle", "Cercle"}}),
        numberField("base_radius_mm", "Rayon de l'arc ou du cercle", 1, 1000, "50", "mm"),
        boolField("base_inside", "Texte en dessous (sommets vers le centre)", false),
        choiceField("letter_connector", "Entre les lettres",
                    {{"inherit", "Règle générale"}, {"trim", "Couper"}, {"no_trim", "Sans coupe"}}),
        choiceField(
            "word_connector", "Entre les mots",
            {{"inherit", "Règle générale"}, {"trim", "Couper"}, {"no_trim", "Sans coupe"}})};
}

std::string textStyleJson(const Values& v) {
    return R"(,"letter_spacing_mm":)" + num(number(v, "letter_spacing_mm")) +
           R"(,"word_spacing_mm":)" + num(number(v, "word_spacing_mm")) + R"(,"line_spacing":)" +
           num(number(v, "line_spacing")) + R"(,"kerning":)" +
           (flag(v, "kerning") ? "true" : "false") + R"(,"density_mm":)" +
           num(number(v, "density_mm")) + R"(,"max_satin_width_mm":)" +
           num(number(v, "max_satin_width_mm")) + R"(,"base":")" + v.at("base") +
           R"(","base_radius_mm":)" + num(number(v, "base_radius_mm")) + R"(,"base_inside":)" +
           (flag(v, "base_inside") ? "true" : "false") + R"(,"letter_connector":")" +
           v.at("letter_connector") + R"(","word_connector":")" + v.at("word_connector") + "\"";
}

std::string envVar(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

// Police par défaut : variable d'environnement, puis chemin de développement compilé, puis dossier
// `resources/fonts` du répertoire courant. Vide si introuvable (l'utilisateur choisit alors un
// fichier).
std::string defaultFont() {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (const std::string dir = envVar("OPENSTITCH_FONTS_DIR"); !dir.empty()) {
        candidates.push_back(fs::path(dir) / "VeraBd.ttf");
    }
#ifdef OPENSTITCH_DEFAULT_FONT
    candidates.emplace_back(OPENSTITCH_DEFAULT_FONT);
#endif
    candidates.push_back(fs::path("resources") / "fonts" / "VeraBd.ttf");
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            return c.string();
        }
    }
    return {};
}

std::vector<ActionDef> makeCatalog() {
    std::vector<ActionDef> out;

    {
        ActionDef a;
        a.id = "file.import_svg";
        a.label = "Importer un SVG…";
        a.group = "Fichier";
        a.fields = {
            fileField("file", "Fichier SVG", ""),
            choiceField("stitch_type", "Type de points",
                        {{"auto", "Automatique"}, {"tatami", "Tatami"}, {"running", "Contour"}})};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"import_svg","file":)" + quote(v.at("file")) +
                        R"(,"stitch_type":")" + v.at("stitch_type") + "\"}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "file.import_dxf";
        a.label = "Importer un DXF…";
        a.group = "Fichier";
        a.fields = {fileField("file", "Fichier DXF", "")};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"import_dxf","file":)" + quote(v.at("file")) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "text.add";
        a.label = "Ajouter du texte…";
        a.group = "Texte";
        a.fields = {
            textField("text", "Texte", "OpenStitch", true),
            numberField("cap_height_mm", "Hauteur des capitales", 1, 500, "12", "mm"),
            choiceField("fill", "Remplissage",
                        {{"auto", "Automatique"},
                         {"satin", "Satin"},
                         {"tatami", "Tatami"},
                         {"contour", "Contour"}}),
            choiceField("align", "Alignement",
                        {{"center", "Centré"}, {"left", "À gauche"}, {"right", "À droite"}}),
            numberField("rotation_deg", "Rotation", -360, 360, "0", "°"),
            fileField("font_file", "Police (TTF / OTF)", defaultFont())};
        for (auto& f : textStyleFields()) {
            a.fields.push_back(std::move(f));
        }
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"add_text","text":)" + quote(v.at("text")) +
                        R"(,"cap_height_mm":)" + num(number(v, "cap_height_mm")) + R"(,"fill":")" +
                        v.at("fill") + R"(","align":")" + v.at("align") + R"(","rotation_deg":)" +
                        num(number(v, "rotation_deg")) + R"(,"font_file":)" +
                        quote(v.at("font_file")) + textStyleJson(v) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "canvas.set";
        a.label = "Cadre de broderie…";
        a.group = "Production";
        a.fields = {numberField("width_mm", "Largeur", 1, 2000, "100", "mm"),
                    numberField("height_mm", "Hauteur", 1, 2000, "100", "mm"),
                    numberField("margin_mm", "Marge de sécurité", 0, 500, "0", "mm"),
                    choiceField("shape", "Forme",
                                {{"rectangle", "Rectangle"},
                                 {"ellipse", "Ellipse"},
                                 {"rounded", "Rectangle arrondi"}})};
        a.build = [](const Values& v, const ActionContext&, std::string& error) {
            const double margin = number(v, "margin_mm");
            if (2.0 * margin >= std::min(number(v, "width_mm"), number(v, "height_mm"))) {
                error = "La marge doit rester inférieure à la moitié du plus petit côté.";
                return std::string();
            }
            return wrap(R"({"op":"set_canvas","width_mm":)" + num(number(v, "width_mm")) +
                        R"(,"height_mm":)" + num(number(v, "height_mm")) + R"(,"margin_mm":)" +
                        num(margin) + R"(,"shape":")" + v.at("shape") + "\"}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "machine.set";
        a.label = "Machine…";
        a.group = "Production";
        std::vector<Choice> machines;
        for (const auto& m : document::machine_profiles()) {
            machines.push_back({std::string(m.id), std::string(m.name)});
        }
        a.fields = {choiceField("machine", "Machine", std::move(machines))};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"set_machine","machine":)" + quote(v.at("machine")) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "satin.auto";
        a.label = "Convertir en satin automatique…";
        a.group = "Satin";
        a.needsSelection = true;
        a.fields = {numberField("spacing_mm", "Écart des points", 0.1, 1.5, "0.4", "mm"),
                    choiceField("strategy", "Stratégie",
                                {{"skeleton", "Squelette"},
                                 {"parallel_sweep", "Balayage parallèle"},
                                 {"medial_axis", "Axe médian"},
                                  {"guided_satin", "Satin guidé"}})};
        a.build = [](const Values& v, const ActionContext& c, std::string&) {
            return perObject(c, "auto_satin",
                             R"(,"spacing_mm":)" + num(number(v, "spacing_mm")) +
                                 R"(,"strategy":")" + v.at("strategy") + "\"");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "fill.guides";
        a.label = "Guides de direction automatiques";
        a.group = "Remplissage";
        a.needsSelection = true;
        a.build = [](const Values&, const ActionContext& c, std::string&) {
            return perObject(c, "directional_guides", "");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "image.denoise";
        a.label = "Réduire le bruit de l'image…";
        a.group = "Image";
        a.fields = {
            choiceField("method", "Méthode",
                        {{"median", "Médian"}, {"bilateral", "Bilatéral"}, {"flatten", "Aplatir"}}),
            intField("strength", "Force", 1, 3, "1")};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"image_denoise","method":")" + v.at("method") +
                        R"(","strength":)" +
                        std::to_string(static_cast<int>(number(v, "strength"))) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "image.segment";
        a.label = "Segmenter l'image…";
        a.group = "Image";
        a.fields = {intField("max_colors", "Couleurs maximum", 2, 64, "8"),
                    intField("min_region_px", "Taille minimale d'une région (px)", 1, 100000, "16"),
                    intField("smoothing_radius_px", "Lissage (px)", 0, 10, "3")};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"segment_image","max_colors":)" +
                        std::to_string(static_cast<int>(number(v, "max_colors"))) +
                        R"(,"min_region_px":)" +
                        std::to_string(static_cast<int>(number(v, "min_region_px"))) +
                        R"(,"smoothing_radius_px":)" +
                        std::to_string(static_cast<int>(number(v, "smoothing_radius_px"))) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "finishing.set";
        a.label = "Finitions (coupes, arrêts, ponts cachés)…";
        a.group = "Production";
        a.fields = {boolField("enabled", "Finitions activées", true),
                    numberField("trim_threshold_mm", "Seuil de coupe", 0.5, 100, "3", "mm"),
                    choiceField("lock", "Arrêt de couture",
                                {{"backforth", "Aller-retour"},
                                 {"triangle", "Triangle"},
                                 {"zigzag", "Zigzag"},
                                 {"none", "Aucun"}}),
                    boolField("hidden_travel", "Ponts cachés (au lieu de sauts)", false)};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"set_finishing","enabled":)" +
                        std::string(flag(v, "enabled") ? "true" : "false") +
                        R"(,"trim_threshold_mm":)" + num(number(v, "trim_threshold_mm")) +
                        R"(,"lock":")" + v.at("lock") + R"(","hidden_travel":)" +
                        std::string(flag(v, "hidden_travel") ? "true" : "false") + "}");
        };
        out.push_back(std::move(a));
    }

    // --- Formes (géométrie vectorielle) ---------------------------------------------------
    {
        ActionDef a;
        a.id = "shape.merge";
        a.label = "Fusionner les formes…";
        a.group = "Formes";
        a.needsSelection = true;
        a.fields = {choiceField("mode", "Opération",
                                {{"union", "Union"},
                                 {"subtract", "Soustraction"},
                                 {"intersect", "Intersection"},
                                 {"exclude", "Exclusion"}})};
        a.build = [](const Values& v, const ActionContext& c, std::string& error) {
            if (c.ids.size() < 2) {
                error = "Sélectionnez au moins deux formes (la dernière est la principale).";
                return std::string();
            }
            return wrap(R"({"op":"merge","objects":)" + idList(c) + R"(,"mode":")" + v.at("mode") +
                        "\"}");
        };
        out.push_back(std::move(a));
    }
    for (const char* kind : {"simplify", "smooth"}) {
        ActionDef a;
        const bool simplify = std::string(kind) == "simplify";
        a.id = std::string("shape.") + kind;
        a.label = simplify ? "Simplifier les contours…" : "Lisser les contours…";
        a.group = "Formes";
        a.needsSelection = true;
        a.fields = {
            numberField("tolerance_mm", "Tolérance", 0.005, 5, simplify ? "0.1" : "0.15", "mm")};
        a.build = [simplify](const Values& v, const ActionContext& c, std::string&) {
            return wrap(std::string(R"({"op":")") +
                        (simplify ? "simplify_shapes" : "smooth_shapes") + R"(","objects":)" +
                        idList(c) + R"(,"tolerance_mm":)" + num(number(v, "tolerance_mm")) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "shape.fill_holes";
        a.label = "Combler les trous…";
        a.group = "Formes";
        a.needsSelection = true;
        a.fields = {numberField("max_area_mm2", "Aire maximale des trous comblés", 0.0001, 1000000,
                                "4", "mm²")};
        a.build = [](const Values& v, const ActionContext& c, std::string&) {
            return wrap(R"({"op":"fill_small_holes","objects":)" + idList(c) +
                        R"(,"max_area_mm2":)" + num(number(v, "max_area_mm2")) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "shape.remove_small";
        a.label = "Supprimer les petits morceaux…";
        a.group = "Formes";
        a.needsSelection = true;
        a.fields = {
            numberField("min_area_mm2", "Aire minimale conservée", 0.0001, 1000000, "1", "mm²")};
        a.build = [](const Values& v, const ActionContext& c, std::string&) {
            return wrap(R"({"op":"remove_small_pieces","objects":)" + idList(c) +
                        R"(,"min_area_mm2":)" + num(number(v, "min_area_mm2")) + "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "shape.break_apart";
        a.label = "Séparer les morceaux disjoints";
        a.group = "Formes";
        a.needsSelection = true;
        a.build = [](const Values&, const ActionContext& c, std::string&) {
            return perObject(c, "break_apart", "");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "shape.convert_vectors";
        a.label = "Numériser les formes vectorielles";
        a.group = "Formes";
        a.build = [](const Values&, const ActionContext&, std::string&) {
            return wrap(R"({"op":"convert_vectors"})");
        };
        out.push_back(std::move(a));
    }
    // --- Ordre et enchaînements -----------------------------------------------------------
    {
        ActionDef a;
        a.id = "order.optimize";
        a.label = "Optimiser l'ordre de broderie…";
        a.group = "Ordre de couture";
        a.fields = {choiceField("strategy", "Stratégie",
                                {{"color_then_proximity", "Couleur puis proximité"},
                                 {"by_color", "Par couleur"},
                                 {"by_proximity", "Par proximité"},
                                 {"layered", "Par couches"},
                                 {"document", "Ordre du document"}}),
                    boolField("improve", "Amélioration locale (2-opt)", true)};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"optimize_order","strategy":")" + v.at("strategy") +
                        R"(","improve":)" + std::string(flag(v, "improve") ? "true" : "false") +
                        "}");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "order.connector";
        a.label = "Coupe après l'objet…";
        a.group = "Ordre de couture";
        a.needsSelection = true;
        a.fields = {choiceField("mode", "Connecteur",
                                {{"inherit", "Règles du projet"},
                                 {"always_trim", "Toujours couper"},
                                 {"never_trim", "Ne jamais couper"}})};
        a.build = [](const Values& v, const ActionContext& c, std::string&) {
            return perObject(c, "set_connector", R"(,"mode":")" + v.at("mode") + "\"");
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "order.join";
        a.label = "Entrée / sortie automatiques…";
        a.group = "Ordre de couture";
        a.needsSelection = true;
        a.fields = {choiceField(
            "mode", "Mode",
            {{"inherit", "Règles du projet"}, {"auto", "Automatiques"}, {"off", "Désactivées"}})};
        a.build = [](const Values& v, const ActionContext& c, std::string&) {
            return perObject(c, "set_join", R"(,"mode":")" + v.at("mode") + "\"");
        };
        out.push_back(std::move(a));
    }
    // --- Segmentation ---------------------------------------------------------------------
    {
        ActionDef a;
        a.id = "image.reduce_colors";
        a.label = "Réduire la palette de l'image…";
        a.group = "Image";
        a.fields = {intField("max_colors", "Couleurs maximum", 1, 256, "6"),
                    boolField("merge_adjacent", "Fusionner les régions voisines", true)};
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"seg_reduce_colors","max_colors":)" +
                        std::to_string(static_cast<int>(number(v, "max_colors"))) +
                        R"(,"merge_adjacent":)" +
                        std::string(flag(v, "merge_adjacent") ? "true" : "false") + "}");
        };
        out.push_back(std::move(a));
    }

    // --- Fondu de deux fils (ICommand : `run`, pas de lot JSON) ----------------------------
    {
        ActionDef a;
        a.id = "fade.create";
        a.label = "Fondu de deux fils…";
        a.group = "Fondu";
        a.needsSelection = true;
        a.fields = {textField("foreground", "Fil du dessus (#RRGGBB)", "#FF8800", true),
                    boolField("complementary", "Rampes de densité opposées", true)};
        a.run = [](DocumentSession& session, const Values& v, const ActionContext& c) {
            const std::string& hex = v.at("foreground");
            unsigned channel[3] = {0, 0, 0};
            bool valid = hex.size() == 7 && hex[0] == '#';
            for (int i = 0; valid && i < 3; ++i) {
                const char* begin = hex.data() + 1 + 2 * i;
                const auto [end, ec] = std::from_chars(begin, begin + 2, channel[i], 16);
                valid = ec == std::errc() && end == begin + 2;
            }
            if (!valid) {
                return ServiceResult{false, "Fil du dessus : couleur au format #RRGGBB attendue."};
            }
            const unsigned r = channel[0];
            const unsigned g = channel[1];
            const unsigned b = channel[2];
            if (c.ids.size() != 1) {
                return ServiceResult{false, "Sélectionnez UN remplissage pour y créer un fondu."};
            }
            FadeSettings settings;
            settings.foreground = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                   static_cast<std::uint8_t>(b)};
            settings.complementary = flag(v, "complementary");
            return createFade(session, c.ids.front(), settings);
        };
        out.push_back(std::move(a));
    }
    {
        ActionDef a;
        a.id = "fade.remove";
        a.label = "Supprimer le fondu";
        a.group = "Fondu";
        a.needsSelection = true;
        a.run = [](DocumentSession& session, const Values&, const ActionContext& c) {
            if (c.ids.size() != 1) {
                return ServiceResult{false, "Sélectionnez UNE couche du fondu à supprimer."};
            }
            return removeFade(session, c.ids.front());
        };
        out.push_back(std::move(a));
    }
    return out;
}

} // namespace

const std::vector<ActionDef>& actionCatalog() {
    static const std::vector<ActionDef> kCatalog = makeCatalog();
    return kCatalog;
}

const ActionDef* findAction(const std::string& id) {
    for (const auto& a : actionCatalog()) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

Values defaultValues(const ActionDef& action) {
    Values v;
    for (const auto& f : action.fields) {
        v[f.key] = f.def;
    }
    return v;
}

bool validateAction(const ActionDef& action, Values& values, const ActionContext& context,
                    std::string& error) {
    error.clear();
    if (action.needsSelection && context.ids.empty()) {
        error = "Sélectionnez d'abord un ou plusieurs objets.";
        return false;
    }
    for (const auto& f : action.fields) {
        if (!values.contains(f.key)) {
            values[f.key] = f.def;
        }
        const std::string& text = values[f.key];
        switch (f.kind) {
        case FieldKind::Number:
        case FieldKind::Integer: {
            char* end = nullptr;
            const double x = std::strtod(text.c_str(), &end);
            if (text.empty() || end == text.c_str() || *end != '\0' || !std::isfinite(x) ||
                x < f.min - 1e-9 || x > f.max + 1e-9 ||
                (f.kind == FieldKind::Integer && std::abs(x - std::round(x)) > 1e-9)) {
                error = f.label + " : valeur entre " + num(f.min) + " et " + num(f.max) +
                        (f.kind == FieldKind::Integer ? " (entier)" : "") + " attendue.";
                return false;
            }
            break;
        }
        case FieldKind::Bool:
            if (text != "0" && text != "1" && text != "true" && text != "false") {
                error = f.label + " : oui ou non attendu.";
                return false;
            }
            break;
        case FieldKind::Choice:
            if (std::ranges::none_of(f.choices, [&](const Choice& c) { return c.value == text; })) {
                error = f.label + " : choix inconnu.";
                return false;
            }
            break;
        case FieldKind::Text:
        case FieldKind::File:
            if (f.required && text.empty()) {
                error = f.label + " : obligatoire.";
                return false;
            }
            if (f.kind == FieldKind::File) {
                std::error_code ec;
                if (!std::filesystem::exists(std::filesystem::path(text), ec)) {
                    error = f.label + " : fichier introuvable.";
                    return false;
                }
            }
            break;
        }
    }
    return true;
}

std::string buildAction(const ActionDef& action, Values values, const ActionContext& context,
                        std::string& error) {
    if (!validateAction(action, values, context, error)) {
        return {};
    }
    if (!action.build) {
        error = "Cette action s'exécute directement (pas de lot).";
        return {};
    }
    const std::string batch = action.build(values, context, error);
    if (batch.empty() && error.empty()) {
        error = "Action impossible avec les valeurs données.";
    }
    return batch;
}

std::optional<std::uint64_t> selectedTextOwner(const document::Project& project,
                                               const std::vector<std::uint64_t>& objectIds) {
    std::optional<std::uint64_t> owner;
    for (const auto id : objectIds) {
        const auto* emb = project.findEmbroidery(ObjectId{id});
        if (emb == nullptr) {
            return std::nullopt;
        }
        const auto* vec = project.findObject(emb->source_vector);
        if (vec == nullptr || !vec->text_owner || project.findText(*vec->text_owner) == nullptr) {
            return std::nullopt;
        }
        if (owner && *owner != vec->text_owner->value) {
            return std::nullopt;
        }
        owner = vec->text_owner->value;
    }
    return owner;
}

Values textEditValues(const document::Project& project, std::uint64_t textId) {
    Values v;
    const auto* t = project.findText(ObjectId{textId});
    if (t == nullptr) {
        return v;
    }
    const auto mm = [](Micrometers um) { return num(to_millimeters(um).value); };
    v["text"] = t->text;
    v["cap_height_mm"] = mm(t->cap_height);
    v["letter_spacing_mm"] = mm(t->letter_spacing);
    v["word_spacing_mm"] = mm(t->word_spacing);
    v["line_spacing"] = num(t->line_spacing);
    v["kerning"] = t->kerning ? "1" : "0";
    v["density_mm"] = mm(t->density);
    v["max_satin_width_mm"] = mm(t->max_satin_width);
    v["rotation_deg"] = num(t->rotation.radians * 180.0 / std::numbers::pi);
    switch (t->align) {
    case document::TextAlign::Center:
        v["align"] = "center";
        break;
    case document::TextAlign::Right:
        v["align"] = "right";
        break;
    default:
        v["align"] = "left";
        break;
    }
    switch (t->fill) {
    case document::TextFill::Satin:
        v["fill"] = "satin";
        break;
    case document::TextFill::Tatami:
        v["fill"] = "tatami";
        break;
    case document::TextFill::Contour:
        v["fill"] = "contour";
        break;
    default:
        v["fill"] = "auto";
        break;
    }
    switch (t->base.kind) {
    case document::TextBaseKind::Arc:
        v["base"] = "arc";
        break;
    case document::TextBaseKind::Circle:
        v["base"] = "circle";
        break;
    default:
        v["base"] = "line"; // Path n'a pas de champ ici : repli sur la ligne, voir la doc
        break;
    }
    v["base_radius_mm"] = mm(t->base.radius);
    v["base_inside"] = t->base.inside ? "1" : "0";
    const auto connector = [](document::ConnectorMode m) -> std::string {
        switch (m) {
        case document::ConnectorMode::AlwaysTrim:
            return "trim";
        case document::ConnectorMode::NeverTrim:
            return "no_trim";
        default:
            return "inherit";
        }
    };
    v["letter_connector"] = connector(t->letter_connector);
    v["word_connector"] = connector(t->word_connector);
    return v;
}

const ActionDef& textEditAction() {
    static const ActionDef def = [] {
        ActionDef a;
        a.id = "text.edit";
        a.label = "Modifier le texte…";
        a.group = "Texte";
        a.needsSelection = true;
        a.fields = {
            textField("text", "Texte", "OpenStitch", true),
            numberField("cap_height_mm", "Hauteur des capitales", 1, 500, "12", "mm"),
            choiceField("fill", "Remplissage",
                        {{"auto", "Automatique"},
                         {"satin", "Satin"},
                         {"tatami", "Tatami"},
                         {"contour", "Contour"}}),
            choiceField("align", "Alignement",
                        {{"center", "Centré"}, {"left", "À gauche"}, {"right", "À droite"}}),
            numberField("rotation_deg", "Rotation", -360, 360, "0", "°")};
        for (auto& f : textStyleFields()) {
            a.fields.push_back(std::move(f));
        }
        a.build = [](const Values& v, const ActionContext& c, std::string& error) {
            const auto owner =
                c.project != nullptr ? selectedTextOwner(*c.project, c.ids) : std::nullopt;
            if (!owner) {
                error = "Sélectionnez les lettres d'un seul texte.";
                return std::string();
            }
            return wrap(R"({"op":"set_text","text_object":)" + std::to_string(*owner) +
                        R"(,"text":)" + quote(v.at("text")) + R"(,"cap_height_mm":)" +
                        num(number(v, "cap_height_mm")) + R"(,"fill":")" + v.at("fill") +
                        R"(","align":")" + v.at("align") + R"(","rotation_deg":)" +
                        num(number(v, "rotation_deg")) + textStyleJson(v) + "}");
        };
        return a;
    }();
    return def;
}

namespace {

// Lettres du texte dans l'ordre des formes : une forme dont le nom finit par « (n) » prolonge la
// lettre précédente. Rend le rang de chaque forme (par identifiant de forme).
struct LetterRanks {
    std::vector<std::pair<std::uint64_t, std::uint32_t>> byVector; // (forme, rang)
    std::vector<std::string> letters;                              // rang -> lettre affichée
};

LetterRanks letterRanks(const document::Project& project, std::uint64_t textId) {
    LetterRanks out;
    for (const auto& vec : project.vector_objects) {
        if (!vec.text_owner || vec.text_owner->value != textId) {
            continue;
        }
        const auto& name = vec.name;
        bool continuation = false;
        if (name.size() > 4 && name.back() == ')') {
            const auto open = name.rfind(" (");
            continuation = open != std::string::npos && open + 3 < name.size() &&
                           std::all_of(name.begin() + static_cast<std::ptrdiff_t>(open) + 2,
                                       name.end() - 1, [](char c) { return c >= '0' && c <= '9'; });
        }
        if (!continuation || out.letters.empty()) {
            std::string letter = name.rfind("Lettre ", 0) == 0 ? name.substr(7) : name;
            out.letters.push_back(std::move(letter));
        }
        out.byVector.emplace_back(vec.id.value, static_cast<std::uint32_t>(out.letters.size() - 1));
    }
    return out;
}

// Nombre de lettres attendues (espaces exclus) ; faux si le texte sort du cas simple.
bool countExpectedLetters(const std::string& text, std::size_t& count) {
    count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        std::uint32_t cp = c;
        if (c >= 0xF0) {
            len = 4;
            cp = c & 0x07U;
        } else if (c >= 0xE0) {
            len = 3;
            cp = c & 0x0FU;
        } else if (c >= 0xC0) {
            len = 2;
            cp = c & 0x1FU;
        } else if (c >= 0x80) {
            return false; // octet de continuation isolé : texte invalide
        }
        if (i + len > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        i += len;
        if (cp >= 0x0300 && cp <= 0x036F) {
            return false; // accent combinant : le rang n'est plus celui des caractères
        }
        if (cp >= 0x0590 && cp <= 0x08FF) {
            return false; // écritures de droite à gauche : ordre visuel différent
        }
        if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) {
            return false; // caractères invisibles
        }
        if (cp > 0x20 && cp != 0x7F && cp != 0xA0) {
            ++count;
        }
    }
    return true;
}

std::string hexOf(const std::array<std::uint8_t, 3>& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c[0], c[1], c[2]);
    return buf;
}

bool parseHex(const std::string& s, std::array<std::uint8_t, 3>& out) {
    if (s.size() != 7 || s[0] != '#') {
        return false;
    }
    for (std::size_t i = 1; i < 7; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = static_cast<std::uint8_t>(std::stoi(s.substr(1 + 2 * i, 2), nullptr, 16));
    }
    return true;
}

// Retouches du texte, avec la lettre `index` remplacée par `edit` (ou retirée si `edit` est vide).
std::string glyphBatch(const document::TextObject& text, std::uint32_t index,
                       const std::optional<document::GlyphEdit>& edit) {
    std::vector<document::GlyphEdit> edits;
    for (const auto& e : text.glyph_edits) {
        if (e.index != index) {
            edits.push_back(e);
        }
    }
    if (edit) {
        edits.push_back(*edit);
    }
    std::sort(edits.begin(), edits.end(),
              [](const auto& a, const auto& b) { return a.index < b.index; });
    std::string list;
    for (const auto& e : edits) {
        list += std::string(list.empty() ? "" : ",") + R"({"index":)" + std::to_string(e.index) +
                R"(,"dx_mm":)" + num(to_millimeters(e.dx).value) + R"(,"dy_mm":)" +
                num(to_millimeters(e.dy).value) + R"(,"rotation_deg":)" +
                num(e.rotation.radians * 180.0 / std::numbers::pi) + R"(,"scale":)" + num(e.scale) +
                (e.rgb ? R"(,"rgb":")" + hexOf(*e.rgb) + "\"" : std::string()) + "}";
    }
    return wrap(R"({"op":"set_text","text_object":)" + std::to_string(text.id.value) +
                R"(,"glyph_edits":[)" + list + "]}");
}

} // namespace

GlyphTarget glyphTargetOf(const document::Project& project,
                          const std::vector<std::uint64_t>& objectIds) {
    GlyphTarget t;
    const auto refuse = [&t](std::string why) {
        t.ok = false;
        t.message = std::move(why);
        return t;
    };
    if (objectIds.empty()) {
        return refuse("Sélectionnez une lettre d'un texte.");
    }
    std::optional<std::uint64_t> owner;
    std::vector<std::uint64_t> vectors;
    for (const auto id : objectIds) {
        const auto* emb = project.findEmbroidery(ObjectId{id});
        const auto* vec = emb != nullptr ? project.findObject(emb->source_vector) : nullptr;
        if (vec == nullptr || !vec->text_owner || project.findText(*vec->text_owner) == nullptr) {
            return refuse("Sélectionnez une lettre d'un texte.");
        }
        if (owner && *owner != vec->text_owner->value) {
            return refuse("Sélectionnez les lettres d'un seul texte.");
        }
        owner = vec->text_owner->value;
        vectors.push_back(vec->id.value);
    }
    const auto* text = project.findText(ObjectId{*owner});
    std::size_t expected = 0;
    if (!countExpectedLetters(text->text, expected)) {
        return refuse("Ce texte contient des caractères particuliers (accents combinés, écriture "
                      "de droite à gauche, caractères invisibles) : l'édition lettre par lettre "
                      "n'est pas fiable, utilisez « Modifier le texte ».");
    }
    if (text->ligatures) {
        return refuse("Les ligatures fusionnent des lettres : désactivez-les pour retoucher une "
                      "lettre.");
    }
    const auto ranks = letterRanks(project, *owner);
    if (ranks.letters.size() != expected) {
        return refuse("Les formes du texte ne correspondent plus à ses lettres (formes "
                      "supprimées ou ajoutées à la main) : régénérez le texte avec « Modifier le "
                      "texte ».");
    }
    std::optional<std::uint32_t> rank;
    for (const auto vecId : vectors) {
        const auto it = std::find_if(ranks.byVector.begin(), ranks.byVector.end(),
                                     [&](const auto& p) { return p.first == vecId; });
        if (it == ranks.byVector.end()) {
            return refuse("Lettre introuvable dans le texte.");
        }
        if (rank && *rank != it->second) {
            return refuse("Sélectionnez les formes d'UNE seule lettre à la fois.");
        }
        rank = it->second;
    }
    t.ok = true;
    t.textId = *owner;
    t.index = *rank;
    t.letter = ranks.letters[*rank];
    t.hasEdit = std::any_of(text->glyph_edits.begin(), text->glyph_edits.end(),
                            [&](const auto& e) { return e.index == *rank; });
    return t;
}

Values glyphEditValues(const document::Project& project, const GlyphTarget& target) {
    Values v{{"dx_mm", "0"}, {"dy_mm", "0"}, {"rotation_deg", "0"}, {"scale", "1"}, {"color", ""}};
    const auto* text = project.findText(ObjectId{target.textId});
    if (text == nullptr) {
        return v;
    }
    for (const auto& e : text->glyph_edits) {
        if (e.index == target.index) {
            v["dx_mm"] = num(to_millimeters(e.dx).value);
            v["dy_mm"] = num(to_millimeters(e.dy).value);
            v["rotation_deg"] = num(e.rotation.radians * 180.0 / std::numbers::pi);
            v["scale"] = num(e.scale);
            v["color"] = e.rgb ? hexOf(*e.rgb) : "";
        }
    }
    return v;
}

const ActionDef& glyphEditAction() {
    static const ActionDef def = [] {
        ActionDef a;
        a.id = "text.glyph";
        a.label = "Modifier la lettre sélectionnée…";
        a.group = "Texte";
        a.needsSelection = true;
        a.fields = {numberField("dx_mm", "Décalage horizontal", -200, 200, "0", "mm"),
                    numberField("dy_mm", "Décalage vertical", -200, 200, "0", "mm"),
                    numberField("rotation_deg", "Rotation", -360, 360, "0", "°"),
                    numberField("scale", "Échelle", 0.1, 10, "1"),
                    textField("color", "Couleur (#RRGGBB, vide = celle du texte)", "", false)};
        a.build = [](const Values& v, const ActionContext& c, std::string& error) {
            const auto target =
                c.project != nullptr ? glyphTargetOf(*c.project, c.ids) : GlyphTarget{};
            if (!target.ok) {
                error =
                    target.message.empty() ? "Sélectionnez une lettre d'un texte." : target.message;
                return std::string();
            }
            document::GlyphEdit edit;
            edit.index = target.index;
            edit.dx = to_micrometers(Millimeters{number(v, "dx_mm")});
            edit.dy = to_micrometers(Millimeters{number(v, "dy_mm")});
            edit.rotation = Angle{number(v, "rotation_deg") * std::numbers::pi / 180.0};
            edit.scale = number(v, "scale");
            const std::string color = v.at("color");
            if (!color.empty()) {
                std::array<std::uint8_t, 3> rgb{};
                if (!parseHex(color, rgb)) {
                    error = "Couleur : format #RRGGBB attendu (ou laisser vide).";
                    return std::string();
                }
                edit.rgb = rgb;
            }
            const auto* text = c.project->findText(ObjectId{target.textId});
            const bool neutral = edit.dx.value == 0 && edit.dy.value == 0 &&
                                 std::fabs(edit.rotation.radians) < 1e-9 &&
                                 std::fabs(edit.scale - 1.0) < 1e-9 && !edit.rgb;
            const auto existing =
                std::find_if(text->glyph_edits.begin(), text->glyph_edits.end(),
                             [&](const auto& e) { return e.index == target.index; });
            if (existing == text->glyph_edits.end() ? neutral : *existing == edit) {
                error =
                    "Aucun changement : la lettre « " + target.letter + " » a déjà ces réglages.";
                return std::string();
            }
            return glyphBatch(*text, target.index,
                              neutral ? std::nullopt : std::optional<document::GlyphEdit>{edit});
        };
        return a;
    }();
    return def;
}

const ActionDef& glyphResetAction() {
    static const ActionDef def = [] {
        ActionDef a;
        a.id = "text.glyph.reset";
        a.label = "Rétablir la lettre sélectionnée";
        a.group = "Texte";
        a.needsSelection = true;
        a.build = [](const Values&, const ActionContext& c, std::string& error) {
            const auto target =
                c.project != nullptr ? glyphTargetOf(*c.project, c.ids) : GlyphTarget{};
            if (!target.ok) {
                error =
                    target.message.empty() ? "Sélectionnez une lettre d'un texte." : target.message;
                return std::string();
            }
            if (!target.hasEdit) {
                error = "La lettre « " + target.letter + " » n'a pas été retouchée.";
                return std::string();
            }
            return glyphBatch(*c.project->findText(ObjectId{target.textId}), target.index,
                              std::nullopt);
        };
        return a;
    }();
    return def;
}

const ActionDef& textRemoveAction() {
    static const ActionDef def = [] {
        ActionDef a;
        a.id = "text.remove";
        a.label = "Supprimer le texte";
        a.group = "Texte";
        a.needsSelection = true;
        a.build = [](const Values&, const ActionContext& c, std::string& error) {
            const auto owner =
                c.project != nullptr ? selectedTextOwner(*c.project, c.ids) : std::nullopt;
            if (!owner) {
                error = "Sélectionnez les lettres d'un seul texte.";
                return std::string();
            }
            return wrap(R"({"op":"remove_text","text_object":)" + std::to_string(*owner) + "}");
        };
        return a;
    }();
    return def;
}

const ActionDef* contextAction(const std::string& id) {
    if (id == "text.edit") {
        return &textEditAction();
    }
    if (id == "text.glyph") {
        return &glyphEditAction();
    }
    if (id == "text.glyph.reset") {
        return &glyphResetAction();
    }
    if (id == "text.remove") {
        return &textRemoveAction();
    }
    return nullptr;
}

} // namespace openstitch::desktop_v2
