// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::document {

// Couleur d'un objet de broderie dans un coloris (HP-THR-006).
struct ColorwayEntry {
    ObjectId object;
    std::optional<thread_palette::ThreadKey> thread; // vide = couleur libre
    std::array<std::uint8_t, 3> rgb{};

    friend bool operator==(const ColorwayEntry&, const ColorwayEntry&) = default;
};

// Un coloris : un jeu de couleurs complet du même dessin (le motif, lui, ne change pas). C'est un
// instantané des fils de TOUS les objets de broderie au moment où il a été capturé ; les objets
// créés plus tard n'y figurent pas et gardent leur couleur à l'activation.
// Le coloris ACTIF est reflété par les objets eux-mêmes (ce sont eux la vérité, donc l'export, le
// rendu et la fiche de production utilisent déjà le bon coloris) ; son instantané est rafraîchi
// quand on en active un autre.
struct Colorway {
    std::string name;
    std::vector<ColorwayEntry> entries; // ordre du document au moment de la capture
};

// Tissu de fond (HP-THR-008) : couleur utilisée par le rendu et par l'analyse (fil trop proche du
// tissu). Défaut = blanc, l'apparence historique du canevas.
struct FabricSettings {
    std::array<std::uint8_t, 3> color{255, 255, 255};
    std::string name; // libellé libre (« Coton écru »…), facultatif
    std::string material; // identifiant libre (woven, jersey, terry, felt…), facultatif
    std::string stabilizer; // stabilisateur déclaré ; aucune heuristique textile non validée

    friend bool operator==(const FabricSettings&, const FabricSettings&) = default;
};

} // namespace openstitch::document
