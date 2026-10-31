// SPDX-License-Identifier: Apache-2.0
// Pont « projet vivant » : voir live_bridge.hpp et docs/adr/0001-compagnon-ia-session-live.md.
#include "openstitch/project_ops/live_bridge.hpp"

#include <random>
#include <utility>

#include "openstitch/commands/replace_project_command.hpp"
#include "openstitch/project_ops/errors.hpp"

namespace openstitch::project_ops {

namespace {

std::string random_session_id() {
    std::random_device rd;
    std::string out;
    out.reserve(32);
    constexpr std::string_view digits = "0123456789abcdef";
    for (int i = 0; i < 4; ++i) {
        std::uint32_t v = rd();
        for (int k = 0; k < 8; ++k) {
            out.push_back(digits[v & 0xFU]);
            v >>= 4;
        }
    }
    return out;
}

std::string failure_code(const BatchReport& report) {
    if (!report.error_code.empty()) {
        return report.error_code;
    }
    return report.outcomes.empty() ? std::string(code::kRefused) : report.outcomes.back().code;
}

std::string failure_message(const BatchReport& report) {
    if (!report.error_message.empty()) {
        return report.error_message;
    }
    return report.outcomes.empty() ? std::string("lot refusé") : report.outcomes.back().message;
}

} // namespace

LiveBridge::LiveBridge(document::Project& project, commands::UndoStack& stack,
                       std::string session_id)
    : project_(project), stack_(stack),
      session_id_(session_id.empty() ? random_session_id() : std::move(session_id)),
      seen_version_(stack.version()) {}

void LiveBridge::sync() {
    const std::uint64_t now = stack_.version();
    if (now > seen_version_) {
        revision_ += now - seen_version_;
        seen_version_ = now;
    }
}

std::uint64_t LiveBridge::revision() {
    sync();
    return revision_;
}

LiveBridge::Snapshot LiveBridge::snapshot() {
    sync();
    return {session_id_, revision_, std::make_shared<const document::Project>(project_)};
}

void LiveBridge::reset(std::string session_id) {
    sync();
    ++revision_;
    session_id_ = session_id.empty() ? random_session_id() : std::move(session_id);
    closed_ = false;
    busy_ = false;
}

LiveBridge::Result LiveBridge::guard(std::string_view session_id, std::uint64_t revision) {
    sync();
    Result r;
    r.revision = revision_;
    if (closed_ || session_id != session_id_) {
        r.code = code::kSessionClosed;
        r.message = closed_ ? "l'application est fermée" : "le document actif a été remplacé";
        return r;
    }
    if (revision != revision_) {
        r.code = code::kStaleRevision;
        r.message = "le document a changé depuis votre lecture (révision " +
                    std::to_string(revision) + ", actuelle " + std::to_string(revision_) +
                    ") : relisez un instantané";
        return r;
    }
    r.ok = true;
    return r;
}

LiveBridge::Result LiveBridge::preview(std::string_view session_id, std::uint64_t revision,
                                       std::string_view batch_json) {
    Result r = guard(session_id, revision);
    if (!r.ok) {
        return r;
    }
    document::Project work = project_;
    BatchOptions options;
    options.dry_run = true;
    r.report = apply_batch(work, batch_json, options);
    r.ok = r.report.ok;
    if (!r.ok) {
        r.code = failure_code(r.report);
        r.message = failure_message(r.report);
    }
    return r;
}

LiveBridge::Result LiveBridge::commit(std::string_view session_id, std::uint64_t revision,
                                      std::string_view batch_json, const std::string& label,
                                      bool approved) {
    Result r = guard(session_id, revision);
    if (!r.ok) {
        return r;
    }
    if (busy_) {
        r.ok = false;
        r.code = code::kBusy;
        r.message = "un commit est déjà en cours";
        return r;
    }
    if (!approved) {
        r.ok = false;
        r.code = code::kApprovalRequired;
        r.message = "la modification du projet ouvert exige l'approbation de l'utilisateur";
        return r;
    }
    busy_ = true;
    auto before = std::make_shared<const document::Project>(project_);
    auto work = std::make_shared<document::Project>(*before);
    r.report = apply_batch(*work, batch_json);
    if (!r.report.ok) {
        busy_ = false;
        r.ok = false;
        r.code = failure_code(r.report);
        r.message = failure_message(r.report);
        return r;
    }
    if (r.report.operation_count == 0) {
        busy_ = false;
        return r;
    }
    return install(std::move(r), std::move(before), std::move(work), label);
}

LiveBridge::Result LiveBridge::install(Result r, std::shared_ptr<const document::Project> before,
                                       std::shared_ptr<const document::Project> after,
                                       const std::string& label) {
    stack_.execute(std::make_unique<commands::ReplaceProjectCommand>(
                       label.empty() ? std::string("IA : lot d'opérations") : label,
                       std::move(before), std::move(after)),
                   project_);
    r.applied = true;
    busy_ = false;
    sync();
    r.revision = revision_;
    return r;
}

LiveBridge::Result LiveBridge::commit_project(std::string_view session_id, std::uint64_t revision,
                                              std::shared_ptr<const document::Project> before,
                                              std::shared_ptr<const document::Project> result,
                                              const std::string& label, bool approved) {
    Result r = guard(session_id, revision);
    if (!r.ok) {
        return r;
    }
    if (busy_) {
        r.ok = false;
        r.code = code::kBusy;
        r.message = "un commit est déjà en cours";
        return r;
    }
    if (!approved) {
        r.ok = false;
        r.code = code::kApprovalRequired;
        r.message = "la modification du projet ouvert exige l'approbation de l'utilisateur";
        return r;
    }
    busy_ = true;
    return install(std::move(r), std::move(before), std::move(result), label);
}

} // namespace openstitch::project_ops
