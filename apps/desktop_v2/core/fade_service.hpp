// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- fondu de deux fils sur un remplissage (dégradé de densité) : crée, édite, supprime
// le couple de couches d'un fondu via `project_ops::gradient_fill`, chaque fois par UNE `ICommand`
// annulable. Sans Qt. Le moteur décide de la compatibilité (`gradient_incompatibility`) : on relaie
// son message tel quel.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

struct FadeSettings {
    std::array<std::uint8_t, 3> foreground{255, 255, 255}; // fil du dessus
    bool complementary{true};                              // rampes de densité opposées
    int texture{0};  // 0 régulier, 1 bords irréguliers, 2 micro-tramage
    int geometry{0}; // 0 linéaire, 1 radial, 2 courbe
};

// Texte « pourquoi ce remplissage ne peut pas porter un fondu » ; vide = compatible.
[[nodiscard]] std::string fadeIncompatibility(const document::Project& project,
                                              std::uint64_t objectId);

// Réglages actuels du fondu dont `objectId` est une couche ; rien si l'objet n'en porte pas.
[[nodiscard]] std::optional<FadeSettings> readFade(const document::Project& project,
                                                   std::uint64_t objectId);

// Crée un fondu sur le remplissage `objectId` : deux couches, le fil du dessus choisi.
ServiceResult createFade(DocumentSession& session, std::uint64_t objectId,
                         const FadeSettings& settings);
// Modifie un fondu existant (fil du dessus, rampes complémentaires ou non).
ServiceResult editFade(DocumentSession& session, std::uint64_t objectId,
                       const FadeSettings& settings);
ServiceResult removeFade(DocumentSession& session, std::uint64_t objectId);

// Le vrai camaïeu (2 à 8 fils) s'appuie sur un groupe n-aire persistant,
// jamais sur des paires de fondus chaînées.
struct MultiFadeSettings {
    std::vector<std::array<std::uint8_t, 3>> colors;
    bool complementary{true};
    int texture{0};
    int geometry{0};
};

[[nodiscard]] std::optional<MultiFadeSettings> readMultiFade(const document::Project& project,
                                                             std::uint64_t objectId);
// Orientation de l'axe from->to en degrés modèle (-180..180), commune aux fondus binaires et
// n-aires. Le radial conserve son axe interne mais l'UI ne présente pas cet angle comme un réglage.
[[nodiscard]] std::optional<double> readFadeAngle(const document::Project& project,
                                                  std::uint64_t objectId);
// Tourne l'axe autour de son centre sans changer sa longueur, en une commande annulable.
ServiceResult setFadeAngle(DocumentSession& session, std::uint64_t objectId, double degrees);
ServiceResult createMultiFade(DocumentSession& session, std::uint64_t objectId,
                              const MultiFadeSettings& settings);
ServiceResult editMultiFade(DocumentSession& session, std::uint64_t objectId,
                            const MultiFadeSettings& settings);
// Profil d'une couche (0..N-1) : chaque modification est une seule commande undoable.
ServiceResult setMultiFadeLayerCurve(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t layerIndex, int curve);
ServiceResult removeMultiFade(DocumentSession& session, std::uint64_t objectId);

} // namespace openstitch::desktop_v2
