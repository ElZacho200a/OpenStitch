// SPDX-License-Identifier: Apache-2.0
#include "theme_package.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace openstitch::desktop_v2::theme {

namespace {

using json = nlohmann::json;
using Severity = Diagnostic::Severity;

void warn(std::vector<Diagnostic>& out, std::string where, std::string message) {
    out.push_back({Severity::Warning, std::move(where), std::move(message)});
}

void fail(std::vector<Diagnostic>& out, std::string where, std::string message) {
    out.push_back({Severity::Error, std::move(where), std::move(message)});
}

std::optional<std::string> readLimited(const std::filesystem::path& file, std::uintmax_t limit,
                                       std::vector<Diagnostic>& diagnostics,
                                       const std::string& where) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    if (size > limit) {
        fail(diagnostics, where, "fichier trop volumineux (" + std::to_string(size) + " octets)");
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        fail(diagnostics, where, "fichier illisible");
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::optional<json> parseJson(std::string_view text, std::vector<Diagnostic>& diagnostics,
                              const std::string& where) {
    try {
        auto value = json::parse(text.begin(), text.end(), nullptr, true, true);
        if (!value.is_object()) {
            fail(diagnostics, where, "le document doit être un objet JSON");
            return std::nullopt;
        }
        return value;
    } catch (const json::parse_error& e) {
        fail(diagnostics, where, "JSON invalide (octet " + std::to_string(e.byte) + ")");
        return std::nullopt;
    }
}

std::optional<std::string> optionalString(const json& doc, const char* key, std::size_t maxLength,
                                          std::vector<Diagnostic>& diagnostics) {
    const auto it = doc.find(key);
    if (it == doc.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        fail(diagnostics, std::string("manifest.json:") + key, "doit être un texte");
        return std::nullopt;
    }
    auto value = it->get<std::string>();
    if (value.size() > maxLength) {
        warn(diagnostics, std::string("manifest.json:") + key,
             "texte tronqué à " + std::to_string(maxLength) + " caractères");
        value.resize(maxLength);
    }
    return value;
}

bool isIconName(std::string_view name) {
    if (name.empty() || name.size() > 48) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    });
}

bool isPathData(std::string_view data) {
    if (data.empty() || data.size() > 4096) {
        return false;
    }
    constexpr std::string_view kAllowed = "MmLlHhVvCcSsQqTtAaZz0123456789.,-+eE \t\n";
    return data.find_first_not_of(kAllowed) == std::string_view::npos;
}

// Aplatit un objet JSON en clés « a.b.c ». Les clés « $… » sont des commentaires.
void flatten(const json& node, const std::string& prefix, std::map<std::string, RawScalar>& out,
             std::vector<Diagnostic>& diagnostics, const std::string& where, int depth) {
    if (depth > 5) {
        warn(diagnostics, where + ":" + prefix, "imbrication trop profonde, ignorée");
        return;
    }
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (it.key().empty() || it.key().front() == '$') {
            continue;
        }
        const auto key = prefix.empty() ? it.key() : prefix + "." + it.key();
        const auto& value = it.value();
        if (value.is_object()) {
            flatten(value, key, out, diagnostics, where, depth + 1);
        } else if (value.is_number()) {
            out[key] = value.get<double>();
        } else if (value.is_boolean()) {
            out[key] = value.get<bool>();
        } else if (value.is_string()) {
            out[key] = value.get<std::string>();
        } else if (value.is_array() && std::all_of(value.begin(), value.end(),
                                                   [](const json& v) { return v.is_string(); })) {
            // Liste de familles de polices : jointe par des virgules.
            std::string joined;
            for (const auto& v : value) {
                joined += (joined.empty() ? "" : ", ") + v.get<std::string>();
            }
            out[key] = joined;
        } else {
            warn(diagnostics, where + ":" + key, "valeur non prise en charge, ignorée");
        }
    }
}

void readTokens(const json& doc, ThemeLayer& layer, std::vector<Diagnostic>& diagnostics,
                const std::string& where) {
    json base = doc;
    if (const auto it = doc.find("variants"); it != doc.end()) {
        if (it->is_object()) {
            for (auto v = it->begin(); v != it->end(); ++v) {
                const auto appearance = parseAppearance(v.key());
                if (!appearance || !v->is_object()) {
                    warn(diagnostics, where + ":variants." + v.key(),
                         "variante inconnue (attendu « dark » ou « light »), ignorée");
                    continue;
                }
                flatten(*v, "", layer.variants[*appearance], diagnostics, where, 0);
            }
        }
        base.erase("variants");
    }
    flatten(base, "", layer.tokens, diagnostics, where, 0);
}

bool isState(std::string_view key) {
    const auto states = componentStates();
    return std::find(states.begin(), states.end(), key) != states.end();
}

