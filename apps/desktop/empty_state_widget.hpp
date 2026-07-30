// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QFrame>

namespace openstitch::desktop {

// État d'accueil affiché au centre du canevas tant qu'aucun document n'est
// ouvert. Sobre : un titre, trois portes d'entrée, une phrase d'explication.
// Pas d'emoji, pas d'illustration, pas de slogan (cf. direction artistique).
class EmptyStateWidget : public QFrame {
    Q_OBJECT

public:
    explicit EmptyStateWidget(QWidget* parent = nullptr);

signals:
    void openImageRequested();
    void openProjectRequested();
    void importDstRequested();
};

}  // namespace openstitch::desktop
