// SPDX-License-Identifier: Apache-2.0
#include "preferences_controller.hpp"

#include <QGuiApplication>
#include <QStyleHints>

#include <fstream>

#include "theme_package.hpp"
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
    panelLayout_.clear();
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
        // "panel.<id>.<field>=<valeur>" : clé ouverte, pas d'allowlist (le nombre de panneaux
        // ×4 espaces grossira avec le temps).
        if (key.rfind("panel.", 0) == 0) {
            const auto rest = key.substr(6);
            const auto dot = rest.find('.');
            if (dot != std::string::npos && dot > 0 && dot + 1 < rest.size()) {
                panelLayout_[rest.substr(0, dot)][rest.substr(dot + 1)] = value;
            }
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
    // Le fichier est partagé : les réglages du rendu réaliste y sont écrits par l'analyse. On
    // relit donc le fichier et on n'y remplace que les champs de ce contrôleur ; écrire la copie
    // chargée au démarrage effaçait à la fermeture les réglages de rendu de la session.
    auto onDisk = loadPreferences(prefsFile_);
    onDisk.theme = prefs_.theme;
    onDisk.themeId = prefs_.themeId;
    onDisk.density = prefs_.density;
    (void)savePreferences(prefsFile_, onDisk);
    std::error_code ec;
    std::filesystem::create_directories(extraFile_.parent_path(), ec);
    std::ofstream out(extraFile_, std::ios::trunc);
    out << "reducedMotion=" << (reducedMotion_ ? "1" : "0") << '\n';
    for (const auto& [key, value] : session_) {
        out << key << '=' << value << '\n';
    }
    for (const auto& [panelId, fields] : panelLayout_) {
        for (const auto& [field, value] : fields) {
            out << "panel." << panelId << '.' << field << '=' << value << '\n';
        }
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

QString PreferencesController::themeId() const {
    return QString::fromStdString(prefs_.themeId);
}

void PreferencesController::setThemeId(const QString& id) {
    const auto value = id.toStdString();
    if (value == prefs_.themeId || !theme::isValidThemeId(value)) {
        return;
    }
    prefs_.themeId = value;
    save();
    emit changed();
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

namespace {
// x/y sont une position libre en pixels (nombre) ; les autres champs connus sont des bascules.
bool isNumericPanelField(const std::string& field) {
    return field == "x" || field == "y";
}
} // namespace

QVariantMap PreferencesController::panelLayout() const {
    QVariantMap map;
    for (const auto& [panelId, fields] : panelLayout_) {
        QVariantMap entry;
        for (const auto& [field, value] : fields) {
            const auto qvalue = QString::fromStdString(value);
            entry.insert(QString::fromStdString(field),
                         isNumericPanelField(field) ? QVariant(qvalue.toDouble())
                                                    : QVariant(qvalue == QStringLiteral("1")));
        }
        map.insert(QString::fromStdString(panelId), entry);
    }
    return map;
}

void PreferencesController::setPanelState(const QString& panelId, const QVariantMap& state) {
    if (panelId.isEmpty() || state.isEmpty()) {
        return;
    }
    auto& fields = panelLayout_[panelId.toStdString()];
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        const auto field = it.key().toStdString();
        QString text = it.value().toString();
        if (it.value().typeId() == QMetaType::Bool) {
            text = it.value().toBool() ? QStringLiteral("1") : QStringLiteral("0");
        }
        fields[field] = text.toStdString();
    }
    save();
    emit panelLayoutChanged();
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
