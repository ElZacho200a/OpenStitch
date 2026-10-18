// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/thread_palette/thread.hpp"
#include "openstitch/thread_palette/thread_library.hpp"
#include "openstitch/thread_palette/thread_stock.hpp"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace openstitch::desktop {

// Panneau « Fils » (HP-THR-004/005) : fils du projet, catalogues, film couleur.
// Présentation seule : aucune règle métier ici. Les données viennent de
// `stitch_analysis::thread_usage` / `color_film` (via MainWindow) et chaque
// action est un signal que MainWindow traduit en commande annulable.
class ThreadPanel : public QWidget {
    Q_OBJECT

public:
    explicit ThreadPanel(QWidget* parent = nullptr);

    // Bibliothèque affichée (propriété de MainWindow, doit survivre au panneau).
    void setLibrary(const thread_palette::ThreadLibrary* library);
    // Recharge la liste des nuanciers (après import / retrait).
    void refreshCharts();

    void setUsage(std::vector<stitch_analysis::ThreadUsage> usage);
    void setFilm(std::vector<stitch_analysis::ColorFilmBlock> film);
    // Résultats d'une recherche « plus proche » affichés à la place du catalogue.
    void showMatches(const std::vector<thread_palette::Thread>& matches, const QString& title);
    // Texte d'état de la sélection courante (« 3 objets sélectionnés »).
    void setSelectionInfo(const QString& text);

    // « Mes fils » (HP-THR-007) : préférence utilisateur affichée par le panneau. Le panneau
    // n'édite jamais le stock lui-même : il émet des demandes que MainWindow applique, persiste
    // puis renvoie par `setStock`. Aucune mutation du document.
    void setStock(thread_palette::ThreadStock stock);
    [[nodiscard]] const thread_palette::ThreadStock& stock() const { return stock_; }
    // Bascule « limiter la recherche du fil le plus proche à mes fils » (sans signal).
    void setLimitToStock(bool limit);
    [[nodiscard]] bool limitToStock() const;

    [[nodiscard]] QString currentChartId() const; // vide = tous les nuanciers
    [[nodiscard]] std::optional<thread_palette::Thread> currentCatalogThread() const;
    [[nodiscard]] std::optional<stitch_analysis::ThreadIdentity> currentUsage() const;
    [[nodiscard]] int currentFilmRow() const;

signals:
    void assignThreadRequested(const thread_palette::Thread& thread);
    void selectUsageObjectsRequested(const stitch_analysis::ThreadIdentity& identity);
    void replaceUsageRequested(const stitch_analysis::ThreadIdentity& identity,
                               const thread_palette::Thread& thread);
    void snapFreeColorsRequested(const QString& chartId);
    void reduceColorsRequested(int maxThreads);
    void exportListRequested();
    void nearestRequested(const QString& chartId);
    void importChartRequested();
    void removeChartRequested(const QString& chartId);
    // Une couleur entière (ligne `from` de la liste des fils) passe à la ligne `to` : tous ses
    // objets sont cousus ensemble, une couleur plus bas est cousue après donc dessus.
    void threadMoved(int from, int to);
    void filmBlockMoved(int from, int to);
    void filmBlockSelected(int row);
    void mergeFilmBlocksRequested();
    // Case « je possède ce fil » (catalogue ou liste Mes fils) cochée / décochée.
    void stockOwnedToggled(const thread_palette::ThreadKey& key, bool owned);
    void personalThreadAddRequested(const thread_palette::Thread& thread);
    void personalThreadRemoveRequested(const thread_palette::ThreadKey& key);
    void limitToStockToggled(bool limit);

private:
    void rebuildCatalog();
    void rebuildStock();
    [[nodiscard]] std::optional<thread_palette::ThreadKey> currentStockKey() const;
    void updateButtons();
    void applyTheme();
    [[nodiscard]] QString labelOf(const stitch_analysis::ThreadIdentity& id) const;

    const thread_palette::ThreadLibrary* library_{nullptr};
    std::vector<stitch_analysis::ThreadUsage> usage_;
    std::vector<stitch_analysis::ColorFilmBlock> film_;
    std::vector<thread_palette::Thread> catalogRows_;
    bool showingMatches_{false};
    std::vector<QFrame*> cards_; // cartes de résumé (style local issu des jetons)

    QTabWidget* tabs_{nullptr};
    QLabel* selectionInfo_{nullptr};
    // Fils du projet.
    QListWidget* usageList_{nullptr};
    QLabel* usageSummary_{nullptr};
    QPushButton* threadUpBtn_{nullptr};
    QPushButton* threadDownBtn_{nullptr};
    QPushButton* selectUsageBtn_{nullptr};
    QPushButton* replaceUsageBtn_{nullptr};
    QPushButton* snapBtn_{nullptr};
    QSpinBox* reduceSpin_{nullptr};
    QPushButton* reduceBtn_{nullptr};
    QPushButton* exportBtn_{nullptr};
    // Catalogues.
    QComboBox* chartCombo_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QListWidget* catalogList_{nullptr};
    QLabel* chartNote_{nullptr};
    QPushButton* nearestBtn_{nullptr};
    QPushButton* importBtn_{nullptr};
    QPushButton* removeChartBtn_{nullptr};
    QCheckBox* ownCheck_{nullptr};
    // Mes fils.
    thread_palette::ThreadStock stock_;
    std::vector<thread_palette::Thread> stockRows_;
    QCheckBox* limitCheck_{nullptr};
    QLabel* stockSummary_{nullptr};
    QListWidget* stockList_{nullptr};
    QPushButton* addPersonalBtn_{nullptr};
    QPushButton* removeStockBtn_{nullptr};
    // Film couleur.
    QListWidget* filmList_{nullptr};
    QLabel* filmSummary_{nullptr};
    QPushButton* filmUpBtn_{nullptr};
    QPushButton* filmDownBtn_{nullptr};
    QPushButton* filmSelectBtn_{nullptr};
    QPushButton* filmMergeBtn_{nullptr};
};

} // namespace openstitch::desktop
