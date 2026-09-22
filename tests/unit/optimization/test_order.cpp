// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "openstitch/optimization/order.hpp"

using namespace openstitch;
using namespace openstitch::optimization;

namespace {

OrderItem item(std::uint64_t id, std::array<std::uint8_t, 3> rgb, std::int32_t x, std::int32_t y,
               bool locked = false) {
    return OrderItem{ObjectId{id}, rgb, Vec2um{Micrometers{x}, Micrometers{y}}, locked};
}

std::vector<std::uint64_t> ids(const std::vector<ObjectId>& v) {
    std::vector<std::uint64_t> out;
    for (const auto& id : v) {
        out.push_back(id.value);
    }
    return out;
}

const std::array<std::uint8_t, 3> red{200, 0, 0};
const std::array<std::uint8_t, 3> blue{0, 0, 200};

} // namespace

TEST_CASE("cout : deplacements et changements de couleur") {
    // rouge(0,0) bleu(3,0) rouge(6,0) : 2 changements, 6 mm de trajet.
    const std::vector<OrderItem> items = {item(1, red, 0, 0), item(2, blue, 3'000, 0),
                                          item(3, red, 6'000, 0)};
    const auto cost = compute_cost(items);
    CHECK(cost.travel_um == 6'000.0);
    CHECK(cost.color_changes == 2);
}

TEST_CASE("ByColor : regroupe les couleurs, reduit les changements") {
    const std::vector<OrderItem> items = {item(1, red, 0, 0), item(2, blue, 1'000, 0),
                                          item(3, red, 2'000, 0), item(4, blue, 3'000, 0)};
    const auto order = optimize_order(items, OrderStrategy::ByColor);
    CHECK(ids(order) == std::vector<std::uint64_t>{1, 3, 2, 4}); // rouges puis bleus
}

TEST_CASE("ByProximity : plus proche voisin") {
    // Points en desordre ; proximite depuis le premier.
    const std::vector<OrderItem> items = {item(1, red, 0, 0), item(2, red, 10'000, 0),
                                          item(3, red, 1'000, 0), item(4, red, 9'000, 0)};
    const auto order = optimize_order(items, OrderStrategy::ByProximity);
    // 0 -> 1000(id3) -> 9000(id4) -> 10000(id2)
    CHECK(ids(order) == std::vector<std::uint64_t>{1, 3, 4, 2});
}

TEST_CASE("optimisation ameliore (ou egale) le cout") {
    const std::vector<OrderItem> items = {item(1, red, 0, 0), item(2, blue, 1'000, 0),
                                          item(3, red, 2'000, 0), item(4, blue, 3'000, 0)};
    const double before = compute_cost(items).score();

    std::vector<OrderItem> reordered;
    const auto order = optimize_order(items, OrderStrategy::ColorThenProximity);
    for (const auto& id : order) {
        for (const auto& it : items) {
            if (it.id == id) {
                reordered.push_back(it);
            }
        }
    }
    CHECK(compute_cost(reordered).score() <= before);
}

TEST_CASE("objets verrouilles gardent leur position") {
    // id2 verrouille en position 1 : il ne bouge pas.
    const std::vector<OrderItem> items = {item(1, blue, 0, 0), item(2, red, 1'000, 0, true),
                                          item(3, blue, 2'000, 0)};
    const auto order = optimize_order(items, OrderStrategy::ByColor);
    CHECK(order[1].value == 2); // le verrou reste en 2e position
}

TEST_CASE("strategie Document ne change rien") {
    const std::vector<OrderItem> items = {item(3, red, 0, 0), item(1, blue, 1'000, 0),
                                          item(2, red, 2'000, 0)};
    CHECK(ids(optimize_order(items, OrderStrategy::Document)) ==
          std::vector<std::uint64_t>{3, 1, 2});
}

TEST_CASE("deterministe") {
    const std::vector<OrderItem> items = {item(1, red, 0, 0), item(2, blue, 5'000, 3'000),
                                          item(3, red, 2'000, 8'000)};
    CHECK(ids(optimize_order(items, OrderStrategy::ColorThenProximity)) ==
          ids(optimize_order(items, OrderStrategy::ColorThenProximity)));
}

// Lot C (audit marine) : pour qu'un remplissage qui déborde sur ses voisins
// reste propre, les grandes régions « de fond » (mer, ciel) sont cousues
// AVANT les petites régions posées dessus.
TEST_CASE("LayeredColorThenProximity : couleurs des grandes regions d'abord") {
    auto sea = item(1, blue, 0, 0);
    sea.area_mm2 = 5'000.0;
    auto boat = item(2, red, 1'000, 0);
    boat.area_mm2 = 300.0;
    auto bird = item(3, red, 50'000, 0);
    bird.area_mm2 = 20.0;
    // Ordre document : rouge d'abord (première apparition) -- la stratégie
    // par couleur simple coudrait le bateau AVANT la mer qu'il chevauche.
    const std::vector<OrderItem> items = {boat, bird, sea};
    CHECK(ids(optimize_order(items, OrderStrategy::ColorThenProximity)) ==
          std::vector<std::uint64_t>{2, 3, 1});
    const auto order = optimize_order(items, OrderStrategy::LayeredColorThenProximity);
    CHECK(ids(order) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(compute_cost({sea, boat, bird}).color_changes == 1); // regroupement conservé
}

TEST_CASE("LayeredColorThenProximity : a couleur egale, grandes avant petites puis proximite") {
    std::vector<OrderItem> items = {item(1, blue, 0, 0), item(2, blue, 40'000, 0),
                                    item(3, blue, 2'000, 0), item(4, blue, 38'000, 0)};
    items[0].area_mm2 = 10.0;    // petite, près de l'origine
    items[1].area_mm2 = 4'000.0; // grande (fond)
    items[2].area_mm2 = 12.0;
    items[3].area_mm2 = 3'000.0; // grande
    const auto order = optimize_order(items, OrderStrategy::LayeredColorThenProximity);
    // Grandes (>= 25 % de la plus grande du groupe) par aire décroissante,
    // puis les petites par proximité depuis la dernière grande.
    CHECK(ids(order) == std::vector<std::uint64_t>{2, 4, 3, 1});
}

TEST_CASE("LayeredColorThenProximity : les objets verrouilles gardent leur place") {
    std::vector<OrderItem> items = {item(1, red, 0, 0, true), item(2, red, 1'000, 0),
                                    item(3, blue, 2'000, 0)};
    items[0].area_mm2 = 1.0;
    items[1].area_mm2 = 1.0;
    items[2].area_mm2 = 900.0;
    const auto order = optimize_order(items, OrderStrategy::LayeredColorThenProximity);
    CHECK(order.front().value == 1);
    CHECK(ids(order) == std::vector<std::uint64_t>{1, 3, 2});
}