void readStateProps(const json& props, const std::string& styleKey, ThemeLayer& layer,
                    std::vector<Diagnostic>& diagnostics) {
    auto& target = layer.styles[styleKey];
    for (auto it = props.begin(); it != props.end(); ++it) {
        if (it.key().empty() || it.key().front() == '$') {
            continue;
        }
        const auto where = "components.json:" + styleKey + "." + it.key();
        if (!findStyleProperty(it.key())) {
            warn(diagnostics, where, "propriété de style inconnue, ignorée");
            continue;
        }
        if (it->is_number()) {
            target[it.key()] = it->get<double>();
        } else if (it->is_string()) {
            target[it.key()] = it->get<std::string>();
        } else {
            warn(diagnostics, where, "valeur non prise en charge, ignorée");
        }
    }
}

void readComponents(const json& doc, ThemeLayer& layer, std::vector<Diagnostic>& diagnostics) {
    for (auto c = doc.begin(); c != doc.end(); ++c) {
        if (c.key().empty() || c.key().front() == '$') {
            continue;
        }
        const auto* spec = findComponent(c.key());
        if (!spec || !c->is_object()) {
            warn(diagnostics, "components.json:" + c.key(), "composant inconnu, ignoré");
            continue;
        }
        json shorthand = json::object(); // propriétés posées directement = état « normal »
        for (auto k = c->begin(); k != c->end(); ++k) {
            if (k.key().empty() || k.key().front() == '$') {
                continue;
            }
            if (findStyleProperty(k.key())) {
                shorthand[k.key()] = k.value();
            } else if (isState(k.key()) && k->is_object()) {
                readStateProps(*k, c.key() + "/default/" + k.key(), layer, diagnostics);
            } else if (componentHasVariant(*spec, k.key()) && k->is_object()) {
                json variantShorthand = json::object();
                for (auto s = k->begin(); s != k->end(); ++s) {
                    if (s.key().empty() || s.key().front() == '$') {
                        continue;
                    }
                    if (findStyleProperty(s.key())) {
                        variantShorthand[s.key()] = s.value();
                    } else if (isState(s.key()) && s->is_object()) {
                        readStateProps(*s, c.key() + "/" + k.key() + "/" + s.key(), layer,
                                       diagnostics);
                    } else {
                        warn(diagnostics,
                             "components.json:" + c.key() + "." + k.key() + "." + s.key(),
                             "état inconnu, ignoré");
                    }
                }
                if (!variantShorthand.empty()) {
                    readStateProps(variantShorthand, c.key() + "/" + k.key() + "/normal", layer,
                                   diagnostics);
                }
            } else {
                warn(diagnostics, "components.json:" + c.key() + "." + k.key(),
                     "état, variante ou propriété inconnu, ignoré");
            }
        }
        if (!shorthand.empty()) {
            readStateProps(shorthand, c.key() + "/default/normal", layer, diagnostics);
        }
    }
}

void readIcons(const json& doc, ThemeLayer& layer, std::vector<Diagnostic>& diagnostics) {
    for (auto it = doc.begin(); it != doc.end(); ++it) {
        if (it.key().empty() || it.key().front() == '$') {
            continue;
        }
        if (!isIconName(it.key()) || !it->is_string() || !isPathData(it->get<std::string>())) {
            warn(diagnostics, "icons.json:" + it.key(),
                 "icône refusée (nom [a-zA-Z0-9_-] et tracé SVG M/L/C/Q/A/Z… attendus)");
            continue;
        }
        layer.iconPaths[it.key()] = it->get<std::string>();
    }
}

void readOptionalJson(const std::filesystem::path& dir, const char* name,
                      std::vector<Diagnostic>& diagnostics,
                      const std::function<void(const json&)>& use) {
    const auto file = dir / name;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return;
    }
    const auto text = readLimited(file, kMaxJsonBytes, diagnostics, name);
    if (!text) {
        return;
    }
    std::vector<Diagnostic> parseErrors;
    const auto doc = parseJson(*text, parseErrors, name);
    // Un fichier secondaire illisible n'empêche pas l'activation : avertissement seulement.
    for (auto& d : parseErrors) {
        d.severity = Severity::Warning;
        d.message += " ; fichier ignoré";
        diagnostics.push_back(std::move(d));
    }
    if (doc) {
        use(*doc);
    }
}

