// SPDX-License-Identifier: Apache-2.0
#pragma once

// Adaptateur Qt Quick des colonnes satin (famille G) : création par barreaux, nœuds des rails A/B
// (déplacer, insérer), barreaux de correspondance (ajouter, déplacer, supprimer, insérer entre),
// diagnostic de satinabilité. Aucune logique métier : services `satin_column` du coeur, chaque
// action est UNE `ICommand` annulable. Coordonnées en mm « modèle » (Y vers le haut).

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <optional>
#include <vector>

#include "satin_column.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class SatinController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString objectId READ objectId NOTIFY changed)
    Q_PROPERTY(bool isSatin READ isSatin NOTIFY changed)
    Q_PROPERTY(bool isAutoSatin READ isAutoSatin NOTIFY changed)
    Q_PROPERTY(bool isGuidedSatin READ isGuidedSatin NOTIFY changed)
    Q_PROPERTY(bool hasGuidedPath READ hasGuidedPath NOTIFY changed)
    Q_PROPERTY(QString guidedSatinDisabledReason READ guidedSatinDisabledReason NOTIFY changed)
    Q_PROPERTY(int railNodesA READ railNodesA NOTIFY changed)
    Q_PROPERTY(int railNodesB READ railNodesB NOTIFY changed)
    Q_PROPERTY(int rungCount READ rungCount NOTIFY changed)
    Q_PROPERTY(bool clean READ clean NOTIFY changed)
    Q_PROPERTY(QString diagnosis READ diagnosis NOTIFY changed)
    Q_PROPERTY(QVariantMap diagnosisInfo READ diagnosisInfo NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    explicit SatinController(StudioController& studio, QObject* parent = nullptr);
    ~SatinController() override;

    [[nodiscard]] QString objectId() const;
    [[nodiscard]] bool isSatin() const { return railA_ > 0; }
    [[nodiscard]] bool isAutoSatin() const { return autoStrategy_.has_value(); }
    [[nodiscard]] bool isGuidedSatin() const {
        return autoStrategy_ == document::AutoSatinStrategy::Guided;
    }
    [[nodiscard]] bool hasGuidedPath() const { return hasGuidedPath_; }
    [[nodiscard]] QString guidedSatinDisabledReason() const;
    Q_INVOKABLE bool useGuidedStrategy();
    [[nodiscard]] int railNodesA() const { return railA_; }
    [[nodiscard]] int railNodesB() const { return railB_; }
    [[nodiscard]] int rungCount() const { return rungs_; }
    [[nodiscard]] bool clean() const { return diagnosis_.clean(); }
    [[nodiscard]] QString diagnosis() const;
    [[nodiscard]] QVariantMap diagnosisInfo() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    // Barreaux : [{ax, ay, bx, by}].
    Q_INVOKABLE [[nodiscard]] QVariantList rungs() const;
    // Création : au moins deux barreaux {ax, ay, bx, by}. Rend l'identifiant créé ("" si refus) et
    // sélectionne le nouveau satin.
    Q_INVOKABLE QString createFromRungs(const QVariantList& rungs);

    Q_INVOKABLE bool moveRailNode(const QString& side, int node, double xMm, double yMm);
    Q_INVOKABLE bool insertRailNode(const QString& side, int segment, double t);
    Q_INVOKABLE bool addRung(int index, double ax, double ay, double bx, double by);
    Q_INVOKABLE bool insertRungBetween(int index);
    Q_INVOKABLE bool moveRung(int index, double ax, double ay, double bx, double by);
    Q_INVOKABLE bool removeRung(int index);
    // Suggestions du moteur : -1 si aucune.
    Q_INVOKABLE [[nodiscard]] int widestGap() const;
    Q_INVOKABLE [[nodiscard]] int redundantRungIndex() const;
    // Guides de direction et lignes de rupture du remplissage sélectionné (-1 : pas directionnel).
    // `points` : [{x, y}] en mm modèle ; un tatami est converti au premier tracé (même étape).
    Q_PROPERTY(int guideCount READ guideCount NOTIFY changed)
    Q_PROPERTY(int breakLineCount READ breakLineCount NOTIFY changed)
public:
    [[nodiscard]] int guideCount() const { return guides_; }
    [[nodiscard]] int breakLineCount() const { return breaks_; }
    Q_INVOKABLE bool addGuide(const QVariantList& points, bool isBreak);
    Q_INVOKABLE bool clearGuides(bool breakLines);
    // Tableau de courbes centrales : [[{x,y}, ...], ...] en mm modèle.
    Q_INVOKABLE [[nodiscard]] QVariantList autoSatinGuidePaths() const;
    Q_INVOKABLE bool moveAutoSatinGuidePoint(int guide, int node, double xMm, double yMm);

    // Guides d'un remplissage (directionnel, ou convertible au premier guide).
    Q_PROPERTY(bool canGuideFill READ canGuideFill NOTIFY changed)
public:
    [[nodiscard]] bool canGuideFill() const { return canGuideFill_; }
    // [{nodes: [{x,y}], curve: [{x,y}]}] en mm modèle (guides ou lignes de rupture).
    Q_INVOKABLE [[nodiscard]] QVariantList fillGuideCurves(bool breakLines) const;
    // Indice du guide sous (x, y) à `toleranceMm` près, -1 si aucun.
    Q_INVOKABLE [[nodiscard]] int fillGuideAt(bool breakLines, double xMm, double yMm,
                                              double toleranceMm) const;
    Q_INVOKABLE bool moveFillGuidePoint(bool breakLines, int guide, int node, double xMm,
                                        double yMm);
    Q_INVOKABLE bool removeFillGuide(bool breakLines, int guide);
    // Retire le dernier guide tracé (ou la dernière rupture).
    Q_INVOKABLE bool removeLastFillGuide(bool breakLines);
    Q_INVOKABLE bool autoFillGuides();
signals:
    void changed();
    void messageChanged();

private:
    void refresh();
    bool report(const ServiceResult& result, const QString& success);
    bool refuse(const QString& why);
    [[nodiscard]] static std::optional<RailSide> sideOf(const QString& side);

    StudioController& studio_;
    std::uint64_t lastSeenRevision_{~std::uint64_t{0}};
    std::vector<Target> lastSeenSelection_;
    std::uint64_t id_{0};
    int railA_{0};
    int railB_{0};
    int rungs_{0};
    int guides_{-1};
    int breaks_{-1};
    bool canGuideFill_{false};
    std::optional<document::AutoSatinStrategy> autoStrategy_;
    bool hasGuidedPath_{false};
    SatinDiagnosis diagnosis_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
