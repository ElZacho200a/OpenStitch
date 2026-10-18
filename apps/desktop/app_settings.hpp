// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

#include "openstitch/thread_palette/thread_stock.hpp"

class QSettings;

namespace openstitch::desktop {

// Réglages d'édition et d'interface regroupés (onglet « Paramètres »). Persistés dans le fichier
// INI de l'application (voir `configureSettingsStorage`), donc conservés d'une instance à l'autre.
// Aucun de ces réglages ne change les points de broderie générés.
struct EditSettings {
    double unionGapMm{0.05};      // Unir : écart maximal soudé entre deux formes juxtaposées
    double knifeWidthMm{0.02};    // Couteau : largeur de la bande retirée
    double knifeMinLengthMm{0.5}; // Couteau : longueur minimale du trait
    bool snapNodesOnDrag{false};  // accrochage des nœuds au glisser
    double regionMapOpacity{0.9}; // opacité de la carte des régions
    int longPressMs{500};         // délai de l'appui long

    [[nodiscard]] static EditSettings defaults() { return {}; }
    // Même réglages ramenés dans leurs bornes (une valeur hors bornes lue du fichier ne plante
    // pas).
    [[nodiscard]] EditSettings clamped() const;

    bool operator==(const EditSettings&) const = default;
};

[[nodiscard]] EditSettings loadEditSettings();
void saveEditSettings(const EditSettings& settings);

// Copie dans `to` toutes les clés de `from`, seulement si `to` est vide (ne jamais écraser un
// fichier existant). Renvoie le nombre de clés copiées.
int migrateSettings(QSettings& from, QSettings& to);

// À appeler une fois au démarrage, après la pose du nom d'organisation/application : fait de
// l'INI le format par défaut de tous les `QSettings()` (fichier `OpenStitch Studio.ini` dans le
// dossier de configuration de l'utilisateur) et y importe une fois les anciennes clés du registre.
void configureSettingsStorage();

// Chemin du fichier de réglages (pour l'afficher à l'utilisateur).
[[nodiscard]] QString settingsFilePath();

// « Mes fils » (HP-THR-007) : le stock de bobines est une préférence utilisateur, JSON à côté
// du fichier de réglages (jamais dans le .osp).
[[nodiscard]] QString threadStockFilePath();
// Relit le stock. Fichier absent : stock vide, sans erreur. Fichier illisible : stock vide,
// `error` renseigné, et le fichier fautif est conservé sous `<nom>.invalide` (jamais écrasé
// en silence par la sauvegarde suivante).
[[nodiscard]] thread_palette::ThreadStock loadThreadStock(const QString& path,
                                                          QString* error = nullptr);
// Écriture atomique (QSaveFile). Faux + `error` si le disque refuse.
bool saveThreadStock(const thread_palette::ThreadStock& stock, const QString& path,
                     QString* error = nullptr);
// Bascule « limiter la recherche du fil le plus proche à mes fils » (préférence).
[[nodiscard]] bool loadLimitToThreadStock();
void saveLimitToThreadStock(bool limit);

} // namespace openstitch::desktop
