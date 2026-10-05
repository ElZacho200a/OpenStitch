// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/color_distance.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openstitch::thread_palette {

namespace {

// Référence du blanc D65, observateur 2° (valeurs usuelles, normalisées à
// Y=100).
constexpr double kRefX = 95.047;
constexpr double kRefY = 100.0;
constexpr double kRefZ = 108.883;

double srgb_to_linear(double channel) {
    // channel dans [0,1].
    if (channel <= 0.04045) {
        return channel / 12.92;
    }
    return std::pow((channel + 0.055) / 1.055, 2.4);
}

double lab_f(double t) {
    constexpr double kDelta = 6.0 / 29.0;
    constexpr double kDeltaCubed = kDelta * kDelta * kDelta; // (6/29)^3
    if (t > kDeltaCubed) {
        return std::cbrt(t);
    }
    return t / (3.0 * kDelta * kDelta) + 4.0 / 29.0;
}

double degrees_to_radians(double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

double radians_to_degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

// Ramène un angle en degrés dans [0, 360).
double normalize_degrees(double degrees) {
    double result = std::fmod(degrees, 360.0);
    if (result < 0.0) {
        result += 360.0;
    }
    return result;
}

} // namespace

CieLab to_cielab(std::array<std::uint8_t, 3> srgb) noexcept {
    // sRGB [0,255] -> [0,1] -> linéaire.
    const double r_lin = srgb_to_linear(static_cast<double>(srgb[0]) / 255.0);
    const double g_lin = srgb_to_linear(static_cast<double>(srgb[1]) / 255.0);
    const double b_lin = srgb_to_linear(static_cast<double>(srgb[2]) / 255.0);

    // Linéaire -> XYZ (matrice sRGB, D65), Y normalisé à 100.
    const double x = (0.4124564 * r_lin + 0.3575761 * g_lin + 0.1804375 * b_lin) * 100.0;
    const double y = (0.2126729 * r_lin + 0.7151522 * g_lin + 0.0721750 * b_lin) * 100.0;
    const double z = (0.0193339 * r_lin + 0.1191920 * g_lin + 0.9503041 * b_lin) * 100.0;

    const double fx = lab_f(x / kRefX);
    const double fy = lab_f(y / kRefY);
    const double fz = lab_f(z / kRefZ);

    return CieLab{
        .l = 116.0 * fy - 16.0,
        .a = 500.0 * (fx - fy),
        .b = 200.0 * (fy - fz),
    };
}

// Implémentation standard de CIEDE2000 (Sharma, Wu & Dalal, 2005).
double ciede2000(const CieLab& a, const CieLab& b) noexcept {
    const double l1 = a.l;
    const double a1 = a.a;
    const double b1 = a.b;
    const double l2 = b.l;
    const double a2 = b.a;
    const double b2 = b.b;

    const double c1 = std::sqrt(a1 * a1 + b1 * b1);
    const double c2 = std::sqrt(a2 * a2 + b2 * b2);
    const double c_bar = (c1 + c2) / 2.0;

    const double c_bar7 = std::pow(c_bar, 7.0);
    constexpr double kPow25To7 = 6103515625.0; // 25^7
    const double g = 0.5 * (1.0 - std::sqrt(c_bar7 / (c_bar7 + kPow25To7)));

    const double a1_prime = a1 * (1.0 + g);
    const double a2_prime = a2 * (1.0 + g);

    const double c1_prime = std::sqrt(a1_prime * a1_prime + b1 * b1);
    const double c2_prime = std::sqrt(a2_prime * a2_prime + b2 * b2);

    auto hue_prime = [](double a_prime, double b_component) {
        if (a_prime == 0.0 && b_component == 0.0) {
            return 0.0;
        }
        return normalize_degrees(radians_to_degrees(std::atan2(b_component, a_prime)));
    };
    const double h1_prime = hue_prime(a1_prime, b1);
    const double h2_prime = hue_prime(a2_prime, b2);

    const double delta_l_prime = l2 - l1;
    const double delta_c_prime = c2_prime - c1_prime;

    double delta_h_prime_raw = h2_prime - h1_prime;
    double delta_h_prime;
    if (c1_prime * c2_prime == 0.0) {
        delta_h_prime = 0.0;
    } else if (std::abs(delta_h_prime_raw) <= 180.0) {
        delta_h_prime = delta_h_prime_raw;
    } else if (delta_h_prime_raw > 180.0) {
        delta_h_prime = delta_h_prime_raw - 360.0;
    } else {
        delta_h_prime = delta_h_prime_raw + 360.0;
    }
    const double delta_big_h_prime =
        2.0 * std::sqrt(c1_prime * c2_prime) * std::sin(degrees_to_radians(delta_h_prime) / 2.0);

    const double l_bar_prime = (l1 + l2) / 2.0;
    const double c_bar_prime = (c1_prime + c2_prime) / 2.0;

    double h_bar_prime;
    if (c1_prime * c2_prime == 0.0) {
        h_bar_prime = h1_prime + h2_prime;
    } else if (std::abs(h1_prime - h2_prime) <= 180.0) {
        h_bar_prime = (h1_prime + h2_prime) / 2.0;
    } else if (h1_prime + h2_prime < 360.0) {
        h_bar_prime = (h1_prime + h2_prime + 360.0) / 2.0;
    } else {
        h_bar_prime = (h1_prime + h2_prime - 360.0) / 2.0;
    }

    const double t = 1.0 - 0.17 * std::cos(degrees_to_radians(h_bar_prime - 30.0)) +
                     0.24 * std::cos(degrees_to_radians(2.0 * h_bar_prime)) +
                     0.32 * std::cos(degrees_to_radians(3.0 * h_bar_prime + 6.0)) -
                     0.20 * std::cos(degrees_to_radians(4.0 * h_bar_prime - 63.0));

    const double delta_theta = 30.0 * std::exp(-std::pow((h_bar_prime - 275.0) / 25.0, 2.0));
    const double c_bar_prime7 = std::pow(c_bar_prime, 7.0);
    const double r_c = 2.0 * std::sqrt(c_bar_prime7 / (c_bar_prime7 + kPow25To7));
    const double r_t = -r_c * std::sin(degrees_to_radians(2.0 * delta_theta));

    const double s_l = 1.0 + (0.015 * std::pow(l_bar_prime - 50.0, 2.0)) /
                                 std::sqrt(20.0 + std::pow(l_bar_prime - 50.0, 2.0));
    const double s_c = 1.0 + 0.045 * c_bar_prime;
    const double s_h = 1.0 + 0.015 * c_bar_prime * t;

    constexpr double kL = 1.0;
    constexpr double kC = 1.0;
    constexpr double kH = 1.0;

    const double term_l = delta_l_prime / (kL * s_l);
    const double term_c = delta_c_prime / (kC * s_c);
    const double term_h = delta_big_h_prime / (kH * s_h);

    const double delta_e =
        std::sqrt(term_l * term_l + term_c * term_c + term_h * term_h + r_t * term_c * term_h);
    return delta_e;
}

std::vector<ThreadMatch> nearest_threads(std::array<std::uint8_t, 3> rgb, const ThreadChart& chart,
                                         std::size_t top_n) {
    const CieLab query = to_cielab(rgb);

    std::vector<ThreadMatch> matches;
    matches.reserve(chart.threads.size());
    for (const Thread& thread : chart.threads) {
        matches.push_back(
            ThreadMatch{.key = thread.key, .distance = ciede2000(query, to_cielab(thread.rgb))});
    }

    // Tri stable : égalités de distance départagées par l'ordre de
    // déclaration du nuancier (AD-S1-2), préservé par std::stable_sort.
    std::stable_sort(
        matches.begin(), matches.end(),
        [](const ThreadMatch& lhs, const ThreadMatch& rhs) { return lhs.distance < rhs.distance; });

    if (matches.size() > top_n) {
        matches.resize(top_n);
    }
    return matches;
}

} // namespace openstitch::thread_palette
