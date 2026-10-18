// SPDX-License-Identifier: Apache-2.0
#include "openstitch/lettering/unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace openstitch::lettering {

namespace {

struct Composition {
    char32_t base;
    char32_t mark;
    char32_t composed;
};

struct TableRow {
    char32_t mark;
    std::u32string_view bases;
    std::u32string_view composed; // même longueur que `bases`
};

// Latin, grec et cyrillique courants. Chaque ligne : marque, bases, précomposés.
constexpr TableRow kRows[] = {
    {0x0300, U"AEIOUaeiou", U"ÀÈÌÒÙàèìòù"},
    {0x0301, U"AEIOUYaeiouyCcLlNnRrSsZzΑΕΗΙΟΥΩαεηιουω", U"ÁÉÍÓÚÝáéíóúýĆćĹĺŃńŔŕŚśŹźΆΈΉΊΌΎΏάέήίόύώ"},
    {0x0302, U"AEIOUaeiouCcGgHhJjSsWwYy", U"ÂÊÎÔÛâêîôûĈĉĜĝĤĥĴĵŜŝŴŵŶŷ"},
    {0x0303, U"ANOanoIiUu", U"ÃÑÕãñõĨĩŨũ"},
    {0x0304, U"AaEeIiOoUu", U"ĀāĒēĪīŌōŪū"},
    {0x0306, U"AaGgUuИи", U"ĂăĞğŬŭЙй"},
    {0x0307, U"CcEeGgZzI", U"ĊċĖėĠġŻżİ"},
    {0x0308, U"AEIOUaeiouyYЕе", U"ÄËÏÖÜäëïöüÿŸЁё"},
    {0x030A, U"AaUu", U"ÅåŮů"},
    {0x030B, U"OoUu", U"ŐőŰű"},
    {0x030C, U"CcDdEeNnRrSsTtZz", U"ČčĎďĚěŇňŘřŠšŤťŽž"},
    {0x0327, U"CcSsTt", U"ÇçŞşŢţ"},
    {0x0328, U"AaEeIiUu", U"ĄąĘęĮįŲų"},
};

const std::vector<Composition>& table() {
    static const std::vector<Composition> t = [] {
        std::vector<Composition> out;
        for (const TableRow& row : kRows) {
            const std::size_t n = std::min(row.bases.size(), row.composed.size());
            for (std::size_t i = 0; i < n; ++i) {
                out.push_back({row.bases[i], row.mark, row.composed[i]});
            }
        }
        return out;
    }();
    return t;
}

enum class Dir { L, R, EN, N };

bool is_digit_like(char32_t c) {
    return (c >= U'0' && c <= U'9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9);
}

bool is_neutral(char32_t c) {
    if (c < 0x80) {
        const bool alnum =
            (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z');
        return !alnum;
    }
    if (c >= 0xA0 && c <= 0xBF) {
        return c != 0xAA && c != 0xB5 && c != 0xBA;
    }
    return c == 0xD7 || c == 0xF7 || (c >= 0x2000 && c <= 0x206F) || (c >= 0x2190 && c <= 0x2BFF) ||
           (c >= 0x3000 && c <= 0x303F);
}

Dir classify(char32_t c) {
    if (is_digit_like(c)) {
        return Dir::EN;
    }
    if (is_rtl_char(c)) {
        return Dir::R;
    }
    return is_neutral(c) ? Dir::N : Dir::L;
}

char32_t mirrored(char32_t c) {
    switch (c) {
    case U'(':
        return U')';
    case U')':
        return U'(';
    case U'[':
        return U']';
    case U']':
        return U'[';
    case U'{':
        return U'}';
    case U'}':
        return U'{';
    case U'<':
        return U'>';
    case U'>':
        return U'<';
    case 0xAB:
        return 0xBB;
    case 0xBB:
        return 0xAB;
    default:
        return c;
    }
}

} // namespace

bool is_combining_mark(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF ||
           c == 0x05C1 || c == 0x05C2 || c == 0x05C4 || c == 0x05C5 || c == 0x05C7 ||
           (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) ||
           (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 || c == 0x06E8 ||
           (c >= 0x06EA && c <= 0x06ED) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) ||
           (c >= 0xFE20 && c <= 0xFE2F);
}

bool is_ignorable(char32_t c) {
    if (c == U'\t' || c == U'\n' || c == U'\r') {
        return false;
    }
    return c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0x00AD || (c >= 0x200B && c <= 0x200F) ||
           (c >= 0x202A && c <= 0x202E) || (c >= 0x2060 && c <= 0x2064) ||
           (c >= 0xFE00 && c <= 0xFE0F) || c == 0xFEFF;
}

bool is_rtl_char(char32_t c) {
    return (c >= 0x0590 && c <= 0x08FF && !is_digit_like(c)) || (c >= 0xFB1D && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFC) || (c >= 0x10800 && c <= 0x10FFF);
}

bool is_arabic_script(char32_t c) {
    return (c >= 0x0600 && c <= 0x06FF && !is_digit_like(c)) || (c >= 0x0750 && c <= 0x077F) ||
           (c >= 0x08A0 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
           (c >= 0xFE70 && c <= 0xFEFC);
}

char32_t compose_pair(char32_t base, char32_t mark) {
    for (const Composition& c : table()) {
        if (c.base == base && c.mark == mark) {
            return c.composed;
        }
    }
    return 0;
}

bool decompose_char(char32_t composed, char32_t& base, char32_t& mark) {
    for (const Composition& c : table()) {
        if (c.composed == composed) {
            base = c.base;
            mark = c.mark;
            return true;
        }
    }
    return false;
}

std::u32string fallback_sequence(char32_t c) {
    switch (c) {
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032:
        return U"'";
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033:
        return U"\"";
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2212:
        return U"-";
    case 0x2026:
        return U"...";
    case 0x2044:
        return U"/";
    case 0x00D7:
        return U"x";
    default:
        break;
    }
    if (c == 0x00A0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000) {
        return U" ";
    }
    return {};
}

std::vector<char32_t> visual_order(const std::vector<char32_t>& logical) {
    // Grappes : une lettre et les marques combinantes qui la suivent.
    struct Cluster {
        std::size_t first{0};
        std::size_t count{0};
        Dir dir{Dir::N};
        int level{0};
    };
    std::vector<Cluster> clusters;
    bool any_rtl = false;
    for (std::size_t i = 0; i < logical.size(); ++i) {
        if (is_combining_mark(logical[i]) && !clusters.empty()) {
            ++clusters.back().count;
            continue;
        }
        Cluster c;
        c.first = i;
        c.count = 1;
        c.dir = classify(logical[i]);
        any_rtl = any_rtl || c.dir == Dir::R;
        clusters.push_back(c);
    }
    if (!any_rtl) {
        return logical;
    }

    // Niveau de paragraphe : premier caractère fort.
    int paragraph = 0;
    for (const Cluster& c : clusters) {
        if (c.dir == Dir::L || c.dir == Dir::R) {
            paragraph = c.dir == Dir::R ? 1 : 0;
            break;
        }
    }
    const Dir para_dir = paragraph == 1 ? Dir::R : Dir::L;

    // W7 : un chiffre précédé d'un fort L devient L.
    Dir prev_strong = para_dir;
    for (Cluster& c : clusters) {
        if (c.dir == Dir::L || c.dir == Dir::R) {
            prev_strong = c.dir;
        } else if (c.dir == Dir::EN && prev_strong == Dir::L) {
            c.dir = Dir::L;
        }
    }
    // N1/N2 : suite de neutres entre deux voisins de même sens (chiffre = R), sinon
    // sens du paragraphe.
    const auto side = [](Dir d) { return d == Dir::EN ? Dir::R : d; };
    for (std::size_t i = 0; i < clusters.size();) {
        if (clusters[i].dir != Dir::N) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < clusters.size() && clusters[j].dir == Dir::N) {
            ++j;
        }
        const Dir before = i == 0 ? para_dir : side(clusters[i - 1].dir);
        const Dir after = j == clusters.size() ? para_dir : side(clusters[j].dir);
        const Dir resolved = before == after ? before : para_dir;
        for (std::size_t k = i; k < j; ++k) {
            clusters[k].dir = resolved == Dir::R ? Dir::R : Dir::L;
        }
        i = j;
    }
    for (Cluster& c : clusters) {
        switch (c.dir) {
        case Dir::R:
            c.level = 1;
            break;
        case Dir::EN:
            c.level = 2;
            break;
        default:
            c.level = paragraph == 1 ? 2 : 0;
            break;
        }
    }

    // L2 : du niveau le plus haut au plus bas niveau impair, inversion des suites.
    int highest = 0;
    int lowest_odd = 1000;
    for (const Cluster& c : clusters) {
        highest = std::max(highest, c.level);
        if (c.level % 2 == 1) {
            lowest_odd = std::min(lowest_odd, c.level);
        }
    }
    for (int level = highest; level >= lowest_odd && level >= 1; --level) {
        for (std::size_t i = 0; i < clusters.size();) {
            if (clusters[i].level < level) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < clusters.size() && clusters[j].level >= level) {
                ++j;
            }
            std::reverse(clusters.begin() + static_cast<std::ptrdiff_t>(i),
                         clusters.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }

    std::vector<char32_t> out;
    out.reserve(logical.size());
    for (const Cluster& c : clusters) {
        for (std::size_t k = 0; k < c.count; ++k) {
            char32_t ch = logical[c.first + k];
            if (k == 0 && c.level % 2 == 1) {
                ch = mirrored(ch);
            }
            out.push_back(ch);
        }
    }
    return out;
}

} // namespace openstitch::lettering
