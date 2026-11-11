// SPDX-License-Identifier: Apache-2.0
#include "satin_controller.hpp"

#include <algorithm>

#include "direction_guides.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}

QVariantList indices(const std::vector<std::size_t>& v) {
    QVariantList list;
    for (const auto i : v) {
        list.push_back(static_cast<qulonglong>(i));
    }
    return list;
}
} // namespace

SatinController::SatinController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &SatinController::refresh);
    refresh();
}

SatinController::~SatinController() = default;

void SatinController::refresh() {
    const auto& session = studio_.session();
    const auto& targets = session.selection().targets();
    if (lastSeenRevision_ == session.revision() && lastSeenSelection_ == targets) {
        return; // diagnostic satin inchangé pour les événements de statut
    }
    lastSeenRevision_ = session.revision();
    lastSeenSelection_ = targets;
    std::uint64_t id = 0;
    int count = 0;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            id = target.id;
            ++count;
        }
    }
    id_ = count == 1 ? id : 0;
    railA_ = railB_ = rungs_ = 0;
    guides_ = breaks_ = -1;
    diagnosis_ = SatinDiagnosis{};
    if (id_ != 0) {
        const auto& project = studio_.session().project();
        railA_ = std::max(0, railNodeCount(project, id_, RailSide::A));
        railB_ = std::max(0, railNodeCount(project, id_, RailSide::B));
        rungs_ = std::max(0, satinRungCount(project, id_));
        diagnosis_ = diagnoseSatin(project, id_);
        guides_ = directionGuideCount(project, id_);
        breaks_ = desktop_v2::breakLineCount(project, id_);
    }
    emit changed();
}

QString SatinController::objectId() const {
    return id_ == 0 ? QString() : QString::number(id_);
}

QString SatinController::diagnosis() const {
    return diagnosis_.isSatin ? q(satinDiagnosisSummary(diagnosis_)) : QString();
}

QVariantMap SatinController::diagnosisInfo() const {
    return {{QStringLiteral("isSatin"), diagnosis_.isSatin},
            {QStringLiteral("railsCross"), diagnosis_.railsCross},
            {QStringLiteral("crossing"), indices(diagnosis_.crossing)},
            {QStringLiteral("tooWide"), indices(diagnosis_.tooWide)},
            {QStringLiteral("widestMm"), diagnosis_.widestMm},
            {QStringLiteral("rungCount"), diagnosis_.rungCount}};
}

QVariantList SatinController::rungs() const {
    QVariantList list;
    if (id_ == 0) {
        return list;
    }
    for (const auto& [a, b] : satinRungs(studio_.session().project(), id_)) {
        list.push_back(QVariantMap{{QStringLiteral("ax"), a.x},
                                   {QStringLiteral("ay"), a.y},
                                   {QStringLiteral("bx"), b.x},
                                   {QStringLiteral("by"), b.y}});
    }
    return list;
}

bool SatinController::refuse(const QString& why) {
    ok_ = false;
    message_ = why;
    emit messageChanged();
    return false;
}

bool SatinController::report(const ServiceResult& result, const QString& success) {
    ok_ = result.ok;
    message_ = result.ok ? success : q(result.message);
    emit messageChanged();
    return result.ok;
}

std::optional<RailSide> SatinController::sideOf(const QString& side) {
    if (side == QLatin1String("A")) {
        return RailSide::A;
    }
    if (side == QLatin1String("B")) {
        return RailSide::B;
    }
    return std::nullopt;
}

QString SatinController::createFromRungs(const QVariantList& rungs) {
    std::vector<std::pair<PointMm, PointMm>> list;
    for (const auto& item : rungs) {
        const auto m = item.toMap();
        list.push_back({PointMm{m.value(QStringLiteral("ax")).toDouble(),
                                m.value(QStringLiteral("ay")).toDouble()},
                        PointMm{m.value(QStringLiteral("bx")).toDouble(),
                                m.value(QStringLiteral("by")).toDouble()}});
    }
    const auto result = createSatinColumn(studio_.session(), list);
    if (!result.ok) {
        refuse(q(result.message));
        return {};
    }
    studio_.session().selection().replace({Target{TargetKind::EmbroideryObject, result.objectId}});
    ok_ = true;
    message_ =
        result.railsCross
            ? q(result.message) + QStringLiteral(" Les rails se croisent : revoyez les barreaux.")
            : QStringLiteral("Colonne satin créée.");
    emit messageChanged();
    return QString::number(result.objectId);
}

