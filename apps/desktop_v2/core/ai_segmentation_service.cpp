// SPDX-License-Identifier: Apache-2.0
#include "ai_segmentation_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stop_token>
#include <system_error>

#include "openstitch/ai_segmentation/backend_run.hpp"
#include "openstitch/ai_segmentation/color_refine.hpp"
#include "openstitch/ai_segmentation/label_map.hpp"
#include "openstitch/ai_segmentation/topology_cleanup.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/color_merge.hpp"
#include "segmentation_preview.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr const char* kClassicalLabel = "Segmentation couleur (CIELAB, sans IA)";

// Dossier de tâche supprimé en sortie, quoi qu'il arrive (le masque PNG n'a aucune valeur après).
class JobDir {
public:
    explicit JobDir(std::filesystem::path path) : path_(std::move(path)) {}
    JobDir(const JobDir&) = delete;
    JobDir& operator=(const JobDir&) = delete;
    ~JobDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

Result<AiSegmentationOutcome> classical(const document::ImageSource& source,
                                        const AiSegmentationRequest& request,
                                        const TaskContext& task, std::string reason) {
    segmentation::SegmentationOptions options;
    options.max_colors = request.max_colors;
    options.min_region_px = request.min_region_px;
    auto seg = segmentImageSource(source, options, task);
    if (!seg) {
        return std::unexpected(seg.error());
    }
    AiSegmentationOutcome out;
    out.segmentation = std::move(*seg);
    out.used_backend = false;
    out.engine_label = kClassicalLabel;
    out.notice = std::move(reason);
    return out;
}

std::string uniqueName() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return "job-" + std::to_string(ticks);
}

} // namespace

