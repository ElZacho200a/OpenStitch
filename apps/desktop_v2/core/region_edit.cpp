// SPDX-License-Identifier: Apache-2.0
#include "region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

#include "openstitch/segmentation/color_merge.hpp"

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/replace_project_command.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kIslandMaxDeltaE = 25.0; // un îlot ne fusionne que dans une couleur assez proche

[[nodiscard]] const segmentation::Segmentation* segmentationOf(const document::Project& p) {
    return p.segmentation ? &*p.segmentation : nullptr;
}

struct HoleFix {
    RegionId target;
    std::vector<std::uint32_t> pixels;
};

// Un trou est un composant de fond (label 0) entièrement intérieur à l'image.
// On choisit la région qui partage le plus de bord, puis l'identifiant le plus petit.
std::vector<HoleFix> planSmallHoles(const segmentation::Segmentation& seg, std::size_t maxPixels) {
    std::vector<HoleFix> fixes;
    if (seg.width <= 0 || seg.height <= 0 || maxPixels == 0) {
        return fixes;
    }
    const auto w = static_cast<std::size_t>(seg.width);
    const auto h = static_cast<std::size_t>(seg.height);
    if (h > std::numeric_limits<std::uint32_t>::max() / w || seg.labels.size() != w * h) {
        return fixes;
    }
    std::vector<std::uint8_t> seen(seg.labels.size(), 0);
    for (std::size_t first = 0; first < seg.labels.size(); ++first) {
        if (seg.labels[first] != 0 || seen[first]) {
            continue;
        }
        std::vector<std::uint32_t> cluster{static_cast<std::uint32_t>(first)};
        std::map<std::uint32_t, std::size_t> borders;
        bool outside = false;
        seen[first] = 1;
        for (std::size_t cursor = 0; cursor < cluster.size(); ++cursor) {
            const auto at = cluster[cursor];
            const auto x = static_cast<int>(at % w);
            const auto y = static_cast<int>(at / w);
            if (x == 0 || y == 0 || x == seg.width - 1 || y == seg.height - 1) {
                outside = true;
            }
            const auto visit = [&](int nx, int ny) {
                if (nx < 0 || ny < 0 || nx >= seg.width || ny >= seg.height) {
                    return;
                }
                const auto next = static_cast<std::size_t>(ny) * w + nx;
                const auto label = seg.labels[next];
                if (label == 0 && !seen[next]) {
                    seen[next] = 1;
                    cluster.push_back(static_cast<std::uint32_t>(next));
                } else if (label != 0 && seg.find(RegionId{label}) != nullptr) {
                    ++borders[label];
                }
            };
            visit(x - 1, y);
            visit(x + 1, y);
            visit(x, y - 1);
            visit(x, y + 1);
        }
        if (outside || cluster.size() > maxPixels) {
            continue;
        }
        RegionId target;
        std::size_t bestBorder = 0;
        for (const auto& [label, length] : borders) {
            if (length > bestBorder) {
                bestBorder = length;
                target = RegionId{label};
            }
        }
        if (target.valid()) {
            fixes.push_back({target, std::move(cluster)});
        }
    }
    return fixes;
}

class FillSmallHolesCommand final : public commands::ICommand {
public:
    explicit FillSmallHolesCommand(std::vector<HoleFix> holes) : holes_(std::move(holes)) {}

    void apply(document::Project& project) override {
        auto& seg = *project.segmentation;
        for (const auto& hole : holes_) {
            for (const auto pixel : hole.pixels) {
                seg.labels[pixel] = static_cast<std::uint32_t>(hole.target.value);
            }
            seg.find(hole.target)->pixel_count += hole.pixels.size();
        }
    }
    void revert(document::Project& project) override {
        auto& seg = *project.segmentation;
        for (const auto& hole : holes_) {
            for (const auto pixel : hole.pixels) {
                seg.labels[pixel] = 0;
            }
            seg.find(hole.target)->pixel_count -= hole.pixels.size();
        }
    }
    [[nodiscard]] std::string name() const override { return "Boucher les petits trous"; }

private:
    std::vector<HoleFix> holes_;
};

// Pixels repeints par le pinceau, avec leur ancienne étiquette ; les comptes de pixels des régions
// touchées sont recalculés à l'application et à l'annulation (ordre des étiquettes : déterministe).
struct PaintedPixel {
    std::uint32_t index{0};
    std::uint32_t before{0};
};

