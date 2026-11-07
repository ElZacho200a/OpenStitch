// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace openstitch::desktop_v2::studio {

// Aide de Studio : feuille des raccourcis GÉNÉRÉE depuis les définitions (table des commandes et
// registre des outils, jamais saisie à la main), guide de prise en main, « À propos ». Aucun état,
// aucune logique métier.
class HelpController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(QVariantList gestures READ gestures CONSTANT)
    Q_PROPERTY(QVariantList guide READ guide CONSTANT)
    Q_PROPERTY(QString aboutText READ aboutText CONSTANT)
public:
    explicit HelpController(QObject* parent = nullptr);
    // [{group, items:[{label, shortcut}]}] : commandes (et outils) qui ont un raccourci, par
    // groupe, dans l'ordre des définitions.
    [[nodiscard]] QVariantList shortcuts() const;
    // [{label, gesture}] : gestes de souris et touches du canevas.
    [[nodiscard]] QVariantList gestures() const;
    // [{title, text}] : les quatre contextes du Studio, dans l'ordre du parcours.
    [[nodiscard]] QVariantList guide() const;
    [[nodiscard]] QString aboutText() const;
    // Aide de l'outil : libellé, raccourci et consigne ; "" si l'outil est inconnu.
    Q_INVOKABLE [[nodiscard]] QString toolHelp(const QString& toolId, const QString& hint) const;
};

} // namespace openstitch::desktop_v2::studio