// Clés plates de l'ancien Theme.qml (format 0) -> clés du format 1.
constexpr std::pair<std::string_view, std::string_view> kLegacyKeys[] = {
    {"accent", "color.accent"},
    {"accentInk", "color.accentInk"},
    {"accentPressed", "color.accentPressed"},
    {"accentSoft", "color.accentSoft"},
    {"animationMs", "motion.durationMs"},
    {"background", "color.background"},
    {"bodySize", "font.bodySize"},
    {"border", "color.border"},
    {"brandSize", "font.brandSize"},
    {"captionSize", "font.captionSize"},
    {"cardRadius", "shape.cardRadius"},
    {"controlHeight", "size.controlHeight"},
    {"deskDot", "color.deskDot"},
    {"deskStep", "effect.deskStep"},
    {"divider", "color.divider"},
    {"error", "color.error"},
    {"faint", "color.faint"},
    {"feedbackFastMs", "motion.fastMs"},
    {"feedbackSlowMs", "motion.slowMs"},
    {"fontFamily", "font.family"},
    {"foreground", "color.foreground"},
    {"gap", "space.gap"},
    {"headingSize", "font.headingSize"},
    {"heroSize", "font.heroSize"},
    {"hover", "color.hover"},
    {"info", "color.info"},
    {"inspectorWidth", "size.inspectorWidth"},
    {"muted", "color.muted"},
    {"outline", "color.outline"},
    {"panel", "color.panel"},
    {"panelPadding", "space.panelPadding"},
    {"radius", "shape.radius"},
    {"railWidth", "size.railWidth"},
    {"raised", "color.raised"},
    {"rowHeight", "size.rowHeight"},
    {"secondaryAccent", "color.secondaryAccent"},
    {"shadow", "color.shadow"},
    {"shadowOffset", "effect.shadowOffset"},
    {"shadowOpacity", "effect.shadowOpacity"},
    {"smallSize", "font.smallSize"},
    {"success", "color.success"},
    {"surface", "color.surface"},
    {"warning", "color.warning"},
};

struct Entry {
    RawScalar value;
    const std::filesystem::path* origin{nullptr}; // dossier qui a posé la valeur (assets)
};

std::string describe(const RawScalar& raw) {
    if (const auto* s = std::get_if<std::string>(&raw)) {
        return "« " + *s + " »";
    }
    if (const auto* d = std::get_if<double>(&raw)) {
        std::ostringstream o;
        o << *d;
        return o.str();
    }
    return std::get<bool>(raw) ? "true" : "false";
}

std::optional<std::string> referenceTarget(const RawScalar& raw) {
    const auto* s = std::get_if<std::string>(&raw);
    if (!s || s->size() < 3 || s->front() != '{' || s->back() != '}') {
        return std::nullopt;
    }
    return s->substr(1, s->size() - 2);
}

std::optional<TokenValue> typedFromRaw(TokenType type, double min, double max,
                                       std::string_view choices, const RawScalar& raw) {
    if (const auto* d = std::get_if<double>(&raw)) {
        return parseTypedNumber(type, min, max, *d);
    }
    if (const auto* s = std::get_if<std::string>(&raw)) {
        return parseTypedValue(type, min, max, choices, *s);
    }
    return std::nullopt;
}

bool sameType(TokenType type, const TokenValue& value) {
    switch (type) {
    case TokenType::Color:
        return std::holds_alternative<Color>(value);
    case TokenType::Number:
        return std::holds_alternative<double>(value);
    default:
        return std::holds_alternative<std::string>(value);
    }
}

std::optional<std::string> resolveAsset(const std::string& relative,
                                        const std::filesystem::path* origin, AssetKind kind,
                                        std::vector<Diagnostic>& diagnostics,
                                        const std::string& where) {
    if (relative.empty()) {
        return std::string{};
    }
    if (!origin) {
        warn(diagnostics, where, "asset sans dossier d'origine, ignoré");
        return std::nullopt;
    }
    const auto path = resolveInside(*origin, relative);
    if (!path || !isAllowedExtension(kind, *path)) {
        warn(diagnostics, where, "chemin d'asset refusé : « " + relative + " »");
        return std::nullopt;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(*path, ec)) {
        warn(diagnostics, where, "asset absent : « " + relative + " » (repli sans asset)");
        return std::nullopt;
    }
    if (std::filesystem::file_size(*path, ec) > kMaxThemeFileBytes) {
        warn(diagnostics, where, "asset trop volumineux : « " + relative + " »");
        return std::nullopt;
    }
    return path->string();
}

class TokenResolver {
public:
    TokenResolver(const std::map<std::string, Entry>& merged, Appearance appearance,
                  std::vector<Diagnostic>& diagnostics)
        : merged_(merged), appearance_(appearance), diagnostics_(diagnostics) {}

    // Tous les jetons du schéma (repli si le thème ne les pose pas) puis les « custom.* ».
    std::map<std::string, TokenValue> resolveAll() {
        for (const auto& spec : tokenSchema()) {
            (void)resolve(std::string(spec.key));
        }
        for (const auto& [key, entry] : merged_) {
            (void)resolve(key);
        }
        return std::move(done_);
    }

private:
    TokenValue fallback(const TokenSpec& spec) {
        const auto text = appearance_ == Appearance::Light && !spec.lightDefault.empty()
                              ? spec.lightDefault
                              : spec.darkDefault;
        if (const auto ref = referenceTarget(RawScalar{std::string(text)})) {
            if (const auto value = resolve(*ref); value && sameType(spec.type, *value)) {
                return *value;
            }
        }
        if (auto value = parseTypedValue(spec.type, spec.min, spec.max, spec.choices, text)) {
            return *value;
        }
        return spec.type == TokenType::Color ? TokenValue{Color{}} : TokenValue{std::string{}};
    }

