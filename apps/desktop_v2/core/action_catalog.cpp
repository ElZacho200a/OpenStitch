// SPDX-License-Identifier: Apache-2.0
#include "action_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

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
        a.build = [](const Values& v, const ActionContext&, std::string&) {
            return wrap(R"({"op":"add_text","text":)" + quote(v.at("text")) +
                        R"(,"cap_height_mm":)" + num(number(v, "cap_height_mm")) + R"(,"fill":")" +
                        v.at("fill") + R"(","align":")" + v.at("align") + R"(","rotation_deg":)" +
                        num(number(v, "rotation_deg")) + R"(,"font_file":)" +
                        quote(v.at("font_file")) + "}");
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
                                 {"medial_axis", "Axe médian"}})};
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

} // namespace openstitch::desktop_v2
