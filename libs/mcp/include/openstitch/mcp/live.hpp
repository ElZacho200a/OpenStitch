// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/project_ops/live_bridge.hpp"

// Mode live du serveur MCP (ADR 0001, docs/adr/0001-compagnon-ia-session-live.md) : les outils
// typés existants s'exécutent sur le projet ACTUELLEMENT OUVERT dans l'application hôte
// (OpenStitch Desktop), pas sur une copie propre au serveur.
//
// Côté hôte : `LiveService` (politique d'outils, instantané, approbation, commit sur la pile de
// l'hôte) et `LiveHost` (protocole ligne JSON + jeton). Côté client : `LiveRelay` (utilisé par
// `openstitch-mcp --live`) au-dessus d'un `ILiveTransport` (tube nommé / socket local). Aucun
// port réseau, aucune dépendance à Qt. L'API publique n'échange que des std::string (JSON).
namespace openstitch::mcp {

inline constexpr const char* kLiveSchema = "openstitch.live/1";
inline constexpr int kLiveVersion = 1;

enum class LivePolicy {
    Read,   // lecture : exécutée sur une copie figée, aucune confirmation
    Apply,  // mutation : approbation explicite (sauf `dry_run`)
    Denied, // jamais exposé en mode live (fichier, cycle de vie, checkpoints de session)
};

// Politique par outil (ADR §6). Inconnu = `Denied`.
[[nodiscard]] LivePolicy live_policy(std::string_view tool);

// Description d'un outil propre au mode live (`live_status`) pour `tools/list`.
struct LiveExtraTool {
    const char* name;
    const char* description;
    const char* input_schema;
};
[[nodiscard]] const std::vector<LiveExtraTool>& live_extra_tools();

// État du document hôte, fourni par l'application à la demande (fil GUI).
struct LiveDocumentInfo {
    std::string name; // nom affiché (sans chemin complet : pas de fuite de chemin)
    bool dirty{false};
    bool never_saved{false};
    std::vector<std::uint64_t> selection; // identifiants d'objets de broderie sélectionnés
};

// Demande d'approbation présentée à l'utilisateur avant toute mutation du projet ouvert.
struct ApprovalItem {
    std::uint64_t object_id{0}; // 0 = opération sans objet cible
    std::string text;           // ex. « set_color #12 »
};

struct ApprovalRequest {
    std::string tool;
    std::vector<ApprovalItem> items;   // une ligne par opération du lot
    std::vector<std::string> warnings; // opérations écartées, réserves
    std::string label;                 // nom de l'étape d'annulation
    std::string report_json;           // résultat de la simulation (rapport du lot)
    std::string session_id;
    std::uint64_t revision{0};
};

class IApprovalGate {
public:
    virtual ~IApprovalGate() = default;
    // Fil GUI. true = l'utilisateur approuve ; l'approbation vaut pour CETTE demande seulement.
    virtual bool approve(const ApprovalRequest& request) = 0;
};

// Un appel d'outil en cours : `begin` (fil GUI) fige un instantané, `run` calcule sur la copie
// (n'importe quel fil, sans toucher au document), `LiveService::finish` (fil GUI) demande
// l'approbation puis installe le résultat.
class LivePendingCall {
public:
    LivePendingCall();
    ~LivePendingCall();
    LivePendingCall(const LivePendingCall&) = delete;
    LivePendingCall& operator=(const LivePendingCall&) = delete;