class PaintRegionCommand final : public commands::ICommand {
public:
    PaintRegionCommand(std::uint32_t target, std::vector<PaintedPixel> pixels, bool erase)
        : target_(target), pixels_(std::move(pixels)), erase_(erase) {
        for (const auto& p : pixels_) {
            if (p.before != 0) {
                ++lost_[p.before];
            }
        }
    }

    void apply(document::Project& project) override {
        auto& seg = *project.segmentation;
        const std::uint32_t to = erase_ ? 0 : target_;
        for (const auto& p : pixels_) {
            seg.labels[p.index] = to;
        }
        adjust(seg, +1);
    }
    void revert(document::Project& project) override {
        auto& seg = *project.segmentation;
        for (const auto& p : pixels_) {
            seg.labels[p.index] = p.before;
        }
        adjust(seg, -1);
    }
    [[nodiscard]] std::string name() const override {
        return erase_ ? "Effacer au pinceau (région)" : "Peindre une région au pinceau";
    }

private:
    // sign = +1 : application (la cible gagne tous les pixels repeints, les anciennes régions les
    // perdent) ; -1 : l'inverse exact. En mode gomme, `lost_` ne compte que la cible elle-même.
    void adjust(segmentation::Segmentation& seg, int sign) const {
        for (const auto& [label, count] : lost_) {
            if (auto* region = seg.find(RegionId{label})) {
                region->pixel_count =
                    sign > 0 ? region->pixel_count - count : region->pixel_count + count;
            }
        }
        if (!erase_) {
            if (auto* region = seg.find(RegionId{target_})) {
                region->pixel_count = sign > 0 ? region->pixel_count + pixels_.size()
                                               : region->pixel_count - pixels_.size();
            }
        }
    }

    std::uint32_t target_;
    std::vector<PaintedPixel> pixels_;
    bool erase_;
    std::map<std::uint32_t, std::size_t> lost_;
};

ServiceResult noSegmentation() {
    return {false, "Ce projet n'a pas de segmentation : numérisez d'abord une image."};
}

} // namespace

std::vector<RegionInfo> listRegions(const document::Project& project) {
    std::vector<RegionInfo> out;
    const auto* seg = segmentationOf(project);
    if (seg == nullptr) {
        return out;
    }
    for (const auto id : segmentation::all_regions(*seg)) {
        if (const auto* region = seg->find(id)) {
            out.push_back({id.value, region->rgb, region->pixel_count});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RegionInfo& a, const RegionInfo& b) {
        return a.pixels != b.pixels ? a.pixels > b.pixels : a.id < b.id;
    });
    return out;
}

std::optional<std::uint64_t> regionAtPixel(const document::Project& project, int x, int y) {
    const auto* seg = segmentationOf(project);
    if (seg == nullptr) {
        return std::nullopt;
    }
    const auto id = segmentation::region_at(*seg, x, y);
    return id && id->valid() ? std::optional<std::uint64_t>(id->value) : std::nullopt;
}

std::vector<std::uint64_t> neighbourRegions(const document::Project& project, std::uint64_t id) {
    std::vector<std::pair<std::size_t, std::uint64_t>> found;
    if (const auto* seg = segmentationOf(project)) {
        for (const auto& border : segmentation::region_adjacency(*seg)) {
            if (border.a.value == id) {
                found.emplace_back(border.length, border.b.value);
            } else if (border.b.value == id) {
                found.emplace_back(border.length, border.a.value);
            }
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::vector<std::uint64_t> out;
    for (const auto& f : found) {
        out.push_back(f.second);
    }
    return out;
}

RegionMapImage renderRegionMap(const document::Project& project,
                               std::optional<std::uint64_t> highlight) {
    RegionMapImage out;
    const auto* seg = segmentationOf(project);
    if (seg == nullptr) {
        return out;
    }
    std::optional<RegionId> marked;
    if (highlight) {
        marked = RegionId{*highlight};
    }
    const auto map = segmentation::render_map(*seg, marked);
    out.width = map.width;
    out.height = map.height;
    out.rgba = map.rgba;
    return out;
}

ServiceResult mergeRegionInto(DocumentSession& session, std::uint64_t keep, std::uint64_t absorb) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    if (keep == absorb) {
        return {false, "Choisissez deux régions différentes."};
    }
    if (seg->find(RegionId{keep}) == nullptr || seg->find(RegionId{absorb}) == nullptr) {
        return {false, "Région introuvable (déjà fusionnée ou supprimée)."};
    }
    session.execute(
        std::make_unique<commands::MergeRegionsCommand>(RegionId{keep}, RegionId{absorb}));
    return {true, {}};
}

ServiceResult deleteRegion(DocumentSession& session, std::uint64_t id) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    if (seg->find(RegionId{id}) == nullptr) {
        return {false, "Région introuvable (déjà fusionnée ou supprimée)."};
    }
    if (seg->region_count() <= 1) {
        return {false, "C'est la dernière région : il ne resterait rien à broder."};
    }
    session.execute(std::make_unique<commands::RemoveRegionCommand>(RegionId{id}));
    return {true, {}};
}

