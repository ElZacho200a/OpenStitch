// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- panneau des calques : liste du fond vers le dessus (case = visible), boutons
// d'édition. Il appelle les services documentaires (`document_services`), donc une commande
// annulable par action ; aucune logique métier ici.

#include <QWidget>

#include "document_session.hpp"

class QListWidget;
class QPushButton;

namespace openstitch::desktop_v2 {

class CalquePanel : public QWidget {
    Q_OBJECT
public:
    explicit CalquePanel(DocumentSession& session, QWidget* parent = nullptr);
    ~CalquePanel() override;

    // Actions (boutons et tests). Retournent vrai si une étape d'annulation a été empilée.
    bool addNamed(const QString& name);
    bool renameCurrent(const QString& name);
    bool removeCurrent();
    bool toggleLockCurrent();
    bool moveCurrent(int direction);
    bool moveSelectionHere();
    void setCurrentRow(int row);
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] QString rowText(int row) const;
    [[nodiscard]] bool rowVisible(int row) const;
    bool setRowVisible(int row, bool visible);

signals:
    void refused(const QString& message);

private:
    void refresh();
    [[nodiscard]] std::uint64_t currentId() const;
    bool report(bool ok, const std::string& message);

    DocumentSession& session_;
    QListWidget* list_;
    DocumentSession::Token sessionToken_{0};
    bool refreshing_{false};
};

} // namespace openstitch::desktop_v2
