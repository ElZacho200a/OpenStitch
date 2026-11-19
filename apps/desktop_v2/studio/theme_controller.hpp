// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <map>
#include <memory>

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "theme_library.hpp"

namespace openstitch::desktop_v2::studio {

class PreferencesController;

// Adaptateur Qt Quick du moteur de thèmes (core/theme_*, docs/desktop-v2/22-moteur-de-themes.md).
// Aucune logique de thème ici : le cœur charge, valide, hérite et résout ; ce contrôleur traduit
// le thème résolu en valeurs QML (couleurs, URL de fichiers locaux), enregistre ses polices,
// suit l'apparence demandée dans les préférences, persiste le choix et réalise les entrées /
// sorties propres à Qt (archives .zip, surveillance du dossier pour le rechargement à chaud).
// Un changement de thème ne touche ni au document ni au canevas : seules les liaisons QML qui
// lisent `Theme` se réévaluent.
class ThemeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap tokens READ tokens NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap styles READ styles NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap icons READ icons NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap meta READ meta NOTIFY themeChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY themeChanged)
    Q_PROPERTY(QVariantList themes READ themes NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY themeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int revision READ revision NOTIFY themeChanged)
    Q_PROPERTY(bool systemReducedMotion READ systemReducedMotion CONSTANT)
    Q_PROPERTY(QString userThemesDir READ userThemesDir CONSTANT)
public:
    // `builtinDir` : thèmes livrés (lecture seule) ; `userDir` : thèmes importés. `prefs` (peut
    // être nul) fournit l'apparence et garde l'identifiant du thème actif entre deux sessions.
    ThemeController(std::filesystem::path builtinDir, std::filesystem::path userDir,
                    PreferencesController* prefs, QObject* parent = nullptr);
    ~ThemeController() override;

    // Dossier des thèmes livrés : `<exe>/themes`, sinon les sources (exécution depuis le build).
    [[nodiscard]] static std::filesystem::path defaultBuiltinDir();

    [[nodiscard]] QVariantMap tokens() const { return tokens_; }
    [[nodiscard]] QVariantMap styles() const { return styles_; }
    [[nodiscard]] QVariantMap icons() const { return icons_; }
    [[nodiscard]] QVariantMap meta() const { return meta_; }
    [[nodiscard]] QString activeId() const;
    [[nodiscard]] QVariantList themes() const { return themes_; }
    [[nodiscard]] QVariantList diagnostics() const { return diagnostics_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] int revision() const { return revision_; }
    [[nodiscard]] bool systemReducedMotion() const;
    [[nodiscard]] QString userThemesDir() const;
    [[nodiscard]] const theme::ThemeManager& manager() const { return *manager_; }

    // Active un thème installé. Faux (et thème courant inchangé) s'il est invalide ; la raison
    // est dans `lastError`. `persist` faux : essai sans toucher aux préférences (recettes).
    Q_INVOKABLE bool activate(const QString& id, bool persist = true);
    // Neo Studio, apparence des préférences.
    Q_INVOKABLE void resetToDefault();
    // Relit les dossiers (thème édité à la main, ajouté par l'explorateur…).
    Q_INVOKABLE void reload();
    // Import depuis un dossier ou une archive .zip. Résultat : {ok, id, exists, messages[]}.
    // `exists` : un thème utilisateur de même identifiant est installé (rappeler avec replace).
    Q_INVOKABLE QVariantMap importFolder(const QUrl& folder, bool replace = false);
    Q_INVOKABLE QVariantMap importArchive(const QUrl& archive, bool replace = false);
    // Archive .zip du dossier du thème (fichiers autorisés seulement), racine « <id>/ ».
    Q_INVOKABLE bool exportTheme(const QString& id, const QUrl& archive);
    // Point de départ d'un nouveau thème : enfant vide de `parentId` dans le dossier utilisateur.
    Q_INVOKABLE QVariantMap createDerived(const QString& parentId, const QString& id,
                                          const QString& name);
    Q_INVOKABLE bool removeTheme(const QString& id);

signals:
    void themeChanged();
    void libraryChanged();
    void lastErrorChanged();

private:
    void apply();
    void refreshLibrary();
    void followPreferences();
    void watchActive();
    void setError(const QString& message);
    [[nodiscard]] theme::Appearance requestedAppearance() const;
    [[nodiscard]] QString resolveFamily(const std::string& list) const;
    [[nodiscard]] QVariantMap importResult(const theme::ThemeLibrary::ImportResult& result);

    std::unique_ptr<theme::ThemeManager> manager_;
    PreferencesController* prefs_{nullptr};
    QString prefsThemeId_; // dernier `themeId` lu dans les préférences
    QVariantMap tokens_;
    QVariantMap styles_;
    QVariantMap icons_;
    QVariantMap meta_;
    QVariantList themes_;
    QVariantList diagnostics_;
    QString lastError_;
    int revision_{0};
    std::map<std::string, int> loadedFonts_; // chemin -> id QFontDatabase (cache de session)
    QFileSystemWatcher watcher_;
    QTimer reloadTimer_;
};

} // namespace openstitch::desktop_v2::studio
