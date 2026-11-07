// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "action_catalog.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Commande de la palette (hors catalogue d'actions paramétrées) : une intention de l'interface
// (fichier, édition, sélection, espace, vue, outil, aide). Source UNIQUE : la palette, la feuille
// des raccourcis et les tests la lisent ; Main.qml l'exécute par un seul répartiteur.
struct StudioCommand {
    std::string id;    // « cmd.file.open », « cmd.tool.draw.ellipse »…
    std::string label; // français = langue source
    std::string group;
    std::string shortcut; // affichage ; vide = aucun
    std::string needs;    // "" | "selection" | "multi" | "undo" | "redo" | "idle"
};
[[nodiscard]] const std::vector<StudioCommand>& studioCommands();

// Qt adapter over the declarative action catalog (same definitions as the CLI, Claude and the
// Widgets shell): it lists actions, describes their fields so QML can build a form, validates and
// runs them through DocumentSession (one undo step). No engine logic lives here.
class ActionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY resultChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY resultChanged)
public:
    explicit ActionController(StudioController& studio, QObject* parent = nullptr);
    ~ActionController() override;
    // [{id, label, group, needsSelection, hasFields, enabled}] filtered by an accent-insensitive
    // query ("" = all).
    Q_INVOKABLE [[nodiscard]] QVariantList search(const QString& query) const;
    [[nodiscard]] QVariantList actions() const { return search({}); }
    // Palette universelle : actions du catalogue ET commandes de l'interface, filtrage insensible
    // aux accents (mots multiples), activées d'abord. [{id,label,group,shortcut,kind,enabled,
    // reason,needsSelection,hasFields}]
    Q_INVOKABLE [[nodiscard]] QVariantList searchAll(const QString& query) const;
    // Commandes seules (voir StudioCommand) avec leur état courant.
    Q_INVOKABLE [[nodiscard]] QVariantList commands() const;
    // Vérifie l'état puis demande l'exécution au répartiteur de l'interface (`commandRequested`).
    // "" = demandé ; sinon la raison du refus (commande grisée ou inconnue).
    Q_INVOKABLE QString runCommand(const QString& id);
    // [{key, label, kind, min, max, value, unit, choices:[{value,label}], required}]
    Q_INVOKABLE [[nodiscard]] QVariantList fields(const QString& id) const;
    // Validates then runs; returns "" on success, otherwise a readable refusal.
    Q_INVOKABLE QString run(const QString& id, const QVariantMap& values);
    [[nodiscard]] QString lastMessage() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
signals:
    void commandRequested(const QString& id);
    void actionsChanged();
    void resultChanged();

private:
    [[nodiscard]] const ActionDef* find(const QString& id) const;
    // "" = exécutable, sinon la raison de l'indisponibilité.
    [[nodiscard]] QString commandReason(const StudioCommand& command) const;
    [[nodiscard]] ActionContext context() const;
    QString finish(const ActionDef& def, bool ok, const QString& message);
    StudioController& studio_;
    DocumentSession::Token token_{};
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
