// SPDX-License-Identifier: Apache-2.0
#include "action_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

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

// Police par défaut : variable d'environnement, puis chemin de développement compilé, puis dossier
// `resources/fonts` du répertoire courant. Vide si introuvable (l'utilisateur choisit alors un
// fichier).
std::string defaultFont() {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (const char* dir = std::getenv("OPENSTITCH_FONTS_DIR")) {
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

std::string buildAction(const ActionDef& action, Values values, const ActionContext& context,
                        std::string& error) {
    error.clear();
    if (action.needsSelection && context.ids.empty()) {
        error = "Sélectionnez d'abord un ou plusieurs objets.";
        return {};
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
                return {};
            }
            break;
        }
        case FieldKind::Bool:
            if (text != "0" && text != "1" && text != "true" && text != "false") {
                error = f.label + " : oui ou non attendu.";
                return {};
            }
            break;
        case FieldKind::Choice:
            if (std::ranges::none_of(f.choices, [&](const Choice& c) { return c.value == text; })) {
                error = f.label + " : choix inconnu.";
                return {};
            }
            break;
        case FieldKind::Text:
        case FieldKind::File:
            if (f.required && text.empty()) {
                error = f.label + " : obligatoire.";
                return {};
            }
            if (f.kind == FieldKind::File) {
                std::error_code ec;
                if (!std::filesystem::exists(std::filesystem::path(text), ec)) {
                    error = f.label + " : fichier introuvable.";
                    return {};
                }
            }
            break;
        }
    }
    const std::string batch = action.build(values, context, error);
    if (batch.empty() && error.empty()) {
        error = "Action impossible avec les valeurs données.";
    }
    return batch;
}

} // namespace openstitch::desktop_v2
