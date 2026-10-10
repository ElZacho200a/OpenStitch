// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

#include "openstitch/document/text_object.hpp"
#include "openstitch/lettering/font.hpp"

namespace openstitch::desktop {

// Une police proposée à l'utilisateur : intégrée à l'application (ressource Qt) ou
// installée (fichier TTF/OTF). Le chemin du fichier est ce que la bibliothèque de
// lettrage reçoit ; Qt ne sert qu'à ÉNUMÉRER (dossiers de polices du système) et à
// lire les ressources intégrées.
struct FontEntry {
    QString display; // « Vera Sans Gras (intégrée) », « Arial Bold »
    QString family;
    QString style;
    QString path;    // fichier (vide pour une police intégrée)
    QString builtin; // identifiant d'une police intégrée (vide sinon)
    int faceIndex{0};
};

// Catalogue des polices : intégrées d'abord, puis installées (analysées une seule
// fois, à la première demande : lecture minimale de chaque fichier par
// lettering::inspect_font_file). Interface utilisateur uniquement : thread principal.
class FontCatalog {
public:
    static FontCatalog& instance();

    [[nodiscard]] const QList<FontEntry>& entries();
    // Index de l'entrée désignée par la référence mémorisée dans le document ; -1 si
    // aucune (fichier déplacé et famille inconnue).
    [[nodiscard]] int indexOf(const document::TextFontRef& ref);
    [[nodiscard]] static document::TextFontRef refOf(const FontEntry& entry);

    // Charge la police d'une référence. Repli : fichier disparu -> police de même
    // famille du catalogue. `error` reçoit un message montrable en cas d'échec.
    [[nodiscard]] std::shared_ptr<lettering::Font> load(const document::TextFontRef& ref,
                                                        QString* error = nullptr);

    // Pour les tests : remplace les dossiers de polices système par `dirs` (sans
    // toucher aux polices intégrées) et vide les caches.
    void setSystemFontDirsForTesting(const QStringList& dirs);

private:
    FontCatalog() = default;
    void scan();

    bool scanned_{false};
    std::optional<QStringList> overrideDirs_;
    QList<FontEntry> entries_;
    QList<std::pair<QString, std::shared_ptr<lettering::Font>>> loaded_;
};

} // namespace openstitch::desktop
