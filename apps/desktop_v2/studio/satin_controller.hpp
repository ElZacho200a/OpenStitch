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
#include <vector>

#include "satin_column.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class SatinController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString objectId READ objectId NOTIFY changed)
    Q_PROPERTY(bool isSatin READ isSatin NOTIFY changed)
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
    SatinDiagnosis diagnosis_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
