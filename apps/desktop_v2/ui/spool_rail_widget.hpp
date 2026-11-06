// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- rail de bobines : rangée horizontale des objets dans l'ordre de couture, un clic
// sélectionne, un glisser (ou Alt+flèches) réordonne. Aucune logique métier : lit `spoolRail`,
// agit par `moveSpool` (une commande annulable).

#include <QListWidget>

class QTimer;

#include "document_session.hpp"

namespace openstitch::desktop_v2 {

class SpoolRailWidget : public QListWidget {
    Q_OBJECT
public:
    explicit SpoolRailWidget(DocumentSession& session, QWidget* parent = nullptr);
    ~SpoolRailWidget() override;

    [[nodiscard]] QString summaryText() const { return summary_; }
    [[nodiscard]] std::uint64_t spoolId(int index) const;
    // Place la bobine d'index `from` à l'index `to` ; faux (et message émis) si refusé.
    bool moveSpoolTo(int from, int to);
    // Reconstruit tout de suite (sinon regroupé : une seule reconstruction par tour de boucle).
    void refreshNow();

signals:
    void summaryChanged(const QString& summary);
    void refused(const QString& message);

protected:
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void scheduleRefresh();
    void refresh();
    void syncSelection();

    DocumentSession& session_;
    DocumentSession::Token sessionToken_{0};
    SelectionModel::Token selectionToken_{0};
    QString summary_;
    QTimer* pending_{nullptr};
    bool refreshing_{false};
};

} // namespace openstitch::desktop_v2
