// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- nuanciers de fils et mélange : parcourir les nuanciers compilés du moteur,
// chercher par nom ou référence, trouver les fils les plus proches d'une couleur (CIEDE2000),
// assigner un fil de nuancier à un fil du plan, et proposer un MÉLANGE de deux fils (fils mêlés
// ou torsadés) qui approche une couleur mieux qu'un fil seul. Sans Qt, déterministe.
//
// HONNÊTETÉ : les RGB des nuanciers sont des approximations publiées par les fabricants et le
// mélange est simulé en lumière linéaire ; ni l'un ni l'autre ne remplace un essai sur tissu.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"

namespace openstitch::desktop_v2 {

using Rgb = std::array<std::uint8_t, 3>;

struct ChartInfo {
    std::string id;
    std::string name;
    std::string source;
    std::size_t count{0};
    bool user{false}; // nuancier importé par l'utilisateur (retirable)
};

struct ThreadChoice {
    std::string chartId;
    std::string code;
    std::string name;
    std::string brand;
    Rgb rgb{};
    double distance{0.0}; // CIEDE2000 à la couleur cherchée (0 quand elle n'a pas de sens)
};

[[nodiscard]] std::vector<ChartInfo> chartInfos();

// --- Nuanciers de l'utilisateur (les cartes de marques ne sont pas fournies : on charge les
// siennes) --- Lit un `.csv` ou `.json` (formats de `project_io::thread_chart_io`), le copie dans
// `directory` (créé au besoin) pour le retrouver au prochain démarrage, et l'ajoute aux nuanciers.
// Refus clair : fichier illisible, ligne invalide (numéro cité), identifiant déjà pris par un
// nuancier intégré ou importé (jamais de remplacement silencieux).
[[nodiscard]] Result<ChartInfo> importThreadChart(const std::filesystem::path& file,
                                                  const std::filesystem::path& directory);
// Relit les nuanciers copiés dans `directory` ; renvoie les messages des fichiers refusés.
std::vector<std::string> loadUserCharts(const std::filesystem::path& directory);
// Retire un nuancier IMPORTÉ (et sa copie dans `directory`) ; faux pour un intégré ou un inconnu.
bool removeUserChart(const std::string& chartId, const std::filesystem::path& directory);
// Fils d'un nuancier dont le nom ou la référence contient `query` (sans casse), dans l'ordre du
// nuancier, au plus `limit`. `query` vide : les premiers fils. Nuancier inconnu : vide.
[[nodiscard]] std::vector<ThreadChoice>
searchThreads(const std::string& chartId, const std::string& query, std::size_t limit = 200);
// Les `topN` fils du nuancier les plus proches de `rgb`, du plus proche au plus lointain.
[[nodiscard]] std::vector<ThreadChoice> nearestThreads(Rgb rgb, const std::string& chartId,
                                                       std::size_t topN = 8);

// Donne à tous les objets de l'entrée `entry` du plan de fils (`threadEntries`) ce fil de nuancier
// et sa couleur : UNE commande annulable. Refus : entrée ou fil inconnu.
ServiceResult assignChartThread(DocumentSession& session, std::size_t entry,
                                const std::string& chartId, const std::string& code);

// Mélange de deux fils vus ensemble : `ratioB` ∈ [0 ; 1] = part du fil B, moyenne en lumière
// linéaire (sRGB -> linéaire -> sRGB).
[[nodiscard]] Rgb blendRgb(Rgb a, Rgb b, double ratioB);

struct BlendSuggestion {
    ThreadChoice a;
    ThreadChoice b;
    double ratioB{0.5};    // part du fil B (pas de 0,1)
    Rgb mixed{};           // couleur obtenue
    double mixedError{0};  // CIEDE2000 entre le mélange et la cible
    double singleError{0}; // CIEDE2000 entre le meilleur fil seul et la cible
    [[nodiscard]] bool betterThanSingle() const { return mixedError + 1e-9 < singleError; }
    [[nodiscard]] std::string summary() const;
};

// Meilleur mélange de deux fils (parmi les `candidates` plus proches de la cible) qui approche
// `target` ; nullopt si le nuancier a moins de deux fils. Peut ne pas battre un fil seul : voir
// `betterThanSingle()`.
[[nodiscard]] std::optional<BlendSuggestion> suggestBlend(Rgb target, const std::string& chartId,
                                                          std::size_t candidates = 12);

} // namespace openstitch::desktop_v2
