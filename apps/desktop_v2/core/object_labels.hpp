// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- identifiants stables des types de points (l'index du variant `StitchParams` ne
// change jamais : les alternatives sont ajoutées en fin). Qt-free : la traduction est faite par
// l'interface, la logique (tri, filtres, compagnon IA) travaille sur ces identifiants.

#include <string_view>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::desktop_v2 {

[[nodiscard]] std::string_view stitchTypeId(const document::EmbroideryObject& object);

} // namespace openstitch::desktop_v2