    std::optional<TokenValue> resolve(const std::string& key) {
        if (const auto it = done_.find(key); it != done_.end()) {
            return it->second;
        }
        const auto* spec = findToken(key);
        const auto entry = merged_.find(key);
        if (!spec && (entry == merged_.end() || !isCustomTokenKey(key))) {
            return std::nullopt;
        }
        if (std::find(stack_.begin(), stack_.end(), key) != stack_.end()) {
            std::string cycle;
            for (const auto& k : stack_) {
                cycle += k + " → ";
            }
            warn(diagnostics_, "tokens:" + key, "référence circulaire : " + cycle + key);
            return std::nullopt;
        }
        stack_.push_back(key);
        auto value = compute(key, spec, entry == merged_.end() ? nullptr : &entry->second);
        stack_.pop_back();
        if (value) {
            done_[key] = *value;
        }
        return value;
    }

    std::optional<TokenValue> compute(const std::string& key, const TokenSpec* spec,
                                      const Entry* entry) {
        const auto where = "tokens:" + key;
        if (!entry) {
            return fallback(*spec);
        }
        if (const auto ref = referenceTarget(entry->value)) {
            const auto target = resolve(*ref);
            if (!target) {
                warn(diagnostics_, where, "référence inconnue {" + *ref + "}, valeur de repli");
                return spec ? std::optional{fallback(*spec)} : std::nullopt;
            }
            if (spec && !sameType(spec->type, *target)) {
                warn(diagnostics_, where,
                     "référence {" + *ref + "} d'un autre type, valeur de repli");
                return fallback(*spec);
            }
            return target;
        }
        if (!spec) { // custom.* : nombre ou texte, couleur si elle en a la forme
            if (const auto* d = std::get_if<double>(&entry->value)) {
                return TokenValue{*d};
            }
            if (const auto* s = std::get_if<std::string>(&entry->value)) {
                if (const auto c = parseColor(*s)) {
                    return TokenValue{*c};
                }
                return TokenValue{*s};
            }
            return std::nullopt;
        }
        auto value = typedFromRaw(spec->type, spec->min, spec->max, spec->choices, entry->value);
        if (!value) {
            warn(diagnostics_, where,
                 "valeur invalide " + describe(entry->value) + ", valeur de repli");
            return fallback(*spec);
        }
        if (spec->type == TokenType::Asset) {
            const auto path = resolveAsset(std::get<std::string>(*value), entry->origin,
                                           AssetKind::Image, diagnostics_, where);
            return TokenValue{path.value_or(std::string{})};
        }
        return value;
    }

    const std::map<std::string, Entry>& merged_;
    Appearance appearance_;
    std::vector<Diagnostic>& diagnostics_;
    std::map<std::string, TokenValue> done_;
    std::vector<std::string> stack_;
};

