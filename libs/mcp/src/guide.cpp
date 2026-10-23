// SPDX-License-Identifier: Apache-2.0
#include "guide.hpp"

#include <algorithm>
#include <format>

namespace openstitch::mcp {

namespace {

constexpr const char* kMcpTopic = R"(# Utiliser le guide avec les outils MCP

Le guide `workflow` est écrit pour la CLI `openstitch-cli`. Avec ce serveur, les mêmes gestes
passent par des outils (les liens `references/x.md` du guide désignent le sujet `x` de
`digitize_guide`).

| Dans le guide (CLI) | Outil MCP |
|---|---|
| `project digitize` | `project_digitize` : `settings` = options de la CLI avec `_` au lieu de `-` ; le cadre est ajusté au motif ; DPI = largeur_source_px x 25,4 / largeur_mm |
| `project inspect --json` | `project_inspect` : résumé compact par défaut, `detail=full` pour tout, `object=ID` pour un objet |
| `project select --where …` | `project_select` : `color=`, `type=`, `area_gt=`, `area_lt=`, `within=` |
| `project apply` / `op` / `set` | `project_apply` : `dry_run`, `skip_refused` ; `set_canvas` / `set_finishing` pour le cadre et les finitions |
| `project operations --json` | `project_operations` |
| `project check --json` | `project_check` |
| `project render` | `project_render` : image PNG, `crop` pour zoomer sur une zone |
| `osp_palette.py --report` | `project_palette` : palette réelle dans l'ordre de couture, aire et part par couleur |
| enregistrer, `--dst` | `project_save`, `project_export` |

**Pas encore d'outil MCP** : `project sweep`, `diagnose`, `fidelity`, `density`, `at`, `diff`,
`fabric-risk`, `--debug-dir`, `stats` d'un DST. Si le client a un shell et `openstitch-cli`,
enregistrer le projet (`project_save`) puis lancer la CLI dessus ; sinon le consigner comme limite
dans le rapport plutôt que d'inventer une mesure.

## Ce que le serveur change

- Une session garde UN projet ouvert : pas besoin de copies de fichiers pour comparer. Pour
  conserver une variante, `project_save` sous un autre nom puis `project_open` pour y revenir.
- `project_apply` est tout ou rien ; `skip_refused` écarte les opérations refusées (listées dans
  `skipped`) et applique le reste : utile pour tester `auto_satin` sur beaucoup d'objets.
- Les trois satins : `project_apply` avec `{"op":"auto_satin","object":ID,"strategy":"skeleton"
  |"parallel_sweep"|"medial_axis"}`, en `dry_run` pour comparer les refus, puis rendu zoomé.

## Sémantique : ce qui reste à l'agent

La segmentation sémantique (cartes, instances, reconstruction avant vectorisation) n'est pas
native au moteur. L'agent prépare l'image (reconstruction fidèle, aplats nets) avant
`project_digitize`, puis raisonne par instance avec `project_select` (couleur, aire, zone
`within=x0,y0,x1,y1` en mm) et `project_palette`. Ne jamais fusionner deux zones sur la seule
couleur.

## Boucle conseillée

`project_digitize` -> `project_render` (voir) -> `project_check` (mesurer) -> `project_palette`
(couleurs réellement cousues) -> `project_select` + `project_apply` (retouches, `dry_run` d'abord)
-> `project_check` / `project_render` zoomé -> `project_save` -> `project_export`.
)";

std::string normalize(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') {
            continue;
        }
        out += raw[i];
    }
    return out;
}

// Retire le front matter YAML (`---` … `---`) d'un SKILL.md.
std::string strip_front_matter(std::string text) {
    if (!text.starts_with("---\n")) {
        return text;
    }
    const auto end = text.find("\n---\n", 3);
    if (end == std::string::npos) {
        return text;
    }
    text.erase(0, end + 5);
    const auto first = text.find_first_not_of('\n');
    return first == std::string::npos ? std::string{} : text.substr(first);
}

std::string first_title(const std::string& text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto eol = text.find('\n', pos);
        const auto line =
            text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (line.starts_with("# ")) {
            return line.substr(2);
        }
        if (eol == std::string::npos) {
            break;
        }
        pos = eol + 1;
    }
    return {};
}

std::string describe(const std::string& name, const std::string& title) {
    if (name == "workflow") {
        return "Méthode complète en 10 phases : lecture sémantique, numérisation, trois satins, "
               "retouches, mesures, livrables. À lire AVANT de numériser.";
    }
    if (name == "mcp") {
        return "Correspondance CLI -> outils MCP, ce qui manque, boucle conseillée.";
    }
    if (name == "segmentation-semantique") {
        return "Masques, instances, structures porteuses, refus de fusion.";
    }
    if (name == "selection-satins") {
        return "Comparer skeleton / parallel_sweep / medial_axis : couverture, rendu, refus.";
    }
    if (name == "retouches-et-validation") {
        return "Audit des contours, conversions, densité, coupes, validation et livrables.";
    }
    if (name == "cas-ruines-v1-v9") {
        return "Cas chiffré de neuf essais : hypothèses, gains réels, échecs.";
    }
    if (name == "parametres") {
        return "Choisir les options de numérisation et retouches manuelles.";
    }
    if (name == "diagnostic") {
        return "Diagnostic d'un résultat décevant.";
    }
    if (name == "journal-essais") {
        return "Journal d'essais historiques (contexte, pas une recette).";
    }
    return title;
}

std::vector<GuideTopic> build_topics() {
    std::vector<GuideTopic> topics;
    for (std::size_t i = 0; i < kGuideBlobCount; ++i) {
        const auto& blob = kGuideBlobs[i];
        GuideTopic t;
        t.name = blob.name;
        t.text = strip_front_matter(
            normalize(std::string_view(reinterpret_cast<const char*>(blob.data), blob.size)));
        t.title = first_title(t.text);
        t.description = describe(t.name, t.title);
        topics.push_back(std::move(t));
        if (topics.back().name == "workflow") {
            GuideTopic mcp;
            mcp.name = "mcp";
            mcp.text = kMcpTopic;
            mcp.title = first_title(mcp.text);
            mcp.description = describe("mcp", mcp.title);
            topics.push_back(std::move(mcp));
        }
    }
    return topics;
}

} // namespace

const std::vector<GuideTopic>& guide_topics() {
    static const std::vector<GuideTopic> topics = build_topics();
    return topics;
}

const GuideTopic* find_topic(std::string_view name) {
    const auto& topics = guide_topics();
    const auto it = std::find_if(topics.begin(), topics.end(),
                                 [&](const GuideTopic& t) { return t.name == name; });
    return it == topics.end() ? nullptr : &*it;
}

std::string guide_index_text() {
    std::string out =
        "Guide de numérisation OpenStitch (sujets pour digitize_guide{topic}).\n"
        "Ordre de lecture conseillé : « workflow » (méthode), « mcp » (outils), puis les "
        "références au besoin.\n\n";
    for (const auto& t : guide_topics()) {
        out +=
            std::format("- {} ({} Ko) : {}\n", t.name, (t.text.size() + 512) / 1024, t.description);
    }
    return out;
}

} // namespace openstitch::mcp
