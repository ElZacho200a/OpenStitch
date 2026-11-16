// SPDX-License-Identifier: Apache-2.0
#pragma once

// Adaptateur Qt du journal d'usage local (`core/usage_log`) : le QML y déclare un clic
// (`usage.click(id)`), le C++ y déclare outils, actions et espaces. Aucune logique métier.
// Voir docs/source/usage-log.md pour le format, la confidentialité et la désactivation.

#include <QObject>
#include <QString>

#include "usage_log.hpp"

namespace openstitch::desktop_v2::studio {

class ActionController;
class ToolController;

class UsageController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Vrai si OPENSTITCH_USAGE_LOG=0 coupe le journal : l'interrupteur ne peut pas le réactiver.
    Q_PROPERTY(bool lockedByEnvironment READ lockedByEnvironment CONSTANT)
    Q_PROPERTY(QString folder READ folder CONSTANT)
public:
    explicit UsageController(UsageLog& log, QObject* parent = nullptr);

    [[nodiscard]] bool enabled() const { return log_.enabled(); }
    [[nodiscard]] bool lockedByEnvironment() const { return lockedByEnvironment_; }
    // Persiste le choix par le fichier `DISABLED` du dossier du journal (valable d'une version à
    // l'autre). Sans effet si l'environnement coupe le journal ou si le fichier ne peut pas être
    // écrit.
    void setEnabled(bool on);
    [[nodiscard]] QString folder() const;

    // Un contrôle cliqué : l'identifiant est le `objectName` du bouton, à défaut son texte.
    Q_INVOKABLE void click(const QString& id);
    // Événement d'un autre type depuis le QML (changement d'espace, par exemple).
    Q_INVOKABLE void track(const QString& kind, const QString& id, const QString& detail = {});
    void record(const QString& kind, const QString& id, const QString& detail = {});

    // Journalise les actions exécutées (ActionController::ran), les changements d'outil et, par un
    // filtre d'événements de l'application, les raccourcis clavier (kind "key").
    void observe(ActionController& actions, ToolController& tools);

    // Dossier et activation selon l'environnement et l'emplacement des données locales.
    [[nodiscard]] static UsageLogOptions defaultOptions(const QString& version);

signals:
    void enabledChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    UsageLog& log_;
    bool lockedByEnvironment_{false};
};

} // namespace openstitch::desktop_v2::studio
