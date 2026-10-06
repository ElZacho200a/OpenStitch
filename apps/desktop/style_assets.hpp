// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>

#include "design_tokens.hpp"

// Glyphes que la QSS ne sait pas dessiner (coche, point radio, tiret, chevrons).
// Aucun fichier sur disque : les PNG sont rendus par QPainter, assemblés EN
// MÉMOIRE dans un blob rcc (format « qres » v1) et enregistrés via
// QResource::registerResource sous une racine « :/openstitch-<n> » (n incrémenté à
// chaque installation ; l'ancienne racine est désenregistrée). La QSS les
// référence par `url(":/openstitch-<n>/<nom>")`, toujours entre guillemets.
namespace openstitch::desktop::style_assets {

// Noms de fichiers de base (sans variante @2x), ex. « check.png ». Chaque nom
// existe aussi en « <base>@2x.png ».
[[nodiscard]] QStringList glyph_names();

// Rendu d'un glyphe (scale 1 ou 2) coloré depuis les tokens ; image nulle si le
// nom est inconnu.
[[nodiscard]] QImage glyph_image(const QString& name, const Tokens& tokens, int scale);

// Blob rcc complet (pur, sans enregistrement) : en-tête, données, noms, arbre.
[[nodiscard]] QByteArray build_resource_blob(const Tokens& tokens);

// Enregistre les glyphes pour ces tokens ; renvoie la racine (« :/openstitch-3 »)
// ou une chaîne VIDE en cas d'échec (la QSS est alors générée sans `image:`).
// Désenregistre la racine précédente.
[[nodiscard]] QString install(const Tokens& tokens);

// Désenregistre la racine courante (no-op si aucune).
void uninstall();

// Racine actuellement enregistrée (vide si aucune).
[[nodiscard]] QString currentRoot();

// Crochet de test : force l'échec d'install() (chemin de repli).
void forceFailureForTesting(bool fail);

} // namespace openstitch::desktop::style_assets