Result<AiSegmentationOutcome> segmentWithAi(const document::ImageSource& source,
                                            const AiSegmentationRequest& request,
                                            const TaskContext& task,
                                            const ai_segmentation::TransportFactory& factory) {
    using namespace ai_segmentation;
    if (task.cancelled()) {
        return cancelled_error();
    }
    if (request.engine == kAiEngineClassical) {
        return classical(source, request, task, {});
    }

    // 1) Quel backend ? Toute impasse est un repli expliqué, jamais un échec silencieux.
    const std::filesystem::path settingsPath =
        request.settings_file.empty() ? std::filesystem::path(backend_settings_env_path())
                                      : request.settings_file;
    if (settingsPath.empty()) {
        return classical(source, request, task,
                         "Aucun backend IA déclaré (fichier de réglages ou variable "
                         "OPENSTITCH_AI_BACKENDS) — segmentation couleur (CIELAB).");
    }
    auto settings = load_backend_settings(settingsPath);
    if (!settings) {
        return classical(source, request, task,
                         "Réglages IA illisibles (" + settings.error().message +
                             ") — segmentation couleur (CIELAB).");
    }
    const std::string wanted = request.engine.empty() ? settings->preferred_id : request.engine;
    if (wanted.empty()) {
        return classical(source, request, task,
                         "Aucun backend IA préféré — segmentation couleur (CIELAB).");
    }
    const auto found = std::find_if(settings->backends.begin(), settings->backends.end(),
                                    [&](const BackendConfig& b) { return b.id == wanted; });
    if (found == settings->backends.end()) {
        return classical(source, request, task,
                         "Backend IA « " + wanted +
                             " » non déclaré dans les réglages — segmentation couleur (CIELAB).");
    }
    if (!found->enabled) {
        return classical(source, request, task,
                         "Backend IA « " + found->display_name +
                             " » désactivé dans les réglages — segmentation couleur (CIELAB).");
    }
    const std::string name = found->display_name.empty() ? found->id : found->display_name;

    // 2) Image de travail -> dossier de tâche.
    auto working = image::apply_pipeline(source.original, source.ops);
    if (!working) {
        return std::unexpected(working.error());
    }
    auto png = image::encode_png(*working);
    if (!png) {
        return std::unexpected(png.error());
    }
    const std::filesystem::path root =
        request.job_root.empty() ? std::filesystem::temp_directory_path() / "OpenStitch" / "ai-jobs"
                                 : request.job_root;
    const JobDir job(root / uniqueName());
    std::error_code ec;
    std::filesystem::create_directories(job.path(), ec);
    {
        std::ofstream out(job.path() / "input.png", std::ios::binary);
        if (ec || !out ||
            !out.write(reinterpret_cast<const char*>(png->data()),
                       static_cast<std::streamsize>(png->size()))) {
            return fail(ErrorCategory::OperationImpossible,
                        "Impossible de préparer l'image pour le backend IA : le dossier "
                        "temporaire n'est pas accessible en écriture.");
        }
    }

    // 3) Analyse par le backend (annulable : le jeton d'arrêt de la tâche arme `cancel`).
    BackendRunRequest run;
    run.backend = *found;
    run.prompt = PromptKind::Automatic;
    run.job_dir = job.path();
    if (!request.profile.empty()) {
        run.options.emplace_back("profile", request.profile);
    }
    if (request.max_resolution > 0) {
        run.options.emplace_back("max_resolution", std::to_string(request.max_resolution));
    }
    run.expected_size = std::pair{working->width, working->height};
    // PyTorch et les poids se chargent avant le `hello` : délai de démarrage large.
    run.session.start_timeout = std::chrono::seconds{180};
    std::atomic<bool> cancel{false};
    const std::stop_callback onStop(task.stop, [&cancel] { cancel = true; });
    auto outcome = run_backend_segmentation(run, factory, {}, &cancel);

    if (outcome.status == BackendRunStatus::Cancelled || task.cancelled()) {
        return cancelled_error();
    }
    if (outcome.status == BackendRunStatus::FellBack) {
        return classical(source, request, task, outcome.fallback_reason);
    }
    if (outcome.masks.masks.empty()) {
        return classical(source, request, task,
                         "Backend IA « " + name +
                             " » : aucun masque proposé — segmentation couleur (CIELAB).");
    }

    // 4) Masques -> carte de labels sans chevauchement (tous retenus, ordre par numéro).
    std::vector<LabelMaskInput> inputs;
    inputs.reserve(outcome.masks.masks.size());
    for (const auto& entry : outcome.masks.masks) {
        auto bitmap = image::load_image(job.path() / entry.file);
        if (!bitmap || bitmap->width != working->width || bitmap->height != working->height) {
            return classical(source, request, task,
                             "Backend IA « " + name + " » : masque " + std::to_string(entry.id) +
                                 " illisible ou de mauvaise taille — segmentation couleur "
                                 "(CIELAB).");
        }
        LabelMaskInput input;
        input.mask_id = entry.id;
        input.pixels.resize(static_cast<std::size_t>(bitmap->width) * bitmap->height);
        for (std::size_t i = 0; i < input.pixels.size(); ++i) {
            input.pixels[i] = bitmap->rgba[i * 4] > 127 ? 1 : 0;
        }
        input.rgb = {static_cast<std::uint8_t>((entry.id * 53) % 180 + 60),
                     static_cast<std::uint8_t>((entry.id * 97) % 180 + 60),
                     static_cast<std::uint8_t>((entry.id * 151) % 180 + 60)};
        input.predicted_iou = entry.predicted_iou;
        input.stability_score = entry.stability_score;
        inputs.push_back(std::move(input));
    }
    std::sort(inputs.begin(), inputs.end(),
              [](const auto& a, const auto& b) { return a.mask_id < b.mask_id; });
    if (task.cancelled()) {
        return cancelled_error();
    }

    auto labelMap = build_label_map(inputs, LabelMapOptions{working->width, working->height});
    if (!labelMap) {
        return classical(source, request, task,
                         "Backend IA « " + name + " » : carte de labels impossible (" +
                             labelMap.error().message + ") — segmentation couleur (CIELAB).");
    }
    TopologyCleanupOptions cleanup;
    cleanup.mm_per_px = source.mm_per_px.value;
    cleanup.min_island_area_mm2 = request.min_island_mm2;
    cleanup.min_hole_area_mm2 = request.min_hole_mm2;
    if (auto report = cleanup_topology(*labelMap, cleanup); !report) {
        return classical(source, request, task,
                         "Backend IA « " + name + " » : nettoyage topologique impossible (" +
                             report.error().message + ") — segmentation couleur (CIELAB).");
    }

    AiSegmentationOutcome result;
    result.used_backend = true;
    result.mask_count = inputs.size();
    result.engine_label = name;
    result.notice = std::to_string(inputs.size()) + " forme(s) proposée(s) par " + name +
                    " (modèle " + outcome.report.model_id + ").";

    // 5) Formes -> blocs de couleur (CIELAB). Un échec ici garde les formes IA telles quelles.
    bool refinedOk = false;
    if (request.refine_by_color) {
        ColorRefineOptions colorOptions;
        colorOptions.max_colors = request.max_colors;
        colorOptions.min_region_px = request.min_region_px;
        auto refined = refine_label_map_by_color(*labelMap, *working, colorOptions);
        if (refined) {
            result.segmentation = std::move(*refined);
            refinedOk = true;
        } else {
            result.notice += " Découpe par couleur impossible (" + refined.error().message +
                             ") : formes IA conservées telles quelles.";
        }
    }
    if (!refinedOk) {
        result.segmentation = std::move(*labelMap);
        // Les formes IA portent des teintes de repère : on les remplace par la couleur moyenne.
        for (auto& slot : result.segmentation.region_slots) {
            if (slot) {
                if (const auto mean =
                        segmentation::region_mean_color(result.segmentation, *working, slot->id)) {
                    slot->rgb = *mean;
                }
            }
        }
    }
    return result;
}

} // namespace openstitch::desktop_v2
