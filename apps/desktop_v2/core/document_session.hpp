// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- session documentaire : l'UNIQUE porte d'entrée vers le projet vivant.
// Elle possède le `Project`, la `UndoStack` et la `SelectionModel`. Aucune autre couche ne tient de
// pointeur mutable sur le projet : toute mutation passe par `execute` (ICommand) ou `applyBatch`
// (project_ops, le MÊME chemin que la CLI et le compagnon IA), puis les abonnés sont notifiés.
// Sans Qt.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/live_bridge.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "selection_model.hpp"

namespace openstitch::desktop_v2 {

enum class ChangeKind : std::uint8_t { Edited, Undone, Redone, Replaced };

class DocumentSession {
public:
    using Listener = std::function<void(ChangeKind)>;
    using Token = std::size_t;

    DocumentSession();

    // --- Lecture ---------------------------------------------------------------------------
    [[nodiscard]] const document::Project& project() const { return project_; }
    // Copie immuable du projet, partagée entre appelants à la même révision : c'est ce que reçoit
    // une tâche d'arrière-plan (jamais le projet vivant).
    [[nodiscard]] std::shared_ptr<const document::Project> snapshot() const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] bool dirty() const { return !undo_.isClean(); }
    [[nodiscard]] bool canUndo() const { return undo_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return undo_.canRedo(); }
    [[nodiscard]] std::string undoName() const { return undo_.undoName(); }
    [[nodiscard]] std::string redoName() const { return undo_.redoName(); }
    [[nodiscard]] std::vector<std::string> undoNames() const { return undo_.undoNames(); }
    [[nodiscard]] std::vector<std::string> redoNames() const { return undo_.redoNames(); }
    // CX11: positions belong to the one command stack, never to the monotonic revision.
    [[nodiscard]] std::size_t historyPosition() const { return undo_.position(); }
    [[nodiscard]] std::size_t historySize() const { return undo_.historySize(); }
    [[nodiscard]] long long savedHistoryPosition() const { return undo_.cleanPosition(); }

    // --- Sélection (source de vérité unique, élaguée après chaque changement) -----------------
    [[nodiscard]] SelectionModel& selection() { return selection_; }
    [[nodiscard]] const SelectionModel& selection() const { return selection_; }
    [[nodiscard]] bool exists(const Target& target) const;

    // Réserve l'identifiant suivant (jamais réutilisé). Ce n'est PAS une modification du document :
    // le compteur n'est ni annulable ni visible ; c'est la seule écriture hors commande.
    // Pour les fabriques du moteur qui RÉSERVENT des identifiants avant de rendre une commande
    // (fondus) : ne jamais modifier autre chose que le compteur d'identifiants par ce biais.
    [[nodiscard]] document::Project& projectForIdAllocation() { return project_; }
    [[nodiscard]] ObjectId allocateObjectId() { return project_.object_ids.next(); }

    // --- Mutations --------------------------------------------------------------------------
    void execute(std::unique_ptr<commands::ICommand> command);
    bool undo();
    bool redo();
    // Jump within the existing history. Reject invalid positions before touching the project;
    // notify/prune once at the final state, so observers never rebuild intermediate geometry.
    bool navigateHistory(std::size_t position);

    struct BatchResult {
        project_ops::BatchReport report;
        bool committed{false}; // une étape d'annulation a été empilée
    };
    // Aperçu : valide ET simule sur une copie ; le projet vivant n'est jamais touché.
    [[nodiscard]] BatchResult previewBatch(std::string_view batchJson) const;
    // Applique le lot de façon transactionnelle puis l'empile en UNE étape d'annulation `label`.
    // Refus (opération invalide) : projet strictement inchangé, `committed == false`.
    BatchResult applyBatch(std::string_view batchJson, const std::string& label);

    // Ouverture / numérisation terminée : installe un projet et repart d'un historique vierge
    // (`clean` : le document est considéré comme enregistré).
    void replaceProject(document::Project project, bool clean);
    // Pont « projet vivant » (ADR 0001) lié à CE projet et à CETTE pile d'annulation : un agent
    // (MCP) modifie le même document que l'utilisateur, avec le même historique. Créé à la
    // demande ; renouvelé (nouvelle session) quand le document est remplacé.
    [[nodiscard]] project_ops::LiveBridge& liveBridge();
    // À appeler après une modification installée par le pont : révision, sélection et abonnés
    // suivent comme pour un Annuler / Rétablir.
    void notifyExternalChange() { changed(ChangeKind::Edited); }
    void markSaved() {
        undo_.markClean();
        notify(ChangeKind::Edited);
    }

    // --- Abonnements ------------------------------------------------------------------------
    [[nodiscard]] Token subscribe(Listener listener);
    void unsubscribe(Token token);

private:
    void changed(ChangeKind kind);
    void notify(ChangeKind kind);

    document::Project project_;
    commands::UndoStack undo_;
    SelectionModel selection_;
    std::uint64_t revision_{1};
    mutable std::shared_ptr<const document::Project> cachedSnapshot_;
    mutable std::uint64_t cachedRevision_{0};
    std::vector<std::pair<Token, Listener>> listeners_;
    Token nextToken_{1};
    std::unique_ptr<project_ops::LiveBridge> bridge_;
};

} // namespace openstitch::desktop_v2