ResolvedTheme resolveChain(const std::vector<const ThemePackage*>& chain, Appearance requested,
                           ResolvedTheme result) {
    auto& diagnostics = result.diagnostics;
    // Apparence : variantes déclarées par le thème le plus proche de la feuille.
    std::vector<Appearance> variants{Appearance::Dark};
    std::optional<Appearance> defaultVariant;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!(*it)->manifest.variants.empty()) {
            variants = (*it)->manifest.variants;
            break;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend() && !defaultVariant; ++it) {
        defaultVariant = (*it)->manifest.defaultVariant;
    }
    if (std::find(variants.begin(), variants.end(), requested) != variants.end()) {
        result.appearance = requested;
    } else if (defaultVariant &&
               std::find(variants.begin(), variants.end(), *defaultVariant) != variants.end()) {
        result.appearance = *defaultVariant;
    } else {
        result.appearance = variants.front();
    }

    std::map<std::string, Entry> merged;
    for (const auto* package : chain) {
        const auto apply = [&](const std::map<std::string, RawScalar>& tokens) {
            for (const auto& [key, value] : tokens) {
                if (!findToken(key) && !isCustomTokenKey(key)) {
                    warn(diagnostics, package->manifest.id + ":" + key, "jeton inconnu, ignoré");
                    continue;
                }
                merged[key] = Entry{value, &package->dir};
            }
        };
        apply(package->layer.tokens);
        if (const auto v = package->layer.variants.find(result.appearance);
            v != package->layer.variants.end()) {
            apply(v->second);
        }
    }
    TokenResolver resolver(merged, result.appearance, diagnostics);
    result.tokens = resolver.resolveAll();

    // Styles de composants : fusion propriété par propriété, racine -> feuille.
    std::map<std::string, std::map<std::string, Entry>> styles;
    for (const auto* package : chain) {
        for (const auto& [key, props] : package->layer.styles) {
            for (const auto& [prop, value] : props) {
                styles[key][prop] = Entry{value, &package->dir};
            }
        }
    }
    std::set<std::string> components;
    for (const auto& [key, props] : styles) {
        components.insert(key.substr(0, key.find('/')));
    }
    const auto resolveProp = [&](const std::string& where, const std::string& prop,
                                 const Entry& entry) -> std::optional<TokenValue> {
        const auto* spec = findStyleProperty(prop);
        if (const auto ref = referenceTarget(entry.value)) {
            const auto it = result.tokens.find(*ref);
            if (it == result.tokens.end() || !sameType(spec->type, it->second)) {
                warn(diagnostics, where, "référence {" + *ref + "} inconnue ou d'un autre type");
                return std::nullopt;
            }
            return it->second;
        }
        auto value = typedFromRaw(spec->type, spec->min, spec->max, spec->choices, entry.value);
        if (!value) {
            warn(diagnostics, where, "valeur invalide " + describe(entry.value) + ", ignorée");
            return std::nullopt;
        }
        if (spec->type == TokenType::Asset) {
            const auto path = resolveAsset(std::get<std::string>(*value), entry.origin,
                                           AssetKind::Image, diagnostics, where);
            if (!path || path->empty()) {
                return std::nullopt;
            }
            return TokenValue{*path};
        }
        return value;
    };
    // Une fois par clé source : chaque avertissement n'apparaît qu'une fois.
    std::map<std::string, std::map<std::string, TokenValue>> typed;
    for (const auto& [key, props] : styles) {
        for (const auto& [prop, entry] : props) {
            if (auto value = resolveProp("components:" + key + "." + prop, prop, entry)) {
                typed[key][prop] = *value;
            }
        }
    }
    for (const auto& component : components) {
        const auto* spec = findComponent(component);
        std::vector<std::string> variantNames{"default"};
        std::string_view rest = spec->variants;
        while (!rest.empty()) {
            const auto bar = rest.find('|');
            variantNames.emplace_back(rest.substr(0, bar));
            rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
        }
        for (const auto& variant : variantNames) {
            for (const auto state : componentStates()) {
                std::map<std::string, TokenValue> out;
                const auto layer = [&](const std::string& v, std::string_view s) {
                    const auto it = typed.find(component + "/" + v + "/" + std::string(s));
                    if (it != typed.end()) {
                        for (const auto& [prop, value] : it->second) {
                            out[prop] = value;
                        }
                    }
                };
                layer("default", "normal");
                layer("default", state);
                if (variant != "default") {
                    layer(variant, "normal");
                    layer(variant, state);
                }
                if (!out.empty()) {
                    result.styles[component][variant][std::string(state)] = std::move(out);
                }
            }
        }
    }

    // Icônes, polices, aperçu.
    for (const auto* package : chain) {
        for (const auto& [name, file] : package->layer.iconFiles) {
            result.iconFiles[name] = file;
            result.iconPaths.erase(name);
        }
        for (const auto& [name, path] : package->layer.iconPaths) {
            result.iconPaths[name] = path;
            result.iconFiles.erase(name);
        }
        for (const auto& font : package->manifest.fonts) {
            const auto where = package->manifest.id + ":fonts";
            const auto path = resolveInside(package->dir, font);
            std::error_code ec;
            if (!path || !isAllowedExtension(AssetKind::Font, *path)) {
                warn(diagnostics, where, "police refusée : « " + font + " »");
            } else if (!std::filesystem::is_regular_file(*path, ec)) {
                warn(diagnostics, where,
                     "police absente : « " + font + " » (repli sur les familles suivantes)");
            } else if (std::find(result.fonts.begin(), result.fonts.end(), *path) ==
                       result.fonts.end()) {
                result.fonts.push_back(*path);
            }
        }
    }
    if (!chain.empty() && !chain.back()->manifest.preview.empty()) {
        const auto* leaf = chain.back();
        const auto path = resolveInside(leaf->dir, leaf->manifest.preview);
        std::error_code ec;
        if (path && isAllowedExtension(AssetKind::Image, *path) &&
            std::filesystem::is_regular_file(*path, ec)) {
            result.preview = *path;
        } else {
            warn(diagnostics, leaf->manifest.id + ":preview", "aperçu absent ou refusé");
        }
    }
    for (auto& d : checkContrast(result)) {
        diagnostics.push_back(std::move(d));
    }
    return result;
}

} // namespace

bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

bool isValidThemeId(std::string_view id) {
    if (id.size() < 2 || id.size() > 48) {
        return false;
    }
    const auto ok = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!ok(id.front())) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [&](char c) { return ok(c) || c == '-'; });
}

