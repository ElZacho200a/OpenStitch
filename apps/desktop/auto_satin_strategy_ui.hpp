// SPDX-License-Identifier: Apache-2.0
// Libellés, infobulles et ligne de comparaison des méthodes d'auto-satin (lots C et E de
// docs/auto-satin-research.md). Aucune logique de calcul : le moteur est dans libs/auto_satin ;
// l'interface ne fait que nommer les méthodes et afficher des mesures.
#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <array>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::desktop {

// Ordre = valeurs de `document::AutoSatinStrategy` (l'indice d'un QComboBox est la valeur).
inline constexpr std::array<document::AutoSatinStrategy, 3> kAutoSatinStrategies = {
    document::AutoSatinStrategy::Skeleton, document::AutoSatinStrategy::ParallelSweep,
    document::AutoSatinStrategy::MedialAxis};

[[nodiscard]] inline QString autoSatinStrategyLabel(document::AutoSatinStrategy strategy) {
    switch (strategy) {
    case document::AutoSatinStrategy::Skeleton:
        return QCoreApplication::translate("AutoSatinStrategy", "Squelette (actuel)");
    case document::AutoSatinStrategy::ParallelSweep:
        return QCoreApplication::translate("AutoSatinStrategy", "Balayage parallèle");
    case document::AutoSatinStrategy::MedialAxis:
        return QCoreApplication::translate("AutoSatinStrategy", "Axe médian vectoriel");
    }
    return {};
}

[[nodiscard]] inline QString autoSatinStrategyTip(document::AutoSatinStrategy strategy) {
    switch (strategy) {
    case document::AutoSatinStrategy::Skeleton:
        return QCoreApplication::translate(
            "AutoSatinStrategy",
            "Méthode historique : amincissement de la forme en pixels (Zhang-Suen), axe puis "
            "traversées perpendiculaires. Suit les rubans, lettres et formes branchées ; refuse "
            "les formes compactes (disque, pétale).");
    case document::AutoSatinStrategy::ParallelSweep:
        return QCoreApplication::translate(
            "AutoSatinStrategy",
            "Fils parallèles à un angle unique, par bandes continues. Couvre les formes "
            "compactes et les blocs larges ; les concavités et trous coupent la bande. Les "
            "guides locaux ne s'appliquent pas (angle global).");
    case document::AutoSatinStrategy::MedialAxis:
        return QCoreApplication::translate(
            "AutoSatinStrategy",
            "Même moteur de traversées que le squelette, mais l'axe est calculé sur le contour "
            "vectoriel (diagramme de Voronoï) : pas de discrétisation par pixels. Refuse un "
            "contour qui se coupe lui-même.");
    }
    return {};
}

[[nodiscard]] inline QStringList autoSatinStrategyItems() {
    QStringList items;
    for (const auto s : kAutoSatinStrategies) {
        items << autoSatinStrategyLabel(s);
    }
    return items;
}

// Résultat de la méthode sur toute la forme source (somme sur ses morceaux), pour le tableau
// « Comparer les méthodes ». Mesures géométriques des traversées, avant finitions.
struct AutoSatinStrategyComparison {
    document::AutoSatinStrategy strategy{document::AutoSatinStrategy::Skeleton};
    bool ok{false}; // la méthode a produit au moins une colonne
    int columns{0};
    int crossings{0};
    int crossingPairs{0};
    double coverage{0.0}; // 0 à 1, pondérée par l'aire des morceaux
    double overlap{0.0};
    double uncoveredMm2{0.0};
    double maxThreadMm{0.0};
    QStringList messages; // refus et avertissements nommés par le moteur
};

} // namespace openstitch::desktop
