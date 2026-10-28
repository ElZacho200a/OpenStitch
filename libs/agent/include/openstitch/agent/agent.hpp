// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// Agent IA du compagnon (ADR 0002, docs/adr/0002-fournisseur-claude.md). Bibliothèque sans Qt, sans
// réseau : le transport HTTP, le stockage de secrets et l'accès aux outils du projet sont des
// interfaces fournies par l'hôte. Rien ici n'exécute de commande shell ni ne lit le disque.
namespace openstitch::agent {

// --- Messages ---------------------------------------------------------------------------------

struct ContentBlock {
    enum class Kind { Text, Image, ToolUse, ToolResult };
    Kind kind{Kind::Text};
    std::string text;                  // Text
    std::string media_type;            // Image (ex. image/png)
    std::string data_b64;              // Image : octets en base64
    std::string id;                    // ToolUse / ToolResult : identifiant d'appel
    std::string name;                  // ToolUse : nom de l'outil
    std::string input_json;            // ToolUse : arguments (objet JSON)
    bool is_error{false};              // ToolResult
    std::vector<ContentBlock> content; // ToolResult : blocs texte / image
};

struct Message {
    std::string role; // "user" | "assistant"
    std::vector<ContentBlock> blocks;
};

struct ToolSpec {
    std::string name;
    std::string description;
    std::string input_schema_json; // JSON Schema (objet)
};

struct Usage {
    std::uint64_t input_tokens{0};
    std::uint64_t output_tokens{0};
    void add(const Usage& other) {
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
    }
};

// --- Annulation -------------------------------------------------------------------------------

class CancelToken {
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const { flag_->store(true); }
    [[nodiscard]] bool cancelled() const { return flag_->load(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// --- Fournisseur ------------------------------------------------------------------------------

namespace code {
inline constexpr const char* kAuth = "E_AUTH";           // clé absente / refusée
inline constexpr const char* kQuota = "E_QUOTA";         // limite de débit ou de crédit
inline constexpr const char* kNetwork = "E_NETWORK";     // hôte injoignable, connexion coupée
inline constexpr const char* kTimeout = "E_TIMEOUT";     // délai dépassé
inline constexpr const char* kProvider = "E_PROVIDER";   // erreur 4xx/5xx du fournisseur
inline constexpr const char* kCancelled = "E_CANCELLED"; // interrompu par l'utilisateur
inline constexpr const char* kBadResponse = "E_BAD_RESPONSE"; // réponse illisible
inline constexpr const char* kDisabled = "E_DISABLED";        // compagnon désactivé
inline constexpr const char* kStepLimit = "E_STEP_LIMIT";     // trop d'allers-retours d'outils
} // namespace code

struct ProviderError {
    std::string code;
    std::string message; // phrase pour l'humain, sans secret
    bool retryable{false};
    int retry_after_ms{0}; // 0 = laisser l'appelant choisir
};

struct ProviderRequest {
    std::string model;
    std::string system;
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    int max_tokens{4096};
};

struct ProviderResponse {
    std::vector<ContentBlock> blocks;
    std::string stop_reason; // "end_turn" | "tool_use" | "max_tokens" | …
    Usage usage;
};

struct ProviderResult {
    bool ok{false};
    ProviderResponse response;
    ProviderError error;
};

struct ProviderCapabilities {
    bool tool_use{true};
    bool images{true};
    bool streaming{false};
};

class IAgentProvider {
public:
    virtual ~IAgentProvider() = default;
    [[nodiscard]] virtual std::string name() const = 0; // affiché à l'utilisateur
    [[nodiscard]] virtual ProviderCapabilities capabilities() const = 0;
    // Bloquant ; appelé hors du fil GUI. Respecte `cancel` au plus tôt.
    [[nodiscard]] virtual ProviderResult complete(const ProviderRequest& request,
                                                  const CancelToken& cancel) = 0;
};

// --- Transport HTTP (fourni par l'hôte) ------------------------------------------------------

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    int status{0}; // 0 = pas de réponse (voir `error`)
    std::string body;
    std::string error; // texte d'erreur de transport, sans secret
    bool timed_out{false};
    bool cancelled{false};
    int retry_after_ms{0}; // en-tête Retry-After converti, 0 = absent
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    [[nodiscard]] virtual HttpResponse
    post(const std::string& url, const std::vector<HttpHeader>& headers, const std::string& body,
         std::chrono::milliseconds timeout, const CancelToken& cancel) = 0;
};

// --- Secrets ----------------------------------------------------------------------------------

// Stockage sécurisé du système (Windows : Credential Manager). Jamais de texte clair dans
// QSettings, journaux, ni projet `.osp`.
class ISecretStore {
public:
    virtual ~ISecretStore() = default;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::optional<std::string> read(const std::string& key) = 0;
    virtual bool write(const std::string& key, const std::string& secret) = 0;
    virtual bool remove(const std::string& key) = 0;
};

// Stockage en mémoire (tests, session sans stockage système).
class MemorySecretStore final : public ISecretStore {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::optional<std::string> read(const std::string& key) override;
    bool write(const std::string& key, const std::string& secret) override;
    bool remove(const std::string& key) override;

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

// Remplace toute occurrence d'un secret (et sa forme tronquée à 8 caractères) par « *** » :
// à appliquer à tout texte destiné à un journal ou à l'interface.
[[nodiscard]] std::string redact(std::string text, const std::vector<std::string>& secrets);

// --- Journal de diagnostic ---------------------------------------------------------------------

// Journal OPTIONNEL (désactivé par défaut), borné, qui masque clés et chemins sensibles et
// n'enregistre AUCUN contenu utilisateur (messages, résultats d'outils) sans consentement
// explicite. Sert au dépannage : événements, codes d'erreur, noms d'outils, durées, comptes.
class DiagnosticLog {
public:
    explicit DiagnosticLog(std::size_t capacity = 500) : capacity_(capacity) {}

    void set_enabled(bool on);
    [[nodiscard]] bool enabled() const;
    void set_content_consent(bool on);
    [[nodiscard]] bool content_consent() const;
    void add_secret(const std::string& secret); // à masquer partout (clé API)

    // Métadonnée (nom d'événement, code, compte) : toujours masquée avant écriture.
    void record(const std::string& event, const std::string& detail = {});
    // Contenu utilisateur : ignoré sans consentement.
    void record_content(const std::string& event, const std::string& content);

    [[nodiscard]] std::string dump() const;
    [[nodiscard]] std::size_t size() const;
    void clear();

    // Masque secrets et chemins absolus (« C:\\Users\\x\\a.osp » -> « <chemin> »).
    [[nodiscard]] std::string sanitize(std::string text) const;

private:
    mutable std::mutex mutex_;
    std::size_t capacity_;
    bool enabled_{false};
    bool content_{false};
    std::vector<std::string> secrets_;
    std::vector<std::string> lines_;
    std::uint64_t next_{1};
};

// --- Espace de travail Claude Code ---------------------------------------------------------------

// Écrit dans `directory` un espace de travail prêt pour la CLI `claude` : `.mcp.json` (serveur
// `openstitch-mcp --live`), `CLAUDE.md` et hook SessionStart qui expliquent le mode live, et deux
// skills (édition, satin). Idempotent ; ne lance rien. Retourne faux si l'écriture échoue.
[[nodiscard]] bool write_claude_workspace(const std::string& directory,
                                          const std::string& mcp_exe_path, int desktop_pid = 0);

// --- Anthropic --------------------------------------------------------------------------------

struct AnthropicOptions {
    std::string model{"claude-sonnet-5-5"};
    std::string endpoint{"https://api.anthropic.com/v1/messages"};
    std::string api_version{"2023-06-01"};
    std::chrono::milliseconds timeout{std::chrono::seconds(120)};
    int max_retries{2};  // tentatives supplémentaires (réseau, 429, 5xx) ; bornées
    int backoff_ms{800}; // base du recul exponentiel
    std::function<void(int ms, const CancelToken&)> sleeper; // injecté en test (défaut : attente)
};

// API Messages d'Anthropic, utilisation d'outils comprise. La clé vient du stockage sécurisé et
// n'est jamais journalisée (`redact`).
class AnthropicProvider final : public IAgentProvider {
public:
    AnthropicProvider(IHttpClient& http, std::string api_key, AnthropicOptions options = {});
    [[nodiscard]] std::string name() const override { return "Anthropic (Claude)"; }
    [[nodiscard]] ProviderCapabilities capabilities() const override { return {true, true, false}; }
    [[nodiscard]] ProviderResult complete(const ProviderRequest& request,
                                          const CancelToken& cancel) override;

    // Corps de requête / lecture de réponse exposés pour les tests.
    [[nodiscard]] std::string build_body(const ProviderRequest& request) const;
    [[nodiscard]] static ProviderResult parse_response(int status, const std::string& body);

private:
    IHttpClient& http_;
    std::string api_key_;
    AnthropicOptions options_;
};

// --- Politique d'outils -----------------------------------------------------------------------

// Liste d'autorisation / de refus : l'agent ne reçoit et n'exécute QUE les outils OpenStitch
// autorisés. Le refus l'emporte ; une liste d'autorisation vide n'autorise rien.
class ToolPolicy {
public:
    ToolPolicy() = default;
    ToolPolicy(std::set<std::string> allow, std::set<std::string> deny)
        : allow_(std::move(allow)), deny_(std::move(deny)) {}
    [[nodiscard]] bool permits(const std::string& tool) const {
        return !deny_.contains(tool) && allow_.contains(tool);
    }

private:
    std::set<std::string> allow_;
    std::set<std::string> deny_;
};

// --- Boucle d'agent ---------------------------------------------------------------------------

struct ToolHostResult {
    std::vector<ContentBlock> content; // texte / image
    bool is_error{false};
};

// Accès aux outils du projet (fourni par l'hôte : bloquant, appelé hors fil GUI).
class IToolHost {
public:
    virtual ~IToolHost() = default;
    [[nodiscard]] virtual std::vector<ToolSpec> tools() = 0;
    [[nodiscard]] virtual ToolHostResult call(const std::string& name,
                                              const std::string& args_json) = 0;
};

// Passerelle avec le format des outils MCP d'OpenStitch (texte JSON en entrée / sortie).
// `[{"name","description","input_schema"}]` -> outils ; `{"content":[…],"isError":b}` -> résultat ;
// arguments d'appel -> résumé court « clé=valeur » (cartes d'outils de l'interface).
[[nodiscard]] std::vector<ToolSpec> parse_tool_specs(const std::string& json);
[[nodiscard]] ToolHostResult parse_tool_result(const std::string& json);
[[nodiscard]] std::string describe_arguments(const std::string& args_json, std::size_t limit = 160);
// Première valeur texte d'un résultat (rapport du lot, message d'erreur), pour l'affichage.
[[nodiscard]] std::string first_text(const ToolHostResult& result);

class IAgentObserver {
public:
    virtual ~IAgentObserver() = default;
    virtual void on_text(const std::string& text) = 0;
    // `refusal` non vide : l'outil a été refusé par la politique ou a échoué.
    virtual void on_tool_call(const std::string& name, const std::string& args_json,
                              const ToolHostResult& result, const std::string& refusal) = 0;
    virtual void on_usage(const Usage& step, const Usage& total) = 0;
};

struct RunnerOptions {
    std::string model{"claude-sonnet-5-5"};
    std::string system;
    int max_steps{12}; // allers-retours modèle/outils par demande
    int max_tokens{4096};
    std::size_t max_result_chars{60000}; // un résultat d'outil texte plus long est tronqué
    std::uint64_t token_budget{0};       // 0 = illimité ; sinon arrêt quand le total est atteint
};

struct RunOutcome {
    enum class Status { Done, Cancelled, Failed, StepLimit, BudgetExceeded };
    Status status{Status::Done};
    ProviderError error; // si Failed
    Usage usage;
    int steps{0};
};

class AgentRunner {
public:
    AgentRunner(IAgentProvider& provider, IToolHost& host, ToolPolicy policy,
                RunnerOptions options);

    // Ajoute `user_text` à `history` et boucle jusqu'à la réponse finale. L'historique reçoit
    // chaque tour (assistant, résultats d'outils) : une nouvelle demande reprend le contexte.
    [[nodiscard]] RunOutcome run(std::vector<Message>& history, const std::string& user_text,
                                 IAgentObserver& observer, const CancelToken& cancel);

private:
    IAgentProvider& provider_;
    IToolHost& host_;
    ToolPolicy policy_;
    RunnerOptions options_;
};

} // namespace openstitch::agent
