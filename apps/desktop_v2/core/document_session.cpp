// SPDX-License-Identifier: Apache-2.0
#include "document_session.hpp"

#include <algorithm>

#include "openstitch/commands/replace_project_command.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop_v2 {

DocumentSession::DocumentSession() {
    undo_.markClean();
}

std::shared_ptr<const document::Project> DocumentSession::snapshot() const {
    if (!cachedSnapshot_ || cachedRevision_ != revision_) {
        cachedSnapshot_ = std::make_shared<const document::Project>(project_);
        cachedRevision_ = revision_;
    }
    return cachedSnapshot_;
}

bool DocumentSession::exists(const Target& target) const {
    switch (target.kind) {
    case TargetKind::EmbroideryObject:
        return std::ranges::any_of(project_.embroidery_objects,
                                   [&](const auto& o) { return o.id.value == target.id; });
    case TargetKind::VectorObject:
        return std::ranges::any_of(project_.vector_objects,
                                   [&](const auto& o) { return o.id.value == target.id; });
    case TargetKind::Region:
        if (!project_.segmentation) {
            return false;
        }
        return project_.segmentation->find(RegionId{target.id}) != nullptr;
    }
    return false;
}

void DocumentSession::changed(ChangeKind kind) {
    ++revision_;
    // La sélection ne doit jamais pointer un objet disparu (undo d'une création, remplacement…).
    selection_.prune([this](const Target& t) { return exists(t); });
    notify(kind);
}

void DocumentSession::notify(ChangeKind kind) {
    const auto snapshotListeners = listeners_;
    for (const auto& [token, listener] : snapshotListeners) {
        (void)token;
        listener(kind);
    }
}

void DocumentSession::execute(std::unique_ptr<commands::ICommand> command) {
    if (!command) {
        return;
    }
    undo_.execute(std::move(command), project_);
    lastChangeAffectsEmbroidery_ = undo_.lastChangeAffectsEmbroidery();
    lastChangeAffectsSegmentation_ = undo_.lastChangeAffectsSegmentation();
    lastChangeAffectsSegmentation_ = undo_.lastChangeAffectsSegmentation();
    lastChangeAffectsSegmentation_ = undo_.lastChangeAffectsSegmentation();
    changed(ChangeKind::Edited);
}

bool DocumentSession::undo() {
    if (!undo_.undo(project_)) {
        return false;
    }
    lastChangeAffectsEmbroidery_ = undo_.lastChangeAffectsEmbroidery();
    changed(ChangeKind::Undone);
    return true;
}

bool DocumentSession::redo() {
    if (!undo_.redo(project_)) {
        return false;
    }
    lastChangeAffectsEmbroidery_ = undo_.lastChangeAffectsEmbroidery();
    changed(ChangeKind::Redone);
    return true;
}

bool DocumentSession::navigateHistory(std::size_t position) {
    if (position > undo_.historySize()) {
        return false;
    }
    const auto previous = undo_.position();
    if (position == previous) {
        return true;
    }
    bool affectsEmbroidery = false;
    bool affectsSegmentation = false;
    while (undo_.position() > position) {
        undo_.undo(project_);
        affectsEmbroidery |= undo_.lastChangeAffectsEmbroidery();
        affectsSegmentation |= undo_.lastChangeAffectsSegmentation();
        affectsSegmentation |= undo_.lastChangeAffectsSegmentation();
    }
    while (undo_.position() < position) {
        undo_.redo(project_);
        affectsEmbroidery |= undo_.lastChangeAffectsEmbroidery();
    }
    lastChangeAffectsEmbroidery_ = affectsEmbroidery;
    lastChangeAffectsSegmentation_ = affectsSegmentation;
    changed(position < previous ? ChangeKind::Undone : ChangeKind::Redone);
    return true;
}

DocumentSession::BatchResult DocumentSession::previewBatch(std::string_view batchJson) const {
    document::Project scratch = project_;
    project_ops::BatchOptions options;
    options.dry_run = true;
    return {project_ops::apply_batch(scratch, batchJson, options), false};
}

