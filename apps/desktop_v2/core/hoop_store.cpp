// SPDX-License-Identifier: Apache-2.0
#include "hoop_store.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>

#include "openstitch/commands/project_commands.hpp"

namespace openstitch::desktop_v2 {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

const char* shapeId(UserHoopShape s) {
    switch (s) {
    case UserHoopShape::Ellipse:
        return "ellipse";
    case UserHoopShape::RoundedRectangle:
        return "rounded";
    case UserHoopShape::Rectangle:
        break;
    }
    return "rect";
}

bool parseShape(const std::string& id, UserHoopShape& out) {
    if (id == "rect") {
        out = UserHoopShape::Rectangle;
    } else if (id == "ellipse") {
        out = UserHoopShape::Ellipse;
    } else if (id == "rounded") {
        out = UserHoopShape::RoundedRectangle;
    } else {
        return false;
    }
    return true;
}

bool parseNumber(const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && end != text.c_str() && *end == '\0' && std::isfinite(out);
}

document::HoopShape engineShape(UserHoopShape s) {
    switch (s) {
    case UserHoopShape::Ellipse:
        return document::HoopShape::Ellipse;
    case UserHoopShape::RoundedRectangle:
        return document::HoopShape::RoundedRectangle;
    case UserHoopShape::Rectangle:
        break;
    }
    return document::HoopShape::Rectangle;
}

Micrometers um(double mm) {
    return to_micrometers(Millimeters{mm});
}
} // namespace

std::string validateUserHoop(const UserHoop& h) {
    if (h.name.empty() || h.name.size() > 40) {
        return "Le nom du cadre doit faire entre 1 et 40 caractères.";
    }
    for (const unsigned char c : h.name) {
        if (c == ';' || c < 0x20 || c == 0x7f) {
            return "Le nom du cadre ne peut contenir ni « ; » ni caractère de contrôle.";
        }
    }
    if (h.name.find_first_not_of(' ') == std::string::npos) {
        return "Le nom du cadre ne peut pas être vide.";
    }
    const auto bad = [](double v, double lo, double hi) {
        return !std::isfinite(v) || v < lo || v > hi;
    };
    if (bad(h.widthMm, 10.0, 1000.0) || bad(h.heightMm, 10.0, 1000.0)) {
        return "Dimensions du cadre : entre 10 et 1000 mm.";
    }
    if (bad(h.marginMm, 0.0, 500.0) || 2.0 * h.marginMm >= std::min(h.widthMm, h.heightMm)) {
        return "Marge de sécurité invalide : positive, et inférieure à la moitié du plus petit "
               "côté du cadre.";
    }
    if (h.shape == UserHoopShape::RoundedRectangle &&
        (bad(h.cornerRadiusMm, 0.0, 500.0) ||
         h.cornerRadiusMm > std::min(h.widthMm, h.heightMm) / 2.0)) {
        return "Rayon des coins : positif et au plus la moitié du plus petit côté.";
    }
    return {};
}

ServiceResult addUserHoop(std::vector<UserHoop>& list, const UserHoop& hoop) {
    if (const auto why = validateUserHoop(hoop); !why.empty()) {
        return {false, why};
    }
    const std::string key = lower(hoop.name);
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const UserHoop& h) { return lower(h.name) == key; });
    if (it != list.end()) {
        *it = hoop;
        return {true, {}};
    }
    if (list.size() >= kMaxUserHoops) {
        return {false, "Trop de cadres personnels (50 au maximum) : supprimez-en un d'abord."};
    }
    list.push_back(hoop);
    return {true, {}};
}

bool removeUserHoop(std::vector<UserHoop>& list, const std::string& name) {
    const std::string key = lower(name);
    const auto it = std::find_if(list.begin(), list.end(),
                                 [&](const UserHoop& h) { return lower(h.name) == key; });
    if (it == list.end()) {
        return false;
    }
    list.erase(it);
    return true;
}

const UserHoop* findUserHoop(const std::vector<UserHoop>& list, const std::string& name) {
    const std::string key = lower(name);
    for (const auto& h : list) {
        if (lower(h.name) == key) {
            return &h;
        }
    }
    return nullptr;
}

std::string userHoopsToText(const std::vector<UserHoop>& list) {
    std::string out;
    char buffer[160];
    for (const auto& h : list) {
        std::snprintf(buffer, sizeof buffer, ";%s;%.3f;%.3f;%.3f;%.3f\n", shapeId(h.shape),
                      h.widthMm, h.heightMm, h.cornerRadiusMm, h.marginMm);
        out += h.name + buffer;
    }
    return out;
}

std::vector<UserHoop> userHoopsFromText(const std::string& text) {
    std::vector<UserHoop> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::vector<std::string> fields;
        std::size_t from = 0;
        while (true) {
            const auto cut = line.find(';', from);
            fields.push_back(line.substr(from, cut == std::string::npos ? cut : cut - from));
            if (cut == std::string::npos) {
                break;
            }
            from = cut + 1;
        }
        if (fields.size() != 6) {
            continue;
        }
        UserHoop h;
        h.name = fields[0];
        if (!parseShape(fields[1], h.shape) || !parseNumber(fields[2], h.widthMm) ||
            !parseNumber(fields[3], h.heightMm) || !parseNumber(fields[4], h.cornerRadiusMm) ||
            !parseNumber(fields[5], h.marginMm)) {
            continue;
        }
        if (out.size() < kMaxUserHoops) {
            (void)addUserHoop(out, h); // ignore silencieusement une ligne invalide
        }
    }
    return out;
}

bool saveUserHoops(const std::filesystem::path& file, const std::vector<UserHoop>& list) {
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    const auto tmp = std::filesystem::path(file).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << userHoopsToText(list);
        if (!out) {
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

std::vector<UserHoop> loadUserHoops(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return userHoopsFromText(text.str());
}

UserHoop userHoopFromProject(const document::Project& project, const std::string& name) {
    const auto& c = project.canvas;
    UserHoop h;
    h.name = name;
    switch (c.shape) {
    case document::HoopShape::Ellipse:
        h.shape = UserHoopShape::Ellipse;
        break;
    case document::HoopShape::RoundedRectangle:
        h.shape = UserHoopShape::RoundedRectangle;
        break;
    case document::HoopShape::Rectangle:
        h.shape = UserHoopShape::Rectangle;
        break;
    }
    h.widthMm = to_millimeters(c.width).value;
    h.heightMm = to_millimeters(c.height).value;
    h.cornerRadiusMm = to_millimeters(c.corner_radius).value;
    h.marginMm = to_millimeters(c.margin).value;
    return h;
}

ServiceResult applyUserHoop(DocumentSession& session, const UserHoop& hoop) {
    if (const auto why = validateUserHoop(hoop); !why.empty()) {
        return {false, why};
    }
    document::Canvas c;
    c.width = um(hoop.widthMm);
    c.height = um(hoop.heightMm);
    c.margin = um(hoop.marginMm);
    c.shape = engineShape(hoop.shape);
    c.corner_radius =
        hoop.shape == UserHoopShape::RoundedRectangle ? um(hoop.cornerRadiusMm) : Micrometers{0};
    const auto& cur = session.project().canvas;
    if (cur.width == c.width && cur.height == c.height && cur.margin == c.margin &&
        cur.shape == c.shape && cur.corner_radius == c.corner_radius) {
        return {false, "Le cadre est déjà celui-là."};
    }
    session.execute(std::make_unique<commands::SetCanvasCommand>(c));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
