// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>
#include <utility>

namespace openstitch {

// Taxonomie des erreurs (cahier des charges §26). `message` est montrable
// tel quel à l'utilisateur ; `detail` est réservé aux logs techniques.
enum class ErrorCategory {
    UserInput,
    InvalidFile,
    UnsupportedFormat,
    OperationImpossible,
    PhysicalWarning,
    Internal,
    OutOfMemory,
    Cancelled,
};

struct Error {
    ErrorCategory category{ErrorCategory::Internal};
    std::string message;
    std::string detail;
    // Code stable lisible par machine (« E_ZIP_DUPLICATE_ENTRY »...) et conseil de correction ;
    // vides pour les erreurs historiques sans code (HP-QA-013).
    std::string code;
    std::string hint;
};

// Les API publiques des bibliothèques retournent Result<T> ; les exceptions
// ne traversent pas les frontières de bibliothèque (ADR-011).
template <typename T> using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCategory category, std::string message,
                                                 std::string detail = {}) {
    return std::unexpected(Error{category, std::move(message), std::move(detail)});
}

// Erreur codée : `code` (préfixe « E_ ») stable, `hint` = que faire pour s'en sortir.
[[nodiscard]] inline std::unexpected<Error> fail_coded(ErrorCategory category, std::string code,
                                                       std::string message, std::string hint,
                                                       std::string detail = {}) {
    return std::unexpected(
        Error{category, std::move(message), std::move(detail), std::move(code), std::move(hint)});
}

} // namespace openstitch
