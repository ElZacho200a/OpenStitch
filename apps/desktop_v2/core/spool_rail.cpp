// SPDX-License-Identifier: Apache-2.0
#include "spool_rail.hpp"

#include "thread_plan.hpp"

namespace openstitch::desktop_v2 {

std::string SpoolRail::summary() const {
    if (spools.empty()) {
        return "Aucune bobine";
    }
    const std::string bobines =
        std::to_string(spools.size()) + (spools.size() > 1 ? " bobines" : " bobine");
    const std::string changes = std::to_string(threadChanges) +
                                (threadChanges > 1 ? " changements de fil" : " changement de fil");
    return bobines + " · " + changes;
}

SpoolRail spoolRail(const document::Project& project) {
    SpoolRail out;
    for (const auto& row : orderRows(project)) {
        Spool s;
        s.id = row.id;
        s.name = row.name;
        s.rgb = row.rgb;
        s.threadChangeBefore = !out.spools.empty() && out.spools.back().rgb != row.rgb;
        out.threadChanges += s.threadChangeBefore ? 1 : 0;
        out.spools.push_back(std::move(s));
    }
    return out;
}

ServiceResult moveSpool(DocumentSession& session, std::uint64_t id, std::size_t toIndex) {
    return moveInOrder(session, id, toIndex);
}

} // namespace openstitch::desktop_v2
