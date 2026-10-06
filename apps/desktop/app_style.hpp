// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QFont>
#include <QMap>
#include <QPalette>
#include <QString>
#include <QStringList>

#include "design_tokens.hpp"

// Génération PURE (aucune QApplication requise) de la palette, de la police et de
// la feuille de style de l'application à partir des tokens. Le QSS est un gabarit
// unique à placeholders `@token@` (voir app_style.cpp) : un nom inconnu laisse un
// `@x@` détectable par les tests. Aucun cache de chaîne, aucune règle `QWidget {}`.
namespace openstitch::desktop {

// Palette COMPLÈTE (tous les rôles, groupe Disabled inclus).
[[nodiscard]] QPalette build_palette(const Tokens& tokens);

// Feuille de style pour un jeu de tokens. `assetRoot` = racine des glyphes
// enregistrés (« :/openstitch-3 », cf. style_assets) ; vide => aucune déclaration
// `image:` n'est émise (état par couleur seule).
[[nodiscard]] QString build_stylesheet(const Tokens& tokens, const QString& assetRoot = {});

// Police de l'application (pile de familles, taille de base, graisse régulière).
[[nodiscard]] QFont app_font(const Tokens& tokens);

// Table placeholder -> valeur (tous les champs de Tokens + métriques dérivées).
[[nodiscard]] QMap<QString, QString> style_token_map(const Tokens& tokens);

// Placeholders `@nom@` du gabarit (hors glyphes) et noms de glyphes `@asset:x.png@`
// référencés : alimentent les tests.
[[nodiscard]] QStringList stylesheet_placeholders();
[[nodiscard]] QStringList stylesheet_asset_names();

// Le gabarit brut (pour les tests de structure).
[[nodiscard]] QString stylesheet_template();

} // namespace openstitch::desktop
