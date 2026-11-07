// SPDX-License-Identifier: Apache-2.0
#include "preferences_controller.hpp"

#include <QGuiApplication>
#include <QStyleHints>

#include <fstream>
#include <sstream>

namespace openstitch::desktop_v2::studio {

namespace {
constexpr const char* kSessionKeys[] = {"file", "workspace", "editOpen", "treeMode", "zoom"};

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}
} // namespace

PreferencesController::PreferencesController(std::filesystem::path configDir, QObject* parent)
    : QObject(parent), prefsFile_(configDir / "preferences.txt"),
      extraFile_(configDir / "studio-session.txt") {
    load();
}

void PreferencesController::load() {
    prefs_ = loadPreferences(prefsFile_);
    reducedMotion_ = false;
    session_.clear();
    std::ifstream in(extraFile_);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = line.substr(0, eq);
        const auto value = trim(line.substr(eq + 1));
        if (key == "reducedMotion") {
            reducedMotion_ = value == "1";
            continue;
        }
        for (const char* known : kSessionKeys) {
            if (key == known) {
                session_[key] = value;
            }
        }
    }
}

void PreferencesController::save() const {
    (void)savePreferences(prefsFile_, prefs_);
    std::error_code ec;
    std::filesystem::create_directories(extraFile_.parent_path(), ec);
    std::ofstream out(extraFile_, std::ios::trunc);
    out << "reducedMotion=" << (reducedMotion_ ? "1" : "0") << '\n';
    for (const auto& [key, value] : session_) {
        out << key << '=' << value << '\n';
    }
}

QString PreferencesController::theme() const {
    return QString::fromStdString(prefs_.theme);
}

QString PreferencesController::effectiveTheme() const {
    if (prefs_.theme == "light" || prefs_.theme == "dark") {
        return QString::fromStdString(prefs_.theme);
    }
    if (const auto* hints = QGuiApplication::styleHints()) {
        return hints->colorScheme() == Qt::ColorScheme::Light ? QStringLiteral("light")
                                                              : QStringLiteral("dark");
    }
    return QStringLiteral("dark");
}

QString PreferencesController::density() const {
    return QString::fromStdString(densityId(prefs_.density));
}

double PreferencesController::densityScale() const {
    return desktop_v2::densityScale(prefs_.density);
}

QVariantMap PreferencesController::session() const {
    QVariantMap map;
    for (const auto& [key, value] : session_) {
        map.insert(QString::fromStdString(key), QString::fromStdString(value));
    }
    return map;
}

void PreferencesController::setTheme(const QString& id) {
    const auto value = id.toStdString();
    if ((value != "system" && value != "light" && value != "dark") || value == prefs_.theme) {
        return;
    }
    prefs_.theme = value;
    save();
    emit changed();
}

void PreferencesController::setDensity(const QString& id) {
    const auto parsed = parseDensity(id.toStdString());
    if (!parsed || *parsed == prefs_.density) {
        return;
    }
    prefs_.density = *parsed;
    save();
    emit changed();
}

void PreferencesController::setReducedMotion(bool on) {
    if (on == reducedMotion_) {
        return;
    }
    reducedMotion_ = on;
    save();
    emit changed();
}

void PreferencesController::rememberSession(const QVariantMap& state) {
    bool any = false;
    for (const char* key : kSessionKeys) {
        const auto found = state.constFind(QString::fromLatin1(key));
        if (found == state.cend()) {
            continue;
        }
        QString text = found->toString();
        if (found->typeId() == QMetaType::Bool) {
            text = found->toBool() ? QStringLiteral("1") : QStringLiteral("0");
        }
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        session_[key] = text.toStdString();
        any = true;
    }
    if (any) {
        save();
        emit sessionChanged();
    }
}

void PreferencesController::forgetSession() {
    session_.clear();
    save();
    emit sessionChanged();
}

void PreferencesController::resetDefaults() {
    prefs_ = Preferences{};
    reducedMotion_ = false;
    save();
    emit changed();
}

} // namespace openstitch::desktop_v2::studio
