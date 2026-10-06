// SPDX-License-Identifier: Apache-2.0
#pragma once

// Dialogues du menu Aide (lot L5, specs/plans/ui-interaction-model.md §4).
// Aucune dépendance à MainWindow : les dialogues reçoivent un QObject racine
// (la fenêtre principale) dont ils lisent les QAction. Aucune logique métier,
// aucun geste écrit à la main : tout vient d'InteractionMap::allRows() et des
// raccourcis réels des actions.

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <vector>

#include "interaction_map.hpp"

class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;

namespace openstitch::desktop {

// « &Ouvrir une image… » -> « Ouvrir une image… » ; « && » reste une esperluette.
[[nodiscard]] QString plainActionText(const QString& text);

// Texte riche (Qt::RichText) de la boîte « À propos » : nom, version, licence, dépôt.
[[nodiscard]] QString aboutText();

// « Gestes souris et clavier » (F1). Tableau Contexte / Geste / Action,
// consultable et filtrable ; non modal.
class GesturesDialog : public QDialog {
    Q_OBJECT

public:
    // `actionRoot` : objet dont les QAction descendantes (avec raccourci) sont
    // listées sous « Raccourcis des commandes ». Peut être nul.
    explicit GesturesDialog(QObject* actionRoot, QWidget* parent = nullptr);

    [[nodiscard]] int totalRowCount() const;   // lignes du modèle (avant filtre)
    [[nodiscard]] int visibleRowCount() const; // lignes après filtre de recherche
    [[nodiscard]] static QString commandsGroupName();

    [[nodiscard]] QTableView* table() const { return table_; }
    [[nodiscard]] QLineEdit* searchField() const { return search_; }
    [[nodiscard]] QComboBox* presetCombo() const { return presetCombo_; }
    [[nodiscard]] QPushButton* closeButton() const { return closeButton_; }

public slots:
    void setFilterText(const QString& text);
    // Reconstruit le tableau (préréglage actif d'InteractionMap, actions courantes).
    void refresh();

signals:
    // Le choix de préréglage a été appliqué (setPreset + savePreset) : la fenêtre
    // principale peut rafraîchir sa ligne d'indications et son menu.
    void presetChanged(openstitch::desktop::Preset preset);

private:
    void rebuildModel();
    void updateSummary();
    void onPresetIndexChanged(int index);

    QPointer<QObject> actionRoot_;
    QLineEdit* search_{nullptr};
    QComboBox* presetCombo_{nullptr};
    QTableView* table_{nullptr};
    QLabel* summary_{nullptr};
    QPushButton* closeButton_{nullptr};
    QStandardItemModel* model_{nullptr};
    QSortFilterProxyModel* proxy_{nullptr};
};

struct QuickStartStep {
    QString title;
    QString body;
    // objectName de QAction à déclencher (un bouton par nom) ; vide = étape
    // purement informative.
    QStringList actionNames;
};

// « Guide de prise en main » : 6 étapes, non modal.
class QuickStartDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickStartDialog(QObject* actionRoot, QWidget* parent = nullptr);
    QuickStartDialog(QObject* actionRoot, std::vector<QuickStartStep> steps,
                     QWidget* parent = nullptr);

    // Les 6 étapes du flux réel. Seul endroit à éditer pour relier une étape à
    // une action (ajouter son objectName dans `actionNames`).
    [[nodiscard]] static std::vector<QuickStartStep> defaultSteps();

    [[nodiscard]] int stepCount() const { return static_cast<int>(steps_.size()); }
    [[nodiscard]] int stepButtonCount(int step) const;
    // nullptr si l'étape est informative ou hors bornes.
    [[nodiscard]] QPushButton* stepButton(int step, int index = 0) const;
    [[nodiscard]] QPushButton* closeButton() const { return closeButton_; }

private:
    void build(QObject* actionRoot);
    void bindButton(QPushButton* button, QAction* action, const QString& actionName);

    std::vector<QuickStartStep> steps_;
    std::vector<QList<QPushButton*>> buttons_;
    QPushButton* closeButton_{nullptr};
};

} // namespace openstitch::desktop
