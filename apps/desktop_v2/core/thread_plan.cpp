// SPDX-License-Identifier: Apache-2.0
#include "thread_plan.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

#include "openstitch/commands/colorway_commands.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"

namespace openstitch::desktop_v2 {

namespace {
ServiceResult ok() {
    return {true, {}};
}
ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}
std::string hexLabel(const std::array<std::uint8_t, 3>& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c[0], c[1], c[2]);
    return buf;
}
struct Key {
    std::optional<thread_palette::ThreadKey> thread;
    std::array<std::uint8_t, 3> rgb{};
    bool operator==(const Key& o) const {
        return thread ? (o.thread && *thread == *o.thread) : (!o.thread && rgb == o.rgb);
    }
};
std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string{} : s.substr(b, s.find_last_not_of(" \t") - b + 1);
}
std::vector<ObjectId> toIds(const std::vector<std::uint64_t>& raw) {
    std::vector<ObjectId> ids;
    for (const auto id : raw) {
        ids.push_back(ObjectId{id});
    }
    return ids;
}
} // namespace

std::vector<ThreadEntry> threadEntries(const document::Project& project) {
    std::vector<ThreadEntry> out;
    std::vector<Key> keys;
    for (const std::size_t index : project.sewingOrder()) {
        const auto& o = project.embroidery_objects[index];
        const Key key{o.thread, o.rgb};
        auto it = std::find(keys.begin(), keys.end(), key);
        if (it == keys.end()) {
            keys.push_back(key);
            ThreadEntry e;
            e.rgb = o.rgb;
            e.label = o.thread ? o.thread->chart_id + " " + o.thread->code : hexLabel(o.rgb);
            out.push_back(std::move(e));
            it = keys.end() - 1;
        }
        out[static_cast<std::size_t>(it - keys.begin())].objects.push_back(o.id.value);
    }
    return out;
}

ServiceResult mergeThreads(DocumentSession& session, std::size_t from, std::size_t into) {
    const auto entries = threadEntries(session.project());
    if (from >= entries.size() || into >= entries.size()) {
        return refuse("Choisissez deux fils existants.");
    }
    if (from == into) {
        return refuse("Choisissez deux fils différents.");
    }
    std::optional<thread_palette::ThreadKey> thread;
    if (const auto* target =
            session.project().findEmbroidery(ObjectId{entries[into].objects.front()})) {
        thread = target->thread;
    }
    session.execute(std::make_unique<commands::SetObjectThreadCommand>(
        toIds(entries[from].objects), thread, entries[into].rgb, "Fusionner des fils"));
    return ok();
}

ServiceResult recolorThread(DocumentSession& session, std::size_t entry,
                            std::array<std::uint8_t, 3> rgb) {
    const auto entries = threadEntries(session.project());
    if (entry >= entries.size()) {
        return refuse("Choisissez un fil existant.");
    }
    session.execute(std::make_unique<commands::SetObjectThreadCommand>(
        toIds(entries[entry].objects), std::nullopt, rgb, "Recolorer un fil"));
    return ok();
}

ServiceResult setThreadVisible(DocumentSession& session, std::size_t entry, bool visible) {
    const auto entries = threadEntries(session.project());
    if (entry >= entries.size()) {
        return refuse("Choisissez un fil existant.");
    }
    session.execute(std::make_unique<commands::SetThreadVisibleCommand>(
        toIds(entries[entry].objects), visible));
    return ok();
}

std::vector<OrderRow> orderRows(const document::Project& project) {
    std::vector<OrderRow> out;
    for (const std::size_t index : project.sewingOrder()) {
        const auto& o = project.embroidery_objects[index];
        out.push_back({o.id.value, o.name, o.rgb});
    }
    return out;
}

ServiceResult moveInOrder(DocumentSession& session, std::uint64_t id, std::size_t toPosition) {
    const auto& project = session.project();
    if (project.findEmbroidery(ObjectId{id}) == nullptr) {
        return refuse("Choisissez un objet de broderie.");
    }
    if (!commands::moved_sewing_order(project, ObjectId{id}, toPosition)) {
        return refuse("Déplacement impossible : même position, hors liste ou ordre des calques "
                      "non respecté.");
    }
    session.execute(std::make_unique<commands::MoveInSewingOrderCommand>(ObjectId{id}, toPosition));
    return ok();
}

std::vector<ColorwayRow> colorwayRows(const document::Project& project) {
    std::vector<ColorwayRow> out;
    for (std::size_t i = 0; i < project.colorways.size(); ++i) {
        out.push_back({project.colorways[i].name, static_cast<int>(i) == project.active_colorway});
    }
    return out;
}

ServiceResult addColorway(DocumentSession& session, const std::string& name) {
    const auto n = trimmed(name);
    if (n.empty()) {
        return refuse("Donnez un nom au coloris.");
    }
    if (session.project().embroidery_objects.empty()) {
        return refuse("Aucun objet de broderie à colorier.");
    }
    session.execute(std::make_unique<commands::AddColorwayCommand>(n));
    return ok();
}

ServiceResult activateColorway(DocumentSession& session, int index) {
    const auto& p = session.project();
    if (index < 0 || static_cast<std::size_t>(index) >= p.colorways.size()) {
        return refuse("Ce coloris n'existe pas.");
    }
    if (index == p.active_colorway) {
        return refuse("Ce coloris est déjà actif.");
    }
    session.execute(std::make_unique<commands::SetActiveColorwayCommand>(index));
    return ok();
}

ServiceResult removeColorway(DocumentSession& session, int index) {
    const auto& p = session.project();
    if (index < 0 || static_cast<std::size_t>(index) >= p.colorways.size()) {
        return refuse("Ce coloris n'existe pas.");
    }
    if (index == p.active_colorway) {
        return refuse("Activez un autre coloris avant de supprimer celui-ci.");
    }
    session.execute(std::make_unique<commands::RemoveColorwayCommand>(index));
    return ok();
}

ServiceResult renameColorway(DocumentSession& session, int index, const std::string& name) {
    const auto n = trimmed(name);
    if (n.empty()) {
        return refuse("Donnez un nom au coloris.");
    }
    if (index < 0 || static_cast<std::size_t>(index) >= session.project().colorways.size()) {
        return refuse("Ce coloris n'existe pas.");
    }
    session.execute(std::make_unique<commands::RenameColorwayCommand>(index, n));
    return ok();
}

} // namespace openstitch::desktop_v2
