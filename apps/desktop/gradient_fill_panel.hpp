// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QColor>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace openstitch::desktop {

// Section « Remplissage : Uni / Dégradé » de l'inspecteur. Pure présentation : elle
// affiche un `State` fourni par MainWindow (relu du document) et émet l'état souhaité ;
// aucune règle de dégradé ici, tout part en commandes annulables via le cœur.
class GradientProfilePreview;

class GradientFillPanel : public QWidget {
    Q_OBJECT
public:
    struct State {
        bool visible{false};   // la sélection est un remplissage (uni ou dégradé)
        bool available{false}; // peut devenir un dégradé
        QString reason;        // pourquoi indisponible (vide si disponible)
        bool gradient{false};
        QColor start;
        QColor end;
        QString startThread;  // référence exacte ou « Couleur libre »
        QString endThread;
        double zoneStartPercent{0.0};
        double zoneEndPercent{100.0};
        int curve{0}; // DensityCurve
        double spacingStartMm{4.0};
        double spacingEndMm{0.4};
        bool complementary{true};
        bool handles{false}; // poignées affichées sur le canevas
        int layerCount{2};
    };

    explicit GradientFillPanel(QWidget* parent = nullptr);
    void setState(const State& state);
    [[nodiscard]] const State& state() const { return state_; }

signals:
    // true : Uni → Dégradé ; false : Dégradé → Uni.
    void modeRequested(bool gradient);
    // Nouvel état souhaité (champ édité + valeurs courantes des autres).
    void edited(openstitch::desktop::GradientFillPanel::State state);
    void handlesToggled(bool shown);
    void assistantRequested();
    // Choix d'une référence de fil dans le nuancier (et pas seulement d'un RGB).
    void threadRequested(bool start);
    void densityRequested();

private:
    void buildGradientControls();
    void emitEdited();
    void pickColor(bool start);
    QPushButton* colorButton(const QString& objectName);
    static void paintSwatch(QPushButton* button, const QColor& color);

    State state_;
    bool updating_{false};
    bool built_{false};
    QComboBox* mode_;
    QLabel* reason_;
    QWidget* gradientBox_;
    QPushButton* startButton_;
    QPushButton* endButton_;
    QPushButton* invert_;
    QPushButton* startThreadButton_;
    QPushButton* endThreadButton_;
    QLabel* startThreadLabel_;
    QLabel* endThreadLabel_;
    GradientProfilePreview* profilePreview_;
    QPushButton* previewButton_;
    QPushButton* densityButton_;
    QLabel* densityNote_;
    QDoubleSpinBox* zoneStart_;
    QDoubleSpinBox* zoneEnd_;
    QComboBox* curve_;
    QPushButton* handles_;
    QToolButton* advancedToggle_;
    QWidget* advanced_;
    QDoubleSpinBox* spacingStart_;
    QDoubleSpinBox* spacingEnd_;
    QComboBox* profile_;
    QLabel* layers_;
};

} // namespace openstitch::desktop

Q_DECLARE_METATYPE(openstitch::desktop::GradientFillPanel::State)
