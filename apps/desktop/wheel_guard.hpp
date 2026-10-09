// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QWheelEvent>
#include <QWidget>

namespace openstitch::desktop {

// Filtre d'évènements : la molette ne change la valeur d'un champ (spinbox,
// liste déroulante) QUE s'il a le focus. Sinon l'évènement est transmis au
// parent, ce qui fait défiler la zone de l'inspecteur au lieu de modifier un
// paramètre par inadvertance en parcourant le formulaire.
class WheelGuard : public QObject {
public:
    explicit WheelGuard(QObject* parent = nullptr) : QObject(parent) {}

    // Applique la politique à `field` : focus au clic/Tab (pas à la molette) + filtre.
    void guard(QWidget* field) {
        field->setFocusPolicy(Qt::StrongFocus);
        field->installEventFilter(this);
    }

    // Garde tous les spinbox et listes déroulantes descendants de `root`.
    void guardAll(QWidget* root) {
        for (auto* spin : root->findChildren<QAbstractSpinBox*>()) {
            guard(spin);
        }
        for (auto* combo : root->findChildren<QComboBox*>()) {
            guard(combo);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Wheel) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget != nullptr && !widget->hasFocus()) {
                if (QWidget* parent = widget->parentWidget()) {
                    QCoreApplication::sendEvent(parent, event);
                }
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace openstitch::desktop
