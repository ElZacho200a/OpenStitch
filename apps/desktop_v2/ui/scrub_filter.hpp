// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- rend un libellé « glissable » : presser puis déplacer la souris horizontalement
// change la valeur du champ associé (voir core/scrub.hpp). Maj = grossier, Ctrl = fin. Aucune
// logique métier : la valeur est écrite dans le champ, qui valide comme s'il avait été saisi.

#include <QObject>
#include <functional>

class QWidget;

namespace openstitch::desktop_v2 {

class ScrubFilter : public QObject {
    Q_OBJECT
public:
    struct Field {
        std::function<double()> get;
        std::function<void(double)> set;
        double step{1.0};
        double min{0.0}; // plus petite valeur réelle (hors valeur « mixte »)
        double max{1.0};
    };
    ScrubFilter(QWidget* label, Field field);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Field field_;
    bool dragging_{false};
    double pressX_{0.0};
    double startValue_{0.0};
};

} // namespace openstitch::desktop_v2
