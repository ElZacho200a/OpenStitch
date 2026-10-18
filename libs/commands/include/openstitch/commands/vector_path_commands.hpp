// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::commands {

// Réécriture de la géométrie d'objets vectoriels : « Lisser » (HP-VEC-007) et « Convertir en
// courbes » (HP-VEC-008). La géométrie est calculée à la construction (fonctions pures de
// libs/geometry) ; `apply` pose le résultat, `revert` rend EXACTEMENT les chemins d'origine
// (instantané pris à l'`apply`). Une commande couvre tous les objets de la sélection : un seul
// pas d'annulation.
class RewriteVectorPathsCommand : public ICommand {
public:
    struct Entry {
        ObjectId object;
        std::vector<geometry::PathSet> paths; // géométrie APRÈS l'opération
    };

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;

protected:
    explicit RewriteVectorPathsCommand(std::vector<Entry> entries) : entries_(std::move(entries)) {}

private:
    std::vector<Entry> entries_;
    std::vector<Entry> before_;
};

// Lisse les chemins (`geometry::smooth_path_set`, écart maximal `tolerance`).
class SmoothVectorPathsCommand final : public RewriteVectorPathsCommand {
public:
    SmoothVectorPathsCommand(std::vector<Entry> entries, Micrometers tolerance)
        : RewriteVectorPathsCommand(std::move(entries)), tolerance_(tolerance) {}
    [[nodiscard]] std::string name() const override { return "Lisser"; }
    [[nodiscard]] Micrometers tolerance() const { return tolerance_; }

private:
    Micrometers tolerance_;
};

// Convertit les polygones en courbes de Bézier (`geometry::convert_to_curves`).
class ConvertToCurvesCommand final : public RewriteVectorPathsCommand {
public:
    explicit ConvertToCurvesCommand(std::vector<Entry> entries)
        : RewriteVectorPathsCommand(std::move(entries)) {}
    [[nodiscard]] std::string name() const override { return "Convertir en courbes"; }
};

// Tolérance de lissage par défaut : 0,2 mm.
inline constexpr Micrometers kDefaultSmoothTolerance{200};

// Fabriques : calculent la géométrie maintenant, ne modifient pas le document. Commande nulle
// (avec `error`) si aucun objet de `ids` n'existe ou si l'opération ne change rien.
// `resulting_objects` = nombre d'objets réellement modifiés.
[[nodiscard]] VectorOpResult make_smooth_command(const document::Project& project,
                                                 const std::vector<ObjectId>& ids,
                                                 Micrometers tolerance = kDefaultSmoothTolerance);
[[nodiscard]] VectorOpResult make_convert_to_curves_command(const document::Project& project,
                                                            const std::vector<ObjectId>& ids);

} // namespace openstitch::commands
