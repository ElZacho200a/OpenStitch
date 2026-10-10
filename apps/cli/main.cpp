// SPDX-License-Identifier: Apache-2.0
#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_analysis/metrics.hpp"
#include "openstitch/stitch_analysis/production_sheet.hpp"
#include "openstitch/stitch_analysis/project_metrics.hpp"
#include "openstitch/stitch_generation/border_satin.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/join.hpp"
#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/tatami.hpp"
#include "openstitch/vectorization/vectorize.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Mesures de qualité d'une séquence (Lot G) : communes à `stats` (DST relu,
// verrous reconnus par leur forme) et `digitize` (séquence effective, passes
// connues).
void print_sequence_metrics(std::FILE* out, const openstitch::stitch::StitchSequence& seq,
                            bool fromDst,
                            openstitch::stitch_analysis::SequenceMetrics* result = nullptr) {
    using namespace openstitch;
    stitch_analysis::SequenceMetricsOptions opts;
    opts.infer_locks = fromDst;
    if (fromDst) {
        opts.length_tolerance = Micrometers{100}; // résolution DST : 0,1 mm
    }
    const auto m = stitch_analysis::sequence_metrics(seq, opts);
    if (result) {
        *result = m;
    }
    fmt::print(out, "Déplacements        : {}\n", m.moves);
    fmt::print(out, "  > {:.1f} mm sans coupe : {}\n", opts.trim_threshold.value / 1000.0,
               m.long_moves_without_trim);
    fmt::print(out,
               "Points < {:.1f} mm    : {} ({:.2f} %) hors points d'arrêt ; {} dans les points "
               "d'arrêt{}\n",
               opts.short_stitch.value / 1000.0, m.short_stitches,
               m.stitches
                   ? 100.0 * static_cast<double>(m.short_stitches) / static_cast<double>(m.stitches)
                   : 0.0,
               m.short_lock_stitches, fromDst ? " (reconnus par leur forme)" : "");
    // Histogramme des directions : les 6 tranches de 5° les plus fréquentes.
    std::vector<std::pair<std::size_t, int>> bins;
    std::size_t total = 0;
    for (int b = 0; b < 36; ++b) {
        bins.emplace_back(m.direction_histogram[static_cast<std::size_t>(b)], b * 5);
        total += m.direction_histogram[static_cast<std::size_t>(b)];
    }
    std::stable_sort(bins.begin(), bins.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    fmt::print(out, "Directions (points >= 1 mm, modulo 180°) :");
    for (int k = 0; k < 6 && bins[static_cast<std::size_t>(k)].first > 0; ++k) {
        fmt::print(out, " {}° {:.0f} %", bins[static_cast<std::size_t>(k)].second,
                   100.0 * static_cast<double>(bins[static_cast<std::size_t>(k)].first) /
                       static_cast<double>(std::max<std::size_t>(1, total)));
    }
    fmt::print(out, "\n");
}

// Codes de sortie (documentés dans `openstitch-cli --help` et docs/source/cli.md) :
// 0 succès ; 1 entrée ou E/S (fichier illisible, option incohérente, rien à produire) ;
// 2 contrôle qualité non satisfait (stitchdebug --shape ring). Les erreurs d'usage de
// la ligne de commande (option inconnue, valeur hors plage…) gardent les codes de CLI11.
constexpr int kExitOk = 0;
constexpr int kExitInput = 1;
constexpr int kExitQuality = 2;

// Message d'erreur homogène : « openstitch-cli <sous-commande> : message », suivi
// d'une piste de correction optionnelle. Renvoie kExitInput pour `return cli_error(...)`.
int cli_error(const char* command, const std::string& message, const std::string& hint = {}) {
    fmt::print(stderr, "openstitch-cli {} : {}\n", command, message);
    if (!hint.empty()) {
        fmt::print(stderr, "  Piste : {}\n", hint);
    }
    return kExitInput;
}

constexpr const char* kImageFormatsHint =
    "formats acceptés : PNG, JPEG, BMP, TIFF ; vérifiez le chemin et l'extension du fichier";
constexpr const char* kDstHint =
    "attendu : un fichier de broderie machine (.dst, .pes, .jef ou .exp) ; produisez-le avec "
    "`digitize` ou l'export du bureau";
constexpr const char* kOspHint =
    "attendu : un projet .osp enregistré par OpenStitch Studio (Fichier -> Enregistrer)";

// Échappement minimal pour les chaînes du JSON (chemins, noms).
std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (const char ch : in) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += fmt::format("\\u{:04x}", c);
            } else {
                out += ch;
            }
        }
    }
    return out;
}

std::string json_str(const std::string& s) {
    return "\"" + json_escape(s) + "\"";
}

// Refuse tôt (avant tout calcul coûteux) une sortie impossible : dossier parent absent,
// ou fichier existant avec --no-clobber. Renvoie un code de sortie si refus.
std::optional<int> check_output_path(const char* command, const std::string& path, bool noClobber) {
    if (path.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path p(path);
    std::error_code ec;
    const auto parent = p.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, ec)) {
        return cli_error(command,
                         fmt::format("le dossier de sortie « {} » n'existe pas", parent.string()),
                         "créez-le d'abord ou choisissez un autre chemin");
    }
    if (noClobber && std::filesystem::exists(p, ec)) {
        return cli_error(command, fmt::format("« {} » existe déjà (--no-clobber)", path),
                         "retirez --no-clobber (ou ajoutez --force) pour l'écraser");
    }
    return std::nullopt;
}

