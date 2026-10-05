// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>

class QVBoxLayout;

namespace openstitch::desktop {

// État d'accueil affiché au centre du canevas tant qu'aucun document n'est
// ouvert. Sobre : un titre, trois portes d'entrée, une phrase d'explication.
// Pas d'emoji, pas d'illustration, pas de slogan (cf. direction artistique).
class EmptyStateWidget : public QFrame {
    Q_OBJECT

public:
    explicit EmptyStateWidget(QWidget* parent = nullptr);

    // Reconstruit la petite liste de boutons "récents" sous les 3 boutons
    // existants (masquée -- aucune ligne -- si `paths` est vide). Pas de
    // vignette (HP-FMT-020 hors périmètre) : texte = nom de fichier, tooltip
    // = chemin complet.
    void setRecentFiles(const QStringList& paths);

signals:
    void openImageRequested();
    void openProjectRequested();
    void importDstRequested();
    void openRecentRequested(const QString& path);

private:
    QVBoxLayout* recentLayout_{nullptr};
};

} // namespace openstitch::desktop
