// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/thread_stock.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <utility>

namespace openstitch::thread_palette {

namespace {

constexpr int kStockVersion = 1;

bool key_less(const ThreadKey& a, const ThreadKey& b) {
    return a < b;
}

// ---------------------------------------------------------------------------
// Mini JSON (sous-ensemble suffisant : objets, tableaux, chaînes, entiers, booléens, null).
// thread_palette ne lie aucune bibliothèque tierce : pas de nlohmann ici.
// ---------------------------------------------------------------------------
struct JsonValue {
    enum class Kind { Null, Bool, Int, String, Array, Object };
    Kind kind = Kind::Null;
    long long number = 0;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    [[nodiscard]] const JsonValue* find(std::string_view key) const {
        for (const auto& [k, v] : members) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    Result<JsonValue> parse() {
        auto v = value(0);
        if (!v) {
            return v;
        }
        skip_ws();
        if (pos_ != text_.size()) {
            return error("contenu après la fin du document");
        }
        return v;
    }

private:
    std::unexpected<Error> error(const std::string& what) const {
        return fail(ErrorCategory::InvalidFile, "Mes fils : JSON invalide (" + what + ")",
                    "offset " + std::to_string(pos_));
    }

    void skip_ws() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    Result<std::string> string_literal() {
        // Le guillemet ouvrant est déjà consommé.
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return error("caractère de contrôle dans une chaîne");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                break;
            }
            const char e = text_[pos_++];
            switch (e) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                if (pos_ + 4 > text_.size()) {
                    return error("séquence \\u tronquée");
                }
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = text_[pos_++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp |= static_cast<unsigned>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp |= static_cast<unsigned>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp |= static_cast<unsigned>(h - 'A' + 10);
                    } else {
                        return error("séquence \\u invalide");
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    return error("substitut UTF-16 non pris en charge");
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return error("échappement inconnu");
            }
        }
        return error("chaîne non terminée");
    }

    Result<JsonValue> value(int depth) {
        if (depth > 16) {
            return error("imbrication trop profonde");
        }
        skip_ws();
        if (pos_ >= text_.size()) {
            return error("fin de document inattendue");
        }
        JsonValue v;
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            v.kind = JsonValue::Kind::Object;
            if (consume('}')) {
                return v;
            }
            while (true) {
                if (!consume('"')) {
                    return error("clé attendue");
                }
                auto key = string_literal();
                if (!key) {
                    return std::unexpected(key.error());
                }
                if (!consume(':')) {
                    return error("':' attendu");
                }
                auto member = value(depth + 1);
                if (!member) {
                    return member;
                }
                v.members.emplace_back(std::move(*key), std::move(*member));
                if (consume(',')) {
                    continue;
                }
                if (consume('}')) {
                    return v;
                }
                return error("',' ou '}' attendu");
            }
        }
        if (c == '[') {
            ++pos_;
            v.kind = JsonValue::Kind::Array;
            if (consume(']')) {
                return v;
            }
            while (true) {
                auto item = value(depth + 1);
                if (!item) {
                    return item;
                }
                v.items.push_back(std::move(*item));
                if (consume(',')) {
                    continue;
                }
                if (consume(']')) {
                    return v;
                }
                return error("',' ou ']' attendu");
            }
        }
        if (c == '"') {
            ++pos_;
            auto s = string_literal();
            if (!s) {
                return std::unexpected(s.error());
            }
            v.kind = JsonValue::Kind::String;
            v.text = std::move(*s);
            return v;
        }
        if (literal("true") || literal("false")) {
            v.kind = JsonValue::Kind::Bool;
            return v;
        }
        if (literal("null")) {
            return v;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos_;
            if (text_[pos_] == '-') {
                ++pos_;
            }
            long long n = 0;
            std::size_t digits = 0;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                n = n * 10 + (text_[pos_] - '0');
                if (++digits > 15) {
                    return error("entier trop grand");
                }
                ++pos_;
            }
            if (digits == 0) {
                return error("nombre invalide");
            }
            if (pos_ < text_.size() &&
                (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
                return error("nombre non entier");
            }
            v.kind = JsonValue::Kind::Int;
            v.number = text_[start] == '-' ? -n : n;
            return v;
        }
        return error("valeur inattendue");
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

void write_string(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += ch; // UTF-8 conservé tel quel
            }
        }
    }
    out += '"';
}

