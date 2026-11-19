// SPDX-License-Identifier: Apache-2.0
// Mode « QML externe » de Studio Dev (BUILD-OPT-01.3) : charge les .qml depuis le disque et
// recharge la vue à chaud, sans recompiler le C++. Opt-in, jamais actif en Release.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>

class QFileSystemWatcher;
class QQmlApplicationEngine;
class QTemporaryDir;

namespace openstitch::desktop_v2::studio {

// Variable d'environnement : « 1 » = répertoire par défaut, « 0 » = coupé, autre = répertoire.
inline constexpr const char* kQmlDevEnv = "OPENSTITCH_DEV_QML";

struct QmlSourceInputs {
    bool capabilityCompiled = false; // option CMake OPENSTITCH_STUDIO_DEV_QML (OFF par défaut)
    bool releaseBuild = false;       // garde-fou : jamais d'externe en Release
    QString envValue;                // valeur de OPENSTITCH_DEV_QML (vide = non définie)
    QString defaultDir;              // apps/desktop_v2/studio/qml des sources
};

struct QmlSourceDecision {
    bool external = false;
    QString dir;    // répertoire des .qml (mode externe)
    QUrl mainUrl;   // URL de Main.qml à charger
    QString reason; // explication lisible (journal)
};

// Décision pure (testable sans Qt Quick) : embarqué sauf opt-in explicite valide.
QmlSourceDecision decideQmlSource(const QmlSourceInputs& in);

// Entrées réelles du processus : constantes de compilation + environnement.
QmlSourceInputs qmlSourceInputsFromBuild();

// Recharge à chaud. Le document vit dans les contrôleurs C++ (hors moteur) : seul l'arbre QML
// est recréé. En cas d'erreur QML, l'ancienne vue reste affichée et l'erreur est rapportée.
class QmlDevReloader : public QObject {
    Q_OBJECT
public:
    QmlDevReloader(QQmlApplicationEngine& engine, QUrl mainUrl, QString dir,
                   QObject* parent = nullptr);
    ~QmlDevReloader() override;

    // Synchrone ; vrai si la nouvelle vue a remplacé l'ancienne.
    bool reloadNow();
    void setWatching(bool on);
    [[nodiscard]] bool watching() const { return m_watching; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] int reloadCount() const { return m_reloads; }

signals:
    void reloaded();
    void reloadFailed(const QString& message);

private:
    void rescan();
    void showErrorOnView(const QString& message);

    QQmlApplicationEngine& m_engine;
    QUrl m_main;
    QString m_dir;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer m_debounce;
    bool m_watching = false;
    QString m_lastError;
    int m_reloads = 0;
    int m_generation = 0;
    std::unique_ptr<QTemporaryDir> m_snapshots;
};

} // namespace openstitch::desktop_v2::studio
