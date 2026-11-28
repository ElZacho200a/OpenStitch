// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace openstitch::scripting::detail {

// Code Python exécuté une fois par lancement de script (voir prelude.cpp). Définit
// `_openstitch_run(source, inspect_json, selected_ids) -> str` (JSON du résultat) et la
// classe `ScriptTimeout` levée par la fonction de trace du délai.
extern const char* const kPrelude;

} // namespace openstitch::scripting::detail
