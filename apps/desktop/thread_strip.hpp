// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

class QMenu;

namespace openstitch::desktop {

// Barre des fils (bas de la fenêtre) : une pastille ronde par fil utilisé par le motif,
// dans l'ordre de couture, plus un résumé à droite. Purement visuel : MainWindow fournit les
// fils (déduits de `stitch_analysis::thread_usage`) et traduit un clic en sélection d'objets.
class ThreadStrip : public QWidget {
    Q_OBJECT

public:
    struct Swatch {
        QColor color;
        QString tooltip;
    };

    explicit ThreadStrip(QWidget* parent = nullptr);

    void setThreads(std::vector<Swatch> swatches, const QString& summary);
    // HP-SIM-003 : pastille du fil en cours de couture pendant la simulation (-1 : aucune).
    void setCurrent(int index);
    [[nodiscard]] int currentIndex() const { return current_; }
    [[nodiscard]] int threadCount() const { return static_cast<int>(swatches_.size()); }
    // Centre de la pastille `index` (coordonnées du widget) : pour les tests et l'accessibilité.
    [[nodiscard]] QPointF swatchCenter(int index) const;

    // Clavier (HP-UX-041) : la barre est un seul arrêt de tabulation ; ←/→ (Début/Fin) déplacent le
    // focus d'une pastille à l'autre, Entrée ou Espace sélectionne, Ctrl+←/→ change l'ordre. Quand
    // toutes les pastilles ne tiennent pas, la dernière case « +N » ouvre la liste COMPLÈTE.
    [[nodiscard]] int focusedIndex() const { return focus_; }
    [[nodiscard]] int hiddenCount() const;
    // Ouvre (sans bloquer) la liste de tous les fils ; choisir une entrée émet threadClicked.
    QMenu* showOverflowMenu();
    [[nodiscard]] QMenu* overflowMenu() const { return overflowMenu_; }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void threadClicked(int index);
    // Une pastille glissée de la position `from` à la position `to` : la couleur entière change
    // d'ordre de couture (plus à droite = cousue après, donc au-dessus). Jamais émis pour un
    // simple clic ni quand la pastille est relâchée à sa place.
    void threadMoved(int from, int to);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    [[nodiscard]] QRectF moreRect() const;
    [[nodiscard]] bool moreAt(const QPointF& pos) const;
    void announceFocus(); // nom et code du fil focalisé, pour les technologies d'assistance
    [[nodiscard]] QRectF swatchRect(int index) const;
    [[nodiscard]] int visibleCount() const;
    [[nodiscard]] int indexAt(const QPointF& pos) const;
    // Position d'arrivée d'une pastille lâchée à l'abscisse `x` (bornée aux pastilles visibles).
    [[nodiscard]] int dropSlotAt(qreal x) const;
    void endDrag();

    std::vector<Swatch> swatches_;
    QString summary_;
    int hovered_{-1};
    int current_{-1};      // HP-SIM-003 : fil en cours de simulation
    int pressed_{-1};      // pastille sous le bouton enfoncé
    QPointF pressPos_;     // où le bouton a été enfoncé
    bool dragging_{false}; // le seuil de glissement est dépassé
    qreal dragX_{0.0};     // abscisse du curseur pendant le glissement
    int dropSlot_{-1};     // position d'arrivée courante pendant le glissement
    int focus_{0};         // pastille focalisée ; == nombre de pastilles visibles : case « +N »
    bool pressedMore_{false};
    QMenu* overflowMenu_{nullptr};
};

} // namespace openstitch::desktop
