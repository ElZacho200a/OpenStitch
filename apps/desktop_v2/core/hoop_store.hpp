// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- « Mes cadres » : cadres personnels de l'utilisateur, conservés hors document dans
// un petit fichier texte (une ligne par cadre : nom;forme;largeur;hauteur;rayon;marge, en mm). La
// bibliothèque générique du moteur n'est jamais modifiée. Lecture tolérante : une ligne illisible
// est ignorée sans faire échouer les autres. Sans Qt, déterministe.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

enum class UserHoopShape { Rectangle, Ellipse, RoundedRectangle };

struct UserHoop {
    std::string name;
    UserHoopShape shape{UserHoopShape::Rectangle};
    double widthMm{100.0};
    double heightMm{100.0};
    double cornerRadiusMm{0.0}; // RoundedRectangle uniquement
    double marginMm{0.0};
};

inline constexpr std::size_t kMaxUserHoops = 50;

// Refus clair (message) si le cadre n'est pas valide ; chaîne vide sinon. Le nom : 1 à 40
// caractères, ni « ; » ni caractère de contrôle ; dimensions 10 à 1000 mm ; marge < moitié du petit
// côté.
[[nodiscard]] std::string validateUserHoop(const UserHoop& hoop);

// Ajoute ou REMPLACE (nom identique sans tenir compte de la casse) ; refuse un cadre invalide ou le
// dépassement de `kMaxUserHoops`. Garde l'ordre d'insertion.
[[nodiscard]] ServiceResult addUserHoop(std::vector<UserHoop>& list, const UserHoop& hoop);
// Retire le cadre de ce nom ; faux si absent.
bool removeUserHoop(std::vector<UserHoop>& list, const std::string& name);
[[nodiscard]] const UserHoop* findUserHoop(const std::vector<UserHoop>& list,
                                           const std::string& name);

[[nodiscard]] std::string userHoopsToText(const std::vector<UserHoop>& list);
// Texte vide, inconnu ou en partie corrompu : on garde les lignes valides, jamais d'échec.
[[nodiscard]] std::vector<UserHoop> userHoopsFromText(const std::string& text);
// Écriture atomique (fichier temporaire puis renommage).
[[nodiscard]] bool saveUserHoops(const std::filesystem::path& file,
                                 const std::vector<UserHoop>& list);
[[nodiscard]] std::vector<UserHoop> loadUserHoops(const std::filesystem::path& file);

// Cadre actuel du projet, sous ce nom (forme, dimensions, marge relues du document).
[[nodiscard]] UserHoop userHoopFromProject(const document::Project& project,
                                           const std::string& name);
// Cadre du moteur correspondant (le rayon n'est gardé que pour un rectangle arrondi).
[[nodiscard]] document::Canvas canvasOfUserHoop(const UserHoop& hoop);
// Pose ce cadre sur un projet NEUF, avant qu'il ne rejoigne la session (pas de commande : rien à
// annuler) ; faux, projet inchangé, si le cadre est invalide.
bool seedUserHoop(document::Project& project, const UserHoop& hoop);

// Dernier cadre choisi : retenu hors document (même format que « Mes cadres », une ligne) pour
// que chaque nouveau projet parte du cadre utilisé en dernier.
inline constexpr const char* kLastHoopName = "dernier";
[[nodiscard]] bool saveLastHoop(const std::filesystem::path& file,
                                const document::Project& project);
// Absent ou illisible : aucun cadre retenu.
[[nodiscard]] std::optional<UserHoop> loadLastHoop(const std::filesystem::path& file);

// Applique ce cadre au projet en UNE commande annulable ; refus si invalide ou inchangé.
ServiceResult applyUserHoop(DocumentSession& session, const UserHoop& hoop);

} // namespace openstitch::desktop_v2
