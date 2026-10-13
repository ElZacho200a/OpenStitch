// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

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
    [[nodiscard]] int threadCount() const { return static_cast<int>(swatches_.size()); }

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void threadClicked(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    [[nodiscard]] QRectF swatchRect(int index) const;
    [[nodiscard]] int visibleCount() const;
    [[nodiscard]] int indexAt(const QPointF& pos) const;

    std::vector<Swatch> swatches_;
    QString summary_;
    int hovered_{-1};
};

} // namespace openstitch::desktop