std::optional<ThemeManifest> parseManifest(std::string_view text,
                                           std::vector<Diagnostic>& diagnostics) {
    const auto before = diagnostics.size();
    const auto doc = parseJson(text, diagnostics, "manifest.json");
    if (!doc) {
        return std::nullopt;
    }
    ThemeManifest m;
    if (const auto it = doc->find("schema");
        it != doc->end() && (!it->is_string() || it->get<std::string>() != "openstitch-theme")) {
        fail(diagnostics, "manifest.json:schema", "doit valoir « openstitch-theme »");
    }
    const auto version = doc->find("schemaVersion");
    if (version == doc->end() || !version->is_number_integer()) {
        fail(diagnostics, "manifest.json:schemaVersion", "entier requis (version du format)");
    } else {
        m.schemaVersion = version->get<int>();
        if (m.schemaVersion > kSchemaVersion) {
            fail(diagnostics, "manifest.json:schemaVersion",
                 "format " + std::to_string(m.schemaVersion) + " plus récent que ce Studio (" +
                     std::to_string(kSchemaVersion) + ") : mettez OpenStitch à jour");
        } else if (m.schemaVersion < 0) {
            fail(diagnostics, "manifest.json:schemaVersion", "version négative");
        }
    }
    m.id = optionalString(*doc, "id", 64, diagnostics).value_or("");
    if (!isValidThemeId(m.id)) {
        fail(diagnostics, "manifest.json:id",
             "identifiant requis : minuscules, chiffres et tirets (2 à 48 caractères)");
    }
    m.name = optionalString(*doc, "name", 64, diagnostics).value_or("");
    if (m.name.empty()) {
        fail(diagnostics, "manifest.json:name", "nom requis");
    }
    m.description = optionalString(*doc, "description", 500, diagnostics).value_or("");
    m.author = optionalString(*doc, "author", 120, diagnostics).value_or("");
    m.version = optionalString(*doc, "version", 32, diagnostics).value_or("1.0.0");
    m.license = optionalString(*doc, "license", 64, diagnostics).value_or("");
    m.preview = optionalString(*doc, "preview", 240, diagnostics).value_or("");
    m.parent = optionalString(*doc, "parent", 64, diagnostics).value_or("");
    if (!m.parent.empty() && (!isValidThemeId(m.parent) || m.parent == m.id)) {
        fail(diagnostics, "manifest.json:parent", "parent invalide : « " + m.parent + " »");
    }
    if (const auto it = doc->find("compatibility"); it != doc->end()) {
        if (!it->is_object()) {
            fail(diagnostics, "manifest.json:compatibility", "doit être un objet");
        } else {
            const auto level = [&](const char* key, int& out) {
                if (const auto k = it->find(key); k != it->end()) {
                    if (k->is_number_integer()) {
                        out = k->get<int>();
                    } else {
                        fail(diagnostics, std::string("manifest.json:compatibility.") + key,
                             "entier requis");
                    }
                }
            };
            level("minEngine", m.minEngine);
            level("maxEngine", m.maxEngine);
            if (m.minEngine > kEngineLevel || (m.maxEngine > 0 && m.maxEngine < kEngineLevel)) {
                fail(diagnostics, "manifest.json:compatibility",
                     "incompatible avec ce moteur (niveau " + std::to_string(kEngineLevel) +
                         ", thème : " + std::to_string(m.minEngine) + " à " +
                         (m.maxEngine > 0 ? std::to_string(m.maxEngine) : std::string("∞")) + ")");
            }
        }
    }
    if (const auto it = doc->find("variants"); it != doc->end()) {
        if (!it->is_array()) {
            fail(diagnostics, "manifest.json:variants",
                 "liste attendue, ex. [\"dark\", \"light\"]");
        } else {
            for (const auto& v : *it) {
                const auto a = v.is_string() ? parseAppearance(v.get<std::string>()) : std::nullopt;
                if (!a) {
                    fail(diagnostics, "manifest.json:variants", "variante inconnue");
                } else if (std::find(m.variants.begin(), m.variants.end(), *a) ==
                           m.variants.end()) {
                    m.variants.push_back(*a);
                }
            }
        }
    }
    if (const auto d = optionalString(*doc, "defaultVariant", 16, diagnostics)) {
        m.defaultVariant = parseAppearance(*d);
        if (!m.defaultVariant) {
            fail(diagnostics, "manifest.json:defaultVariant", "« dark » ou « light » attendu");
        }
    }
    if (const auto it = doc->find("fonts"); it != doc->end()) {
        if (!it->is_array()) {
            fail(diagnostics, "manifest.json:fonts", "liste de chemins attendue");
        } else {
            for (const auto& f : *it) {
                if (f.is_string() && m.fonts.size() < 16) {
                    m.fonts.push_back(f.get<std::string>());
                }
            }
        }
    }
    for (auto i = before; i < diagnostics.size(); ++i) {
        if (diagnostics[i].severity == Severity::Error) {
            return std::nullopt;
        }
    }
    return m;
}

std::string manifestToJson(const ThemeManifest& m) {
    json doc;
    doc["schema"] = "openstitch-theme";
    doc["schemaVersion"] = m.schemaVersion;
    doc["id"] = m.id;
    doc["name"] = m.name;
    doc["description"] = m.description;
    doc["author"] = m.author;
    doc["version"] = m.version;
    if (!m.license.empty()) {
        doc["license"] = m.license;
    }
    if (!m.parent.empty()) {
        doc["parent"] = m.parent;
    }
    doc["compatibility"] = {{"minEngine", m.minEngine}};
    if (m.maxEngine > 0) {
        doc["compatibility"]["maxEngine"] = m.maxEngine;
    }
    if (!m.variants.empty()) {
        doc["variants"] = json::array();
        for (const auto v : m.variants) {
            doc["variants"].push_back(std::string(appearanceId(v)));
        }
    }
    if (m.defaultVariant) {
        doc["defaultVariant"] = std::string(appearanceId(*m.defaultVariant));
    }
    if (!m.preview.empty()) {
        doc["preview"] = m.preview;
    }
    if (!m.fonts.empty()) {
        doc["fonts"] = m.fonts;
    }
    return doc.dump(2) + "\n";
}

