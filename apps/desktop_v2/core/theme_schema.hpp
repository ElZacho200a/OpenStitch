// SPDX-License-Identifier: Apache-2.0
#pragma once

// Moteur de thèmes du Studio -- schéma déclaratif (docs/desktop-v2/22-moteur-de-themes.md).
//
// Un thème est fait de DONNÉES seulement : jetons (couleurs, formes, typographie, effets,
// mouvements) et styles de composants (surcharges par variante et par état). Ce fichier fixe la
// liste fermée des jetons connus, leur type, leurs bornes et leur valeur de repli (Neo Studio,
// sombre et clair) : un thème partiel, ou même aucun fichier, donne toujours une interface
// complète. Sans Qt : l'adaptateur `studio/theme_controller` traduit ces valeurs pour Qt Quick.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace openstitch::desktop_v2::theme {

// Version du format de fichier que ce Studio écrit et lit nativement. Les versions antérieures
// sont migrées à la lecture (`theme_package.cpp`, `migrateTokens`) ; une version supérieure est
// refusée avec un message explicite.
inline constexpr int kSchemaVersion = 1;
// Niveau d'API du moteur : un thème peut exiger `compatibility.minEngine` / `maxEngine`.
inline constexpr int kEngineLevel = 1;

struct Color {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};
    bool operator==(const Color&) const = default;
};

// « #rgb », « #rrggbb », « #rrggbbaa » (ordre CSS, alpha en dernier) ou « transparent ».
[[nodiscard]] std::optional<Color> parseColor(std::string_view text);
// « #rrggbb » si opaque, sinon « #rrggbbaa ».
[[nodiscard]] std::string formatColor(Color color);
// Contraste WCAG 2.x entre deux couleurs opaques (alpha ignoré), de 1 à 21.
[[nodiscard]] double contrastRatio(Color a, Color b);

enum class TokenType {
    Color,  // couleur
    Number, // nombre borné (px, ms, facteur)
    Text,   // texte libre (liste de familles de polices séparées par des virgules…)
    Easing, // nom de courbe Qt Quick (OutCubic, OutBack, InOutQuad…)
    Asset,  // chemin relatif vers un fichier du thème (texture, illustration)
    Choice, // valeur parmi une liste fermée
};

// Une valeur résolue. Les assets sont des chemins ABSOLUS après résolution (vide = absent).
using TokenValue = std::variant<Color, double, std::string>;

enum class Appearance { Dark, Light };
[[nodiscard]] std::string_view appearanceId(Appearance appearance); // "dark" | "light"
[[nodiscard]] std::optional<Appearance> parseAppearance(std::string_view id);

struct TokenSpec {
    std::string_view key; // « famille.nom », ex. « color.accent »
    TokenType type;
    std::string_view darkDefault;  // valeur de repli textuelle (Neo Studio sombre)
    std::string_view lightDefault; // vide = identique au sombre
    double min{0};
    double max{0};
    std::string_view choices{}; // Choice : valeurs séparées par « | »
    std::string_view doc{};
};

// Tous les jetons connus, triés par clé. Les clés « custom.* » sont libres (extensions futures)
// et ne figurent pas ici.
[[nodiscard]] std::span<const TokenSpec> tokenSchema();
[[nodiscard]] const TokenSpec* findToken(std::string_view key);
[[nodiscard]] bool isCustomTokenKey(std::string_view key);

// Propriétés d'un style de composant (surface + contenu). Liste fermée : une propriété inconnue
// est signalée et ignorée.
struct StylePropertySpec {
    std::string_view key;
    TokenType type;
    double min{0};
    double max{0};
    std::string_view choices{};
    std::string_view doc{};
};
[[nodiscard]] std::span<const StylePropertySpec> stylePropertySchema();
[[nodiscard]] const StylePropertySpec* findStyleProperty(std::string_view key);

// États d'un composant, du plus faible au plus fort. « normal » est la base de tous les autres.
[[nodiscard]] std::span<const std::string_view> componentStates();

struct ComponentSpec {
    std::string_view id;       // « button », « slider.handle »…
    std::string_view variants; // variantes nommées séparées par « | » (vide = aucune)
    std::string_view doc{};
};
[[nodiscard]] std::span<const ComponentSpec> componentSchema();
[[nodiscard]] const ComponentSpec* findComponent(std::string_view id);
[[nodiscard]] bool componentHasVariant(const ComponentSpec& component, std::string_view variant);

// Convertit une valeur textuelle de fichier selon un type ; nullopt si invalide (type, borne,
// choix). Les assets restent relatifs ici (la résolution de chemin vient ensuite).
[[nodiscard]] std::optional<TokenValue> parseTypedValue(TokenType type, double min, double max,
                                                        std::string_view choices,
                                                        std::string_view text);
[[nodiscard]] std::optional<TokenValue> parseTypedNumber(TokenType type, double min, double max,
                                                         double number);

} // namespace openstitch::desktop_v2::theme
