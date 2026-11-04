// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- panneau des fils : trois onglets (plan de fils, ordre de couture, coloris). Il
// appelle `thread_plan` (une commande annulable par action) ; aucune logique métier ici.

#include <QWidget>

#include "document_session.hpp"

class QListWidget;
class QTabWidget;

namespace openstitch::desktop_v2 {

class ThreadPanel : public QWidget {
    Q_OBJECT
public:
    explicit ThreadPanel(DocumentSession& session, QWidget* parent = nullptr);
    ~ThreadPanel() override;

    // Fils
    [[nodiscard]] int threadCount() const;
    [[nodiscard]] QString threadText(int row) const;
    void setCurrentThread(int row);
    bool selectCurrentThread();
    bool recolorCurrentThread(const QColor& color);
    bool mergeCurrentThreadInto(int targetRow);
    bool setThreadRowVisible(int row, bool visible);
    // Ordre de couture
    [[nodiscard]] int orderCount() const;
    [[nodiscard]] QString orderText(int row) const;
    void setCurrentOrder(int row);
    bool moveCurrentOrder(int direction); // -1 plus tôt, +1 plus tard
    // Coloris
    [[nodiscard]] int colorwayCount() const;
    [[nodiscard]] QString colorwayText(int row) const;
    void setCurrentColorway(int row);
    bool addColorwayNamed(const QString& name);
    bool activateCurrentColorway();
    bool renameCurrentColorway(const QString& name);
    bool removeCurrentColorway();

signals:
    void refused(const QString& message);

private:
    void refresh();
    bool report(bool ok, const std::string& message);

    DocumentSession& session_;
    QTabWidget* tabs_;
    QListWidget* threads_;
    QListWidget* order_;
    QListWidget* colorways_;
    DocumentSession::Token sessionToken_{0};
    bool refreshing_{false};
};

} // namespace openstitch::desktop_v2