ServiceResult recolorRegion(DocumentSession& session, std::uint64_t id,
                            std::array<std::uint8_t, 3> rgb) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    const auto* region = seg->find(RegionId{id});
    if (region == nullptr) {
        return {false, "Région introuvable (déjà fusionnée ou supprimée)."};
    }
    if (region->rgb == rgb) {
        return {false, "Cette région a déjà cette couleur."};
    }
    session.execute(std::make_unique<commands::RecolorRegionCommand>(RegionId{id}, rgb));
    return {true, {}};
}

ServiceResult mergeRegionColors(DocumentSession& session, std::uint64_t source,
                                std::uint64_t target) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    const auto* from = seg->find(RegionId{source});
    const auto* into = seg->find(RegionId{target});
    if (from == nullptr || into == nullptr) {
        return {false, "Couleur introuvable : choisissez deux régions existantes."};
    }
    if (from->rgb == into->rgb) {
        return {false, "Les régions portent déjà la même couleur."};
    }
    const std::array<std::array<std::uint8_t, 3>, 1> sources{from->rgb};
    auto plan = segmentation::plan_merge_colors(*seg, sources, into->rgb, true);
    if (plan.empty()) {
        return {false, "Aucune couleur à fusionner."};
    }
    session.execute(std::make_unique<commands::MergeColorsCommand>(std::move(plan)));
    return {true, {}};
}

ServiceResult fillSmallHoles(DocumentSession& session, std::size_t maxPixels) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    if (maxPixels == 0) {
        return {false, "La taille maximale doit être au moins un pixel."};
    }
    auto plan = planSmallHoles(*seg, maxPixels);
    if (plan.empty()) {
        return {false, "Aucun petit trou fermé : le fond extérieur reste intact."};
    }
    session.execute(std::make_unique<FillSmallHolesCommand>(std::move(plan)));
    return {true, {}};
}

ServiceResult paintRegionStroke(DocumentSession& session, std::uint64_t regionId,
                                const std::vector<std::pair<double, double>>& centresPx,
                                double radiusPx, bool erase) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    const RegionId target{regionId};
    if (!target.valid() || seg->find(target) == nullptr) {
        return {false, "Choisissez d'abord la région à peindre (outil Régions)."};
    }
    if (!std::isfinite(radiusPx) || radiusPx < 0.5 || radiusPx > 200.0) {
        return {false, "Rayon du pinceau : entre 0,5 et 200 pixels."};
    }
    if (centresPx.empty()) {
        return {false, "Le trait est vide."};
    }
    const std::uint32_t label = static_cast<std::uint32_t>(regionId);
    const double r2 = radiusPx * radiusPx;
    const int reach = static_cast<int>(std::ceil(radiusPx));
    std::vector<bool> seen(seg->labels.size(), false);
    std::vector<PaintedPixel> pixels;
    for (const auto& [cx, cy] : centresPx) {
        if (!std::isfinite(cx) || !std::isfinite(cy)) {
            return {false, "Position de pinceau invalide."};
        }
        const int x0 = static_cast<int>(std::floor(cx)) - reach;
        const int y0 = static_cast<int>(std::floor(cy)) - reach;
        for (int y = std::max(0, y0); y <= std::min(seg->height - 1, y0 + 2 * reach); ++y) {
            for (int x = std::max(0, x0); x <= std::min(seg->width - 1, x0 + 2 * reach); ++x) {
                const double dx = (x + 0.5) - cx;
                const double dy = (y + 0.5) - cy;
                if (dx * dx + dy * dy > r2) {
                    continue;
                }
                const auto index =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(seg->width) +
                    static_cast<std::size_t>(x);
                if (seen[index]) {
                    continue;
                }
                seen[index] = true;
                const std::uint32_t before = seg->labels[index];
                const bool change = erase ? before == label : before != label;
                if (change) {
                    pixels.push_back({static_cast<std::uint32_t>(index), before});
                }
            }
        }
    }
    if (pixels.empty()) {
        return {false, erase ? "Rien à effacer : le trait ne touche pas cette région."
                             : "Rien à peindre : le trait est déjà entièrement dans cette région."};
    }
    // Une région voisine ne doit pas disparaître en silence (une région vide casserait la carte).
    std::map<std::uint32_t, std::size_t> lost;
    for (const auto& p : pixels) {
        if (p.before != 0) {
            ++lost[p.before];
        }
    }
    for (const auto& [other, count] : lost) {
        if (const auto* region = seg->find(RegionId{other});
            region != nullptr && count >= region->pixel_count) {
            return {false, "Le trait effacerait entièrement une région : utilisez « Supprimer »."};
        }
    }
    session.execute(std::make_unique<PaintRegionCommand>(label, std::move(pixels), erase));
    return {true, {}};
}

