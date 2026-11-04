// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- réglages de couture essentiels par type de point, pilotés par une TABLE (principe E
// : paramètres essentiels d'abord) et appliqués par l'opération `set_params` (mise à jour
// partielle, une étape d'annulation) : le même chemin que Claude et la CLI. Sans Qt. Les valeurs
// sont exprimées dans l'unité d'affichage (mm, degrés, entier, booléen) ; la conversion µm /
// radians est ici.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::desktop_v2 {

enum class ParamKind { Number, Integer, Bool, Choice };

struct ParamSpec {
    std::string_view type;  // identifiant de type (`stitchTypeId`) auquel le réglage s'applique
    std::string_view key;   // nom du champ de `set_params` (« row_spacing_mm »)
    std::string_view label; // libellé affiché (français = langue source)
    ParamKind kind{ParamKind::Number};
    double min{0};
    double max{0};
    double step{0.1};
    std::string_view unit; // « mm », « ° », « » ; affichage seulement
    // Choice : « valeur|libellé;valeur|libellé » ; la valeur lue / écrite est l'INDEX (0 .. n-1).
    std::string_view options;
};

// Options d'un réglage de nature Choice : (valeur envoyée au moteur, libellé).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> paramOptions(const ParamSpec& spec);

// Réglages essentiels du type ; vide si le type n'en expose pas (encore).
[[nodiscard]] std::vector<ParamSpec> paramSpecs(std::string_view typeId);

// Valeur actuelle du réglage `key` de l'objet (unité d'affichage ; booléen = 0 / 1), ou rien si le
// type de l'objet n'a pas ce réglage.
[[nodiscard]] std::optional<double> readParam(const document::EmbroideryObject& object,
                                              std::string_view key);

// Lot `set_params` pour tous les objets ; chaîne vide si la clé est inconnue, la valeur non finie
// ou hors [min ; max] de la spécification de `typeId`, ou s'il n'y a aucun objet.
[[nodiscard]] std::string batchSetParam(const std::vector<std::uint64_t>& ids,
                                        std::string_view typeId, std::string_view key,
                                        double value);

} // namespace openstitch::desktop_v2