std::unexpected<Error> bad(const std::string& what) {
    return fail(ErrorCategory::InvalidFile, "Mes fils : " + what);
}

Result<std::string> require_string(const JsonValue& obj, std::string_view key) {
    const JsonValue* v = obj.find(key);
    if (v == nullptr || v->kind != JsonValue::Kind::String) {
        return bad("champ « " + std::string(key) + " » manquant ou non textuel");
    }
    return v->text;
}

std::string optional_string(const JsonValue& obj, std::string_view key) {
    const JsonValue* v = obj.find(key);
    return (v != nullptr && v->kind == JsonValue::Kind::String) ? v->text : std::string{};
}

} // namespace

bool ThreadStock::own(const ThreadKey& key) {
    if (key.chart_id.empty() || key.code.empty() || key.chart_id == kPersonalChartId) {
        return false;
    }
    const auto it = std::lower_bound(owned_.begin(), owned_.end(), key, key_less);
    if (it != owned_.end() && *it == key) {
        return false;
    }
    owned_.insert(it, key);
    return true;
}

bool ThreadStock::disown(const ThreadKey& key) {
    if (key.chart_id == kPersonalChartId) {
        const auto it = std::find_if(personal_.begin(), personal_.end(),
                                     [&](const Thread& t) { return t.key.code == key.code; });
        if (it == personal_.end()) {
            return false;
        }
        personal_.erase(it);
        return true;
    }
    const auto it = std::lower_bound(owned_.begin(), owned_.end(), key, key_less);
    if (it == owned_.end() || !(*it == key)) {
        return false;
    }
    owned_.erase(it);
    return true;
}

bool ThreadStock::owns(const ThreadKey& key) const {
    if (key.chart_id == kPersonalChartId) {
        return std::any_of(personal_.begin(), personal_.end(),
                           [&](const Thread& t) { return t.key.code == key.code; });
    }
    return std::binary_search(owned_.begin(), owned_.end(), key, key_less);
}

Result<void> ThreadStock::add_personal(Thread thread) {
    if (thread.key.code.empty()) {
        return fail(ErrorCategory::UserInput, "Le fil personnel doit avoir un code");
    }
    thread.key.chart_id = kPersonalChartId;
    const auto it = std::lower_bound(
        personal_.begin(), personal_.end(), thread.key.code,
        [](const Thread& t, const std::string& code) { return t.key.code < code; });
    if (it != personal_.end() && it->key.code == thread.key.code) {
        *it = std::move(thread);
    } else {
        personal_.insert(it, std::move(thread));
    }
    return {};
}

std::vector<Thread> ThreadStock::resolve(const ThreadLibrary& library) const {
    std::vector<Thread> out;
    out.reserve(size());
    for (const ThreadKey& key : owned_) {
        if (auto t = library.find(key)) {
            out.push_back(std::move(*t));
        }
    }
    out.insert(out.end(), personal_.begin(), personal_.end());
    return out;
}

std::vector<ThreadMatch> ThreadStock::nearest(const ThreadLibrary& library,
                                              std::array<std::uint8_t, 3> rgb,
                                              std::size_t top_n) const {
    std::vector<ThreadMatch> all;
    const CieLab target = to_cielab(rgb);
    for (const Thread& t : resolve(library)) {
        all.push_back(ThreadMatch{t.key, ciede2000(target, to_cielab(t.rgb))});
    }
    std::stable_sort(all.begin(), all.end(), [](const ThreadMatch& a, const ThreadMatch& b) {
        return a.distance < b.distance;
    });
    if (all.size() > top_n) {
        all.resize(top_n);
    }
    return all;
}