std::optional<std::string> sanitizeRelativePath(std::string_view path) {
    if (path.empty() || path.size() > 200) {
        return std::nullopt;
    }
    std::string out;
    std::string segment;
    const auto flush = [&]() -> bool {
        if (segment.empty() || segment == "." || segment == ".." || segment.back() == '.' ||
            segment.back() == ' ') {
            return false;
        }
        out += (out.empty() ? "" : "/") + segment;
        segment.clear();
        return true;
    };
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (static_cast<unsigned char>(c) < 0x20 || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') {
            return std::nullopt;
        }
        if (c == '/' || c == '\\') {
            if (i == 0 || !flush()) {
                return std::nullopt;
            }
            continue;
        }
        segment += c;
    }
    if (!flush()) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::filesystem::path> resolveInside(const std::filesystem::path& root,
                                                   std::string_view relative) {
    const auto clean = sanitizeRelativePath(relative);
    if (!clean) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto base = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto full = std::filesystem::weakly_canonical(
        base / std::filesystem::path(std::u8string(clean->begin(), clean->end())), ec);
    if (ec) {
        return std::nullopt;
    }
    // Confinement après résolution des liens : le chemin final doit commencer par la racine.
    auto b = base.begin();
    auto f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty() && std::next(b) == base.end()) {
            break; // racine terminée par un séparateur
        }
        if (f == full.end() || *b != *f) {
            return std::nullopt;
        }
    }
    return full;
}

bool isAllowedExtension(AssetKind kind, const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    switch (kind) {
    case AssetKind::Font:
        return ext == ".ttf" || ext == ".otf";
    case AssetKind::Icon:
        return ext == ".svg" || ext == ".png";
    case AssetKind::Image:
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".svg";
    case AssetKind::Data:
        return ext == ".json" || ext == ".txt" || ext == ".md";
    }
    return false;
}

bool isAllowedThemeFile(const std::filesystem::path& path) {
    return isAllowedExtension(AssetKind::Font, path) ||
           isAllowedExtension(AssetKind::Image, path) || isAllowedExtension(AssetKind::Data, path);
}

std::optional<ThemePackage> loadPackage(const std::filesystem::path& dir, bool builtin,
                                        std::vector<Diagnostic>& diagnostics) {
    std::error_code ec;
    const auto manifestFile = dir / "manifest.json";
    if (!std::filesystem::is_regular_file(manifestFile, ec)) {
        fail(diagnostics, "manifest.json", "manifeste absent");
        return std::nullopt;
    }
    const auto text = readLimited(manifestFile, kMaxJsonBytes, diagnostics, "manifest.json");
    if (!text) {
        return std::nullopt;
    }
    auto manifest = parseManifest(*text, diagnostics);
    if (!manifest) {
        return std::nullopt;
    }
    ThemePackage package;
    package.dir = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        package.dir = dir;
    }
    package.manifest = std::move(*manifest);
    package.builtin = builtin;
    auto& local = package.diagnostics;
    for (const char* file : {"tokens.json", "animations.json"}) {
        readOptionalJson(package.dir, file, local,
                         [&](const json& doc) { readTokens(doc, package.layer, local, file); });
    }
    if (package.manifest.schemaVersion < kSchemaVersion) {
        migrateTokens(package.manifest.schemaVersion, package.layer.tokens, local);
        for (auto& [appearance, tokens] : package.layer.variants) {
            migrateTokens(package.manifest.schemaVersion, tokens, local);
        }
    }
    readOptionalJson(package.dir, "components.json", local,
                     [&](const json& doc) { readComponents(doc, package.layer, local); });
    readOptionalJson(package.dir, "icons.json", local,
                     [&](const json& doc) { readIcons(doc, package.layer, local); });
    const auto iconDir = package.dir / "assets" / "icons";
    if (std::filesystem::is_directory(iconDir, ec)) {
        std::vector<std::filesystem::path> files;
        for (const auto& item : std::filesystem::directory_iterator(iconDir, ec)) {
            files.push_back(item.path());
        }
        std::sort(files.begin(), files.end()); // ordre déterministe
        for (const auto& file : files) {
            const auto name = file.stem().string();
            if (!std::filesystem::is_regular_file(file, ec) ||
                !isAllowedExtension(AssetKind::Icon, file) || !isIconName(name)) {
                continue;
            }
            if (std::filesystem::file_size(file, ec) > kMaxThemeFileBytes) {
                warn(local, "assets/icons/" + file.filename().string(), "icône trop volumineuse");
                continue;
            }
            package.layer.iconFiles[name] = file;
        }
    }
    for (const auto& d : local) {
        diagnostics.push_back(d);
    }
    return package;
}

