// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QAbstractButton>
#include <QGroupBox>
#include <QStyle>
#include <QVariant>
#include <QWidget>

// Aides de style du design system v2 : variantes de bouton et rôles de texte
// portés par des propriétés dynamiques que la QSS (app_style.cpp) sélectionne
// (`QPushButton[variant="primary"]`, `QLabel[role="title"]`…). Toute valeur
// inconnue retombe sur le rendu par défaut (jamais d'erreur).
namespace openstitch::desktop::ui {

inline constexpr const char* kPropVariant = "variant";
inline constexpr const char* kPropRole = "role";

enum class ButtonVariant { Primary, Tonal, Ghost, Danger };
enum class LabelRole { Title, Heading, Caption, Mono, Warning, Error, Success, Section };

[[nodiscard]] inline const char* variant_name(ButtonVariant v) {
    switch (v) {
    case ButtonVariant::Primary:
        return "primary";
    case ButtonVariant::Tonal:
        return "tonal";
    case ButtonVariant::Ghost:
        return "ghost";
    case ButtonVariant::Danger:
        return "danger";
    }
    return "tonal";
}

[[nodiscard]] inline const char* role_name(LabelRole r) {
    switch (r) {
    case LabelRole::Title:
        return "title";
    case LabelRole::Heading:
        return "heading";
    case LabelRole::Caption:
        return "caption";
    case LabelRole::Mono:
        return "mono";
    case LabelRole::Warning:
        return "warning";
    case LabelRole::Error:
        return "error";
    case LabelRole::Success:
        return "success";
    case LabelRole::Section:
        return "section";
    }
    return "title";
}

// Repose un style après un changement de propriété dynamique.
inline void repolish(QWidget* w) {
    if (w == nullptr) {
        return;
    }
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// Pose une propriété de chaîne ; no-op si inchangée.
inline void set_string_property(QWidget* w, const char* name, const QString& value) {
    if (w == nullptr || w->property(name).toString() == value) {
        return;
    }
    w->setProperty(name, value);
    repolish(w);
}

inline void setVariant(QAbstractButton* button, ButtonVariant variant) {
    set_string_property(button, kPropVariant, QString::fromLatin1(variant_name(variant)));
}

inline void setRole(QWidget* widget, LabelRole role) {
    set_string_property(widget, kPropRole, QString::fromLatin1(role_name(role)));
}

// Titre de QGroupBox « petites capitales » : QSS ne sait pas poser SmallCapitals
// (et QFont se propagerait aux enfants) → repli prévu par la spec : titre passé en
// majuscules. Le titre d'origine reste dans la propriété `titleSource` et dans
// `accessibleName` (lecteurs d'écran, recherches). Idempotent.
inline void styleGroupBox(QGroupBox* box) {
    if (box == nullptr) {
        return;
    }
    if (!box->property("titleSource").isValid()) {
        box->setProperty("titleSource", box->title());
        if (box->accessibleName().isEmpty()) {
            box->setAccessibleName(box->title());
        }
    }
    box->setTitle(box->property("titleSource").toString().toUpper());
}

} // namespace openstitch::desktop::ui