DocumentSession::BatchResult DocumentSession::applyBatch(std::string_view batchJson,
                                                         const std::string& label) {
    auto before = std::make_shared<const document::Project>(project_);
    document::Project work = project_;
    BatchResult result;
    result.report = project_ops::apply_batch(work, batchJson);
    if (!result.report.ok || !result.report.applied) {
        return result;
    }
    auto after = std::make_shared<const document::Project>(std::move(work));
    // Les opérations strictement en amont ou de métadonnées ne touchent pas aux
    // points déjà générés. Un lot mixte contenant une opération inconnue reste
    // conservateur : il invalide la géométrie.
    const auto changesStitches = [](const std::string& op) {
        if (op == "rename") {
            return false;
        }
        if (op == "set_locked") {
            return false;
        }
        if (op == "set_semantic") {
            return false;
        }
        if (op == "set_source_image") {
            return false;
        }
        if (op == "image_denoise") {
            return false;
        }
        if (op == "segment_image") {
            return false;
        }
        if (op == "seg_merge_colors") {
            return false;
        }
        if (op == "seg_reduce_colors") {
            return false;
        }
        if (op == "seg_clean_islands") {
            return false;
        }
        if (op == "seg_merge_regions") {
            return false;
        }
        if (op == "seg_remove_region") {
            return false;
        }
        if (op == "seg_recolor_region") {
            return false;
        }
        return true;
    };
    bool affectsStitches = false;
    for (const auto& outcome : result.report.outcomes) {
        if (!outcome.ok || changesStitches(outcome.op)) {
            affectsStitches = true;
            break;
        }
    }
    // Only explicitly known non-segmentation operations can skip refreshing
    // the region map. Unknown/mixed batches remain conservative.
    const auto changesSegmentation = [](const std::string& op) {
        if (op == "rename" || op == "set_locked" || op == "set_semantic") {
            return false;
        }
        if (op == "set_color" || op == "set_visible" || op == "set_join") {
            return false;
        }
        if (op == "set_connector" || op == "set_stitch_type") {
            return false;
        }
        if (op == "set_params" || op == "set_order" || op == "optimize_order") {
            return false;
        }
        if (op == "set_machine" || op == "set_finishing" || op == "set_canvas") {
            return false;
        }
        if (op == "discard_overrides" || op == "set_design_points") {
            return false;
        }
        if (op == "translate" || op == "rotate" || op == "scale") {
            return false;
        }
        if (op == "mirror" || op == "duplicate_object" || op == "add_shape") {
            return false;
        }
        if (op == "remove_object" || op == "directional_guides") {
            return false;
        }
        return true;
    };
    bool affectsSegmentation = false;
    for (const auto& outcome : result.report.outcomes) {
        if (!outcome.ok || changesSegmentation(outcome.op)) {
            affectsSegmentation = true;
            break;
        }
    }
    auto replacement = std::make_unique<commands::ReplaceProjectCommand>(
        label, std::move(before), std::move(after), affectsStitches, affectsSegmentation);
    execute(std::move(replacement));
    result.committed = true;
    return result;
}

void DocumentSession::replaceProject(document::Project project, bool clean) {
    project_ = std::move(project);
    undo_.clear();
    if (clean) {
        undo_.markClean();
    } else {
        undo_.markDirty();
    }
    if (bridge_) {
        bridge_->reset(); // nouvelle session pour l'agent : l'ancienne est close
    }
    lastChangeAffectsEmbroidery_ = true;
    lastChangeAffectsSegmentation_ = true;
    changed(ChangeKind::Replaced);
}

project_ops::LiveBridge& DocumentSession::liveBridge() {
    if (!bridge_) {
        bridge_ = std::make_unique<project_ops::LiveBridge>(project_, undo_);
    }
    return *bridge_;
}

DocumentSession::Token DocumentSession::subscribe(Listener listener) {
    const Token token = nextToken_++;
    listeners_.emplace_back(token, std::move(listener));
    return token;
}

void DocumentSession::unsubscribe(Token token) {
    std::erase_if(listeners_, [token](const auto& entry) { return entry.first == token; });
}

} // namespace openstitch::desktop_v2
