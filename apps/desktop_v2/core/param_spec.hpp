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

// Number / Integer / Bool / Choice : réglages scalaires (valeur unique). Les trois autres natures
// portent un état « défini / absent » ou une collection : Point = point facultatif [x, y] en mm
// (`readPointMm`, `batchSetPoint`), OptionalNumber = nombre facultatif (absent = automatique ;
// s'écrit comme un Number, s'efface par `batchClearParam`), List = collection éditée sur le
// canevas (guides, ruptures, barreaux, dégradé) dont l'inspecteur montre l'effectif et que
// `batchClearParam` vide.
enum class ParamKind { Number, Integer, Bool, Choice, Point, OptionalNumber, List };

[[nodiscard]] constexpr bool isScalarKind(ParamKind kind) {
    return kind == ParamKind::Number || kind == ParamKind::Integer || kind == ParamKind::Bool ||
           kind == ParamKind::Choice;
}

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

// Niveau d'un réglage : les essentiels sont toujours visibles ; les avancés se révèlent à la
// demande (case « Réglages avancés ») ou par la recherche. Dérivé de la clé, pas stocké par ligne.
// `Expert` : graines, seuils, points d'entrée / sortie, collections de guides : visibles seulement
// avec le niveau « Expert » ou par la recherche.
enum class ParamLevel { Essential, Advanced, Expert };
[[nodiscard]] ParamLevel paramLevel(const ParamSpec& spec);

// Groupe d'affichage du réglage (« Points », « Sous-couche », « Compensation de tirage »,
// « Extrémités », « Entrée, sortie et fixation », « Coins et bords », « Géométrie et guides »,
// « Aspect fait main »). Dérivé de la clé.
[[nodiscard]] std::string_view paramGroup(const ParamSpec& spec);

// Vrai si `query` (sans accent ni casse) apparaît dans le libellé ou la clé du réglage.
[[nodiscard]] bool paramMatches(const ParamSpec& spec, std::string_view query);

// Options d'un réglage de nature Choice : (valeur envoyée au moteur, libellé).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> paramOptions(const ParamSpec& spec);

// Réglages SCALAIRES du type (Number / Integer / Bool / Choice) ; vide si le type n'en expose pas.
// C'est la vue historique (shell Widgets) : elle ne contient jamais de Point / List.
[[nodiscard]] std::vector<ParamSpec> paramSpecs(std::string_view typeId);
// Tous les réglages du type, non scalaires compris (inspecteur Studio, test de complétude).
[[nodiscard]] std::vector<ParamSpec> allParamSpecs(std::string_view typeId);
// Types de points qui possèdent une table de réglages (identifiants de `stitchTypeId`).
[[nodiscard]] const std::vector<std::string_view>& paramTypeIds();

// Valeur actuelle du réglage `key` de l'objet (unité d'affichage ; booléen = 0 / 1), ou rien si le
// type de l'objet n'a pas ce réglage.
[[nodiscard]] std::optional<double> readParam(const document::EmbroideryObject& object,
                                              std::string_view key);

// Point facultatif (Point) en mm, ou rien s'il est absent / si la clé n'est pas un point.
[[nodiscard]] std::optional<std::pair<double, double>>
readPointMm(const document::EmbroideryObject& object, std::string_view key);
// Vrai si le réglage facultatif (Point, OptionalNumber, List non vide) est défini.
[[nodiscard]] bool paramIsSet(const document::EmbroideryObject& object, std::string_view key);

// Lot `set_params` qui fixe un point (mm, dans ±5000) ; vide si la clé n'est pas un Point de ce
// type, si une coordonnée n'est pas finie / hors plage, ou s'il n'y a aucun objet.
[[nodiscard]] std::string batchSetPoint(const std::vector<std::uint64_t>& ids,
                                        std::string_view typeId, std::string_view key, double xMm,
                                        double yMm);
// Lot qui efface un réglage Point / OptionalNumber (null) ou vide une List ([] ; null pour le
// dégradé) ; vide pour toute autre nature.
[[nodiscard]] std::string batchClearParam(const std::vector<std::uint64_t>& ids,
                                          std::string_view typeId, std::string_view key);

// Lot `set_params` pour tous les objets ; chaîne vide si la clé est inconnue, la valeur non finie
// ou hors [min ; max] de la spécification de `typeId`, ou s'il n'y a aucun objet.
[[nodiscard]] std::string batchSetParam(const std::vector<std::uint64_t>& ids,
                                        std::string_view typeId, std::string_view key,
                                        double value);

} // namespace openstitch::desktop_v2