bool SatinController::moveRailNode(const QString& side, int node, double xMm, double yMm) {
    const auto s = sideOf(side);
    if (id_ == 0 || !s || node < 0) {
        return refuse(QStringLiteral("Rail ou nœud inconnu."));
    }
    return report(moveSatinRailNode(studio_.session(), id_, *s, static_cast<std::size_t>(node),
                                    PointMm{xMm, yMm}),
                  QStringLiteral("Nœud du rail déplacé."));
}

bool SatinController::insertRailNode(const QString& side, int segment, double t) {
    const auto s = sideOf(side);
    if (id_ == 0 || !s || segment < 0) {
        return refuse(QStringLiteral("Rail ou segment inconnu."));
    }
    return report(
        insertSatinRailNode(studio_.session(), id_, *s, static_cast<std::size_t>(segment), t),
        QStringLiteral("Nœud inséré sur le rail."));
}

bool SatinController::addRung(int index, double ax, double ay, double bx, double by) {
    if (id_ == 0 || index < 0) {
        return refuse(QStringLiteral("Sélectionnez une colonne satin."));
    }
    return report(addSatinRung(studio_.session(), id_, static_cast<std::size_t>(index),
                               PointMm{ax, ay}, PointMm{bx, by}),
                  QStringLiteral("Barreau ajouté."));
}

bool SatinController::insertRungBetween(int index) {
    if (id_ == 0 || index < 0) {
        return refuse(QStringLiteral("Sélectionnez une colonne satin."));
    }
    return report(insertSatinRungBetween(studio_.session(), id_, static_cast<std::size_t>(index)),
                  QStringLiteral("Barreau inséré entre deux barreaux."));
}

bool SatinController::moveRung(int index, double ax, double ay, double bx, double by) {
    if (id_ == 0 || index < 0) {
        return refuse(QStringLiteral("Sélectionnez une colonne satin."));
    }
    return report(moveSatinRung(studio_.session(), id_, static_cast<std::size_t>(index),
                                PointMm{ax, ay}, PointMm{bx, by}),
                  QStringLiteral("Barreau déplacé."));
}

bool SatinController::removeRung(int index) {
    if (id_ == 0 || index < 0) {
        return refuse(QStringLiteral("Sélectionnez une colonne satin."));
    }
    return report(removeSatinRung(studio_.session(), id_, static_cast<std::size_t>(index)),
                  QStringLiteral("Barreau supprimé."));
}

int SatinController::widestGap() const {
    if (id_ == 0) {
        return -1;
    }
    const auto gap = widestRungGap(studio_.session().project(), id_);
    return gap ? static_cast<int>(*gap) : -1;
}

int SatinController::redundantRungIndex() const {
    if (id_ == 0) {
        return -1;
    }
    const auto rung = redundantRung(studio_.session().project(), id_);
    return rung ? static_cast<int>(*rung) : -1;
}

bool SatinController::addGuide(const QVariantList& points, bool isBreak) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un remplissage."));
    }
    std::vector<PointMm> list;
    for (const auto& item : points) {
        const auto m = item.toMap();
        list.push_back(PointMm{m.value(QStringLiteral("x")).toDouble(),
                               m.value(QStringLiteral("y")).toDouble()});
    }
    return report(addDirectionGuide(studio_.session(), id_, list, isBreak),
                  isBreak ? QStringLiteral("Ligne de rupture tracée.")
                          : QStringLiteral("Guide de direction tracé."));
}

QVariantList SatinController::autoSatinGuidePaths() const {
    QVariantList result;
    if (id_ == 0) {
        return result;
    }
    for (const auto& path : desktop_v2::autoSatinGuidePaths(studio_.session().project(), id_)) {
        QVariantList nodes;
        for (const auto& p : path) {
            nodes.push_back(QVariantMap{{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}});
        }
        result.push_back(QVariant(nodes));
    }
    return result;
}

bool SatinController::moveAutoSatinGuidePoint(int guide, int node, double xMm, double yMm) {
    if (id_ == 0 || guide < 0 || node < 0) {
        return refuse(QStringLiteral("Sélectionnez un nœud de guide satin."));
    }
    return report(desktop_v2::moveAutoSatinGuideNode(
                      studio_.session(), id_, static_cast<std::size_t>(guide),
                      static_cast<std::size_t>(node), PointMm{xMm, yMm}),
                  QStringLiteral("Nœud du guide satin déplacé."));
}

bool SatinController::clearGuides(bool breakLines) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un remplissage."));
    }
    return report(clearDirectionGuides(studio_.session(), id_, breakLines),
                  breakLines ? QStringLiteral("Lignes de rupture effacées.")
                             : QStringLiteral("Guides effacés."));
}

} // namespace openstitch::desktop_v2::studio
