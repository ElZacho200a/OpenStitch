// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- index de sélection au canevas : retrouve l'objet de broderie sous un point (mm) ou
// dans un rectangle, à partir des brins réellement cousus (donc de la séquence EFFECTIVE, retouches
// comprises). Sans Qt. Repère : celui du rendu (mm « écran », Y vers le bas).

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::desktop_v2 {

struct BoxMm {
    double x0{0}, y0{0}, x1{0}, y1{0};
    [[nodiscard]] bool intersects(const BoxMm& o) const {
        return x0 <= o.x1 && o.x0 <= x1 && y0 <= o.y1 && o.y0 <= y1;
    }
};

// L'identifiant d'objet est encodé dans la couleur (24 bits) des segments passés à `build` : cela
// permet de réutiliser `stitch_render::build_thread_segments` sans dupliquer la conversion d'unités
// ni le retournement de l'axe Y.
[[nodiscard]] std::array<std::uint8_t, 3> encode_id(std::uint64_t id);
[[nodiscard]] std::uint64_t decode_id(const std::array<std::uint8_t, 3>& rgb);
inline constexpr std::uint64_t kMaxEncodableId = (1ULL << 24) - 1;

class HitIndex {
public:
    [[nodiscard]] static HitIndex
    build(const std::vector<stitch_render::ThreadSegment>& idSegments);

    // Objet cousu en dernier (donc visuellement dessus) à moins de `tolMm` du point.
    [[nodiscard]] std::optional<std::uint64_t> pick(double xMm, double yMm, double tolMm) const;
    // Objets dont l'emprise rencontre le rectangle, par identifiant croissant.
    [[nodiscard]] std::vector<std::uint64_t> inRect(const BoxMm& box) const;
    // Objets dont l'emprise est ENTIÈREMENT dans le rectangle (sélection « fenêtre »), id
    // croissant.
    [[nodiscard]] std::vector<std::uint64_t> inside(const BoxMm& box) const;
    [[nodiscard]] std::optional<BoxMm> boundsOf(std::uint64_t id) const;
    // Milieu du DERNIER brin cousu de l'objet : un point qui lui appartient (le centre de son
    // emprise peut tomber dans un trou).
    [[nodiscard]] std::optional<std::pair<double, double>> samplePoint(std::uint64_t id) const;
    [[nodiscard]] std::size_t segmentCount() const { return segments_.size(); }
    [[nodiscard]] std::size_t objectCount() const { return bounds_.size(); }

private:
    struct Seg {
        float ax, ay, bx, by;
        std::uint64_t id;
    };
    static constexpr double kCell = 4.0;
    std::vector<Seg> segments_;
    std::map<std::pair<int, int>, std::vector<std::uint32_t>> grid_;
    std::map<std::uint64_t, BoxMm> bounds_;
    std::map<std::uint64_t, std::uint32_t> lastSegment_;
};

} // namespace openstitch::desktop_v2