    void run();

private:
    friend class LiveService;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class LiveService {
public:
    using InfoProvider = std::function<LiveDocumentInfo()>;

    // `on_applied` : appelé (fil GUI) juste après chaque modification installée dans le document,
    // pour rafraîchir l'interface comme après un Undo/Redo.
    LiveService(project_ops::LiveBridge& bridge, IApprovalGate& gate, InfoProvider info,
                std::function<void()> on_applied = {});

    // Fil GUI.
    [[nodiscard]] std::shared_ptr<LivePendingCall> begin(const std::string& tool,
                                                         const std::string& args_json);
    // Fil GUI. Retourne le résultat MCP `{"content":[…],"isError":b,"live":{…}}`.
    [[nodiscard]] std::string finish(LivePendingCall& call);

    // Version synchrone (tests, hôtes sans fil dédié) : begin + run + finish.
    [[nodiscard]] std::string call(const std::string& tool, const std::string& args_json);

    // Fil GUI. Informations d'attache (session, révision, permissions).
    [[nodiscard]] std::string attach_info_json();

private:
    project_ops::LiveBridge& bridge_;
    IApprovalGate& gate_;
    InfoProvider info_;
    std::function<void()> on_applied_;
};

// Protocole d'un client local : une ligne JSON par requête, une par réponse.
//   {"v":1,"id":3,"token":"…","op":"attach"|"call"|"ping","tool":"…","args":{…}}
//   {"v":1,"id":3,"ok":true,"result":{…}}  |  {"v":1,"id":3,"ok":false,"error":{code,message,hint}}
class LiveHost {
public:
    LiveHost(LiveService& service, std::string token);

    struct Reply {
        std::string immediate; // réponse prête (vide si `call` est non nul)
        std::shared_ptr<LivePendingCall> call;
        std::string id_json; // `id` de la requête, déjà sérialisé
    };

    // Fil GUI : valide la ligne (JSON, version, jeton) puis exécute ou prépare l'appel.
    [[nodiscard]] Reply begin(const std::string& line);
    // Fil GUI : à appeler une fois `reply.call->run()` terminé.
    [[nodiscard]] std::string finish(Reply& reply);

private:
    LiveService& service_;
    std::string token_;
};

// --- Côté client -----------------------------------------------------------------------------

class ILiveTransport {
public:
    virtual ~ILiveTransport() = default;
    // Envoie une ligne, retourne la ligne de réponse ; nullopt = connexion perdue / hôte fermé.
    virtual std::optional<std::string> exchange(const std::string& line) = 0;
};

// Relais : traduit un `tools/call` MCP en requête live et renvoie le résultat MCP.
class LiveRelay {
public:
    // `token` est relu à chaque requête : il change quand l'application hôte redémarre.
    LiveRelay(ILiveTransport& transport, std::function<std::string()> token);

    // Vérifie la connexion et la version (op `attach`). Message d'erreur actionnable sinon.
    [[nodiscard]] std::optional<std::string> attach();
    // Résultat MCP (`{"content":[…],"isError":b}`) ; toute erreur de transport devient un
    // résultat d'erreur `E_SESSION_CLOSED` avec conseil, jamais une exception.
    [[nodiscard]] std::string call(const std::string& tool, const std::string& args_json);

private:
    ILiveTransport& transport_;
    std::function<std::string()> token_;
    std::uint64_t next_id_{1};
};

// Fichier de découverte écrit par l'hôte (lisible par l'utilisateur seul) : où se connecter et
// avec quel jeton. Un fichier par processus hôte.
struct LiveEndpoint {
    int pid{0};
    std::string endpoint; // nom complet du tube / chemin de socket
    std::string token;
};
[[nodiscard]] std::filesystem::path live_directory();
[[nodiscard]] std::string make_live_token();
[[nodiscard]] bool write_live_endpoint(const std::filesystem::path& dir, const LiveEndpoint& e);
void remove_live_endpoint(const std::filesystem::path& dir, int pid);
// Le plus récent des fichiers de `dir` (ou celui de `pid` si > 0).
[[nodiscard]] std::optional<LiveEndpoint> find_live_endpoint(const std::filesystem::path& dir,
                                                             int pid = 0);

// Client bloquant de tube nommé (Windows) / socket de domaine Unix (Linux).
class LocalLineClient final : public ILiveTransport {
public:
    ~LocalLineClient() override;
    [[nodiscard]] static std::unique_ptr<LocalLineClient> connect(const std::string& endpoint);
    std::optional<std::string> exchange(const std::string& line) override;

private:
    LocalLineClient() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openstitch::mcp
