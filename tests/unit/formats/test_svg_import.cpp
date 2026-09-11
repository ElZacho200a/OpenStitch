// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>

#include "openstitch/formats/svg_import.hpp"

using namespace openstitch;
using namespace openstitch::formats;
using openstitch::geometry::NodeType;

namespace {

std::vector<std::uint8_t> to_bytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

double mm(Micrometers um) { return static_cast<double>(um.value) / 1000.0; }

}  // namespace

// Toutes les formes de test utilisent viewBox="0 0 100 100" + width="10mm"
// height="10mm" -> échelle EXACTE de 0,1 mm par unité utilisateur, pour
// vérifier les positions à l'octet près plutôt qu'à une tolérance flottante.
//
// Délimiteur de chaîne brute personnalisé (R"svg(...)svg", pas R"(...)")
// : le contenu contient des `)"` littéraux (ex. `translate(10,0)"` --
// attribut transform suivi de la fermeture du guillemet), qui terminerait
// prématurément une chaîne brute au délimiteur par défaut.

TEST_CASE("svg : rectangle simple -- position, taille et centrage exacts") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <rect x="0" y="0" width="100" height="100"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& outer = result->objects.front().outer;
    REQUIRE(outer.nodes.size() == 4);
    CHECK(outer.closed);

    // Le rectangle couvre TOUT le viewBox -> centré exactement sur l'origine
    // (Vec2um : "origine au centre du canevas") -> coins à +-5mm.
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& n : outer.nodes) {
        minX = std::min(minX, mm(n.pos.x));
        maxX = std::max(maxX, mm(n.pos.x));
        minY = std::min(minY, mm(n.pos.y));
        maxY = std::max(maxY, mm(n.pos.y));
    }
    CHECK(minX == Catch::Approx(-5.0).margin(0.001));
    CHECK(maxX == Catch::Approx(5.0).margin(0.001));
    CHECK(minY == Catch::Approx(-5.0).margin(0.001));
    CHECK(maxY == Catch::Approx(5.0).margin(0.001));
}

TEST_CASE("svg : axe Y inverse (SVG bas -> modele haut, Vec2um Y vers le haut)") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <path d="M 50 0 L 50 100"/>
    </svg>)svg";  // point HAUT du fichier (y=0) puis point BAS (y=100)
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& nodes = result->objects.front().outer.nodes;
    REQUIRE(nodes.size() == 2);
    // y=0 en SVG (visuellement en HAUT du dessin) doit donner le Y modele le
    // PLUS GRAND (modele Y vers le haut) -- l'inverse de y=100 (bas du dessin).
    CHECK(mm(nodes[0].pos.y) > mm(nodes[1].pos.y));
}

TEST_CASE("svg : chemin cubique -- tangentes exactes (pas d'approximation)") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <path d="M 0 0 C 10 0 20 10 20 20"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& nodes = result->objects.front().outer.nodes;
    REQUIRE(nodes.size() == 2);
    CHECK(nodes[0].type == NodeType::Corner);  // premier noeud (M) : jamais de tangente sortante propre
    REQUIRE(nodes[0].tan_out.has_value());
    REQUIRE(nodes[1].tan_in.has_value());
    // C1=(10,0) relatif à M(0,0) -> (1.0mm, 0mm) apres echelle 0.1mm/unite ;
    // Y neutre ici (delta Y du controle = 0), donc pas d'inversion visible.
    CHECK(mm(nodes[0].tan_out->x) == Catch::Approx(1.0).margin(0.001));
    CHECK(mm(nodes[0].tan_out->y) == Catch::Approx(0.0).margin(0.001));
    // C2=(20,10), noeud final=(20,20) -> tan_in = C2 - end = (0,-10) unites
    // -> (0mm, -1mm) SVG, puis Y inverse -> (0mm, +1mm) modele.
    CHECK(mm(nodes[1].tan_in->x) == Catch::Approx(0.0).margin(0.001));
    CHECK(mm(nodes[1].tan_in->y) == Catch::Approx(1.0).margin(0.001));
}

