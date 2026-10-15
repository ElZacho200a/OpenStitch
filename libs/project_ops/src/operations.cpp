// SPDX-License-Identifier: Apache-2.0
// STUB : remplacé par l'implémentation du dispatcher (agent « ops »).
#include "openstitch/project_ops/operations.hpp"

#include "openstitch/project_ops/errors.hpp"

namespace openstitch::project_ops {

std::string BatchReport::to_json() const {
    return "{}";
}

BatchReport apply_batch(document::Project&, std::string_view, const BatchOptions&) {
    BatchReport r;
    r.error_code = code::kInternal;
    r.error_message = "apply_batch non implémenté";
    return r;
}

BatchReport apply_operation(document::Project& project, std::string_view op_json,
                            const BatchOptions& options) {
    return apply_batch(project, op_json, options);
}

std::string operations_catalog_json() {
    return "{}";
}

} // namespace openstitch::project_ops
