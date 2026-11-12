// SPDX-License-Identifier: Apache-2.0
#pragma once

// Journal d'usage LOCAL de Studio : un fichier JSON Lines par mois, hors du dépôt et hors du
// dossier de version (`<données locales>/OpenStitch/usage/usage-AAAA-MM.jsonl`), donc conservé
// d'une version à l'autre et partagé par toutes les installations de la machine. Aucune donnée ne
// quitte l'ordinateur ; rien n'est envoyé.
//
// Une ligne par événement : {"v":1,"t":"2026-11-12T10:00:00.123","sid":"<hex>","app":"studio",
// "ver":"...","os":"windows","k":"click|tool|action|space","id":"..."} (+ "d" facultatif).
// L'identifiant est celui d'un contrôle ou d'une commande (jamais un nom de fichier, un texte saisi
// ou un contenu d'image). `sid` est tiré au hasard à chaque lancement : il relie les événements
// d'une même session, sans identifier la personne.
//
// Désactivation : variable OPENSTITCH_USAGE_LOG=0, ou fichier vide `DISABLED` dans le dossier.
// Dossier alternatif (équipe, disque partagé) : OPENSTITCH_USAGE_DIR.

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace openstitch::desktop_v2 {

struct UsageLogOptions {
    std::filesystem::path dir;
    std::string app{"studio"};
    std::string version;
    bool enabled{true};
    // Horloge injectable (tests) ; défaut : horloge système.
    std::function<std::chrono::system_clock::time_point()> now;
};

class UsageLog {
public:
    explicit UsageLog(UsageLogOptions options);

    // Ajoute un événement. Sans effet si le journal est désactivé ; ne lève jamais d'exception
    // (une erreur d'écriture est ignorée : le journal ne doit jamais gêner le travail).
    void record(std::string_view kind, std::string_view id, std::string_view detail = {});

    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] const std::string& sessionId() const { return sessionId_; }
    // Fichier du mois courant (même si le journal est désactivé : utile aux tests et au rapport).
    [[nodiscard]] std::filesystem::path currentFile() const;

    // Une ligne JSON (sans retour à la ligne final), pour les tests.
    [[nodiscard]] std::string formatLine(std::string_view kind, std::string_view id,
                                         std::string_view detail) const;

private:
    [[nodiscard]] std::chrono::system_clock::time_point timestamp() const;

    UsageLogOptions options_;
    std::string sessionId_;
    bool enabled_{true};
    mutable std::mutex mutex_;
};

using GetEnv = std::function<std::string(std::string_view)>;

// Dossier du journal : OPENSTITCH_USAGE_DIR s'il est défini, sinon
// `<genericDataDir>/OpenStitch/usage` (indépendant du nom de l'application et de sa version).
[[nodiscard]] std::filesystem::path defaultUsageDir(const GetEnv& getenv,
                                                    const std::filesystem::path& genericDataDir);

// Faux si OPENSTITCH_USAGE_LOG vaut 0/false/off/non, ou si `<dir>/DISABLED` existe.
[[nodiscard]] bool usageLogEnabled(const GetEnv& getenv, const std::filesystem::path& dir);

// Échappement JSON d'une chaîne (guillemets, antislash, caractères de contrôle) ; longueur bornée.
[[nodiscard]] std::string usageJsonEscape(std::string_view text, std::size_t maxBytes = 160);

} // namespace openstitch::desktop_v2