std::string stock_to_json(const ThreadStock& stock) {
    std::string out = "{\n  \"version\": " + std::to_string(kStockVersion) + ",\n  \"owned\": [";
    bool first = true;
    for (const ThreadKey& key : stock.owned_keys()) {
        out += first ? "\n    {\"chart\": " : ",\n    {\"chart\": ";
        first = false;
        write_string(out, key.chart_id);
        out += ", \"code\": ";
        write_string(out, key.code);
        out += '}';
    }
    out += first ? "],\n" : "\n  ],\n";
    out += "  \"personal\": [";
    first = true;
    for (const Thread& t : stock.personal_threads()) {
        out += first ? "\n    {\"code\": " : ",\n    {\"code\": ";
        first = false;
        write_string(out, t.key.code);
        out += ", \"name\": ";
        write_string(out, t.name);
        out += ", \"brand\": ";
        write_string(out, t.brand);
        out += ", \"range\": ";
        write_string(out, t.range);
        out += ", \"rgb\": [" + std::to_string(t.rgb[0]) + ", " + std::to_string(t.rgb[1]) + ", " +
               std::to_string(t.rgb[2]) + "]}";
    }
    out += first ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

Result<ThreadStock> stock_from_json(std::string_view text) {
    auto root = JsonParser(text).parse();
    if (!root) {
        return std::unexpected(root.error());
    }
    if (root->kind != JsonValue::Kind::Object) {
        return bad("la racine doit être un objet");
    }
    const JsonValue* version = root->find("version");
    if (version == nullptr || version->kind != JsonValue::Kind::Int || version->number < 1) {
        return bad("version absente ou invalide");
    }
    if (version->number > kStockVersion) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Mes fils : fichier créé par une version plus récente d'OpenStitch (format v" +
                        std::to_string(version->number) + ", cette version lit jusqu'à v" +
                        std::to_string(kStockVersion) + ")");
    }

    ThreadStock stock;
    if (const JsonValue* owned = root->find("owned")) {
        if (owned->kind != JsonValue::Kind::Array) {
            return bad("« owned » doit être un tableau");
        }
        for (const JsonValue& item : owned->items) {
            if (item.kind != JsonValue::Kind::Object) {
                return bad("entrée « owned » invalide");
            }
            auto chart = require_string(item, "chart");
            if (!chart) {
                return std::unexpected(chart.error());
            }
            auto code = require_string(item, "code");
            if (!code) {
                return std::unexpected(code.error());
            }
            if (!stock.own(ThreadKey{*chart, *code})) {
                return bad("fil possédé invalide ou en double : " + *chart + "/" + *code);
            }
        }
    }
    if (const JsonValue* personal = root->find("personal")) {
        if (personal->kind != JsonValue::Kind::Array) {
            return bad("« personal » doit être un tableau");
        }
        std::set<std::string> codes;
        for (const JsonValue& item : personal->items) {
            if (item.kind != JsonValue::Kind::Object) {
                return bad("entrée « personal » invalide");
            }
            auto code = require_string(item, "code");
            if (!code) {
                return std::unexpected(code.error());
            }
            if (code->empty() || !codes.insert(*code).second) {
                return bad("code de fil personnel vide ou en double : " + *code);
            }
            const JsonValue* rgb = item.find("rgb");
            if (rgb == nullptr || rgb->kind != JsonValue::Kind::Array || rgb->items.size() != 3) {
                return bad("« rgb » doit être un tableau de 3 entiers (fil " + *code + ")");
            }
            Thread t;
            t.key = ThreadKey{kPersonalChartId, *code};
            t.name = optional_string(item, "name");
            t.brand = optional_string(item, "brand");
            t.range = optional_string(item, "range");
            for (std::size_t c = 0; c < 3; ++c) {
                const JsonValue& comp = rgb->items[c];
                if (comp.kind != JsonValue::Kind::Int || comp.number < 0 || comp.number > 255) {
                    return bad("composante rgb hors 0..255 (fil " + *code + ")");
                }
                t.rgb[c] = static_cast<std::uint8_t>(comp.number);
            }
            if (auto r = stock.add_personal(std::move(t)); !r) {
                return std::unexpected(r.error());
            }
        }
    }
    return stock;
}

} // namespace openstitch::thread_palette