TEST_CASE("svg : cercle -- rayon exact sur les 4 points cardinaux") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <circle cx="50" cy="50" r="30"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& nodes = result->objects.front().outer.nodes;
    REQUIRE(nodes.size() == 4);
    for (const auto& n : nodes) {
        CHECK(n.type == NodeType::Smooth);
        const double dist = std::hypot(mm(n.pos.x), mm(n.pos.y));  // centre du cercle == centre du canevas ici
        CHECK(dist == Catch::Approx(3.0).margin(0.001));  // r=30 unites * 0.1mm/unite = 3mm
    }
}

TEST_CASE("svg : groupe avec transform translate -- compose correctement") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <g transform="translate(10,0)"><rect x="0" y="0" width="10" height="10"/></g>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& nodes = result->objects.front().outer.nodes;
    double minX = 1e9;
    for (const auto& n : nodes) minX = std::min(minX, mm(n.pos.x));
    // rect en x=[0,10], translate(10,0) -> x=[10,20] en unites -> [1mm,2mm]
    // avant centrage (-5mm) -> [-4mm,-3mm].
    CHECK(minX == Catch::Approx(-4.0).margin(0.001));
}

TEST_CASE("svg : transform rotate(180) sur un rectangle -- symetrique par rapport a l'origine") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <rect x="0" y="0" width="10" height="10"/>
        <g transform="rotate(180)"><rect x="0" y="0" width="10" height="10"/></g>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    // Les deux rectangles ne se touchent pas (rotate(180) autour de
    // l'origine SVG, pas du centre du rect) -- deux morceaux disjoints.
    REQUIRE(result->objects.size() == 2);
}

TEST_CASE("svg : deux sous-chemins opposes dans un meme <path> -- exterieur + trou") {
    // Carre exterieur + carre interieur PLUS PETIT -> geometry::
    // clean_to_path_sets doit reconstruire un seul PathSet avec un trou
    // (regle pair-impair, peu importe le sens de parcours des deux carres).
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <path d="M 0 0 L 100 0 L 100 100 L 0 100 Z M 30 30 L 30 70 L 70 70 L 70 30 Z"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    CHECK(result->objects.front().holes.size() == 1);
}

TEST_CASE("svg : element non pris en charge -- diagnostic explicite, import du reste non bloque") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <text x="10" y="10">Bonjour</text>
        <rect x="0" y="0" width="50" height="50"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    CHECK(result->objects.size() == 1);  // le rect importe malgre le <text> voisin
    REQUIRE_FALSE(result->warnings.empty());
    bool foundTextWarning = false;
    for (const auto& w : result->warnings) {
        if (w.find("text") != std::string::npos) foundTextWarning = true;
    }
    CHECK(foundTextWarning);
}

TEST_CASE("svg : polygone ferme -- nombre de noeuds et fermeture corrects") {
    const std::string svg = R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm">
        <polygon points="0,0 100,0 50,100"/>
    </svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE(result.has_value());
    REQUIRE(result->objects.size() == 1);
    const auto& outer = result->objects.front().outer;
    CHECK(outer.nodes.size() == 3);
    CHECK(outer.closed);
}

TEST_CASE("svg : aucun element racine <svg> -- erreur propre") {
    const auto result = decode_svg(to_bytes("<notsvg></notsvg>"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("svg : XML mal forme -- erreur propre, jamais un crash") {
    const auto result = decode_svg(to_bytes("<svg><path d=\"M 0 0"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("svg : fichier sans geometrie exploitable -- erreur propre") {
    const std::string svg =
        R"svg(<svg viewBox="0 0 100 100" width="10mm" height="10mm"><defs/></svg>)svg";
    const auto result = decode_svg(to_bytes(svg));
    REQUIRE_FALSE(result.has_value());
}
