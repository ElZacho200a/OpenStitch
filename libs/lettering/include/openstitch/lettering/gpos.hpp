// SPDX-License-Identifier: Apache-2.0
// Crénage par paires de la table OpenType GPOS (HP-TXT-003) : lecteur minimal et
// borné, sans dépendance (ni FreeType ni HarfBuzz). Ne lit que la fonctionnalité
// « kern » et les consultations PairPos (formats 1 et 2, y compris via Extension) ;
// seul l'avancement horizontal du premier glyphe est utilisé.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace openstitch::lettering {

class GposKerning {
public:
    // Analyse la table GPOS brute. nullptr si la table est invalide ou ne porte
    // aucune consultation de crénage.
    [[nodiscard]] static std::unique_ptr<GposKerning> parse(std::vector<std::uint8_t> table);

    // Ajustement d'avancement (unités de police, signé) de la paire de glyphes
    // (indices de glyphes, pas des points de code) ; 0 si la paire n'est pas crénée.
    [[nodiscard]] double pair_adjustment(std::uint16_t left, std::uint16_t right) const;

private:
    GposKerning() = default;
    std::vector<std::uint8_t> data_;
    // Chaque consultation = liste de décalages de sous-tables PairPos (depuis data_).
    std::vector<std::vector<std::uint32_t>> lookups_;
};

} // namespace openstitch::lettering
