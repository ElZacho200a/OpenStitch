// SPDX-License-Identifier: Apache-2.0
#include "project_cmd.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>

#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/project_ops/errors.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/project_file.hpp"
#include "openstitch/project_ops/quality.hpp"
#include "openstitch/project_ops/render.hpp"
#include "openstitch/project_ops/report.hpp"
#include "openstitch/project_ops/sweep.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch_cli {

namespace po = openstitch::project_ops;
namespace fs = std::filesystem;

namespace {

int g_exit = 0;

constexpr int kExitInput = 1;
constexpr int kExitQuality = 2;

int fail_with(const std::string& cmd, bool json, const char* code, const std::string& message,
              const std::string& hint = {}, int exitCode = kExitInput) {
    fmt::print(stderr, "openstitch-cli project {} : {}\n", cmd, message);
    if (!hint.empty()) {
        fmt::print(stderr, "  Piste : {}\n", hint);
    }
    if (json) {
        fmt::print("{}\n", po::error_json("project " + cmd, code, message, hint));
    }
    return exitCode;
}

int fail_err(const std::string& cmd, bool json, const char* code, const openstitch::Error& e,
             const std::string& hint = {}, int exitCode = kExitInput) {
    return fail_with(cmd, json, code, e.message, hint.empty() ? e.detail : hint, exitCode);
}

// Écriture atomique d'un fichier texte (temporaire puis renommage).
bool write_text(const fs::path& path, const std::string& text, std::string& error) {
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "écriture impossible : " + path.string();
            return false;
        }
        out << text;
        if (!out) {
            error = "écriture incomplète : " + path.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        error = "renommage impossible vers " + path.string();
        return false;
    }
    return true;
}

// Refuse tôt (avant tout calcul) une sortie impossible.
std::optional<int> check_output(const std::string& cmd, bool json, const std::string& path,
                                bool noClobber) {
    if (path.empty()) {
        return std::nullopt;
    }
    const fs::path p(path);
    std::error_code ec;
    const auto parent = p.parent_path();
    if (!parent.empty() && !fs::is_directory(parent, ec)) {
        return fail_with(cmd, json, po::code::kIo,
                         "le dossier de sortie « " + parent.string() + " » n'existe pas",
                         "créez-le d'abord ou choisissez un autre chemin");
    }
    if (noClobber && fs::exists(p, ec)) {
        return fail_with(cmd, json, po::code::kIo, "« " + path + " » existe déjà (--no-clobber)",
                         "retirez --no-clobber pour l'écraser");
    }
    return std::nullopt;
}

std::optional<po::OpenedProject> load(const std::string& cmd, bool json, const std::string& path,
                                      int& rc) {
    auto opened = po::open_project(fs::path(path));
    if (!opened) {
        rc = fail_err(cmd, json, po::code::kInvalidProject, opened.error(),
                      "attendu : un projet .osp enregistré par OpenStitch (ou `project new` / "
                      "`project digitize`)");
        return std::nullopt;
    }
    return std::move(*opened);
}

bool read_all(const std::string& path, std::string& out, std::string& error) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        out = ss.str();
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "fichier illisible : " + path;
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// ---------------------------------------------------------------------------------------
struct NewArgs {
    std::string out;
    po::NewProjectOptions options;
    bool noClobber{false};
};

