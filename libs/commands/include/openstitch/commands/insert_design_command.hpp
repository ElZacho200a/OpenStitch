// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// HP-FILE-007 : insère un design (projet .osp relu, ou design importé d'un fichier machine) dans le
// projet courant, en UNE commande annulable.
//  - Objets (vecteurs, broderie, texte) : copiés, translatés de `offset` (µm entiers) et
//    ré-identifiés avec le `IdGenerator` du document cible (jamais un id déjà émis). Les liens
//    internes (source_vector, text_owner, fondu) sont réécrits. Les calques, coloris, segmentation
//    et image source du design inséré ne sont pas repris ; les formes arrivent dans le calque actif
//    du projet (ou le calque du fond).
//  - Fichier machine : la séquence brute (positions absolues translatées de `offset`) est ajoutée
//    à la fin du design importé du projet, ou devient le design importé d'un projet sans objet.
//    Un design importé ne se mélange pas avec des objets éditables (la séquence brute remplace la
//    régénération) : refus dans ce cas.
// Renvoie nullptr (et `why` renseigné) si l'insertion est impossible ou vide.
[[nodiscard]] std::unique_ptr<ICommand> make_insert_design_command(const document::Project& target,
                                                                   document::Project incoming,
                                                                   Vec2um offset,
                                                                   std::string* why = nullptr);

class InsertDesignCommand final : public ICommand {
public:
    InsertDesignCommand(document::Project incoming, Vec2um offset);

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;
    [[nodiscard]] std::string name() const override { return "Inserer un design"; }

    // Identifiants des formes ajoutées (vide avant le premier `apply`) : sert à les sélectionner.
    [[nodiscard]] const std::vector<ObjectId>& insertedVectorIds() const { return vector_ids_; }

private:
    document::Project incoming_;
    Vec2um offset_;
    bool prepared_{false};
    std::vector<ObjectId> vector_ids_;
    std::vector<ObjectId> embroidery_ids_;
    std::vector<ObjectId> text_ids_;
    std::optional<document::ImportedDesign> previous_imported_;
    bool imported_touched_{false};
};

} // namespace openstitch::commands
