// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace openstitch::project_ops {

// Pont « projet vivant » (ADR 0001) : permet à un agent d'inspecter et de modifier le document
// ACTIF d'une application hôte (le desktop) sans second exemplaire concurrent.
//
// Le pont ne possède rien : l'hôte lui prête son `Project` et son `UndoStack`. Sans Qt, sans
// fichier, sans thread : tous les appels se font sur le fil qui possède le document (fil GUI).
//
// Révision : monotone, incrémentée à chaque changement visible du document (commande exécutée,
// Undo, Redo, remplacement). Elle dérive de `UndoStack::version()` ; les remplacements hors pile
// (nouveau document, ouverture) appellent `reset()`.
class LiveBridge {
public:
    // `session_id` vide = identifiant aléatoire (128 bits, hexadécimal) ; imposé pour les tests.
    LiveBridge(document::Project& project, commands::UndoStack& stack, std::string session_id = {});

    [[nodiscard]] const std::string& session_id() const { return session_id_; }
    [[nodiscard]] std::uint64_t revision();
    [[nodiscard]] bool closed() const { return closed_; }

    // Instantané en lecture seule, figé à la révision courante. Copie : l'appelant peut calculer
    // longuement (rendu, qualité, comparaison) sans toucher au document.
    struct Snapshot {
        std::string session_id;
        std::uint64_t revision{0};
        std::shared_ptr<const document::Project> project;
    };
    [[nodiscard]] Snapshot snapshot();

    struct Result {
        bool ok{false};      // lot accepté (et appliqué pour `commit`)
        bool applied{false}; // le document a changé (une seule étape d'annulation)
        std::string code;    // `code::k...` si refus
        std::string message;
        std::uint64_t revision{0}; // révision après l'appel
        BatchReport report;        // rapport du lot (vide si refus de session)
    };

    // Simule le lot sur une copie : ni dirty, ni Undo, ni révision. Mêmes refus qu'une application.
    [[nodiscard]] Result preview(std::string_view session_id, std::uint64_t revision,
                                 std::string_view batch_json);

    // Applique le lot au document vivant en UNE étape d'annulation (`label` nomme l'étape).
    // `approved` : l'hôte atteste d'une approbation explicite de l'utilisateur (sinon
    // `E_APPROVAL_REQUIRED`). Refus si la session est close, la révision périmée, ou un commit
    // en cours. En cas d'échec le document reste strictement inchangé.
    [[nodiscard]] Result commit(std::string_view session_id, std::uint64_t revision,
                                std::string_view batch_json, const std::string& label,
                                bool approved);

    // Le document hôte a été remplacé hors pile (nouveau, ouverture) : nouvelle session, ancienne
    // close, révision toujours croissante.
    void reset(std::string session_id = {});
    // L'hôte se ferme : tout appel ultérieur répond `E_SESSION_CLOSED`.
    void close() { closed_ = true; }

private:
    [[nodiscard]] Result guard(std::string_view session_id, std::uint64_t revision);
    void sync();

    document::Project& project_;
    commands::UndoStack& stack_;
    std::string session_id_;
    std::uint64_t revision_{1};
    std::uint64_t seen_version_{0};
    bool closed_{false};
    bool busy_{false};
};

} // namespace openstitch::project_ops