ServiceResult cleanSmallIslands(DocumentSession& session, std::size_t minPixels) {
    const auto* seg = segmentationOf(session.project());
    if (seg == nullptr) {
        return noSegmentation();
    }
    if (minPixels == 0) {
        return {false, "Indiquez une taille minimale d'au moins 1 pixel."};
    }
    const auto regions = listRegions(session.project());
    const bool hasSmall = std::any_of(regions.begin(), regions.end(),
                                      [&](const RegionInfo& r) { return r.pixels < minPixels; });
    if (!hasSmall) {
        return {false, "Aucune région plus petite que cette taille : rien à nettoyer."};
    }
    session.execute(std::make_unique<commands::CleanIslandsCommand>(minPixels, std::nullopt,
                                                                    kIslandMaxDeltaE, 0.0));
    return {true, {}};
}

Result<Regeneration> regenerateObjects(std::shared_ptr<const document::Project> project,
                                       std::uint64_t revision, const image::Image& working,
                                       const project_ops::DigitizeSettings& settings,
                                       const TaskContext& task) {
    if (!project || !project->segmentation) {
        return fail(ErrorCategory::OperationImpossible, "Pas de segmentation à régénérer",
                    "Numérisez d'abord une image.");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    auto copy = std::make_shared<document::Project>(*project);
    Regeneration out;
    out.objectsBefore = copy->embroidery_objects.size();
    out.revision = revision;

    // Même règle de fond que la numérisation : région candidate recommandée, ou choix explicite.
    const auto candidate = segmentation::background_candidate(*copy->segmentation);
    const bool skipLargest = settings.skip_background < 0 ? (candidate && candidate->recommended)
                                                          : (settings.skip_background != 0);

    copy->vector_objects.clear();
    copy->embroidery_objects.clear();
    const project_ops::DigitizeSource source{&*copy->segmentation, &working, copy->mm_per_px};
    auto produced = project_ops::digitize_objects(source, copy->object_ids, settings, skipLargest);
    if (!produced) {
        return std::unexpected(produced.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    for (auto& v : produced->objects.vectors) {
        copy->vector_objects.push_back(std::move(v));
    }
    for (auto& e : produced->objects.embroideries) {
        copy->embroidery_objects.push_back(std::move(e));
    }
    if (copy->embroidery_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun objet de broderie généré",
                    "Les régions restantes sont trop petites ou ne forment que le fond.");
    }
    out.objectsAfter = copy->embroidery_objects.size();
    out.after = std::move(copy);
    return out;
}

ServiceResult applyRegeneration(DocumentSession& session, const Regeneration& regeneration) {
    if (!regeneration.after) {
        return {false, "Rien à appliquer."};
    }
    if (regeneration.revision != session.revision()) {
        return {false, "Le document a changé depuis le calcul : relancez la régénération."};
    }
    session.execute(std::make_unique<commands::ReplaceProjectCommand>(
        "Régénérer les objets depuis les régions", session.snapshot(), regeneration.after));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