int run_new(const NewArgs& a, bool json) {
    if (const auto refused = check_output("new", json, a.out, a.noClobber)) {
        return *refused;
    }
    auto project = po::create_empty_project(a.options);
    if (!project) {
        return fail_err("new", json, po::code::kUsage, project.error());
    }
    if (auto written = po::write_project(fs::path(a.out), *project, nullptr, {}, a.noClobber);
        !written) {
        return fail_err("new", json, po::code::kIo, written.error());
    }
    if (json) {
        fmt::print("{{\"schema\":\"openstitch.project-new/1\",\"ok\":true,\"project\":{},"
                   "\"width_mm\":{},\"height_mm\":{},\"margin_mm\":{}}}\n",
                   po::json_quote(a.out), a.options.width_mm, a.options.height_mm,
                   a.options.margin_mm);
    } else {
        fmt::print("Projet créé : {} (cadre {} x {} mm)\n", a.out, a.options.width_mm,
                   a.options.height_mm);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------
struct DigitizeArgs {
    std::string image;
    std::string out;
    std::string dst;
    std::string svg;
    std::string report;
    bool noClobber{false};
};

// Numérise une image vers un .osp ; `status` = flux des messages d'état.
int digitize_one(const DigitizeArgs& a, const DigitizeOptionValues& v, bool json,
                 std::string* reportOut = nullptr) {
    std::FILE* const st = json ? stderr : stdout;
    for (const auto* path : {&a.out, &a.dst, &a.svg, &a.report}) {
        if (const auto refused = check_output("digitize", json, *path, a.noClobber)) {
            return *refused;
        }
    }
    auto created = po::create_empty_project({});
    if (!created) {
        return fail_err("digitize", json, po::code::kInternal, created.error());
    }
    openstitch::document::Project project = std::move(*created);
    const po::StatusLog log = [st](std::string_view line) { fmt::print(st, "{}\n", line); };
    auto report = po::digitize_image_into(project, fs::path(a.image), v.settings, log);
    if (!report) {
        return fail_err("digitize", json, po::code::kRefused, report.error());
    }
    for (const auto& w : report->warnings) {
        fmt::print(stderr, "  ! {}\n", w);
    }
    if (report->embroidery_objects == 0) {
        return fail_with("digitize", json, po::code::kNothingToDo,
                         fmt::format("aucun objet de broderie généré ({} région(s) segmentée(s)), "
                                     "aucun .osp écrit",
                                     report->regions),
                         "image trop uniforme ou régions trop petites : essayez --min-region-px "
                         "plus petit, --max-colors plus grand, ou --skip-background no si le fond "
                         "a été ignoré");
    }
    fmt::print(st,
               "Objets brodés : {} (satin={} tatami={} running={} directionnel={} anneaux={})\n",
               report->embroidery_objects, report->satin, report->tatami, report->running,
               report->flow_fill, report->rings_satin);

    const auto seq = openstitch::stitch_generation::effective_sequence(project);
    if (!seq) {
        return fail_err("digitize", json, po::code::kRefused, seq.error());
    }
    const auto stats = openstitch::stitch::compute_stats(*seq);
    if (stats.stitches == 0) {
        return fail_with("digitize", json, po::code::kNothingToDo,
                         "aucun point généré, aucun .osp écrit",
                         "modifiez --min-region-px ou --max-colors");
    }
    if (auto written = po::write_project(fs::path(a.out), project, nullptr, {}, a.noClobber);
        !written) {
        return fail_err("digitize", json, po::code::kIo, written.error());
    }
    fmt::print(st, "Projet écrit : {}\n", a.out);
    if (!a.dst.empty()) {
        if (auto w = openstitch::project_io::export_machine_file(project, "dst", fs::path(a.dst));
            !w) {
            return fail_err("digitize", json, po::code::kIo, w.error());
        }
        fmt::print(st, "DST écrit : {}\n", a.dst);
    }
    if (!a.svg.empty()) {
        if (auto w = openstitch::formats::write_svg_file(fs::path(a.svg), *seq); !w) {
            return fail_err("digitize", json, po::code::kIo, w.error());
        }
        fmt::print(st, "SVG écrit : {}\n", a.svg);
    }
    const auto doc = po::digitize_report_json(project, *report, v.settings, a.image, a.out);
    if (!doc) {
        return fail_err("digitize", json, po::code::kInternal, doc.error());
    }
    if (!a.report.empty()) {
        std::string err;
        if (!write_text(fs::path(a.report), *doc, err)) {
            return fail_with("digitize", json, po::code::kIo, err);
        }
    }
    if (reportOut != nullptr) {
        *reportOut = *doc;
    } else if (json) {
        fmt::print("{}\n", *doc);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------
// Chemin commun `apply` / `op` / `set` : un lot (ou une opération) sur un .osp.
struct BatchFlow {
    std::string cmd;
    std::string osp;
    std::string out; // vide = en place (écriture atomique)
    std::string reportPath;
    bool dryRun{false};
    bool noClobber{false};
    bool json{false};
};

int run_batch_flow(const BatchFlow& f, const std::string& text, bool single) {
    int rc = 0;
    if (const auto refused = check_output(f.cmd, f.json, f.out, f.noClobber)) {
        return *refused;
    }
    auto opened = load(f.cmd, f.json, f.osp, rc);
    if (!opened) {
        return rc;
    }
    po::BatchOptions options;
    options.dry_run = f.dryRun;
    const po::BatchReport report = single ? po::apply_operation(opened->project, text, options)
                                          : po::apply_batch(opened->project, text, options);
    const std::string reportJson = report.to_json();
    if (!f.reportPath.empty()) {
        std::string err;
        if (!write_text(fs::path(f.reportPath), reportJson, err)) {
            return fail_with(f.cmd, f.json, po::code::kIo, err);
        }
    }
    if (f.json) {
        fmt::print("{}\n", reportJson);
    } else {
        for (const auto& o : report.outcomes) {
            if (o.ok) {
                fmt::print("  [{}] {} : ok{}\n", o.index, o.op,
                           o.result_json.empty() ? "" : " " + o.result_json);
            } else {
                fmt::print("  [{}] {} : REFUSÉ ({}) {}\n", o.index, o.op, o.code, o.message);
            }
        }
    }
    if (!report.ok) {
        const std::string why =
            report.error_code.empty() ? std::string("lot refusé") : report.error_message;
        fmt::print(stderr, "openstitch-cli project {} : {} -- aucune modification écrite\n", f.cmd,
                   why);
        return kExitRefused;
    }
    if (f.dryRun) {
        fmt::print(stderr, "{} opération(s) valides (dry-run : rien n'est écrit)\n",
                   report.operation_count);
        return 0;
    }
    const fs::path target = f.out.empty() ? fs::path(f.osp) : fs::path(f.out);
    if (auto w =
            po::write_project(target, opened->project, &opened->info, fs::path(f.osp), f.noClobber);
        !w) {
        return fail_err(f.cmd, f.json, po::code::kIo, w.error());
    }
    fmt::print(stderr, "Projet écrit : {}\n", target.string());
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------------------
void add_digitize_options(CLI::App* cmd, DigitizeOptionValues& v) {
    auto& s = v.settings;
    cmd->add_option("--dpi", s.dpi, "Résolution supposée pour l'échelle mm/px (défaut : 96)")
        ->check(CLI::PositiveNumber);
    cmd->add_option("--max-colors", s.max_colors, "Nombre maximal de couleurs (défaut : 8)")
        ->check(CLI::Range(2, 64));
    cmd->add_option("--min-region-px", s.min_region_px,
                    "Taille minimale de région en px (défaut : 16)")
        ->check(CLI::PositiveNumber);
    cmd->add_option("--smoothing-px", s.smoothing_px, "Lissage des formes en px (défaut : 3)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--skip-background", v.skipBackground,
                    "Ignorer la plus grande région (le fond) : auto (défaut : seulement si c'est "
                    "un fond quasi blanc touchant au moins 3 bords) | yes | no | outer (seulement "
                    "les régions "
                    "de cette couleur qui touchent le pourtour de l'image : un blanc enfermé dans "
                    "le motif reste cousu)")
        ->check(CLI::IsMember({"auto", "yes", "no", "outer", "-1", "0", "1", "2"}));
    cmd->add_option("--min-feature-mm", s.min_feature_mm,
                    "Mode formes : les détails plus étroits que cette largeur (mm) sont rendus à "
                    "leur voisin avant numérisation ; 0 = désactivé (défaut : 0)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--min-region-mm2", s.min_region_mm2,
                    "Mode formes : les régions plus petites que cette aire (mm²) sont fusionnées "
                    "avec leur voisine ; 0 = désactivé (défaut : 3)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option(
           "--fill-overlap", s.fill_overlap_mm,
           "Mode formes : débord (mm) de chaque tatami sur ses voisins brodés, le long des "
           "bords partagés (ferme la dentelure de tissu entre zones) ; 0 = désactivé "
           "(défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag(
        "--no-underpath{false}", s.fill_hidden_underpath,
        "Mode formes : pas de liaison cousue entre les morceaux de rangées d'un tatami (elles "
        "restent visibles par-dessus le remplissage en brins obliques) : plus de sauts et "
        "de coupes, remplissages propres ; à associer à --hidden-travel");
    cmd->add_option("--fill-inset", s.fill_inset_mm,
                    "Mode formes : retrait du bord de chaque tatami en mm (défaut : 0.2) ; 0 ferme "
                    "les interstices entre zones voisines (+~16 % de points)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--island-cleanup", s.island_cleanup_mm2,
                    "Mode formes : les îlots plus petits que cette aire (mm²) sont fusionnés dans "
                    "la voisine de COULEUR la plus proche (pas la plus longue frontière) ; 0 = "
                    "désactivé (défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag("--rings-satin", s.rings_satin,
                  "Mode formes : les cercles et ellipses épaissis (hublots, ventouses, bulles, "
                  "yeux) sont reconnus par leurs deux bords concentriques et cousus en auto-satin "
                  "(un rond net), retirés du remplissage qui les portait");
    cmd->add_flag("--flow-fill", s.flow_fill,
                  "Mode formes : les grandes formes allongées (tentacules, rubans, nuages, fumée) "
                  "passent en remplissage directionnel dont les guides suivent l'axe médian : les "
                  "rangées suggèrent le mouvement. La couleur la plus sombre reste en tatami");
    cmd->add_option("--flow-min-area", s.flow_min_area_mm2,
                    "Avec --flow-fill : aire minimale (mm2) d'une forme (défaut : 10)");
    cmd->add_option("--flow-min-elongation", s.flow_min_elongation,
                    "Avec --flow-fill : allongement minimal (plus long guide au carré / aire ; "
                    "défaut : 2.5, un disque vaut ~0, une bande >= 5)");
    cmd->add_option("--island-max-delta-e", s.island_max_delta_e,
                    "Avec --island-cleanup : refuse une voisine plus éloignée que cet écart de "
                    "couleur CIELAB (ΔE) ; 0 = pas de limite (défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--max-threads", s.max_threads,
                    "Mode formes : limite le nombre de couleurs (fils) du résultat en fusionnant "
                    "les plus proches ; 0 = pas de limite (défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag(
        "--solid-block", s.solid_block,
        "Mode formes : bloc uni, sans tissu visible entre zones voisines (les "
        "remplissages débordent sous leurs voisins, les petites régions sont fusionnées)");
    cmd->add_option("--solid-block-overlap", s.solid_block_overlap_mm,
                    "Avec --solid-block : débord sous les voisins, en mm (défaut : 0.5)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--trim-threshold", s.trim_threshold_mm,
                    "Coupe automatique au-delà de ce déplacement, en mm (défaut : 3)")
        ->check(CLI::PositiveNumber);
    cmd->add_flag("--dark-lines", s.dark_lines,
                  "Mode formes : les traits sombres fins (contours noirs, mâts, haubans) sont "
                  "séparés des remplissages et cousus par-dessus, en dernier, en point droit");
    cmd->add_option(
           "--dark-lines-bars", s.dark_lines_bars_mm,
           "Avec --dark-lines : les barres sombres plus larges qu'un trait mais de moins de "
           "cette largeur (mm ; mât, vergues, essayer 4) sont cousues en bandes (satin) au "
           "lieu de rester dans le remplissage noir ; 0 = désactivé (défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_option("--dark-lines-min-length", s.dark_lines_min_length_mm,
                    "Avec --dark-lines : les fragments de trait isolés plus courts que cette "
                    "longueur (mm, essayer 8) ne sont pas cousus ; 0 = désactivé (défaut)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag("--dark-lines-objects-only", s.dark_lines_objects_only,
                  "Avec --dark-lines : ne coud que les vrais objets (haubans, mâts, cordages) ; un "
                  "trait entre deux zones de couleurs différentes est une séparation, laissée aux "
                  "remplissages (le contour du motif contre le fond reste cousu)");
    cmd->add_option("--dark-lines-width", s.dark_lines_width_mm,
                    "Avec --dark-lines : largeur maximale d'un trait, en mm (défaut : 1.6)")
        ->check(CLI::Range(0.3, 6.0));
    cmd->add_option(
           "--dark-lines-lightness", s.dark_lines_lightness,
           "Avec --dark-lines : clarté CIELAB L* maximale d'un pixel de trait (défaut : 25)")
        ->check(CLI::Range(5.0, 70.0));
    cmd->add_flag("--hidden-travel", s.hidden_travel,
                  "Remplace les coupes par des sauts ou des ponts cousus cachés sous les points "
                  "cousus plus tard (passe finale, désactivé par défaut)");
    cmd->add_option("--hidden-travel-levels", s.hidden_levels,
                    "Avec --hidden-travel : niveaux de l'optimisation, parmi between (ponts entre "
                    "objets), within (liens dans un objet), underlay (ordre de la sous-couche) ; "
                    "liste séparée par des virgules (défaut : all)");
    cmd->add_flag("--hidden-travel-same-colour", s.hidden_same_color,
                  "Avec --hidden-travel : une surface déjà cousue du même fil compte aussi comme "
                  "cachée (léger relief possible)");
    cmd->add_option("--hidden-travel-max", s.hidden_max_mm,
                    "Avec --hidden-travel : longueur maximale d'un pont, en mm (défaut : 15)")
        ->check(CLI::Range(0.0, 100.0));
    cmd->add_option("--mode", s.mode,
                    "Stratégie : shapes (formes pleines, défaut) | contours (dessin au trait) | "
                    "stipple (semis de points relié en un trait, rendu des tons)")
        ->check(CLI::IsMember({"shapes", "contours", "stipple"}));
    cmd->add_option("--detail", s.detail, "Mode contours : niveau de détail 0..1 (défaut : 0.5)")
        ->check(CLI::Range(0.0, 1.0));
    cmd->add_option("--technique", s.technique,
                    "Mode contours : auto (défaut) | running | legacy-satin (ancien satin dégradé "
                    "en point droit, pas l'auto-satin)")
        ->check(CLI::IsMember({"auto", "running", "legacy-satin", "satin"}));
    cmd->add_option("--curve-tolerance", s.curve_tolerance_mm,
                    "Mode formes : contours en courbes de Bézier avec cet écart maximal en mm (0 = "
                    "polygones, défaut ; prendre ~1,5 px ou plus, ex. 0.4)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag("--satin-bands", s.satin_bands,
                  "Mode formes : les bandes étroites (anneaux, rubans, traits épais) deviennent "
                  "de l'auto-satin au lieu d'un tatami ; repli tatami si la couverture est "
                  "insuffisante");
    cmd->add_flag("--single-path", s.single_path,
                  "Mode contours : relie les traits en un minimum de polylignes (parcours "
                  "eulérien, repasse un court tronçon plutôt que de couper le fil)");
    cmd->add_option(
        "--pre", s.pre,
        "Traitements d'image AVANT la segmentation (non destructifs, enregistrés dans "
        "le projet), séparés par « + » : median[:1|2] bilateral[:1..3] flatten[:1..3] clahe[:C] "
        "sharpen[:A] quantize[:N] "
        "contrast:B:C gray crop:x:y:w:h flip:h|v rotate:1..3. Ex. : bilateral:2+median:1");
    cmd->add_flag("--coherent-lines", s.coherent_lines,
                  "Mode contours : extrait d'abord des traits de la photo (lignes cohérentes, "
                  "FDoG) avant la segmentation");
    cmd->add_option("--stipple-points", s.stipple_points,
                    "Mode stipple : nombre de points (défaut : 2000)")
        ->check(CLI::Range(1, 200000));
    cmd->add_option("--stipple-seed", s.stipple_seed,
                    "Mode stipple : graine du tirage initial (défaut : 1)")
        ->check(CLI::NonNegativeNumber);
    cmd->add_flag("--stipple-invert", s.stipple_invert,
                  "Mode stipple : points denses sur les zones claires (fil clair, fond sombre)");
    cmd->add_option(
        "--debug-dir", s.debug_dir,
        "Diagnostic : écrit dans ce dossier les masques de décision (regions.png, "
        "traits.png), la couverture réelle des fils (couverture.png), la carte d'erreurs "
        "(fidelite.png) et decisions.json");
    cmd->add_option("--lock", s.lock, "Point d'arrêt : none|backforth|triangle|zigzag")
        ->check(CLI::IsMember({"none", "backforth", "triangle", "zigzag"}));
}

void finalize_digitize_options(DigitizeOptionValues& v) {
    const auto& b = v.skipBackground;
    v.settings.skip_background = (b == "auto" || b == "-1")   ? -1
                                 : (b == "yes" || b == "1")   ? 1
                                 : (b == "outer" || b == "2") ? 2
                                                              : 0;
}

int project_exit_code() {
    return g_exit;
}

CLI::App* register_project_commands(CLI::App& app, bool& jsonOut) {
    auto* project = app.add_subcommand(
        "project",
        "Projets .osp pilotables sans IHM : création, numérisation, inspection, opérations, "
        "lots, contrôle qualité (docs/source/cli-project.md)");
    project->group("Projets");
    project->require_subcommand(1);
    project->footer("Codes de sortie : 0 succès ; 1 entrée/fichier/erreur ; 2 contrôle qualité ou "
                    "validation non satisfait ; 3 lot ou opération refusé (aucune écriture).\n"
                    "Avec --json : un unique document JSON sur stdout (erreurs comprises, schéma "
                    "openstitch.error/1) ; messages lisibles sur stderr.");

    // new -------------------------------------------------------------------------------
    auto newArgs = std::make_shared<NewArgs>();
    auto* c = project->add_subcommand("new", "Crée un projet .osp vide (cadre et finitions)");
    c->add_option("sortie", newArgs->out, "Projet .osp à créer")->required();
    c->add_option("--width-mm", newArgs->options.width_mm, "Largeur du cadre (défaut 100)")
        ->check(CLI::PositiveNumber);
    c->add_option("--height-mm", newArgs->options.height_mm, "Hauteur du cadre (défaut 100)")
        ->check(CLI::PositiveNumber);
    c->add_option("--margin-mm", newArgs->options.margin_mm, "Marge de sécurité (défaut 0)")
        ->check(CLI::NonNegativeNumber);
    c->add_option("--trim-threshold", newArgs->options.trim_threshold_mm,
                  "Seuil de coupe automatique en mm (défaut 3)")
        ->check(CLI::PositiveNumber);
    c->add_option("--lock", newArgs->options.lock, "Point d'arrêt : none|backforth|triangle|zigzag")
        ->check(CLI::IsMember({"none", "backforth", "triangle", "zigzag"}));
    c->add_flag("--no-clobber", newArgs->noClobber, "Refuse d'écraser un fichier existant");
    c->add_flag("--json", jsonOut, "Sortie JSON unique sur stdout");
    c->callback([newArgs, &jsonOut] { g_exit = run_new(*newArgs, jsonOut); });

    // digitize --------------------------------------------------------------------------
    auto dzArgs = std::make_shared<DigitizeArgs>();
    auto dzValues = std::make_shared<DigitizeOptionValues>();
    c = project->add_subcommand("digitize",
                                "Numérise une image en projet .osp natif et éditable (même "
                                "pipeline que `digitize`, le DST devient un export secondaire)");
    c->add_option("image", dzArgs->image, "Image source (PNG, JPEG, BMP, TIFF)")->required();
    c->add_option("--output,-o", dzArgs->out, "Projet .osp à produire")->required();
    add_digitize_options(c, *dzValues);
    c->add_option("--dst", dzArgs->dst, "Exporte aussi un DST (export secondaire)");
    c->add_option("--output-svg", dzArgs->svg, "Exporte aussi un SVG de diagnostic");
    c->add_option("--report", dzArgs->report,
                  "Écrit le rapport de numérisation JSON dans ce fichier");
    c->add_flag("--no-clobber", dzArgs->noClobber, "Refuse d'écraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON unique sur stdout ; états sur stderr");
    c->callback([dzArgs, dzValues, &jsonOut] {
        finalize_digitize_options(*dzValues);
        g_exit = digitize_one(*dzArgs, *dzValues, jsonOut);
    });

    // digitize-batch (initiative) -------------------------------------------------------
    struct BatchDz {
        std::vector<std::string> images;
        std::string outDir;
        bool noClobber{false};
    };
    auto bdArgs = std::make_shared<BatchDz>();
    auto bdValues = std::make_shared<DigitizeOptionValues>();
    c = project->add_subcommand("digitize-batch",
                                "[initiative] Numérise plusieurs images : un .osp par image dans "
                                "--out-dir, rapport agrégé");
    c->add_option("images", bdArgs->images, "Images sources")->required();
    c->add_option("--out-dir", bdArgs->outDir, "Dossier des .osp (doit exister)")->required();
    add_digitize_options(c, *bdValues);
    c->add_flag("--no-clobber", bdArgs->noClobber, "Refuse d'écraser un .osp existant");
    c->add_flag("--json", jsonOut, "Rapport JSON agrégé sur stdout");
    c->callback([bdArgs, bdValues, &jsonOut] {
        finalize_digitize_options(*bdValues);
        std::string items;
        int failures = 0;
        for (const auto& image : bdArgs->images) {
            DigitizeArgs a;
            a.image = image;
            a.out =
                (fs::path(bdArgs->outDir) / (fs::path(image).stem().string() + ".osp")).string();
            a.noClobber = bdArgs->noClobber;
            std::string report;
            const int rc = digitize_one(a, *bdValues, true, &report);
            failures += rc != 0 ? 1 : 0;
            if (!items.empty()) {
                items += ",";
            }
            items +=
                fmt::format("{{\"image\":{},\"project\":{},\"ok\":{},\"exit\":{},\"report\":{}}}",
                            po::json_quote(image), po::json_quote(a.out),
                            rc == 0 ? "true" : "false", rc, rc == 0 ? report : "null");
        }
        if (jsonOut) {
            fmt::print("{{\"schema\":\"openstitch.digitize-batch/1\",\"ok\":{},\"failed\":{},"
                       "\"items\":[{}]}}\n",
                       failures == 0 ? "true" : "false", failures, items);
        } else {
            fmt::print("{} image(s), {} échec(s)\n", bdArgs->images.size(), failures);
        }
        g_exit = failures == 0 ? 0 : kExitInput;
    });

    // inspect ---------------------------------------------------------------------------
    struct InspectArgs {
        std::string osp;
        std::optional<std::uint64_t> object;
        bool noStitches{false};
        bool geometry{false};
    };
    auto inArgs = std::make_shared<InspectArgs>();
    c = project->add_subcommand("inspect", "Inspecte un projet (ou un objet) : JSON stable avec "
                                           "--json, résumé lisible sinon");
    c->add_option("osp", inArgs->osp, "Projet .osp")->required();
    c->add_option("--object", inArgs->object, "Id d'un objet de broderie à détailler");
    c->add_flag("--no-stitches", inArgs->noStitches,
                "N'exécute pas la génération de points (inspection légère)");
    c->add_flag("--geometry", inArgs->geometry, "Inclut les contours (nœuds en mm) avec --object");
    c->add_flag("--json", jsonOut, "Document JSON unique sur stdout");
    c->callback([inArgs, &jsonOut] {
        int rc = 0;
        auto opened = load("inspect", jsonOut, inArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        po::InspectOptions options;
        options.with_stitches = !inArgs->noStitches;
        options.with_geometry = inArgs->geometry;
        if (inArgs->object) {
            const auto text = po::inspect_object_json(
                opened->project, openstitch::ObjectId{*inArgs->object}, options);
            if (!text) {
                g_exit = fail_err("inspect", jsonOut, po::code::kUnknownObject, text.error());
                return;
            }
            fmt::print("{}\n", *text);
            g_exit = 0;
            return;
        }
        const auto text = jsonOut ? po::inspect_project_json(opened->project, options)
                                  : po::inspect_project_text(opened->project, options);
        if (!text) {
            g_exit = fail_err("inspect", jsonOut, po::code::kRefused, text.error());
            return;
        }
        fmt::print("{}{}", *text, jsonOut ? "\n" : "");
        g_exit = 0;
    });

    // apply / op / set ------------------------------------------------------------------
    auto flow = std::make_shared<BatchFlow>();
    auto batchFile = std::make_shared<std::string>();
    c = project->add_subcommand("apply", "Applique un lot d'opérations JSON (transactionnel, "
                                         "écriture atomique, rien n'est écrit en cas d'échec)");
    c->add_option("osp", flow->osp, "Projet .osp")->required();
    c->add_option("lot", *batchFile, "Fichier JSON du lot (« - » = stdin)")->required();
    c->add_option("--output,-o", flow->out, "Écrit le résultat ici (défaut : en place)");
    c->add_option("--report", flow->reportPath, "Écrit le rapport JSON du lot dans ce fichier");
    c->add_flag("--dry-run", flow->dryRun, "Valide et simule sans rien écrire");
    c->add_flag("--no-clobber", flow->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON du lot sur stdout");
    c->callback([flow, batchFile, &jsonOut] {
        flow->cmd = "apply";
        flow->json = jsonOut;
        std::string text, err;
        if (!read_all(*batchFile, text, err)) {
            g_exit = fail_with("apply", jsonOut, po::code::kIo, err);
            return;
        }
        g_exit = run_batch_flow(*flow, text, false);
    });

    auto opFlow = std::make_shared<BatchFlow>();
    auto opName = std::make_shared<std::string>();
    auto opArgs = std::make_shared<std::vector<std::string>>();
    c = project->add_subcommand("op", "Applique UNE opération (même moteur que `apply`) : "
                                      "project op p.osp set_color --arg object=12 --arg "
                                      "rgb=#FF0000");
    c->add_option("osp", opFlow->osp, "Projet .osp")->required();
    c->add_option("operation", *opName, "Nom de l'opération (voir `project operations`)")
        ->required();
    c->add_option("--arg,-a", *opArgs, "Argument clé=valeur (valeur JSON ou texte), répétable");
    c->add_option("--output,-o", opFlow->out, "Écrit le résultat ici (défaut : en place)");
    c->add_option("--report", opFlow->reportPath, "Écrit le rapport JSON dans ce fichier");
    c->add_flag("--dry-run", opFlow->dryRun, "Valide et simule sans rien écrire");
    c->add_flag("--no-clobber", opFlow->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON sur stdout");
    c->callback([opFlow, opName, opArgs, &jsonOut] {
        opFlow->cmd = "op";
        opFlow->json = jsonOut;
        const auto text = po::operation_json_from_args(*opName, *opArgs);
        if (!text) {
            g_exit = fail_err("op", jsonOut, po::code::kUsage, text.error());
            return;
        }
        g_exit = run_batch_flow(*opFlow, *text, true);
    });

    struct SetArgs {
        std::optional<double> width, height, margin, trim;
        std::optional<std::string> lock;
    };
    auto setFlow = std::make_shared<BatchFlow>();
    auto setArgs = std::make_shared<SetArgs>();
    c = project->add_subcommand("set", "Modifie le cadre et les finitions générales (sucre pour "
                                       "les opérations set_canvas / set_finishing)");
    c->add_option("osp", setFlow->osp, "Projet .osp")->required();
    c->add_option("--width-mm", setArgs->width, "Largeur du cadre")->check(CLI::PositiveNumber);
    c->add_option("--height-mm", setArgs->height, "Hauteur du cadre")->check(CLI::PositiveNumber);
    c->add_option("--margin-mm", setArgs->margin, "Marge de sécurité")
        ->check(CLI::NonNegativeNumber);
    c->add_option("--trim-threshold", setArgs->trim, "Seuil de coupe en mm")
        ->check(CLI::PositiveNumber);
    c->add_option("--lock", setArgs->lock, "Point d'arrêt : none|backforth|triangle|zigzag")
        ->check(CLI::IsMember({"none", "backforth", "triangle", "zigzag"}));
    c->add_option("--output,-o", setFlow->out, "Écrit le résultat ici (défaut : en place)");
    c->add_flag("--dry-run", setFlow->dryRun, "Valide et simule sans rien écrire");
    c->add_flag("--no-clobber", setFlow->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON sur stdout");
    c->callback([setFlow, setArgs, &jsonOut] {
        setFlow->cmd = "set";
        setFlow->json = jsonOut;
        int rc = 0;
        auto opened = load("set", jsonOut, setFlow->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        const auto& canvas = opened->project.canvas;
        std::string ops;
        if (setArgs->width || setArgs->height || setArgs->margin) {
            ops += fmt::format("{{\"op\":\"set_canvas\",\"width_mm\":{},\"height_mm\":{},"
                               "\"margin_mm\":{}}}",
                               setArgs->width.value_or(canvas.width.value / 1000.0),
                               setArgs->height.value_or(canvas.height.value / 1000.0),
                               setArgs->margin.value_or(canvas.margin.value / 1000.0));
        }
        if (setArgs->trim || setArgs->lock) {
            std::string fields;
            if (setArgs->trim) {
                fields += fmt::format(",\"trim_threshold_mm\":{}", *setArgs->trim);
            }
            if (setArgs->lock) {
                fields += fmt::format(",\"lock\":{}", po::json_quote(*setArgs->lock));
            }
            ops += std::string(ops.empty() ? "" : ",") + "{\"op\":\"set_finishing\"" + fields + "}";
        }
        if (ops.empty()) {
            g_exit = fail_with("set", jsonOut, po::code::kUsage, "aucun réglage demandé",
                               "ex. --width-mm 120 --lock triangle");
            return;
        }
        g_exit = run_batch_flow(
            *setFlow, "{\"schema\":\"openstitch.batch/1\",\"operations\":[" + ops + "]}", false);
    });

    // export ----------------------------------------------------------------------------
    struct ExportArgs {
        std::string osp, out, format, stops{"native"}, levels{"all"}, start{"first"};
        bool noTrims{false}, noColorChanges{false}, reduceTrims{false}, sameColour{false};
        bool outlines{false}, noClobber{false};
        double reduceMax{15.0};
        std::uint64_t only{0};
    };
    auto exArgs = std::make_shared<ExportArgs>();
    c = project->add_subcommand("export",
                                "Exporte un projet en broderie machine (dst, pes, jef, vp3, "
                                "exp) ou en SVG ; les points sont régénérés depuis le "
                                ".osp (mêmes octets que osp2dst)");
    c->add_option("osp", exArgs->osp, "Projet .osp")->required();
    c->add_option("--output,-o", exArgs->out, "Fichier à produire (format déduit de l'extension)")
        ->required();
    c->add_option("--format", exArgs->format, "Force le format : dst|pes|jef|exp|svg")
        ->check(CLI::IsMember({"dst", "pes", "jef", "vp3", "exp", "svg"}));
    c->add_flag("--no-trims", exArgs->noTrims, "Supprime les coupes (elles deviennent des sauts)");
    c->add_flag("--no-color-changes", exArgs->noColorChanges,
                "Supprime les changements de couleur");
    c->add_option("--stops", exArgs->stops, "Arrêts machine : native, as-color-change ou drop")
        ->check(CLI::IsMember({"native", "as-color-change", "drop"}));
    c->add_option("--start", exArgs->start,
                  "Point de départ machine : first (défaut), center ou X,Y en mm ; l'ordre des "
                  "points n'est pas modifié");
    c->add_flag("--reduce-trims", exArgs->reduceTrims,
                "Remplace des coupes par des ponts cousus cachés (export seulement)");
    c->add_option("--reduce-trims-levels", exArgs->levels,
                  "between, within, underlay, regions ou all (liste séparée par des virgules)");
    c->add_flag("--reduce-trims-same-colour", exArgs->sameColour,
                "Une surface déjà cousue du même fil compte comme cachée");
    c->add_option("--reduce-trims-max", exArgs->reduceMax, "Longueur maximale d'un pont, en mm")
        ->check(CLI::Range(0.0, 100.0));
    c->add_flag("--outlines", exArgs->outlines, "SVG : superpose les contours des régions");
    c->add_option("--only", exArgs->only, "SVG : limite à un objet de broderie");
    c->add_flag("--no-clobber", exArgs->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.export/1) sur stdout");
    c->callback([exArgs, &jsonOut] {
        const auto& a = *exArgs;
        if (const auto refused = check_output("export", jsonOut, a.out, a.noClobber)) {
            g_exit = *refused;
            return;
        }
        std::string format = a.format;
        if (format.empty()) {
            format = fs::path(a.out).extension().string();
            if (!format.empty() && format.front() == '.') {
                format.erase(format.begin());
            }
            std::transform(format.begin(), format.end(), format.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        }
        const auto* info = openstitch::formats::find_format_for_extension(format);
        if (format != "svg" && (info == nullptr || !info->can_write)) {
            g_exit = fail_with("export", jsonOut, po::code::kUsage,
                               "format de sortie « " + format + " » non pris en charge",
                               "extensions : .dst .pes .jef .vp3 .exp .svg (ou --format)");
            return;
        }
        int rc = 0;
        auto opened = load("export", jsonOut, a.osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        std::string extra;
        if (format == "svg") {
            po::QualitySvgOptions so;
            so.outlines = a.outlines;
            so.directions = false;
            so.findings = false;
            so.only_object = openstitch::ObjectId{a.only};
            const auto svg = po::render_quality_svg(opened->project, so);
            if (!svg) {
                g_exit = fail_err("export", jsonOut, po::code::kRefused, svg.error());
                return;
            }
            std::string err;
            if (!write_text(fs::path(a.out), *svg, err)) {
                g_exit = fail_with("export", jsonOut, po::code::kIo, err);
                return;
            }
        } else {
            openstitch::formats::MachineExportOptions options;
            if (a.noTrims) {
                options.trims = openstitch::formats::TrimMode::Drop;
            }
            if (a.noColorChanges) {
                options.color_changes = openstitch::formats::ColorChangeMode::Drop;
            }
            options.stops = a.stops == "drop" ? openstitch::formats::StopMode::Drop
                            : a.stops == "as-color-change"
                                ? openstitch::formats::StopMode::AsColorChange
                                : openstitch::formats::StopMode::Native;
            if (a.start == "center") {
                options.start.mode = openstitch::formats::StartMode::Center;
            } else if (a.start != "first") {
                double xMm = 0.0;
                double yMm = 0.0;
                char tail = 0;
                if (std::sscanf(a.start.c_str(), "%lf,%lf%c", &xMm, &yMm, &tail) != 2) {
                    g_exit = fail_with("export", jsonOut, po::code::kUsage,
                                       "--start : attendu first, center ou X,Y en mm");
                    return;
                }
                options.start.mode = openstitch::formats::StartMode::Custom;
                options.start.point = openstitch::Vec2um{
                    openstitch::Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
                    openstitch::Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}};
            }
            openstitch::project_io::ExportOptimizations optimizations;
            optimizations.reduce_trims = a.reduceTrims;
            optimizations.hidden_travel.allow_same_color_surface = a.sameColour;
            if (auto levels = po::parse_route_levels(a.levels, optimizations.hidden_travel);
                !levels) {
                g_exit = fail_err("export", jsonOut, po::code::kUsage, levels.error());
                return;
            }
            optimizations.hidden_travel.max_bridge_length = openstitch::Micrometers{
                static_cast<std::int32_t>(std::lround(a.reduceMax * 1000.0))};
            openstitch::stitch_generation::TrimReport trimReport;
            if (auto w = openstitch::project_io::export_machine_file(opened->project, info->id,
                                                                     fs::path(a.out), options,
                                                                     optimizations, &trimReport);
                !w) {
                g_exit = fail_err("export", jsonOut, po::code::kRefused, w.error());
                return;
            }
            if (a.reduceTrims) {
                extra = fmt::format(",\"trims_removed\":{}", trimReport.trims_removed);
            }
        }
        std::error_code ec;
        const auto bytes = fs::file_size(fs::path(a.out), ec);
        if (jsonOut) {
            fmt::print("{{\"schema\":\"openstitch.export/1\",\"ok\":true,\"format\":{},"
                       "\"output\":{},\"bytes\":{}{}}}\n",
                       po::json_quote(format), po::json_quote(a.out), ec ? 0 : bytes, extra);
        } else {
            fmt::print("Exporté ({}) : {}\n", format, a.out);
        }
        g_exit = 0;
    });

    // fidelity / density / at / diagnose : analyses sur la séquence EFFECTIVE ---------------
    struct AnalysisArgs {
        std::string osp, output, map, mm, px;
        double a{0.0}, b{0.0}, c{0.0};
        bool noClobber{false};
    };
    const auto write_bytes = [](const std::string& path, const std::vector<std::uint8_t>& bytes,
                                std::string& err) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!f) {
            err = "écriture impossible : " + path;
            return false;
        }
        return true;
    };

    auto fiArgs = std::make_shared<AnalysisArgs>();
    fiArgs->a = 0.55;
    fiArgs->b = 1.6;
    c = project->add_subcommand("fidelity",
                                "Fidélité à l'image source : détails perdus, manques et "
                                "TISSU VISIBLE à l'intérieur du motif (repère exact)");
    c->add_option("osp", fiArgs->osp, "Projet .osp (avec son image source)")->required();
    c->add_option("--thread-mm", fiArgs->a,
                  "Diamètre de fil retenu pour la couverture (défaut 0.55)")
        ->check(CLI::Range(0.1, 2.0));
    c->add_option("--hole-zone-mm", fiArgs->b,
                  "Distance au fond en deçà de laquelle un manque est un bord (défaut 1.6)")
        ->check(CLI::Range(0.2, 10.0));
    c->add_option("--map", fiArgs->map, "Carte d'erreurs PNG (rouge = trou intérieur)");
    c->add_option("--output,-o", fiArgs->output, "Écrit le rapport JSON dans ce fichier");
    c->add_flag("--no-clobber", fiArgs->noClobber, "Refuse d'écraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.fidelity/1) sur stdout");
    c->callback([fiArgs, &jsonOut, write_bytes] {
        for (const auto* p : {&fiArgs->map, &fiArgs->output}) {
            if (const auto refused = check_output("fidelity", jsonOut, *p, fiArgs->noClobber)) {
                g_exit = *refused;
                return;
            }
        }
        int rc = 0;
        auto opened = load("fidelity", jsonOut, fiArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        po::FidelityOptions fo;
        fo.thread_mm = fiArgs->a;
        fo.hole_zone_mm = fiArgs->b;
        fo.error_map = !fiArgs->map.empty();
        const auto report = po::measure_fidelity(opened->project, fo);
        if (!report) {
            g_exit = fail_err("fidelity", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!fiArgs->output.empty() && !write_text(fs::path(fiArgs->output), report->json, err)) {
            g_exit = fail_with("fidelity", jsonOut, po::code::kIo, err);
            return;
        }
        if (!fiArgs->map.empty() && !write_bytes(fiArgs->map, report->error_map_png, err)) {
            g_exit = fail_with("fidelity", jsonOut, po::code::kIo, err);
            return;
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->text);
        g_exit = 0;
    });

    auto deArgs = std::make_shared<AnalysisArgs>();
    deArgs->a = 1.0;
    deArgs->b = 4.0;
    c = project->add_subcommand("density",
                                "Épaisseurs de fil par zone (1 = un remplissage normal) : "
                                "surépaisseurs, carte de chaleur PNG, objets en cause");
    c->add_option("osp", deArgs->osp, "Projet .osp")->required();
    c->add_option("--cell-mm", deArgs->a, "Côté d'une cellule d'analyse en mm (défaut 1)")
        ->check(CLI::Range(0.1, 10.0));
    c->add_option("--hotspot-layers", deArgs->b,
                  "Nombre d'épaisseurs à partir duquel une zone est signalée (défaut 4)")
        ->check(CLI::Range(1.0, 20.0));
    c->add_option("--heatmap", deArgs->map, "Carte de chaleur PNG (bleu clair -> vert -> rouge)");
    c->add_option("--output,-o", deArgs->output, "Écrit le rapport JSON dans ce fichier");
    c->add_flag("--no-clobber", deArgs->noClobber, "Refuse d'écraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.density/1) sur stdout");
    c->callback([deArgs, &jsonOut, write_bytes] {
        for (const auto* p : {&deArgs->map, &deArgs->output}) {
            if (const auto refused = check_output("density", jsonOut, *p, deArgs->noClobber)) {
                g_exit = *refused;
                return;
            }
        }
        int rc = 0;
        auto opened = load("density", jsonOut, deArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        po::DensityOptions dopt;
        dopt.cell_mm = deArgs->a;
        dopt.hotspot_layers = deArgs->b;
        dopt.heatmap = !deArgs->map.empty();
        const auto report = po::measure_density(opened->project, dopt);
        if (!report) {
            g_exit = fail_err("density", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!deArgs->output.empty() && !write_text(fs::path(deArgs->output), report->json, err)) {
            g_exit = fail_with("density", jsonOut, po::code::kIo, err);
            return;
        }
        if (!deArgs->map.empty() && !write_bytes(deArgs->map, report->heatmap_png, err)) {
            g_exit = fail_with("density", jsonOut, po::code::kIo, err);
            return;
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->text);
        g_exit = 0;
    });

    // Carte de risque de tirage : heuristique et profils de tissus NON calibres.
    struct FabricArgs {
        std::string osp, output, map, fabric{"auto"};
        double cellMm{1.0};
        bool noClobber{false};
    };
    auto frArgs = std::make_shared<FabricArgs>();
    c = project->add_subcommand("fabric-risk",
                                "Indice relatif de risque de tirage du tissu (non calibre), "
                                "orientation et concentration des points");
    c->add_option("osp", frArgs->osp, "Projet .osp")->required();
    c->add_option("--fabric", frArgs->fabric, "Profil indicatif : auto, woven, jersey, felt")
        ->check(CLI::IsMember({"auto", "woven", "jersey", "felt"}));
    c->add_option("--cell-mm", frArgs->cellMm, "Cellule d'analyse en mm (defaut 1)")
        ->check(CLI::Range(0.1, 10.0));
    c->add_option("--heatmap", frArgs->map, "Carte PNG de l'indice relatif (0..100)");
    c->add_option("--output,-o", frArgs->output, "Ecrit le rapport JSON");
    c->add_flag("--no-clobber", frArgs->noClobber, "Refuse d'ecraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.fabric-risk/1) sur stdout");
    c->callback([frArgs, &jsonOut, write_bytes] {
        for (const auto* p : {&frArgs->map, &frArgs->output}) {
            if (const auto refused = check_output("fabric-risk", jsonOut, *p, frArgs->noClobber)) {
                g_exit = *refused;
                return;
            }
        }
        int rc = 0;
        auto opened = load("fabric-risk", jsonOut, frArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        po::FabricRiskOptions opt;
        opt.fabric = frArgs->fabric;
        opt.cell_mm = frArgs->cellMm;
        opt.heatmap = !frArgs->map.empty();
        const auto report = po::measure_fabric_risk(opened->project, opt);
        if (!report) {
            g_exit = fail_err("fabric-risk", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!frArgs->output.empty() && !write_text(fs::path(frArgs->output), report->json, err)) {
            g_exit = fail_with("fabric-risk", jsonOut, po::code::kIo, err);
            return;
        }
        if (!frArgs->map.empty() && !write_bytes(frArgs->map, report->heatmap_png, err)) {
            g_exit = fail_with("fabric-risk", jsonOut, po::code::kIo, err);
            return;
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->text);
        g_exit = 0;
    });

    auto atArgs = std::make_shared<AnalysisArgs>();
    atArgs->a = 0.4;
    c = project->add_subcommand("at",
                                "Quel objet a cousu ce point ? Objets dont un brin passe près "
                                "d'un point, du dernier cousu (visible) au premier");
    c->add_option("osp", atArgs->osp, "Projet .osp")->required();
    c->add_option("--mm", atArgs->mm,
                  "Point X,Y en mm (repère modèle : Y vers le haut, origine au "
                  "centre de l'image source)");
    c->add_option("--px", atArgs->px,
                  "Point X,Y en pixels de l'image source (origine en haut à "
                  "gauche)");
    c->add_option("--radius-mm", atArgs->a, "Rayon de recherche en mm (défaut 0.4)")
        ->check(CLI::Range(0.05, 50.0));
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.locate/1) sur stdout");
    c->callback([atArgs, &jsonOut] {
        double x = 0.0, y = 0.0;
        int rc = 0;
        auto opened = load("at", jsonOut, atArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        if (!atArgs->mm.empty() == !atArgs->px.empty()) {
            g_exit = fail_with("at", jsonOut, po::code::kUsage,
                               "indiquer exactement un point : --mm X,Y ou --px X,Y",
                               "exemple : project at p.osp --px 640,410");
            return;
        }
        if (!atArgs->mm.empty()) {
            if (std::sscanf(atArgs->mm.c_str(), "%lf,%lf", &x, &y) != 2) {
                g_exit = fail_with("at", jsonOut, po::code::kUsage,
                                   "point invalide : « " + atArgs->mm + " »", "format : X,Y");
                return;
            }
        } else {
            double px = 0.0, py = 0.0;
            const auto [workW, workH] = po::working_size(opened->project);
            if (std::sscanf(atArgs->px.c_str(), "%lf,%lf", &px, &py) != 2 || workW == 0) {
                g_exit = fail_with("at", jsonOut, po::code::kUsage,
                                   "point en pixels invalide ou projet sans image source",
                                   "format : X,Y ; utiliser --mm sans image");
                return;
            }
            const double mmpp = opened->project.mm_per_px.value;
            x = (px - workW / 2.0) * mmpp;
            y = (workH / 2.0 - py) * mmpp;
        }
        po::LocateQuery q;
        q.x_mm = x;
        q.y_mm = y;
        q.radius_mm = atArgs->a;
        const auto report = po::locate_objects(opened->project, q);
        if (!report) {
            g_exit = fail_err("at", jsonOut, po::code::kRefused, report.error());
            return;
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->text);
        g_exit = 0;
    });

    auto dgArgs = std::make_shared<AnalysisArgs>();
    c = project->add_subcommand("diagnose",
                                "Détecteurs de défauts connus (points hors image, objet "
                                "étalé, fils quasi identiques, tissu visible, "
                                "surépaisseurs, liaisons visibles) avec objets et "
                                "option conseillée (code 2 si erreurs)");
    c->add_option("osp", dgArgs->osp, "Projet .osp")->required();
    c->add_option("--output,-o", dgArgs->output, "Écrit le rapport JSON dans ce fichier");
    c->add_flag("--no-clobber", dgArgs->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.diagnose/1) sur stdout");
    c->callback([dgArgs, &jsonOut] {
        if (const auto refused =
                check_output("diagnose", jsonOut, dgArgs->output, dgArgs->noClobber)) {
            g_exit = *refused;
            return;
        }
        int rc = 0;
        auto opened = load("diagnose", jsonOut, dgArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        const auto report = po::diagnose_project(opened->project);
        if (!report) {
            g_exit = fail_err("diagnose", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!dgArgs->output.empty() && !write_text(fs::path(dgArgs->output), report->json, err)) {
            g_exit = fail_with("diagnose", jsonOut, po::code::kIo, err);
            return;
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->text);
        g_exit = report->errors == 0 ? 0 : kExitQuality;
    });

    // sweep -----------------------------------------------------------------------------
    struct SweepArgs {
        std::string image, sheet, sheetCrop{"0,0,1,1"}, markdown, csv, journal, title, keepDir;
        std::vector<std::string> vary;
        int cols{3}, tile{480};
        bool noClobber{false};
    };
    auto swArgs = std::make_shared<SweepArgs>();
    auto swValues = std::make_shared<DigitizeOptionValues>();
    c = project->add_subcommand(
        "sweep",
        "Balayage d'options de numérisation : une grille de réglages, un tableau comparatif "
        "OBJECTIF (coupes, tissu visible, détails perdus, surépaisseurs, défauts) avec les "
        "variantes non dominées, et une planche de zooms réalistes");
    c->add_option("image", swArgs->image, "Image source (PNG, JPEG, BMP, TIFF)")->required();
    add_digitize_options(c, *swValues);
    c->add_option("--vary,-V", swArgs->vary,
                  "Axe du balayage « option=v1,v2,… » (nom d'une option de digitize sans « -- » ; "
                  "ex. island-cleanup=0,20) ; répétable, produit cartésien (64 variantes au plus)");
    c->add_option("--sheet", swArgs->sheet,
                  "Planche PNG de vignettes réalistes (une par variante)");
    c->add_option("--sheet-crop", swArgs->sheetCrop,
                  "Zone de chaque vignette x0,y0,x1,y1 en fractions du motif (défaut 0,0,1,1)");
    c->add_option("--sheet-cols", swArgs->cols, "Colonnes de la planche (défaut 3)")
        ->check(CLI::Range(1, 12));
    c->add_option("--sheet-tile", swArgs->tile, "Largeur d'une vignette en pixels (défaut 480)")
        ->check(CLI::Range(120, 2000));
    c->add_option("--keep-dir", swArgs->keepDir, "Écrit chaque variante en .osp dans ce dossier");
    c->add_option("--markdown", swArgs->markdown, "Écrit le tableau en Markdown dans ce fichier");
    c->add_option("--csv", swArgs->csv, "Écrit le tableau en CSV dans ce fichier");
    c->add_option("--journal", swArgs->journal,
                  "AJOUTE le tableau (Markdown) à la fin de ce fichier, sous --title");
    c->add_option("--title", swArgs->title, "Titre de la section ajoutée au journal");
    c->add_flag("--no-clobber", swArgs->noClobber, "Refuse d'écraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.sweep/1) sur stdout");
    c->callback([swArgs, swValues, &jsonOut, write_bytes] {
        finalize_digitize_options(*swValues);
        for (const auto* p : {&swArgs->sheet, &swArgs->markdown, &swArgs->csv}) {
            if (const auto refused = check_output("sweep", jsonOut, *p, swArgs->noClobber)) {
                g_exit = *refused;
                return;
            }
        }
        po::SweepOptions so;
        so.base = swValues->settings;
        for (const auto& spec : swArgs->vary) {
            const auto eq = spec.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 >= spec.size()) {
                g_exit = fail_with(
                    "sweep", jsonOut, po::code::kUsage, "axe invalide : « " + spec + " »",
                    "format : option=v1,v2 (options : " + po::digitize_setting_keys() + ")");
                return;
            }
            po::SweepAxis axis;
            axis.key = spec.substr(0, eq);
            std::stringstream values(spec.substr(eq + 1));
            for (std::string v; std::getline(values, v, ',');) {
                axis.values.push_back(v);
            }
            so.axes.push_back(std::move(axis));
        }
        so.sheet = !swArgs->sheet.empty();
        so.sheet_columns = swArgs->cols;
        so.sheet_tile_px = swArgs->tile;
        so.keep_dir = swArgs->keepDir;
        if (std::sscanf(swArgs->sheetCrop.c_str(), "%lf,%lf,%lf,%lf", &so.sheet_crop[0],
                        &so.sheet_crop[1], &so.sheet_crop[2], &so.sheet_crop[3]) != 4) {
            g_exit =
                fail_with("sweep", jsonOut, po::code::kUsage,
                          "zone invalide : « " + swArgs->sheetCrop + " »", "format : x0,y0,x1,y1");
            return;
        }
        if (!so.keep_dir.empty()) {
            std::error_code ec;
            fs::create_directories(so.keep_dir, ec);
        }
        const po::StatusLog log = [](std::string_view line) { fmt::print(stderr, "{}\n", line); };
        const auto report = po::run_sweep(fs::path(swArgs->image), so, log);
        if (!report) {
            g_exit = fail_err("sweep", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!swArgs->markdown.empty() &&
            !write_text(fs::path(swArgs->markdown), report->markdown, err)) {
            g_exit = fail_with("sweep", jsonOut, po::code::kIo, err);
            return;
        }
        if (!swArgs->csv.empty() && !write_text(fs::path(swArgs->csv), report->csv, err)) {
            g_exit = fail_with("sweep", jsonOut, po::code::kIo, err);
            return;
        }
        if (!swArgs->sheet.empty() && !write_bytes(swArgs->sheet, report->sheet_png, err)) {
            g_exit = fail_with("sweep", jsonOut, po::code::kIo, err);
            return;
        }
        if (!swArgs->journal.empty()) {
            std::ofstream journal(swArgs->journal, std::ios::app);
            journal << "\n### " << (swArgs->title.empty() ? "Balayage" : swArgs->title) << "\n\n"
                    << "Image : `" << swArgs->image << "`\n\n"
                    << report->markdown;
            if (!journal) {
                g_exit = fail_with("sweep", jsonOut, po::code::kIo,
                                   "écriture impossible : " + swArgs->journal);
                return;
            }
        }
        fmt::print("{}", jsonOut ? report->json + "\n" : report->markdown);
        g_exit = 0;
    });

    // render ----------------------------------------------------------------------------
    struct RenderArgs {
        std::string osp, out, fabric{"#00FF00"}, crop{"0,0,1,1"};
        double pxPerMm{5.0}, thread{0.35};
        bool noClobber{false};
    };
    auto rdArgs = std::make_shared<RenderArgs>();
    c = project->add_subcommand("render", "Rendu RÉALISTE (fils texturés, le moteur de l'aperçu de "
                                          "l'application) en PNG, sur fond de couleur franche "
                                          "(vert par défaut : trous et interstices visibles)");
    c->add_option("osp", rdArgs->osp, "Projet .osp")->required();
    c->add_option("--output,-o", rdArgs->out, "PNG à produire")->required();
    c->add_option("--px-per-mm", rdArgs->pxPerMm, "Résolution en pixels par mm (défaut : 5)")
        ->check(CLI::Range(0.5, 40.0));
    CLI::Option* fabricOpt =
        c->add_option("--fabric", rdArgs->fabric, "Couleur du tissu #RRGGBB (défaut : #00FF00)");
    c->add_option("--thread-mm", rdArgs->thread, "Diamètre apparent du fil en mm (défaut : 0.35)")
        ->check(CLI::Range(0.1, 1.0));
    c->add_option("--crop", rdArgs->crop,
                  "Zone rendue : x0,y0,x1,y1 en fractions de la boîte du motif, origine en haut à "
                  "gauche (défaut : 0,0,1,1 = tout)");
    c->add_flag("--no-clobber", rdArgs->noClobber, "Refuse d'écraser le fichier de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON sur stdout");
    c->callback([rdArgs, fabricOpt, &jsonOut] {
        const auto& a = *rdArgs;
        if (const auto refused = check_output("render", jsonOut, a.out, a.noClobber)) {
            g_exit = *refused;
            return;
        }
        po::RenderOptions ro;
        ro.px_per_mm = a.pxPerMm;
        ro.thread_width_mm = a.thread;
        unsigned r = 0, g = 0, b = 0;
        if (a.fabric.size() != 7 || a.fabric[0] != '#' ||
            std::sscanf(a.fabric.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
            g_exit = fail_with("render", jsonOut, po::code::kUsage,
                               "couleur de tissu invalide : « " + a.fabric + " »",
                               "format attendu : #RRGGBB");
            return;
        }
        ro.fabric_rgb = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                         static_cast<std::uint8_t>(b)};
        if (std::sscanf(a.crop.c_str(), "%lf,%lf,%lf,%lf", &ro.crop[0], &ro.crop[1], &ro.crop[2],
                        &ro.crop[3]) != 4) {
            g_exit =
                fail_with("render", jsonOut, po::code::kUsage, "zone invalide : « " + a.crop + " »",
                          "format attendu : x0,y0,x1,y1");
            return;
        }
        int rc = 0;
        auto opened = load("render", jsonOut, a.osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        const auto rendered = po::render_realistic_png(opened->project, ro);
        if (!rendered) {
            g_exit = fail_err("render", jsonOut, po::code::kRefused, rendered.error());
            return;
        }
        std::ofstream file(a.out, std::ios::binary);
        file.write(reinterpret_cast<const char*>(rendered->png.data()),
                   static_cast<std::streamsize>(rendered->png.size()));
        if (!file) {
            g_exit = fail_with("render", jsonOut, po::code::kIo, "écriture impossible : " + a.out);
            return;
        }
        if (jsonOut) {
            fmt::print("{{\"schema\":\"openstitch.render/1\",\"ok\":true,\"output\":{},"
                       "\"width\":{},\"height\":{},\"px_per_mm\":{},\"fabric\":{}}}\n",
                       po::json_quote(a.out), rendered->width, rendered->height,
                       rendered->px_per_mm, po::json_quote(a.fabric));
        } else {
            fmt::print("Rendu écrit : {} ({} x {} px, {:.2f} px/mm)\n", a.out, rendered->width,
                       rendered->height, rendered->px_per_mm);
            if (fabricOpt->count() == 0) {
                fmt::print(
                    stderr,
                    "Info : fond par défaut {} (vert franc : les trous du motif ressortent) ;"
                    " --fabric #RRGGBB pour une autre couleur de tissu.\n",
                    a.fabric);
            }
        }
        g_exit = 0;
    });

    // operations ------------------------------------------------------------------------
    c = project->add_subcommand("operations", "Catalogue des opérations (noms, arguments, "
                                              "unités, exemples)");
    c->add_flag("--json", jsonOut, "Catalogue JSON machine-lisible (openstitch.operations/1)");
    c->callback([&jsonOut] {
        const std::string catalog = po::operations_catalog_json();
        if (jsonOut) {
            fmt::print("{}\n", catalog);
            g_exit = 0;
            return;
        }
        const auto text = po::operations_catalog_text(catalog);
        if (!text) {
            g_exit = fail_err("operations", false, po::code::kInternal, text.error());
            return;
        }
        fmt::print("{}", *text);
        g_exit = 0;
    });

    // check -----------------------------------------------------------------------------
    struct CheckArgs {
        std::string osp, svg, output;
        std::uint64_t only{0};
        bool directions{false};
        bool noOutlines{false};
        bool noObjects{false};
        bool noClobber{false};
    };
    auto ckArgs = std::make_shared<CheckArgs>();
    c = project->add_subcommand("check", "Contrôle qualité : métriques du moteur, couverture par "
                                         "objet, cadre, SVG de diagnostic (code 2 si erreurs)");
    c->add_option("osp", ckArgs->osp, "Projet .osp")->required();
    c->add_option("--svg", ckArgs->svg, "SVG de diagnostic (points, contours, directions)");
    c->add_option("--output,-o", ckArgs->output, "Écrit le rapport JSON dans ce fichier");
    c->add_option("--only", ckArgs->only, "Limite le détail à un objet de broderie");
    c->add_flag("--directions", ckArgs->directions, "SVG : flèches de direction de broderie");
    c->add_flag("--no-outlines", ckArgs->noOutlines, "SVG : sans contours des régions d'origine");
    c->add_flag("--no-objects", ckArgs->noObjects, "Rapport sans détail par objet");
    c->add_flag("--no-clobber", ckArgs->noClobber, "Refuse d'écraser les fichiers de sortie");
    c->add_flag("--json", jsonOut, "Rapport JSON (openstitch.quality/1) sur stdout");
    c->callback([ckArgs, &jsonOut] {
        for (const auto* p : {&ckArgs->svg, &ckArgs->output}) {
            if (const auto refused = check_output("check", jsonOut, *p, ckArgs->noClobber)) {
                g_exit = *refused;
                return;
            }
        }
        int rc = 0;
        auto opened = load("check", jsonOut, ckArgs->osp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        po::QualityOptions qo;
        qo.per_object = !ckArgs->noObjects;
        qo.only_object = openstitch::ObjectId{ckArgs->only};
        const auto report = po::build_quality_report(opened->project, qo);
        if (!report) {
            g_exit = fail_err("check", jsonOut, po::code::kRefused, report.error());
            return;
        }
        std::string err;
        if (!ckArgs->output.empty() && !write_text(fs::path(ckArgs->output), report->json, err)) {
            g_exit = fail_with("check", jsonOut, po::code::kIo, err);
            return;
        }
        if (!ckArgs->svg.empty()) {
            po::QualitySvgOptions so;
            so.outlines = !ckArgs->noOutlines;
            so.directions = ckArgs->directions;
            so.only_object = openstitch::ObjectId{ckArgs->only};
            const auto svg = po::render_quality_svg(opened->project, so);
            if (!svg) {
                g_exit = fail_err("check", jsonOut, po::code::kRefused, svg.error());
                return;
            }
            if (!write_text(fs::path(ckArgs->svg), *svg, err)) {
                g_exit = fail_with("check", jsonOut, po::code::kIo, err);
                return;
            }
        }
        if (jsonOut) {
            fmt::print("{}\n", report->json);
        } else if (const auto text = po::quality_report_text(report->json)) {
            fmt::print("{}", *text);
        }
        g_exit = report->passed() ? 0 : kExitQuality;
    });

    // select ----------------------------------------------------------------------------
    auto selOsp = std::make_shared<std::string>();
    auto selWhere = std::make_shared<std::vector<std::string>>();
    c = project->add_subcommand("select", "[initiative] Sélectionne des objets de broderie par "
                                          "critères (couleur, type, aire, position, nom…)");
    c->add_option("osp", *selOsp, "Projet .osp")->required();
    c->add_option("--where,-w", *selWhere,
                  "Critère clé=valeur, répétable (ET) : id, color, type, visible, locked, name, "
                  "area_gt, area_lt, within=x0,y0,x1,y1");
    c->add_flag("--json", jsonOut, "Sélection JSON (openstitch.selection/1)");
    c->callback([selOsp, selWhere, &jsonOut] {
        int rc = 0;
        auto opened = load("select", jsonOut, *selOsp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        const auto ids = po::select_objects(opened->project, *selWhere);
        if (!ids) {
            g_exit = fail_err("select", jsonOut, po::code::kUsage, ids.error());
            return;
        }
        if (jsonOut) {
            std::string list;
            for (const auto id : *ids) {
                list += (list.empty() ? "" : ",") + std::to_string(id.value);
            }
            fmt::print("{{\"schema\":\"openstitch.selection/1\",\"count\":{},\"ids\":[{}]}}\n",
                       ids->size(), list);
        } else {
            for (const auto id : *ids) {
                fmt::print("{}\n", id.value);
            }
        }
        g_exit = 0;
    });

    // diff ------------------------------------------------------------------------------
    auto dfA = std::make_shared<std::string>();
    auto dfB = std::make_shared<std::string>();
    c = project->add_subcommand("diff", "[initiative] Différences entre deux projets (cadre, "
                                        "finitions, objets, paramètres, séquence)");
    c->add_option("a", *dfA, "Projet de référence")->required();
    c->add_option("b", *dfB, "Projet à comparer")->required();
    c->add_flag("--json", jsonOut, "Différences JSON (openstitch.project-diff/1)");
    c->callback([dfA, dfB, &jsonOut] {
        int rc = 0;
        auto a = load("diff", jsonOut, *dfA, rc);
        if (!a) {
            g_exit = rc;
            return;
        }
        auto b = load("diff", jsonOut, *dfB, rc);
        if (!b) {
            g_exit = rc;
            return;
        }
        const auto diff = po::diff_projects_json(a->project, b->project);
        if (!diff) {
            g_exit = fail_err("diff", jsonOut, po::code::kRefused, diff.error());
            return;
        }
        fmt::print("{}\n", *diff);
        g_exit = 0;
    });

    // validate --------------------------------------------------------------------------
    auto vaOsp = std::make_shared<std::string>();
    c = project->add_subcommand("validate", "[initiative] Validation automatisée : intégrité des "
                                            "références + contrôle qualité (code 2 si invalide)");
    c->add_option("osp", *vaOsp, "Projet .osp")->required();
    c->add_flag("--json", jsonOut, "Verdict JSON (openstitch.validation/1)");
    c->callback([vaOsp, &jsonOut] {
        int rc = 0;
        auto opened = load("validate", jsonOut, *vaOsp, rc);
        if (!opened) {
            g_exit = rc;
            return;
        }
        const auto problems = po::check_integrity(opened->project);
        const auto quality = po::build_quality_report(opened->project, {});
        if (!quality) {
            g_exit = fail_err("validate", jsonOut, po::code::kRefused, quality.error());
            return;
        }
        const bool valid = problems.empty() && quality->passed();
        if (jsonOut) {
            std::string list;
            for (const auto& p : problems) {
                list += (list.empty() ? "" : ",") + po::json_quote(p);
            }
            fmt::print("{{\"schema\":\"openstitch.validation/1\",\"valid\":{},\"integrity\":[{}],"
                       "\"quality\":{}}}\n",
                       valid ? "true" : "false", list, quality->json);
        } else {
            for (const auto& p : problems) {
                fmt::print("intégrité : {}\n", p);
            }
            if (const auto text = po::quality_report_text(quality->json)) {
                fmt::print("{}", *text);
            }
            fmt::print("{}\n", valid ? "Projet valide" : "Projet INVALIDE");
        }
        g_exit = valid ? 0 : kExitQuality;
    });

    return project;
}

// Liste machine-lisible des commandes et options (introspection de CLI11).
namespace {

std::string describe_app(const CLI::App& app) {
    std::string out = "{\"name\":" + po::json_quote(app.get_name()) +
                      ",\"description\":" + po::json_quote(app.get_description()) +
                      ",\"options\":[";
    bool first = true;
    for (const auto* opt : app.get_options()) {
        if (opt->get_name().empty() || opt->get_name() == "-h,--help") {
            continue;
        }
        if (!first) {
            out += ",";
        }
        first = false;
        out += "{\"name\":" + po::json_quote(opt->get_name()) +
               ",\"description\":" + po::json_quote(opt->get_description()) +
               ",\"required\":" + (opt->get_required() ? "true" : "false") +
               ",\"positional\":" + (opt->get_positional() ? "true" : "false") + "}";
    }
    out += "],\"subcommands\":[";
    first = true;
    for (const auto* sub : app.get_subcommands(nullptr)) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += describe_app(*sub);
    }
    out += "]}";
    return out;
}

} // namespace

CLI::App* register_commands_listing(CLI::App& app, bool& jsonOut) {
    auto* c = app.add_subcommand("commands", "Liste machine-lisible des commandes et options "
                                             "(introspection, pour les agents)");
    c->group("Projets");
    c->add_flag("--json", jsonOut, "(implicite) JSON openstitch.commands/1");
    CLI::App* root = &app;
    c->callback([root] {
        fmt::print("{{\"schema\":\"openstitch.commands/1\",\"cli\":{}}}\n", describe_app(*root));
        g_exit = 0;
    });
    return c;
}

} // namespace openstitch_cli
