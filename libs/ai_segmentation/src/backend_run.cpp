// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/backend_run.hpp"

#include <algorithm>
#include <system_error>

namespace openstitch::ai_segmentation {

namespace {

std::string describe(const AiError& e) {
    return e.detail.empty() ? e.message : e.message + " (" + e.detail + ")";
}

bool safe_relative(const std::filesystem::path& p) {
    if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) {
        return false;
    }
    return std::none_of(p.begin(), p.end(), [](const auto& part) { return part == ".."; });
}

BackendRunOutcome fell_back(BackendRunOutcome out, const BackendConfig& backend,
                            std::string because, const AiError* error = nullptr) {
    out.status = BackendRunStatus::FellBack;
    out.fallback_reason = "Backend IA « " + backend.display_name + " » : " + std::move(because) +
                          " — repli sur la segmentation couleur (CIELAB).";
    if (error != nullptr) {
        out.error_code = error->code;
        out.detail = describe(*error);
    }
    return out;
}

} // namespace

AiResult<MaskCollection> masks_from_predict(const PredictResult& result,
                                            std::string_view model_id) {
    MaskCollection out;
    out.schema_version = 1;
    out.source_file = "input.png";
    out.model_worker_id = std::string(model_id);
    out.image_width = result.transform.source_width;
    out.image_height = result.transform.source_height;
    if (out.image_width <= 0 || out.image_height <= 0) {
        return std::unexpected(AiError{
            AiErrorCode::InvalidWorkerResponse, default_message(AiErrorCode::InvalidWorkerResponse),
            "dimensions de l'image source absentes de la transformation"});
    }
    int next_id = 0;
    for (const MaskProposal& m : result.masks) {
        if (!safe_relative(std::filesystem::path(m.file))) {
            return std::unexpected(
                AiError{AiErrorCode::InvalidWorkerResponse,
                        default_message(AiErrorCode::InvalidWorkerResponse),
                        "chemin de masque hors du dossier de tâche : " + m.file});
        }
        MaskEntry e;
        e.id = next_id++;
        e.file = m.file;
        e.area_pixels = static_cast<std::size_t>(std::max<std::int64_t>(0, m.area_px));
        e.bbox_xywh = m.bbox_xywh;
        e.predicted_iou = m.confidence.value_or(0.0);
        e.iou_reported = m.confidence.has_value();
        e.stability_score = 0.0;
        e.stability_reported = false; // le contrat n'a pas de score de stabilité
        out.masks.push_back(std::move(e));
    }
    return out;
}

BackendRunOutcome run_backend_segmentation(const BackendRunRequest& request,
                                           const TransportFactory& factory,
                                           const BackendProgressFn& progress,
                                           const std::atomic<bool>* cancel) {
    BackendRunOutcome out;
    out.report.backend_id = request.backend.id;
    out.report.display_name = request.backend.display_name;
    const auto cancelled = [&] {
        BackendRunOutcome c = out;
        c.status = BackendRunStatus::Cancelled;
        return c;
    };
    if (cancel != nullptr && cancel->load()) {
        return cancelled();
    }
    if (!request.backend.enabled) {
        return fell_back(out, request.backend, "désactivé dans les réglages");
    }

    auto transport = factory(request.backend);
    if (!transport) {
        return fell_back(out, request.backend, describe(transport.error()), &transport.error());
    }
    auto session = BackendSession::open(std::move(*transport), request.session);
    if (!session) {
        return fell_back(out, request.backend, describe(session.error()), &session.error());
    }
    auto caps = session->capabilities();
    if (!caps) {
        return fell_back(out, request.backend, describe(caps.error()), &caps.error());
    }
    out.report.model_id = caps->model_id;
    out.report.model_hash = caps->model_hash;
    out.report.license_code = caps->license_code;
    out.report.license_weights = caps->license_weights;
    if (!caps->supports(request.prompt)) {
        return fell_back(out, request.backend,
                         "ne sait pas traiter l'invite « " +
                             std::string(prompt_kind_name(request.prompt)) + " »");
    }
    auto health = session->health();
    if (!health) {
        return fell_back(out, request.backend, describe(health.error()), &health.error());
    }
    if (health->state != "ready") {
        AiError e{AiErrorCode::BackendUnavailable, default_message(AiErrorCode::BackendUnavailable),
                  health->detail};
        return fell_back(
            out, request.backend,
            "indisponible (" + (health->detail.empty() ? health->state : health->detail) + ")", &e);
    }
    if (cancel != nullptr && cancel->load()) {
        return cancelled();
    }

    PredictRequest predict;
    predict.id = "desktop-predict";
    predict.job_dir = request.job_dir.generic_string();
    predict.image_file = request.image_file;
    predict.prompts = {Prompt{request.prompt, std::nullopt, {}}};
    predict.options = request.options;
    auto result = session->predict(predict, progress, cancel);
    if (!result) {
        if (result.error().code == AiErrorCode::Cancelled) {
            return cancelled();
        }
        return fell_back(out, request.backend, describe(result.error()), &result.error());
    }
    out.report.elapsed_ms = result->elapsed_ms;
    out.report.warnings = result->warnings;

    const auto& t = result->transform;
    if (request.expected_size && (t.source_width != request.expected_size->first ||
                                  t.source_height != request.expected_size->second)) {
        AiError e{
            AiErrorCode::InvalidWorkerResponse, default_message(AiErrorCode::InvalidWorkerResponse),
            "masques " + std::to_string(t.source_width) + "x" + std::to_string(t.source_height) +
                " pour une image " + std::to_string(request.expected_size->first) + "x" +
                std::to_string(request.expected_size->second)};
        return fell_back(out, request.backend, "résolution des masques incohérente", &e);
    }
    if (result->masks.empty()) {
        return fell_back(out, request.backend,
                         result->detections.empty()
                             ? "aucun masque produit"
                             : "ne rend que des boîtes (planificateur), pas de masques");
    }
    auto masks = masks_from_predict(*result, caps->model_id);
    if (!masks) {
        return fell_back(out, request.backend, describe(masks.error()), &masks.error());
    }
    for (const MaskEntry& m : masks->masks) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(request.job_dir / m.file, ec)) {
            AiError e{AiErrorCode::InvalidWorkerResponse,
                      default_message(AiErrorCode::InvalidWorkerResponse),
                      "fichier de masque introuvable : " + m.file};
            return fell_back(out, request.backend, "fichier de masque manquant", &e);
        }
    }
    out.status = BackendRunStatus::Completed;
    out.masks = std::move(*masks);
    return out;
}

std::string backend_status_line(const BackendStatus& status) {
    std::string line =
        status.config.display_name.empty() ? status.config.id : status.config.display_name;
    line += " — " + std::string(backend_state_name(status.state)) + " : " + status.diagnostics;
    if (status.capabilities) {
        const BackendCapabilities& c = *status.capabilities;
        line += " · modèle " + c.model_id;
        line += " · empreinte " +
                (c.model_hash ? c.model_hash->substr(0, 12) + "…" : std::string("non fournie"));
        line += " · licence code " + c.license_code + ", poids " + c.license_weights;
        if (c.experimental) {
            line += " · expérimental";
        }
    }
    return line;
}

} // namespace openstitch::ai_segmentation
