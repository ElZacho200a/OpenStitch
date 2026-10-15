// SPDX-License-Identifier: Apache-2.0
// STUB : contrôle qualité (agent « quality »).
#include "openstitch/project_ops/quality.hpp"

namespace openstitch::project_ops {

Result<QualityReport> build_quality_report(const document::Project&, const QualityOptions&) {
    return fail(ErrorCategory::Internal, "build_quality_report non implémenté");
}

Result<std::string> render_quality_svg(const document::Project&, const QualitySvgOptions&) {
    return fail(ErrorCategory::Internal, "render_quality_svg non implémenté");
}

} // namespace openstitch::project_ops