// Résolution déclarée dans l'en-tête du fichier image (PNG pHYs, JPEG JFIF), en dpi.
// Les autres formats, ou une valeur absente/aberrante, donnent nullopt.
std::optional<double> read_file_dpi(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const auto readU32 = [](const unsigned char* b) {
        return (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
               (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    };
    unsigned char sig[8] = {};
    in.read(reinterpret_cast<char*>(sig), 8);
    if (in.gcount() < 8) {
        return std::nullopt;
    }
    std::optional<double> dpi;
    if (sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G') {
        for (int guard = 0; guard < 64; ++guard) {
            unsigned char head[8];
            in.read(reinterpret_cast<char*>(head), 8);
            if (in.gcount() < 8) {
                break;
            }
            const std::uint32_t len = readU32(head);
            const std::string type(reinterpret_cast<char*>(head) + 4, 4);
            if (type == "IDAT" || type == "IEND") {
                break;
            }
            if (type == "pHYs" && len == 9) {
                unsigned char d[9];
                in.read(reinterpret_cast<char*>(d), 9);
                if (in.gcount() == 9 && d[8] == 1) {
                    dpi = readU32(d) * 0.0254;
                }
                break;
            }
            in.seekg(static_cast<std::streamoff>(len) + 4, std::ios::cur); // données + CRC
        }
    } else if (sig[0] == 0xFF && sig[1] == 0xD8) {
        in.seekg(2, std::ios::beg);
        for (int guard = 0; guard < 64; ++guard) {
            unsigned char m[4];
            in.read(reinterpret_cast<char*>(m), 4);
            if (in.gcount() < 4 || m[0] != 0xFF || m[1] == 0xDA) {
                break;
            }
            const std::uint32_t len = (static_cast<std::uint32_t>(m[2]) << 8) | m[3];
            if (m[1] == 0xE0 && len >= 16) {
                unsigned char d[14];
                in.read(reinterpret_cast<char*>(d), 14);
                if (in.gcount() == 14 && std::string(reinterpret_cast<char*>(d), 4) == "JFIF") {
                    const double x = (d[8] << 8) | d[9];
                    if (d[7] == 1) {
                        dpi = x;
                    } else if (d[7] == 2) {
                        dpi = x * 2.54;
                    }
                }
                break;
            }
            in.seekg(static_cast<std::streamoff>(len) - 2, std::ios::cur);
        }
    }
    if (dpi && (*dpi < 10.0 || *dpi > 5000.0)) {
        return std::nullopt;
    }
    return dpi;
}

int run_info(const std::string& path, std::optional<double> dpiOption, bool json) {
    const auto info = openstitch::image::read_image_info(std::filesystem::path(path));
    if (!info) {
        return cli_error("info", info.error().message, kImageFormatsHint);
    }
    double dpi = 96.0;
    const char* dpiSource = "défaut (aucune résolution dans le fichier)";
    if (dpiOption) {
        dpi = *dpiOption;
        dpiSource = "option --dpi";
    } else if (const auto fileDpi = read_file_dpi(std::filesystem::path(path))) {
        dpi = *fileDpi;
        dpiSource = "lue dans le fichier";
    }
    const double mm_per_px = 25.4 / dpi;
    const double wMm = info->width_px * mm_per_px;
    const double hMm = info->height_px * mm_per_px;
    if (json) {
        fmt::print("{{\"file\":{},\"format\":{},\"width_px\":{},\"height_px\":{},\"channels\":{},"
                   "\"has_alpha\":{},\"dpi\":{:g},\"dpi_source\":{},\"width_mm\":{:.2f},"
                   "\"height_mm\":{:.2f}}}\n",
                   json_str(path), json_str(info->format), info->width_px, info->height_px,
                   info->channels, info->has_alpha ? "true" : "false", dpi, json_str(dpiSource),
                   wMm, hMm);
        return kExitOk;
    }
    fmt::print("Fichier        : {}\n", path);
    fmt::print("Format         : {}\n", info->format);
    fmt::print("Dimensions     : {} x {} px\n", info->width_px, info->height_px);
    fmt::print("Canaux         : {}\n", info->channels);
    fmt::print("Canal alpha    : {}\n", info->has_alpha ? "oui" : "non");
    fmt::print("Résolution     : {:g} dpi ({})\n", dpi, dpiSource);
    fmt::print("Taille estimée : {:.1f} x {:.1f} mm (à {:g} dpi)\n", wMm, hMm, dpi);
    return kExitOk;
}

// Lit un fichier de broderie machine ; le format vient de l'extension (registre de formats :
// dst, pes, jef, exp), repli DST pour une extension inconnue (comportement historique).
openstitch::Result<openstitch::stitch::StitchSequence>
read_machine_sequence(const std::string& path) {
    using namespace openstitch;
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    const auto* info = formats::find_format_for_extension(ext);
    if (info == nullptr || !info->can_read || info->decode == nullptr) {
        return formats::read_dst_file(std::filesystem::path(path));
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput, "Fichier introuvable ou illisible : " + path);
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    return info->decode(bytes);
}

int run_stats(const std::string& path, bool json) {
    const auto seq = read_machine_sequence(path);
    if (!seq) {
        return cli_error("stats", seq.error().message, kDstHint);
    }
    const auto stats = openstitch::stitch::compute_stats(*seq);
    const double wMm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double hMm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    if (json) {
        openstitch::stitch_analysis::SequenceMetrics m;
        // Le détail lisible va sur stderr : stdout ne contient que le JSON.
        print_sequence_metrics(stderr, *seq, true, &m);
        fmt::print("{{\"file\":{},\"stitches\":{},\"jumps\":{},\"trims\":{},\"color_changes\":{},"
                   "\"width_mm\":{:.2f},\"height_mm\":{:.2f},\"thread_m\":{:.3f},"
                   "\"moves\":{},\"long_moves_without_trim\":{},\"short_stitches\":{},"
                   "\"short_lock_stitches\":{}}}\n",
                   json_str(path), stats.stitches, stats.jumps, stats.trims, stats.color_changes,
                   wMm, hMm, stats.thread_length_um / 1e6, m.moves, m.long_moves_without_trim,
                   m.short_stitches, m.short_lock_stitches);
        return kExitOk;
    }
    fmt::print("Fichier            : {}\n", path);
    fmt::print("Points             : {}\n", stats.stitches);
    fmt::print("Sauts              : {}\n", stats.jumps);
    fmt::print("Coupes             : {}\n", stats.trims);
    fmt::print("Changements de fil : {}\n", stats.color_changes);
    fmt::print("Dimensions         : {:.1f} x {:.1f} mm\n", wMm, hMm);
    fmt::print("Fil cousu estimé   : {:.2f} m\n", stats.thread_length_um / 1e6);
    print_sequence_metrics(stdout, *seq, true);
    return kExitOk;
}

int run_dst2svg(const std::string& input, const std::string& output, bool noClobber) {
    if (const auto refused = check_output_path("dst2svg", output, noClobber)) {
        return *refused;
    }
    const auto seq = read_machine_sequence(input);
    if (!seq) {
        return cli_error("dst2svg", seq.error().message, kDstHint);
    }
    const auto written = openstitch::formats::write_svg_file(std::filesystem::path(output), *seq);
    if (!written) {
        return cli_error("dst2svg", written.error().message);
    }
    fmt::print("SVG écrit : {}\n", output);
    return kExitOk;
}

// Formes de référence procédurales pour inspecter le moteur (§34-35).
openstitch::geometry::Path debug_shape(const std::string& name) {
    using namespace openstitch;
    using geometry::NodeType;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                  std::nullopt, std::nullopt};
    };
    geometry::Path p;
    if (name == "line") {
        p.closed = false;
        p.nodes = {corner(0, 0), corner(40'000, 0)};
    } else if (name == "corner") {
        p.closed = false;
        p.nodes = {corner(0, 0), corner(30'000, 0), corner(30'000, 30'000)};
    } else if (name == "circle") {
        p.closed = true;
        const int sides = 64;
        for (int i = 0; i < sides; ++i) {
            const double a = 2.0 * std::numbers::pi * i / sides;
            p.nodes.push_back(corner(static_cast<std::int32_t>(std::lround(20'000 * std::cos(a))),
                                     static_cast<std::int32_t>(std::lround(20'000 * std::sin(a)))));
        }
    } else if (name == "bezier") {
        p.closed = false;
        geometry::PathNode a = corner(0, 0);
        a.tan_out = Vec2um{Micrometers{15'000}, Micrometers{30'000}};
        geometry::PathNode b = corner(40'000, 0);
        b.tan_in = Vec2um{Micrometers{-15'000}, Micrometers{30'000}};
        p.nodes = {a, b};
    } else { // "star" : coins vifs
        p.closed = true;
        const int pts = 5;
        for (int i = 0; i < pts * 2; ++i) {
            const double a = std::numbers::pi * i / pts - std::numbers::pi / 2.0;
            const double r = (i % 2 == 0) ? 22'000.0 : 9'000.0;
            p.nodes.push_back(corner(static_cast<std::int32_t>(std::lround(r * std::cos(a))),
                                     static_cast<std::int32_t>(std::lround(r * std::sin(a)))));
        }
    }
    return p;
}

// Remplissage tatami d'un anneau (extérieur 40 mm, trou central 16 mm) pour
// inspecter le routage autour d'un trou, les sous-couches et l'underpath (Lot 7).
// Passe par generate_sequence (passes taguées). Compte les coutures qui
// traverseraient le trou (doit être 0). `underlayMask` : 1 contour, 2 parallèle.
int run_filldebug(double lengthMm, const std::string& outSvg, int underlayMask, bool underpath) {
    using namespace openstitch;
    using geometry::NodeType;
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, NodeType::Corner,
                                  std::nullopt, std::nullopt};
    };
    geometry::PathSet ring;
    ring.outer.closed = true;
    ring.outer.nodes = {corner(0, 0), corner(20'000, 0), corner(20'000, 20'000), corner(0, 20'000)};
    geometry::Path hole;
    hole.closed = true;
    hole.nodes = {corner(6'000, 6'000), corner(14'000, 6'000), corner(14'000, 14'000),
                  corner(6'000, 14'000)};
    ring.holes.push_back(hole);

    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{ring.outer, ring.holes});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    document::TatamiParams tp;
    tp.row_spacing = Micrometers{2'000};
    tp.stitch_length = to_micrometers(Millimeters{lengthMm});
    tp.inset = Micrometers{0};
    tp.underlay_edge = (underlayMask & 1) != 0;
    tp.underlay_parallel = (underlayMask & 2) != 0;
    tp.hidden_underpath = underpath;
    emb.params = tp;
    project.embroidery_objects.push_back(emb);

    // Projet synthetique construit ici meme, jamais charge/sauvegarde, aucune
    // retouche manuelle possible. raw-sequence-ok: generateur de debug.
    const auto seq = stitch_generation::generate_sequence(project);
    if (!seq) {
        return cli_error("stitchdebug", seq.error().message);
    }
    const auto stats = stitch::compute_stats(*seq);

    int sewnCrossingHole = 0;
    for (std::size_t i = 1; i < seq->commands.size(); ++i) {
        const auto& c = seq->commands[i];
        if (c.type != stitch::CommandType::Stitch)
            continue;
        const Vec2um a = seq->commands[i - 1].pos;
        const Vec2um mid{Micrometers{(a.x.value + c.pos.x.value) / 2},
                         Micrometers{(a.y.value + c.pos.y.value) / 2}};
        if (mid.x.value > 6'500 && mid.x.value < 13'500 && mid.y.value > 6'500 &&
            mid.y.value < 13'500) {
            ++sewnCrossingHole;
        }
    }

    int under = 0, travel = 0;
    for (const auto& c : seq->commands) {
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Underlay)
            ++under;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::Travel)
            ++travel;
    }
    fmt::print("Anneau tatami (trou central)\n");
    fmt::print("Points cousus : {}  |  déplacements : {}\n", stats.stitches, stats.jumps);
    fmt::print("Sous-couche : {}  |  underpath (Travel) : {}\n", under, travel);
    fmt::print("Coutures traversant le trou : {}  (doit être 0)\n", sewnCrossingHole);

    if (!outSvg.empty()) {
        // SVG maison : chaque liaison colorée par passe (contour vert, couche sup.
        // gris, underpath bleu, saut rouge pointillé).
        const auto pt = [](Vec2um p) {
            return fmt::format("{:.3f} {:.3f}", p.x.value / 1000.0, -p.y.value / 1000.0);
        };
        std::string body;
        for (std::size_t i = 1; i < seq->commands.size(); ++i) {
            const auto& prev = seq->commands[i - 1];
            const auto& cur = seq->commands[i];
            if (cur.type == stitch::CommandType::End)
                continue;
            const char* stroke = nullptr;
            const char* dash = "";
            if (cur.type == stitch::CommandType::Jump) {
                stroke = "#e00";
                dash = " stroke-dasharray=\"0.6 0.4\"";
            } else if (cur.pass == stitch::StitchPass::Underlay) {
                stroke = "#0a9";
            } else if (cur.pass == stitch::StitchPass::Travel) {
                stroke = "#06c";
            } else {
                stroke = "#333";
            }
            body += fmt::format(
                "<path d=\"M{} L{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"0.18\"{}/>\n",
                pt(prev.pos), pt(cur.pos), stroke, dash);
        }
        std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"-2 -22 24 24\" width=\"800\">\n"
            "<rect x=\"-2\" y=\"-22\" width=\"24\" height=\"24\" fill=\"#fff\"/>\n" +
            body + "</svg>\n";
        std::ofstream f(std::filesystem::path(outSvg), std::ios::binary | std::ios::trunc);
        if (!f) {
            return cli_error("stitchdebug", fmt::format("impossible d'écrire {}", outSvg),
                             "vérifiez que le dossier existe et que le fichier n'est pas ouvert");
        }
        f << svg;
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return sewnCrossingHole == 0 ? kExitOk : kExitQuality;
}

int run_stitchdebug(const std::string& shape, double lengthMm, int repeats,
                    const std::string& outSvg, int underlayMask, bool underpath) {
    using namespace openstitch;
    if (const auto refused = check_output_path("stitchdebug", outSvg, false)) {
        return *refused;
    }
    if (shape == "ring") {
        return run_filldebug(lengthMm, outSvg, underlayMask, underpath);
    }
    const auto path = debug_shape(shape);

    stitch_generation::RunningConfig cfg;
    cfg.target_length = to_micrometers(Millimeters{lengthMm});
    const auto result = stitch_generation::run_stitch(path, cfg);
    const auto points = stitch_generation::apply_repeats(result.points, repeats);

    stitch::StitchSequence seq;
    if (!points.empty()) {
        seq.commands.push_back({points.front(), stitch::CommandType::Jump, ObjectId{}});
        for (const Vec2um& p : points) {
            seq.commands.push_back({p, stitch::CommandType::Stitch, ObjectId{}});
        }
        seq.commands.push_back({points.back(), stitch::CommandType::End, ObjectId{}});
    }
    const auto stats = stitch::compute_stats(seq);

    fmt::print("Forme       : {}\n", shape);
    fmt::print("Longueur    : {:g} mm  |  répétitions : {}\n", lengthMm, repeats);
    fmt::print("Points      : {}\n", stats.stitches);
    fmt::print("Longueur fil : {:.1f} mm\n", stats.thread_length_um / 1000.0);
    if (result.stats.stitches >= 2) {
        fmt::print("Segment min/max : {:.2f} / {:.2f} mm\n", result.stats.min_segment_um / 1000.0,
                   result.stats.max_segment_um / 1000.0);
    }
    for (const auto& w : result.warnings) {
        fmt::print("  ! {}\n", w.message);
    }

    if (!outSvg.empty()) {
        const auto written = formats::write_svg_file(std::filesystem::path(outSvg), seq);
        if (!written) {
            return cli_error("stitchdebug", written.error().message);
        }
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return kExitOk;
}

// Moteur de points (HP-ENG-001/002/010, HP-STI-004) : scène de référence de trois tatami
// empilés plus un satin de bordure circulaire ; affiche les mesures et compare l'entrée/sortie
// automatique au sens naturel (longueur totale des sauts).
int run_engine_debug(double pullMm, bool underlayAuto, double borderMm, const std::string& outSvg) {
    using namespace openstitch;
    if (const auto refused = check_output_path("engine-debug", outSvg, false)) {
        return *refused;
    }
    const auto corner = [](std::int32_t x, std::int32_t y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    const auto build = [&](bool autoJoin) {
        document::Project project;
        project.finishing.auto_join = autoJoin;
        for (int i = 0; i < 3; ++i) {
            const std::int32_t y0 = -i * 12'000;
            document::VectorObject vec;
            vec.id = project.object_ids.next();
            geometry::Path sq;
            sq.closed = true;
            sq.nodes = {corner(0, y0), corner(10'000, y0), corner(10'000, y0 + 10'000),
                        corner(0, y0 + 10'000)};
            vec.paths.push_back(geometry::PathSet{sq, {}});
            project.vector_objects.push_back(vec);
            document::EmbroideryObject emb;
            emb.id = project.object_ids.next();
            emb.source_vector = vec.id;
            document::TatamiParams tp;
            tp.inset = Micrometers{0};
            tp.pull_compensation = to_micrometers(Millimeters{pullMm});
            tp.underlay_mode =
                underlayAuto ? document::UnderlayMode::Auto : document::UnderlayMode::Manual;
            emb.params = tp;
            project.embroidery_objects.push_back(emb);
        }
        if (borderMm > 0.0) {
            geometry::Path ring;
            ring.closed = true;
            for (int k = 0; k < 72; ++k) {
                const double a = 2.0 * std::numbers::pi * k / 72.0;
                ring.nodes.push_back(
                    corner(30'000 + static_cast<std::int32_t>(std::lround(8'000.0 * std::cos(a))),
                           static_cast<std::int32_t>(std::lround(8'000.0 * std::sin(a)))));
            }
            document::BorderSatinSpec spec;
            spec.width = to_micrometers(Millimeters{borderMm});
            if (auto sp = stitch_generation::border_satin_from_path(ring, spec)) {
                document::EmbroideryObject emb;
                emb.id = project.object_ids.next();
                emb.rgb = {200, 0, 0};
                emb.params = std::move(*sp);
                project.embroidery_objects.push_back(emb);
            }
        }
        return project;
    };
    // Projet synthétique construit ici même. raw-sequence-ok: générateur de debug.
    const auto natural = stitch_generation::generate_sequence(build(false)); // raw-sequence-ok
    const auto joined = stitch_generation::generate_sequence(build(true));   // raw-sequence-ok
    if (!natural || !joined) {
        return cli_error("engine-debug", "génération impossible");
    }
    const auto count = [](const stitch::StitchSequence& s, stitch::StitchPass pass) {
        return std::count_if(s.commands.begin(), s.commands.end(), [pass](const auto& c) {
            return c.type == stitch::CommandType::Stitch && c.pass == pass;
        });
    };
    fmt::print("Compensation du tirage : {:g} mm | sous-couche : {} | bordure : {:g} mm\n", pullMm,
               underlayAuto ? "automatique" : "manuelle", borderMm);
    fmt::print("Points couche supérieure : {}\n", count(*natural, stitch::StitchPass::TopStitch));
    fmt::print("Points de sous-couche    : {}\n", count(*natural, stitch::StitchPass::Underlay));
    fmt::print("Sauts (sens naturel)     : {:.1f} mm\n",
               stitch_generation::total_jump_length_um(*natural) / 1000.0);
    fmt::print("Sauts (entrée/sortie auto) : {:.1f} mm\n",
               stitch_generation::total_jump_length_um(*joined) / 1000.0);
    if (!outSvg.empty()) {
        const auto written = formats::write_svg_file(std::filesystem::path(outSvg), *joined);
        if (!written) {
            return cli_error("engine-debug", written.error().message);
        }
        fmt::print("SVG écrit : {}\n", outSvg);
    }
    return kExitOk;
}

struct DigitizeArgs {
    std::string image;
    std::string dst;
    std::string outSvg;
    std::string lock = "backforth";
    std::string mode = "shapes";
    std::string technique = "auto";
    double dpi = 96.0;
    double trimMm = 3.0;
    double detail = 0.5;
    int maxColors = 8;
    int minRegionPx = 16;
    int smoothingPx = 3;
    int skipBg = -1; // -1 = auto (segmentation::background_candidate), 0 = non, 1 = oui
    bool json = false;
    bool noClobber = false;
};

// Pipeline complet image -> DST (segmentation -> numerisation automatique ->
// generation des points -> export), en ligne de commande, pour comparer le
// resultat d'OpenStitch a une reference externe sans passer par l'IHM. Memes
// valeurs par defaut que le dialogue "Numerisation automatique" du desktop
// (main_window.cpp : max_colors=8, min_region_px=16, smoothing_radius_px=3,
// skip_largest_region coche par defaut seulement si la plus grande region est
// un fond quasi blanc qui encadre le motif -- segmentation::background_candidate,
// Lot A ; une valeur explicite de --skip-background reste prioritaire).
//
// Avec --json, stdout ne contient que le JSON final ; tous les messages d'état vont
// sur stderr.
int run_digitize(const DigitizeArgs& a) {
    using namespace openstitch;
    std::FILE* const st = a.json ? stderr : stdout; // flux des messages d'état

    if (const auto refused = check_output_path("digitize", a.dst, a.noClobber)) {
        return *refused;
    }
    if (const auto refused = check_output_path("digitize", a.outSvg, a.noClobber)) {
        return *refused;
    }

    const auto loaded = image::load_image(std::filesystem::path(a.image));
    if (!loaded) {
        return cli_error(
            "digitize",
            fmt::format("chargement de l'image impossible : {}", loaded.error().message),
            kImageFormatsHint);
    }
    fmt::print(st, "Image : {} x {} px (alpha source : {})\n", loaded->width, loaded->height,
               loaded->source_had_alpha ? "oui" : "non");

    document::Project project;
    project.mm_per_px = Millimeters{25.4 / a.dpi};
    project.original = *loaded;
    // Finitions (Lots E/F) : nouveau projet -> activées ; réglables ici.
    project.finishing.trim_threshold =
        Micrometers{static_cast<std::int32_t>(std::lround(a.trimMm * 1000.0))};
    project.finishing.lock_type = a.lock == "none"       ? document::LockStitch::None
                                  : a.lock == "triangle" ? document::LockStitch::Triangle
                                  : a.lock == "zigzag"   ? document::LockStitch::MicroZigzag
                                                         : document::LockStitch::BackAndForth;

    auto seg = segmentation::segment(project.original, {.max_colors = a.maxColors,
                                                        .min_region_px = a.minRegionPx,
                                                        .smoothing_radius_px = a.smoothingPx});
    if (!seg) {
        return cli_error("digitize",
                         fmt::format("segmentation impossible : {}", seg.error().message),
                         "essayez une image plus grande ou --max-colors différent");
    }
    const std::size_t regionCount = seg->region_count();
    fmt::print(st, "Régions segmentées : {}\n", regionCount);
    project.segmentation = std::move(*seg);

    // Même règle que le dialogue desktop (Lot A) : jamais sur le seul
    // critère « pas d'alpha ».
    const auto candidate = segmentation::background_candidate(*project.segmentation);
    if (candidate) {
        fmt::print(st,
                   "Région candidate au fond : #{:02X}{:02X}{:02X}, {:.1f} % de l'image, L* "
                   "{:.0f}, {} bord(s) touché(s)\n",
                   candidate->rgb[0], candidate->rgb[1], candidate->rgb[2],
                   candidate->area_ratio * 100.0, candidate->lightness, candidate->sides_touched);
    }
    const bool skipLargest = a.skipBg < 0 ? (candidate && candidate->recommended) : (a.skipBg != 0);
    autodigitize::AutoOptions opts;
    opts.mm_per_px = project.mm_per_px;
    opts.skip_largest_region = skipLargest;
    fmt::print(st, "Ignorer la plus grande région (fond) : {}{}\n", skipLargest ? "oui" : "non",
               a.skipBg < 0 ? " (automatique)" : " (option explicite)");

    // Strategie « contours / dessin au trait » : lignes medianes cousues en
    // point droit (satin legacy degrade en point droit). « satin » est l'ancien
    // nom de « legacy-satin » (accepté pour compatibilité).
    const bool contours = a.mode == "contours";
    autodigitize::ContourMetrics cm;
    autodigitize::ContourOptions co;
    co.mm_per_px = project.mm_per_px;
    co.skip_largest_region = skipLargest;
    co.detail = a.detail;
    co.technique = a.technique == "running" ? autodigitize::ContourTechnique::Running
                   : (a.technique == "legacy-satin" || a.technique == "satin")
                       ? autodigitize::ContourTechnique::Satin
                       : autodigitize::ContourTechnique::Automatic;
    if (contours) {
        fmt::print(st, "Mode : contours (detail {:.2f}, technique {})\n", a.detail, a.technique);
    }
    auto result =
        contours ? autodigitize::auto_digitize_contours(*project.segmentation, project.object_ids,
                                                        co, &cm)
                 : autodigitize::auto_digitize(*project.segmentation, project.object_ids, opts);
    if (!result) {
        return cli_error("digitize",
                         fmt::format("numérisation impossible : {}", result.error().message),
                         "image trop uniforme, ou seul le fond a été détecté : essayez "
                         "--skip-background no, --max-colors plus grand ou --min-region-px plus "
                         "petit");
    }
    if (contours) {
        fmt::print(st, "Contours : composantes={} segments={} jonctions={} extremites={}\n",
                   cm.components, cm.segments, cm.junctions, cm.endpoints);
        fmt::print(st, "  elagage : branches courtes={} elements petits={}\n",
                   cm.removed_short_branches, cm.removed_small_elements);
        fmt::print(st,
                   "  longueur point droit {:.1f} mm | largeur min/moy/max "
                   "{:.2f}/{:.2f}/{:.2f} mm\n",
                   cm.running_length_mm, cm.min_width_mm, cm.mean_width_mm, cm.max_width_mm);
        fmt::print(st, "  replis legacy->point droit={} rejets={}\n", cm.fallbacks, cm.rejected);
    }
    for (const auto& w : result->warnings) {
        fmt::print(stderr, "  ! {}\n", w);
    }
    for (auto& v : result->vectors) {
        project.vector_objects.push_back(std::move(v));
    }
    for (auto& e : result->embroideries) {
        project.embroidery_objects.push_back(std::move(e));
    }
    if (project.embroidery_objects.empty()) {
        return cli_error(
            "digitize",
            fmt::format(
                "aucun objet de broderie généré ({} région(s) segmentée(s)), aucun DST écrit",
                regionCount),
            "image trop uniforme ou régions trop petites : essayez --min-region-px plus petit, "
            "--max-colors plus grand, ou --skip-background no si le fond a été ignoré");
    }

    int nSatin = 0, nTatami = 0, nRunning = 0;
    for (const auto& e : project.embroidery_objects) {
        if (e.is_satin() || e.is_auto_satin())
            ++nSatin;
        else if (e.is_tatami())
            ++nTatami;
        else if (std::holds_alternative<document::RunningStitchParams>(e.params))
            ++nRunning;
    }
    fmt::print(st, "Objets brodés : {} (satin={} tatami={} running={})\n",
               project.embroidery_objects.size(), nSatin, nTatami, nRunning);

    const auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return cli_error("digitize", fmt::format("génération des points impossible : {}",
                                                 sequence.error().message));
    }
    const auto stats = stitch::compute_stats(*sequence);
    if (stats.stitches == 0) {
        return cli_error("digitize", "aucun point généré, aucun DST écrit",
                         "les objets obtenus sont vides : modifiez --min-region-px ou "
                         "--max-colors");
    }
    const double wMm = (stats.bounds.max.x.value - stats.bounds.min.x.value) / 1000.0;
    const double hMm = (stats.bounds.max.y.value - stats.bounds.min.y.value) / 1000.0;
    fmt::print(st, "Points : {}  |  sauts : {}  |  coupes : {}  |  changements de fil : {}\n",
               stats.stitches, stats.jumps, stats.trims, stats.color_changes);
    fmt::print(st, "Dimensions : {:.1f} x {:.1f} mm  |  fil : {:.2f} m\n", wMm, hMm,
               stats.thread_length_um / 1e6);

    // Mesures de qualité (Lot G).
    stitch_analysis::SequenceMetrics metrics;
    print_sequence_metrics(st, *sequence, false, &metrics);
    stitch_analysis::ProjectMetricsOptions pm;
    if (skipLargest && candidate) {
        pm.excluded_rgb = candidate->rgb;
    }
    const auto quality = stitch_analysis::project_metrics(project, *sequence, pm);
    fmt::print(st, "Objets brodés < {:.0f} mm² : {}\n", pm.small_object_mm2, quality.small_objects);
    if (quality.uncovered_ratio) {
        fmt::print(st, "Surface non couverte (hors fond ignoré) : {:.2f} %\n",
                   *quality.uncovered_ratio * 100.0);
    }
    fmt::print(st, "Angles de remplissage ({} distincts) :", quality.fill_angles_deg.size());
    for (const auto& [deg, n] : quality.fill_angles_deg) {
        fmt::print(st, " {}°x{}", deg, n);
    }
    fmt::print(st, "\n");
    fmt::print(st, "Déplacements par source :");
    for (const auto& [kind, n] : quality.moves_by_kind) {
        fmt::print(st, " {}={}", kind, n);
    }
    fmt::print(st, "\nPoints courts par source :");
    for (const auto& [kind, n] : quality.short_stitches_by_kind) {
        fmt::print(st, " {}={}", kind, n);
    }
    fmt::print(st, "\n");

    const auto written = formats::write_dst_file(std::filesystem::path(a.dst), *sequence);
    if (!written) {
        return cli_error("digitize",
                         fmt::format("écriture du DST impossible : {}", written.error().message),
                         "vérifiez les droits d'écriture et que le fichier n'est pas ouvert "
                         "ailleurs");
    }
    fmt::print(st, "DST écrit : {}\n", a.dst);

    if (!a.outSvg.empty()) {
        const auto svgWritten = formats::write_svg_file(std::filesystem::path(a.outSvg), *sequence);
        if (!svgWritten) {
            return cli_error("digitize", fmt::format("écriture du SVG impossible : {}",
                                                     svgWritten.error().message));
        }
        fmt::print(st, "SVG écrit : {}\n", a.outSvg);
    }

    if (a.json) {
        std::string svgField = a.outSvg.empty() ? "null" : json_str(a.outSvg);
        fmt::print(
            "{{\"image\":{},\"dst\":{},\"svg\":{},\"image_width_px\":{},\"image_height_px\":{},"
            "\"regions\":{},\"background_skipped\":{},\"objects\":{{\"total\":{},\"satin\":{},"
            "\"tatami\":{},\"running\":{}}},\"stitches\":{},\"jumps\":{},\"trims\":{},"
            "\"color_changes\":{},\"width_mm\":{:.2f},\"height_mm\":{:.2f},\"thread_m\":{:.3f},"
            "\"moves\":{},\"long_moves_without_trim\":{},\"short_stitches\":{},"
            "\"small_objects\":{},\"uncovered_ratio\":{}}}\n",
            json_str(a.image), json_str(a.dst), svgField, loaded->width, loaded->height,
            regionCount, skipLargest ? "true" : "false", project.embroidery_objects.size(), nSatin,
            nTatami, nRunning, stats.stitches, stats.jumps, stats.trims, stats.color_changes, wMm,
            hMm, stats.thread_length_um / 1e6, metrics.moves, metrics.long_moves_without_trim,
            metrics.short_stitches, quality.small_objects,
            quality.uncovered_ratio ? fmt::format("{:.4f}", *quality.uncovered_ratio) : "null");
    }
    return kExitOk;
}

// Exporte un projet .osp en DST par le MÊME chemin que le bureau (export_machine_file) :
// permet d'inspecter les coupes et la fin du fichier sans passer par l'interface.
int run_osp2dst(const std::string& ospPath, const std::string& outDst, bool noClobber,
                const openstitch::formats::MachineExportOptions& machineOptions) {
    using namespace openstitch;
    if (const auto refused = check_output_path("osp2dst", outDst, noClobber)) {
        return *refused;
    }
    const auto project = project_io::load_project(std::filesystem::path(ospPath));
    if (!project) {
        return cli_error("osp2dst", project.error().message, kOspHint);
    }
    // Format machine déduit de l'extension de sortie (dst par défaut, pes, jef, exp).
    std::string formatId = "dst";
    {
        std::string ext = std::filesystem::path(outDst).extension().string();
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(ext.begin());
        }
        if (const auto* info = formats::find_format_for_extension(ext);
            info != nullptr && info->can_write) {
            formatId = info->id;
        }
    }
    const auto written = project_io::export_machine_file(
        *project, formatId, std::filesystem::path(outDst), machineOptions);
    if (!written) {
        return cli_error("osp2dst", written.error().message);
    }
    fmt::print("{} écrit : {}\n", formats::find_format(formatId)->display_name, outDst);
    return kExitOk;
}

// Séquence effective d'un projet .osp (la même que l'aperçu, l'export et l'analyse) en SVG de
// diagnostic ; `--outlines` superpose le contour des vecteurs sources (en gris) pour voir
// d'un coup d'œil les points qui débordent de leur forme. `--only` limite à un objet brodé.
int run_osp2svg(const std::string& ospPath, const std::string& outSvg, bool outlines,
                std::uint64_t onlyObject, bool noClobber) {
    using namespace openstitch;
    if (const auto refused = check_output_path("osp2svg", outSvg, noClobber)) {
        return *refused;
    }
    auto project = project_io::load_project(std::filesystem::path(ospPath));
    if (!project) {
        return cli_error("osp2svg", project.error().message, kOspHint);
    }
    if (onlyObject != 0) {
        const bool exists =
            std::any_of(project->embroidery_objects.begin(), project->embroidery_objects.end(),
                        [&](const auto& e) { return e.id.value == onlyObject; });
        if (!exists) {
            std::string ids;
            for (const auto& e : project->embroidery_objects) {
                ids += (ids.empty() ? "" : ", ") + std::to_string(e.id.value);
            }
            return cli_error("osp2svg",
                             fmt::format("objet brodé {} introuvable dans {}", onlyObject, ospPath),
                             ids.empty() ? "le projet ne contient aucun objet brodé"
                                         : "ids disponibles : " + ids);
        }
        std::erase_if(project->embroidery_objects,
                      [&](const auto& e) { return e.id.value != onlyObject; });
    }
    const auto seq = stitch_generation::effective_sequence(*project);
    if (!seq) {
        return cli_error("osp2svg", seq.error().message);
    }
    const auto written = formats::write_svg_file(std::filesystem::path(outSvg), *seq);
    if (!written) {
        return cli_error("osp2svg", written.error().message);
    }
    if (outlines) {
        std::ifstream in(outSvg, std::ios::binary);
        std::string svg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        std::string extra;
        for (const auto& v : project->vector_objects) {
            bool used = onlyObject == 0;
            for (const auto& e : project->embroidery_objects) {
                used = used || e.source_vector == v.id;
            }
            if (!used) {
                continue;
            }
            for (const auto& set : v.paths) {
                std::vector<const geometry::Path*> rings{&set.outer};
                for (const auto& h : set.holes) {
                    rings.push_back(&h);
                }
                for (const auto* ring : rings) {
                    std::string d;
                    for (std::size_t i = 0; i < ring->nodes.size(); ++i) {
                        d += fmt::format("{}{:.3f},{:.3f}", i == 0 ? "M" : "L",
                                         ring->nodes[i].pos.x.value / 1000.0,
                                         -ring->nodes[i].pos.y.value / 1000.0);
                    }
                    extra += "<path d=\"" + d +
                             "Z\" fill=\"none\" stroke=\"#888\" stroke-width=\"0.08\"/>\n";
                }
            }
        }
        const auto close = svg.rfind("</svg>");
        if (close != std::string::npos) {
            svg.insert(close, extra);
        }
        std::ofstream out(outSvg, std::ios::binary | std::ios::trunc);
        out << svg;
    }
    fmt::print("Points : {}  |  SVG : {}\n", seq->commands.size(), outSvg);
    return kExitOk;
}

// Noms du corpus de auto_satin::make_shape (shapes.cpp). make_shape reste la source de
// vérité : un nom absent de cette liste mais accepté par make_shape fonctionne quand même ;
// la liste ne sert qu'à l'aide et au message d'erreur.
const std::vector<std::string> kSatinShapeNames = {"rectangle",
                                                   "capsule",
                                                   "ribbon",
                                                   "s",
                                                   "y",
                                                   "y_symmetric",
                                                   "t",
                                                   "cross",
                                                   "h",
                                                   "circle",
                                                   "disc_15mm",
                                                   "disc_tight_inner_ring",
                                                   "petal",
                                                   "ring",
                                                   "wide",
                                                   "tiny",
                                                   "notch",
                                                   "pinch",
                                                   "trident",
                                                   "star5",
                                                   "asymmetric_star",
                                                   "comb",
                                                   "E",
                                                   "e_trunk_isolated",
                                                   "deep_recursive",
                                                   "multi_neck",
                                                   "dumbbell",
                                                   "deep_channel",
                                                   "two_holes",
                                                   "ring_branch",
                                                   "junction_with_hole",
                                                   "polygonal_cut_fixture",
                                                   "thick_diagonal_blob"};

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        out += (out.empty() ? "" : ", ") + n;
    }
    return out;
}

// Auto-satin par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md) sur une forme de référence : résume
// colonnes, longueurs de traversées et diagnostics, et écrit un SVG (contour, axes,
// traversées, zigzag) pour inspecter orientations et zones non couvertes.
int run_satin_auto_debug(const std::string& shape, double spacingMm,
                         const std::vector<std::string>& guides, const std::string& outSvg,
                         const std::string& ospPath, std::uint64_t vectorId, bool pristine) {
    using namespace openstitch;
    if (const auto refused = check_output_path("satin-auto-debug", outSvg, false)) {
        return *refused;
    }
    std::optional<geometry::PathSet> region;
    std::string sourceLabel = fmt::format("forme {}", shape);
    if (!ospPath.empty()) {
        const auto project = project_io::load_project(std::filesystem::path(ospPath));
        if (!project) {
            return cli_error("satin-auto-debug", project.error().message, kOspHint);
        }
        std::string available;
        for (const auto& v : project->vector_objects) {
            if (!v.paths.empty()) {
                available += (available.empty() ? "" : ", ") + std::to_string(v.id.value);
            }
        }
        if (vectorId == 0) {
            return cli_error("satin-auto-debug", "--osp demande aussi --vector <id>",
                             available.empty() ? "le projet ne contient aucun vecteur"
                                               : "ids de vecteurs disponibles : " + available);
        }
        sourceLabel = fmt::format("vecteur {} de {}", vectorId,
                                  std::filesystem::path(ospPath).filename().string());
        for (const auto& v : project->vector_objects) {
            if (v.id.value == vectorId && !v.paths.empty()) {
                region = v.paths.front();
                if (pristine && v.source_region && project->segmentation) {
                    // Contour brut de la région de segmentation (sans le recouvrement tatami).
                    vectorization::VectorizeOptions vo;
                    vo.mm_per_px = project->mm_per_px;
                    const auto raw = vectorization::vectorize_region(*project->segmentation,
                                                                     *v.source_region, vo);
                    if (raw && !raw->empty()) {
                        double cur = 0.0, base = 0.0;
                        for (const auto& st : v.paths) {
                            cur += geometry::path_set_area_um2(st);
                        }
                        for (const auto& st : *raw) {
                            base += geometry::path_set_area_um2(st);
                        }
                        fmt::print(
                            "Contour brut : aire {:.1f} mm2 (contour du projet : {:.1f} mm2, "
                            "{:+.1f} %)\n",
                            base / 1e6, cur / 1e6, 100.0 * (cur - base) / base);
                        region = raw->front();
                    }
                }
            }
        }
        if (!region) {
            return cli_error("satin-auto-debug",
                             fmt::format("vecteur {} introuvable dans {}", vectorId, ospPath),
                             available.empty() ? "le projet ne contient aucun vecteur"
                                               : "ids de vecteurs disponibles : " + available);
        }
    } else {
        region = auto_satin::make_shape(shape);
    }
    if (!region) {
        return cli_error("satin-auto-debug", fmt::format("forme inconnue « {} »", shape),
                         "formes valides : " + join_names(kSatinShapeNames) +
                             " (voir --list-shapes)");
    }
    auto_satin::SkeletonSatinParameters params;
    params.spacing = to_micrometers(Millimeters{spacingMm});
    params.measure_coverage = true;
    for (const auto& g : guides) {
        double xMm = 0.0, yMm = 0.0, deg = 0.0;
        int absolute = 0;
        // Portable (sscanf_s n'existe pas sous GCC) : champs séparés par des virgules.
        int n = 0;
        {
            std::istringstream in(g);
            std::string field;
            while (n < 4 && std::getline(in, field, ',')) {
                try {
                    std::size_t used = 0;
                    if (n == 0) {
                        xMm = std::stod(field, &used);
                    } else if (n == 1) {
                        yMm = std::stod(field, &used);
                    } else if (n == 2) {
                        deg = std::stod(field, &used);
                    } else {
                        absolute = std::stoi(field, &used);
                    }
                    if (used != field.size()) {
                        break;
                    }
                } catch (const std::exception&) {
                    break;
                }
                ++n;
            }
        }
        if (n < 3) {
            return cli_error("satin-auto-debug", fmt::format("guide invalide « {} »", g),
                             "format attendu : x_mm,y_mm,angle_deg[,1=absolu]");
        }
        params.guides.push_back(
            {Vec2um{to_micrometers(Millimeters{xMm}), to_micrometers(Millimeters{yMm})},
             deg * std::numbers::pi / 180.0, absolute != 0});
    }
    const auto result = auto_satin::generate_skeleton_satin(*region, params);
    if (!result) {
        return cli_error("satin-auto-debug", result.error().message);
    }
    std::size_t total = 0;
    fmt::print("Source : {}  |  colonnes : {}\n", sourceLabel, result->columns.size());
    for (std::size_t c = 0; c < result->columns.size(); ++c) {
        const auto& cr = result->columns[c].crossings;
        double lo = 1e18, hi = 0.0, sum = 0.0;
        for (const auto& x : cr) {
            const double len = length_um(x.b - x.a) / 1000.0;
            lo = (std::min)(lo, len);
            hi = (std::max)(hi, len);
            sum += len;
        }
        total += cr.size();
        fmt::print(
            "  colonne {} : {} traversées, longueur {:.2f} / {:.2f} / {:.2f} mm (min/moy/max)\n",
            c + 1, cr.size(), cr.empty() ? 0.0 : lo,
            cr.empty() ? 0.0 : sum / static_cast<double>(cr.size()), hi);
    }
    const auto& d = result->diagnostics;
    fmt::print("Traversées : {}  |  morceaux : {}\n", total, d.pieces);
    fmt::print("Eventail : {} cordes  |  traversees raccourcies : {}\n", d.fan_chords,
               d.trimmed_crossings);
    fmt::print("Diagnostics : hors région {}  |  trop courtes {}  |  angle ramené {}  |  garde de "
               "rayon {}  |  guides orphelins {}\n",
               d.outside_samples, d.too_short, d.clamped_angle, d.radius_guard_hits,
               d.orphan_guides);
    if (d.coverage_measured) {
        fmt::print("Couverture estimée : {:.1f} %  |  fil en double : x{:.2f}  |  non couvert : "
                   "{:.1f} mm²\n",
                   d.coverage_ratio * 100.0, d.overlap_ratio, d.uncovered_area_mm2);
    }
    for (const auto& m : d.messages) {
        fmt::print("  ! {}\n", m);
    }
    if (!outSvg.empty()) {
        std::ofstream out(outSvg, std::ios::binary);
        if (!out) {
            return cli_error("satin-auto-debug", fmt::format("impossible d'écrire {}", outSvg),
                             "vérifiez que le dossier existe et que le fichier n'est pas ouvert");
        }
        out << auto_satin::skeleton_satin_to_svg(*region, *result);
        fmt::print("SVG : {}\n", outSvg);
    }
    return kExitOk;
}

struct ProductionArgs {
    std::string file;
    std::string output;
    std::string name;
    std::string date;
    std::string notes;
    double speed{700.0};
    bool json{false};
    bool noClobber{false};
};

// Fiche de production (HP-PROD-001) d'un .osp (séquence effective) ou d'un .dst. Sans --output :
// texte lisible, ou JSON avec --json. Avec --output : page HTML autonome (le PDF/impression sont
// produits par le bureau, qui seul dépend de Qt).
int run_production(const ProductionArgs& a) {
    using namespace openstitch;
    if (const auto refused = check_output_path("production", a.output, a.noClobber)) {
        return *refused;
    }
    const std::filesystem::path path(a.file);
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    document::Project project;
    stitch::StitchSequence sequence;
    stitch_analysis::ProductionOptions opts;
    if (ext == ".dst") {
        auto seq = formats::read_dst_file(path);
        if (!seq) {
            return cli_error("production", seq.error().message, kDstHint);
        }
        sequence = std::move(*seq);
        opts.use_project_canvas = false; // un DST ne porte pas de cadre
    } else if (ext == ".osp") {
        auto loaded = project_io::load_project(path);
        if (!loaded) {
            return cli_error("production", loaded.error().message, kOspHint);
        }
        project = std::move(*loaded);
        auto seq = stitch_generation::effective_sequence(project);
        if (!seq) {
            return cli_error("production", seq.error().message);
        }
        sequence = std::move(*seq);
    } else {
        return cli_error("production", fmt::format("extension « {} » non reconnue", ext),
                         "attendu : un projet .osp ou un fichier .dst");
    }
    opts.project_name = a.name.empty() ? path.stem().string() : a.name;
    if (a.date.empty()) {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        opts.date = fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    } else {
        opts.date = a.date;
    }
    opts.notes = a.notes;
    opts.stitches_per_minute = a.speed;
    const auto sheet = stitch_analysis::make_production_sheet(project, sequence, opts);

    if (!a.output.empty()) {
        std::ofstream out(a.output, std::ios::binary);
        out << stitch_analysis::production_to_html(sheet);
        if (!out) {
            return cli_error("production", fmt::format("écriture impossible : {}", a.output));
        }
        fmt::print(a.json ? stderr : stdout, "Fiche de production (HTML) écrite : {}\n", a.output);
        if (!a.json) {
            return kExitOk;
        }
    }
    if (a.json) {
        fmt::print("{}", stitch_analysis::production_to_json(sheet));
        return kExitOk;
    }
    if (!a.output.empty()) {
        return kExitOk;
    }
    fmt::print("Fiche de production : {} ({})\n", sheet.project_name, sheet.date);
    fmt::print("Dimensions         : {:.1f} x {:.1f} mm\n", sheet.width_mm, sheet.height_mm);
    if (sheet.frame_mm) {
        fmt::print("Cadre              : {:.1f} x {:.1f} mm{}\n", sheet.frame_mm->first,
                   sheet.frame_mm->second, sheet.fits_frame ? "" : " (le motif dépasse)");
    }
    fmt::print("Points             : {}\n", sheet.stitches);
    fmt::print("Sauts              : {}\n", sheet.jumps);
    fmt::print("Coupes             : {}\n", sheet.trims);
    fmt::print("Changements de fil : {}\n", sheet.color_changes);
    fmt::print("Fil estimé         : {:.2f} m\n", sheet.thread_length_m);
    fmt::print("Temps estimé       : {} ({:g} points/min)\n",
               stitch_analysis::format_duration_fr(sheet.estimated_minutes),
               sheet.stitches_per_minute);
    fmt::print("Blocs de couleur   :\n");
    for (const auto& b : sheet.blocks) {
        fmt::print("  {:>2}. #{:02x}{:02x}{:02x}  {:>8} points  {:>5} sauts  {}\n", b.number,
                   b.rgb[0], b.rgb[1], b.rgb[2], b.stitches, b.jumps, b.thread_label);
    }
    fmt::print("Avertissements     : {}\n", sheet.findings.size());
    for (const auto& f : sheet.findings) {
        fmt::print("  - {}\n", f.message);
    }
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    openstitch::init_logging();

    CLI::App app{fmt::format("{} — outils en ligne de commande", openstitch::kAppName)};
    app.set_version_flag("--version", openstitch::kAppVersion);
    app.require_subcommand(1);
    app.footer("Exemple de pipeline :\n"
               "  openstitch-cli info logo.png                 # dpi et taille estimée\n"
               "  openstitch-cli digitize logo.png logo.dst --json > rapport.json\n"
               "  openstitch-cli stats logo.dst                # relire le DST produit\n"
               "  openstitch-cli dst2svg logo.dst logo.svg     # aperçu vectoriel\n"
               "\n"
               "Codes de sortie :\n"
               "  0  succès\n"
               "  1  entrée ou fichier invalide, rien à produire (message « openstitch-cli\n"
               "     <sous-commande> : ... » avec une piste de correction)\n"
               "  2  contrôle qualité non satisfait (stitchdebug --shape ring)\n"
               "  autres : erreur d'usage de la ligne de commande (CLI11)\n"
               "Les sous-commandes [diagnostic] inspectent le moteur : leur sortie n'est pas un\n"
               "contrat stable.\n"
               "Documentation : docs/source/cli.md");

    constexpr const char* kMain = "Commandes";
    constexpr const char* kDiag = "Diagnostic";

    bool json_out = false;
    app.add_flag("--json", json_out,
                 "Sortie JSON unique sur stdout pour info, stats et digitize (messages d'état "
                 "sur stderr)");

    std::string image_path;
    std::optional<double> dpi;
    auto* info_cmd = app.add_subcommand("info", "Affiche les métadonnées d'une image");
    info_cmd->group(kMain);
    info_cmd->add_option("image", image_path, "Chemin de l'image (PNG, JPEG, BMP, TIFF)")
        ->required();
    info_cmd
        ->add_option("--dpi", dpi,
                     "Résolution pour l'estimation en mm (défaut : celle du fichier si elle "
                     "est renseignée, sinon 96)")
        ->check(CLI::PositiveNumber);
    info_cmd->add_flag("--json", json_out, "Sortie JSON sur stdout");

    std::string dst_path;
    auto* stats_cmd =
        app.add_subcommand("stats", "Statistiques d'un fichier de broderie (DST, PES, JEF, EXP)");
    stats_cmd->group(kMain);
    stats_cmd->add_option("fichier", dst_path, "Chemin du fichier .dst/.pes/.jef/.exp")->required();
    stats_cmd->add_flag("--json", json_out, "Sortie JSON sur stdout");

    ProductionArgs pr;
    auto* pr_cmd = app.add_subcommand(
        "production", "Fiche de production d'un projet .osp ou d'un DST (texte, --json ou HTML)");
    pr_cmd->group(kMain);
    pr_cmd->add_option("fichier", pr.file, "Projet .osp ou fichier .dst")->required();
    pr_cmd->add_option("--output,-o", pr.output,
                       "Écrit la fiche en page HTML autonome (le PDF s'exporte depuis le bureau)");
    pr_cmd->add_option("--name", pr.name, "Nom du projet (défaut : nom du fichier)");
    pr_cmd->add_option("--date", pr.date, "Date AAAA-MM-JJ (défaut : aujourd'hui)");
    pr_cmd->add_option("--notes", pr.notes, "Notes libres");
    pr_cmd->add_option("--speed", pr.speed, "Vitesse supposée en points/min (défaut : 700)")
        ->check(CLI::Range(100.0, 2000.0));
    pr_cmd->add_flag("--json", json_out, "JSON stable sur stdout");
    pr_cmd->add_flag("--no-clobber", pr.noClobber, "Refuse d'écraser un fichier existant");

    std::string svg_in;
    std::string svg_out;
    bool svg_noclobber = false;
    bool svg_force = false;
    auto* svg_cmd = app.add_subcommand(
        "dst2svg", "Convertit un fichier de broderie (DST, PES, JEF, EXP) en SVG d'aperçu");
    svg_cmd->group(kMain);
    svg_cmd->add_option("entree", svg_in, "Fichier .dst/.pes/.jef/.exp source")->required();
    svg_cmd->add_option("sortie,--output-svg,--output", svg_out, "Fichier .svg à produire")
        ->required();
    auto* svg_nc =
        svg_cmd->add_flag("--no-clobber", svg_noclobber, "Refuse d'écraser un fichier existant");
    svg_cmd->add_flag("--force", svg_force, "Écrase la sortie existante (comportement par défaut)")
        ->excludes(svg_nc);

    DigitizeArgs dz;
    std::string dz_skip_bg = "auto";
    bool dz_force = false;
    auto* dz_cmd = app.add_subcommand(
        "digitize", "Pipeline complet image -> DST (segmentation, numérisation automatique, "
                    "génération des points), sans IHM");
    dz_cmd->group(kMain);
    dz_cmd->add_option("image", dz.image, "Image source (PNG, JPEG, BMP, TIFF)")->required();
    dz_cmd->add_option("sortie", dz.dst, "Fichier .dst à produire")->required();
    dz_cmd->add_option("--dpi", dz.dpi, "Résolution supposée pour l'échelle mm/px (défaut : 96)")
        ->check(CLI::PositiveNumber);
    dz_cmd->add_option("--max-colors", dz.maxColors, "Nombre maximal de couleurs (défaut : 8)")
        ->check(CLI::Range(2, 64));
    dz_cmd
        ->add_option("--min-region-px", dz.minRegionPx,
                     "Taille minimale de région en px (défaut : 16)")
        ->check(CLI::PositiveNumber);
    dz_cmd->add_option("--smoothing-px", dz.smoothingPx, "Lissage des formes en px (défaut : 3)")
        ->check(CLI::NonNegativeNumber);
    dz_cmd
        ->add_option("--skip-background", dz_skip_bg,
                     "Ignorer la plus grande région (le fond) : auto (défaut : seulement si c'est "
                     "un fond quasi blanc touchant au moins 3 bords) | yes | no")
        ->check(CLI::IsMember({"auto", "yes", "no", "-1", "0", "1"}));
    dz_cmd->add_option("--output-svg", dz.outSvg, "SVG de diagnostic à produire en plus du DST");
    dz_cmd
        ->add_option("--trim-threshold", dz.trimMm,
                     "Coupe automatique au-delà de ce déplacement, en mm (défaut : 3)")
        ->check(CLI::PositiveNumber);
    dz_cmd
        ->add_option("--mode", dz.mode,
                     "Stratégie : shapes (formes pleines, défaut) | contours (dessin au trait)")
        ->check(CLI::IsMember({"shapes", "contours"}));
    dz_cmd
        ->add_option("--detail", dz.detail, "Mode contours : niveau de détail 0..1 (défaut : 0.5)")
        ->check(CLI::Range(0.0, 1.0));
    // « satin » (ancien nom, trompeur : ce n'est pas l'auto-satin) reste accepté mais
    // n'apparaît plus dans l'aide.
    dz_cmd
        ->add_option("--technique", dz.technique,
                     "Mode contours : auto (défaut) | running | legacy-satin (ancien satin "
                     "dégradé en point droit, pas l'auto-satin)")
        ->check(CLI::IsMember({"auto", "running", "legacy-satin", "satin"}));
    dz_cmd->add_option("--lock", dz.lock, "Point d'arrêt : none|backforth|triangle|zigzag")
        ->check(CLI::IsMember({"none", "backforth", "triangle", "zigzag"}));
    dz_cmd->add_flag("--json", json_out,
                     "JSON unique sur stdout ; les messages d'état passent sur stderr");
    auto* dz_nc = dz_cmd->add_flag("--no-clobber", dz.noClobber,
                                   "Refuse d'écraser un DST/SVG existant (par défaut : écrasé)");
    dz_cmd->add_flag("--force", dz_force, "Écrase explicitement la sortie existante (défaut)")
        ->excludes(dz_nc);

    std::string sd_shape = "circle";
    double sd_length = 3.0;
    int sd_repeats = 1;
    std::string sd_out;
    auto* sd_cmd = app.add_subcommand(
        "stitchdebug", "[diagnostic] Inspecte le moteur de points sur une forme de référence");
    sd_cmd->group(kDiag);
    sd_cmd->add_option("--shape", sd_shape, "line|corner|circle|bezier|star|ring (défaut : circle)")
        ->check(CLI::IsMember({"line", "corner", "circle", "bezier", "star", "ring"}));
    sd_cmd->add_option("--length", sd_length, "Longueur de point en mm")
        ->check(CLI::PositiveNumber);
    sd_cmd->add_option("--repeats", sd_repeats, "1 simple, 2 aller-retour, 3 bean")
        ->check(CLI::Range(1, 3));
    sd_cmd->add_option("--output-svg", sd_out, "Fichier SVG de diagnostic à produire");
    int sd_underlay = 0;
    bool sd_underpath = false;
    sd_cmd->add_option("--underlay", sd_underlay,
                       "Tatami (ring) : sous-couches (masque : 1 contour, 2 parallèle)");
    sd_cmd->add_flag("--underpath", sd_underpath, "Tatami (ring) : liaisons cousues cachées");

    double ed_pull = 0.0;
    double ed_border = 3.0;
    bool ed_underlay_auto = false;
    std::string ed_out;
    auto* ed_cmd = app.add_subcommand(
        "engine-debug",
        "[diagnostic] Tirage du tatami, sous-couche auto, entrée/sortie auto et satin de bordure "
        "sur une scène de référence");
    ed_cmd->group(kDiag);
    ed_cmd->add_option("--pull", ed_pull, "Compensation du tirage du tatami, en mm (défaut 0)")
        ->check(CLI::Range(0.0, 3.0));
    ed_cmd->add_flag("--underlay-auto", ed_underlay_auto, "Sous-couche automatique");
    ed_cmd->add_option("--border", ed_border, "Largeur du satin de bordure en mm (0 = aucun)")
        ->check(CLI::Range(0.0, 20.0));
    ed_cmd->add_option("--output-svg", ed_out, "SVG de diagnostic (avec entrée/sortie auto)");

    std::string sa_shape = "rectangle";
    double sa_spacing = 0.4;
    std::vector<std::string> sa_guides;
    std::string sa_out;
    std::string sa_osp;
    std::string os_in, os_out;
    std::string od_in, od_out;
    bool od_noclobber = false;
    bool od_force = false;
    auto* od_cmd =
        app.add_subcommand("osp2dst", "Exporte un projet .osp en broderie machine : format "
                                      "choisi par l'extension de sortie (.dst, .pes, .jef, .exp)");
    od_cmd->group(kMain);
    od_cmd->add_option("osp,--osp", od_in, "Projet .osp")->required();
    od_cmd->add_option("sortie,--output", od_out, "Fichier à produire (.dst, .pes, .jef ou .exp)")
        ->required();
    bool od_no_trims = false;
    bool od_no_color_changes = false;
    std::string od_stops = "native";
    od_cmd->add_flag("--no-trims", od_no_trims, "Supprime les coupes (elles deviennent des sauts)");
    od_cmd->add_flag("--no-color-changes", od_no_color_changes,
                     "Supprime les changements de couleur (motif monochrome)");
    od_cmd->add_option("--stops", od_stops, "Arrêts machine : native, as-color-change ou drop")
        ->check(CLI::IsMember({"native", "as-color-change", "drop"}));
    auto* od_nc =
        od_cmd->add_flag("--no-clobber", od_noclobber, "Refuse d'écraser un fichier existant");
    od_cmd->add_flag("--force", od_force, "Écrase la sortie existante (comportement par défaut)")
        ->excludes(od_nc);
    bool os_outlines = false;
    bool os_noclobber = false;
    bool os_force = false;
    std::uint64_t os_only = 0;
    auto* os_cmd = app.add_subcommand(
        "osp2svg", "[diagnostic] Séquence effective d'un projet .osp en SVG de diagnostic");
    os_cmd->group(kDiag);
    os_cmd->add_option("osp,--osp", os_in, "Projet .osp")->required();
    os_cmd->add_option("sortie,--output-svg,--output", os_out, "SVG à produire")->required();
    os_cmd->add_flag("--outlines", os_outlines, "Superpose le contour des vecteurs");
    os_cmd->add_option("--only", os_only, "Id d'un objet brodé (les autres sont ignorés)");
    auto* os_nc =
        os_cmd->add_flag("--no-clobber", os_noclobber, "Refuse d'écraser un fichier existant");
    os_cmd->add_flag("--force", os_force, "Écrase la sortie existante (comportement par défaut)")
        ->excludes(os_nc);
    std::uint64_t sa_vector = 0;
    bool sa_pristine = false;
    bool sa_list = false;
    auto* sa_cmd = app.add_subcommand(
        "satin-auto-debug",
        "[diagnostic] Auto-satin par squelette et traversées orientées (forme de référence ou "
        "vecteur d'un .osp)");
    sa_cmd->group(kDiag);
    auto* sa_shape_opt = sa_cmd->add_option(
        "--shape", sa_shape,
        "Forme de référence du corpus (défaut : rectangle ; liste : --list-shapes)");
    sa_cmd->add_flag("--list-shapes", sa_list, "Liste les formes de référence valides et quitte");
    sa_cmd->add_option("--spacing", sa_spacing, "Espacement des traversées en mm (défaut 0,4)")
        ->check(CLI::PositiveNumber);
    sa_cmd->add_option("--guide", sa_guides,
                       "Guide d'orientation x_mm,y_mm,angle_deg[,1=absolu] (répétable)");
    sa_cmd->add_option("--output-svg", sa_out, "SVG de diagnostic à produire");
    auto* sa_osp_opt = sa_cmd->add_option(
        "--osp", sa_osp, "Projet .osp dont on prend un vecteur (au lieu de --shape)");
    sa_osp_opt->excludes(sa_shape_opt);
    sa_cmd->add_option("--vector", sa_vector, "Id du vecteur dans le projet .osp (avec --osp)")
        ->needs(sa_osp_opt);
    sa_cmd
        ->add_flag("--pristine", sa_pristine,
                   "Avec --osp : utilise le contour brut de la région de segmentation")
        ->needs(sa_osp_opt);

    CLI11_PARSE(app, argc, argv);

    if (info_cmd->parsed()) {
        return run_info(image_path, dpi, json_out);
    }
    if (stats_cmd->parsed()) {
        return run_stats(dst_path, json_out);
    }
    if (pr_cmd->parsed()) {
        pr.json = json_out;
        return run_production(pr);
    }
    if (svg_cmd->parsed()) {
        return run_dst2svg(svg_in, svg_out, svg_noclobber);
    }
    if (dz_cmd->parsed()) {
        dz.json = json_out;
        dz.skipBg = (dz_skip_bg == "auto" || dz_skip_bg == "-1") ? -1
                    : (dz_skip_bg == "yes" || dz_skip_bg == "1") ? 1
                                                                 : 0;
        if (dz.technique == "satin") {
            fmt::print(stderr,
                       "openstitch-cli digitize : --technique satin est un ancien nom, utilisez "
                       "legacy-satin\n");
        }
        return run_digitize(dz);
    }
    if (sd_cmd->parsed()) {
        return run_stitchdebug(sd_shape, sd_length, sd_repeats, sd_out, sd_underlay, sd_underpath);
    }
    if (ed_cmd->parsed()) {
        return run_engine_debug(ed_pull, ed_underlay_auto, ed_border, ed_out);
    }
    if (od_cmd->parsed()) {
        openstitch::formats::MachineExportOptions machineOptions;
        if (od_no_trims) {
            machineOptions.trims = openstitch::formats::TrimMode::Drop;
        }
        if (od_no_color_changes) {
            machineOptions.color_changes = openstitch::formats::ColorChangeMode::Drop;
        }
        machineOptions.stops = od_stops == "drop" ? openstitch::formats::StopMode::Drop
                               : od_stops == "as-color-change"
                                   ? openstitch::formats::StopMode::AsColorChange
                                   : openstitch::formats::StopMode::Native;
        return run_osp2dst(od_in, od_out, od_noclobber, machineOptions);
    }
    if (os_cmd->parsed()) {
        return run_osp2svg(os_in, os_out, os_outlines, os_only, os_noclobber);
    }
    if (sa_cmd->parsed()) {
        if (sa_list) {
            for (const auto& n : kSatinShapeNames) {
                fmt::print("{}\n", n);
            }
            return kExitOk;
        }
        return run_satin_auto_debug(sa_shape, sa_spacing, sa_guides, sa_out, sa_osp, sa_vector,
                                    sa_pristine);
    }
    return kExitOk;
}
