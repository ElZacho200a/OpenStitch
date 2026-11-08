// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "preferences.hpp"

namespace openstitch::desktop_v2::studio {

// Adaptateur Qt des préférences d'interface (thème, densité, animations) et de la restauration de
// session (fichier courant, panneaux ouverts, zoom). Thème et densité passent par
// `core/preferences` (même fichier que le bureau V2) ; le reste vit dans un second fichier
// `clé=valeur` du même dossier. Aucune logique métier : le QML applique les valeurs.
//
// Langue : le moteur n'a qu'une langue (français) et aucune infrastructure de traduction ; aucun
// faux réglage n'est proposé (`languages` ne contient que « fr »).
// Raccourcis : fixes dans V1 comme dans V2 (table de défauts, pas de personnalisation) ; la
// feuille d'aide les liste telle qu'elle est définie.
class PreferencesController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(QString effectiveTheme READ effectiveTheme NOTIFY changed)
    Q_PROPERTY(QString density READ density WRITE setDensity NOTIFY changed)
    Q_PROPERTY(double densityScale READ densityScale NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(QStringList languages READ languages CONSTANT)
    Q_PROPERTY(QVariantMap session READ session NOTIFY sessionChanged)
public:
    explicit PreferencesController(std::filesystem::path configDir, QObject* parent = nullptr);

    [[nodiscard]] QString theme() const;
    // « light » ou « dark » : « system » suit le schéma de couleurs de la plateforme.
    [[nodiscard]] QString effectiveTheme() const;
    [[nodiscard]] QString density() const;
    [[nodiscard]] double densityScale() const;
    [[nodiscard]] bool reducedMotion() const { return reducedMotion_; }
    [[nodiscard]] QStringList languages() const { return {QStringLiteral("fr")}; }
    [[nodiscard]] QVariantMap session() const;

    void setTheme(const QString& id);
    void setDensity(const QString& id);
    void setReducedMotion(bool on);

    // Mémorise l'état à restaurer : {file, workspace, editOpen, treeMode, zoom}. Valeurs
    // inconnues ignorées ; écrit aussitôt (une coupure ne perd rien).
    Q_INVOKABLE void rememberSession(const QVariantMap& state);
    Q_INVOKABLE void forgetSession();
    Q_INVOKABLE void resetDefaults();
    [[nodiscard]] const std::filesystem::path& preferencesFile() const { return prefsFile_; }
    [[nodiscard]] const std::filesystem::path& sessionFile() const { return extraFile_; }

signals:
    void changed();
    void sessionChanged();

private:
    void load();
    void save() const;

    std::filesystem::path prefsFile_;
    std::filesystem::path extraFile_;
    Preferences prefs_;
    bool reducedMotion_{false};
    std::map<std::string, std::string> session_;
};

} // namespace openstitch::desktop_v2::studio