void migrateTokens(int fromVersion, std::map<std::string, RawScalar>& tokens,
                   std::vector<Diagnostic>& diagnostics) {
    if (fromVersion >= 1) {
        return;
    }
    // 0 -> 1 : clés plates de l'ancien Theme.qml.
    std::map<std::string, RawScalar> migrated;
    for (auto& [key, value] : tokens) {
        const auto it = std::find_if(std::begin(kLegacyKeys), std::end(kLegacyKeys),
                                     [&](const auto& p) { return p.first == key; });
        if (it != std::end(kLegacyKeys)) {
            migrated[std::string(it->second)] = std::move(value);
        } else if (key.find('.') != std::string::npos) {
            migrated[key] = std::move(value);
        } else {
            warn(diagnostics, "tokens.json:" + key, "clé du format 0 inconnue, ignorée");
        }
    }
    tokens = std::move(migrated);
    warn(diagnostics, "manifest.json:schemaVersion",
         "format 0 migré vers le format " + std::to_string(kSchemaVersion) +
             " à la lecture (réenregistrez le thème pour le mettre à jour)");
}

ResolvedTheme builtinFallback(Appearance appearance) {
    ResolvedTheme result;
    result.manifest.id = "neo-studio";
    result.manifest.name = "Neo Studio (intégré)";
    result.manifest.description = "Thème de secours compilé dans le Studio.";
    result.manifest.author = "OpenStitch";
    result.manifest.variants = {Appearance::Dark, Appearance::Light};
    ThemePackage none;
    none.manifest = result.manifest;
    const std::vector<const ThemePackage*> chain{&none};
    auto resolved = resolveChain(chain, appearance, std::move(result));
    resolved.chain.clear();
    return resolved;
}

ResolvedTheme resolveTheme(std::string_view id, Appearance requested, const PackageLookup& lookup) {
    ResolvedTheme result;
    std::vector<const ThemePackage*> chain;
    std::vector<std::string> seen;
    std::string current(id);
    while (!current.empty()) {
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
            std::string path;
            for (const auto& s : seen) {
                path += s + " → ";
            }
            fail(result.diagnostics, std::string(id), "héritage circulaire : " + path + current);
            break;
        }
        if (static_cast<int>(seen.size()) >= kMaxInheritanceDepth) {
            fail(result.diagnostics, std::string(id),
                 "chaîne d'héritage trop longue (> " + std::to_string(kMaxInheritanceDepth) + ")");
            break;
        }
        const auto* package = lookup ? lookup(current) : nullptr;
        if (!package) {
            fail(result.diagnostics, std::string(id),
                 seen.empty() ? "thème introuvable : « " + current + " »"
                              : "thème parent introuvable : « " + current + " »");
            break;
        }
        seen.push_back(current);
        chain.insert(chain.begin(), package);
        current = package->manifest.parent;
    }
    if (hasErrors(result.diagnostics)) {
        auto fallback = builtinFallback(requested);
        fallback.diagnostics = std::move(result.diagnostics);
        return fallback;
    }
    result.manifest = chain.back()->manifest;
    for (const auto* package : chain) {
        result.chain.push_back(package->manifest.id);
        for (const auto& d : package->diagnostics) {
            result.diagnostics.push_back(d);
        }
    }
    return resolveChain(chain, requested, std::move(result));
}

std::vector<Diagnostic> checkContrast(const ResolvedTheme& theme) {
    std::vector<Diagnostic> out;
    const auto color = [&](const char* key) {
        const auto it = theme.tokens.find(key);
        return it != theme.tokens.end() && std::holds_alternative<Color>(it->second)
                   ? std::get<Color>(it->second)
                   : Color{};
    };
    const auto check = [&](const char* fg, const char* bg, double minimum) {
        const auto ratio = contrastRatio(color(fg), color(bg));
        if (ratio + 1e-9 < minimum) {
            std::ostringstream text;
            text.precision(2);
            text << std::fixed << fg << " sur " << bg << " : " << ratio << ":1 (minimum " << minimum
                 << ":1)";
            out.push_back({Severity::Warning, "contraste", text.str()});
        }
    };
    for (const char* fg : {"color.foreground", "color.muted", "color.faint", "color.accent",
                           "color.error", "color.warning", "color.success", "color.info"}) {
        for (const char* bg :
             {"color.background", "color.surface", "color.panel", "color.raised"}) {
            check(fg, bg, 4.5);
        }
    }
    check("color.accentInk", "color.accent", 4.5);
    for (const char* bg : {"color.background", "color.surface", "color.panel"}) {
        check("color.outline", bg, 3.0);
        check("color.focus", bg, 3.0);
    }
    return out;
}

} // namespace openstitch::desktop_v2::theme
