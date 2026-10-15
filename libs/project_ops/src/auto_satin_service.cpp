// SPDX-License-Identifier: Apache-2.0
// STUB : service auto-satin (agent « geometry »).
#include "openstitch/project_ops/auto_satin_service.hpp"

namespace openstitch::project_ops {

Result<AutoSatinPlan> plan_auto_satin(const document::Project&, ObjectId,
                                      const AutoSatinPlanRequest&) {
    return fail(ErrorCategory::Internal, "plan_auto_satin non implémenté");
}

} // namespace openstitch::project_ops
