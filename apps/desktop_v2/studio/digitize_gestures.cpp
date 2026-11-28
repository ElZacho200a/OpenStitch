// SPDX-License-Identifier: Apache-2.0
#include "digitize_gestures.hpp"

#include <algorithm>
#include <cmath>

#include "satin_column.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
constexpr double kGripPx = 9.0;
constexpr double kClickPx = 3.0;
constexpr double kStopPx = 12.0; // rayon des poignées numérotées des fils (px)

double dist2(PointMm a, PointMm b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

PointMm mapPoint(const QVariantMap& m) {
    return {m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()};
}
} // namespace

DigitizeGestures::DigitizeGestures(StudioController& studio, PointEditController& points,
                                   FadeController& fade, SatinController& satin, QObject* parent)
    : QObject(parent), studio_(studio), points_(points), fade_(fade), satin_(satin) {
    // Chaque adaptateur publie un snapshot par changement effectif de document/sélection.
    // Ne pas écouter StudioController::changed en plus : une simple sélection lançait
    // quatre reconstructions, dont trois sans rapport avec l'outil actif.
    connect(&points_, &PointEditController::changed, this, [this] {
        if (mode_ == QLatin1String("points")) {
            rebuildStatic();
        }
    });
    connect(&fade_, &FadeController::changed, this, [this] {
        if (mode_ == QLatin1String("fade")) {
            rebuildStatic();
        }
    });
    connect(&fade_, &FadeController::selectedThreadChanged, this, [this] {
        if (mode_ == QLatin1String("fade")) {
            rebuildStatic();
        }
    });
    connect(&satin_, &SatinController::changed, this, [this] {
        if (mode_ == QLatin1String("guided") &&
            (guidedTarget_ != satinId() || !satin_.isGuidedSatin() ||
             !satin_.guidedSatinDisabledReason().isEmpty())) {
            setMode({});
        } else if (mode_ == QLatin1String("satin") || isGuideMode() ||
                   mode_ == QLatin1String("break")) {
            rebuildStatic();
        }
    });
}

void DigitizeGestures::setMode(const QString& mode) {
    static const QStringList known{QString(),
                                   QStringLiteral("points"),
                                   QStringLiteral("fade"),
                                   QStringLiteral("satin"),
                                   QStringLiteral("guide"),
                                   QStringLiteral("guided"),
                                   QStringLiteral("break"),
                                   QStringLiteral("rungs")};
    if (mode == mode_ || !known.contains(mode) ||
        (mode == QLatin1String("guided") &&
         (!satin_.isGuidedSatin() || !satin_.guidedSatinDisabledReason().isEmpty()))) {
        return;
    }
    cancel();
    rungs_.clear();
    if (mode_ == QLatin1String("points") && pointsTurnedOn_) {
        points_.setActive(false);
        pointsTurnedOn_ = false;
    }
    mode_ = mode;
    hoverGuide_ = -1;
    guidedTarget_ = mode_ == QLatin1String("guided") ? satinId() : 0;
    if (mode_ == QLatin1String("points") && !points_.active()) {
        points_.setActive(true);
        pointsTurnedOn_ = true;
    }
    emit modeChanged();
    rebuildStatic();
}

bool DigitizeGestures::isGuideMode() const {
    return mode_ == QLatin1String("guide") || mode_ == QLatin1String("guided");
}

bool DigitizeGestures::isFillGuideMode() const {
    return (mode_ == QLatin1String("guide") || mode_ == QLatin1String("break")) &&
           !satin_.isAutoSatin() && satin_.canGuideFill();
}

void DigitizeGestures::hover(double x, double y, double pxPerMm) {
    if (drag_ != Drag::None || !isFillGuideMode()) {
        return;
    }
    const double scale = std::max(pxPerMm, 1e-6);
    const bool zoomed = std::abs(scale - scale_) > 1e-3 * scale; // espacement des poignées
    scale_ = scale;
    const auto p = toModel(x, y);
    const int guide =
        satin_.fillGuideAt(mode_ == QLatin1String("break"), p.x, p.y, kGripPx / scale_);
    // Le guide montré reste le dernier survolé tant qu'aucun autre ne l'est : aller du tracé
    // vers un nœud un peu à l'écart ne fait pas disparaître ses poignées.
    if (guide >= 0 && guide != hoverGuide_) {
        hoverGuide_ = guide;
        rebuildStatic();
    } else if (zoomed && hoverGuide_ >= 0) {
        rebuildStatic();
    }
}

QString DigitizeGestures::hint() const {
    if (mode_ == QLatin1String("points")) {
        return tr("Cliquez un point, glissez pour déplacer ; rectangle pour sélectionner ; Maj : "
                  "plage ; Ctrl : ajouter ; Alt+clic : insérer ; Suppr : supprimer.");
    }
    if (mode_ == QLatin1String("fade")) {
        return tr("Cliquez un fil numéroté pour le régler ; glissez le premier ou le dernier pour "
                  "placer la transition, la ligne pour la déplacer, le carré pour la tourner "
                  "(Maj : symétrique).");
    }
    if (mode_ == QLatin1String("satin")) {
        return tr("Glissez un nœud de rail ou une extrémité de barreau ; Alt+clic sur un rail : "
                  "insérer un nœud ; Maj+clic sur un barreau : le retirer.");
    }
    if (mode_ == QLatin1String("guide")) {
        if (satin_.isAutoSatin()) {
            return tr("Tracez un guide sur le satin ; sur le satin guidé, glissez ses nœuds pour "
                      "remodeler la ligne centrale.");
        }
        return tr("Tracez un guide sur le remplissage ; survolez un guide pour voir ses nœuds, "
                  "glissez-en un pour le remodeler ; Maj+clic sur un guide : le retirer.");
    }
    if (mode_ == QLatin1String("break")) {
        if (!isFillGuideMode()) {
            return tr("Tracez une ligne de rupture à travers le remplissage.");
        }
        return tr("Tracez une ligne de rupture à travers le remplissage ; survolez une rupture "
                  "pour voir ses nœuds, glissez-en un pour la remodeler ; Maj+clic : la retirer.");
    }
    if (mode_ == QLatin1String("rungs")) {
        return tr("Glissez de rail en rail pour poser chaque barreau, Entrée crée la colonne.");
    }
    return {};
}

void DigitizeGestures::note(const QString& text, bool ok) {
    Q_UNUSED(ok)
    message_ = text;
    emit messageChanged();
}

// --- Poignées ---------------------------------------------------------------------------------

std::vector<DigitizeGestures::Grip> DigitizeGestures::grips() const {
    std::vector<Grip> out;
    const auto id = satinId();
    if (id == 0) {
        return out;
    }
    if (isFillGuideMode()) {
        // Seuls les nœuds du guide sous le pointeur sont saisissables (et dessinés) : sur un
        // remplissage à huit guides automatiques, toutes les poignées formaient un amas.
        const auto curves = satin_.fillGuideCurves(mode_ == QLatin1String("break"));
        for (int guide = 0; guide < curves.size(); ++guide) {
            if (guide != hoverGuide_) {
                continue;
            }
            const auto nodes = curves[guide].toMap().value(QStringLiteral("nodes")).toList();
            for (int node = 0; node < nodes.size(); ++node) {
                out.push_back(
                    {Grip::Type::FillGuideNode, guide, node, mapPoint(nodes[node].toMap())});
            }
        }
        return out;
    }
    if (isGuideMode()) {
        const auto paths = satin_.autoSatinGuidePaths();
        for (int guide = 0; guide < paths.size(); ++guide) {
            const auto points = paths[guide].toList();
            for (int node = 0; node < points.size(); ++node) {
                out.push_back({Grip::Type::GuideNode, guide, node, mapPoint(points[node].toMap())});
            }
        }
        return out;
    }
    const auto& project = studio_.session().project();
    for (int side = 0; side < 2; ++side) {
        const auto nodes = satinRailNodes(project, id, side == 0 ? RailSide::A : RailSide::B);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            out.push_back({Grip::Type::RailNode, side, static_cast<int>(i), nodes[i]});
        }
    }
    const auto rungs = satinRungs(project, id);
    for (std::size_t i = 0; i < rungs.size(); ++i) {
        out.push_back({Grip::Type::RungEnd, 0, static_cast<int>(i), rungs[i].first});
        out.push_back({Grip::Type::RungEnd, 1, static_cast<int>(i), rungs[i].second});
    }
    return out;
}

std::optional<DigitizeGestures::Grip> DigitizeGestures::gripAt(PointMm p) const {
    const double tol = kGripPx / scale_;
    std::optional<Grip> best;
    double bestD = tol * tol;
    for (const auto& g : grips()) {
        const double d = dist2(g.pos, p);
        // Un nœud de rail est prioritaire sur l'extrémité de barreau qui le recouvre.
        if (d < bestD || (best && d == bestD && g.type == Grip::Type::RailNode)) {
            bestD = d;
            best = g;
        }
    }
    return best;
}

PointMm DigitizeGestures::fadeHandlePos(const QVariantMap& axis, int handle) {
    static const char* const keys[] = {"start", "end", "centre", "rotate"};
    return mapPoint(axis.value(QString::fromLatin1(keys[handle])).toMap());
}

int DigitizeGestures::fadeHandleAt(PointMm p) const {
    const auto axis = fade_.axis();
    if (axis.isEmpty()) {
        return -1;
    }
    const double tol = kGripPx / scale_;
    int best = -1;
    double bestD = tol * tol;
    for (const int h : {0, 1, 3, 2}) {
        const double d = dist2(fadeHandlePos(axis, h), p);
        if (d <= bestD) {
            bestD = d;
            best = h;
        }
    }
    return best;
}

PointMm DigitizeGestures::fadeStopPos(const QVariantMap& axis, int index, int count) {
    const auto a = fadeHandlePos(axis, 0);
    const auto b = fadeHandlePos(axis, 1);
    const double t = count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1) : 0.0;
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

bool DigitizeGestures::nearFadeLine(const QVariantMap& axis, PointMm p) const {
    if (axis.isEmpty()) {
        return false;
    }
    const auto a = fadeHandlePos(axis, 0);
    const auto b = fadeHandlePos(axis, 1);
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double t =
        len2 > 0.0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    const double tol = kGripPx / scale_;
    return dist2({a.x + dx * t, a.y + dy * t}, p) <= tol * tol;
}

int DigitizeGestures::fadeStopAt(PointMm p) const {
    const auto axis = fade_.axis();
    const int count = static_cast<int>(fade_.colors().size());
    if (axis.isEmpty() || count < 2) {
        return -1;
    }
    const double tol = kStopPx / scale_;
    int best = -1;
    double bestD = tol * tol;
    for (int i = 0; i < count; ++i) {
        const double d = dist2(fadeStopPos(axis, i, count), p);
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

bool DigitizeGestures::insertRailNodeAt(PointMm p) {
    const auto id = satinId();
    if (id == 0) {
        return false;
    }
    const double tol = kGripPx / scale_;
    for (int side = 0; side < 2; ++side) {
        const auto nodes =
            satinRailNodes(studio_.session().project(), id, side == 0 ? RailSide::A : RailSide::B);
        for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
            const double vx = nodes[i + 1].x - nodes[i].x;
            const double vy = nodes[i + 1].y - nodes[i].y;
            const double len2 = vx * vx + vy * vy;
            if (len2 <= 0.0) {
                continue;
            }
            const double t = ((p.x - nodes[i].x) * vx + (p.y - nodes[i].y) * vy) / len2;
            if (t <= 0.02 || t >= 0.98) {
                continue;
            }
            const PointMm foot{nodes[i].x + t * vx, nodes[i].y + t * vy};
            if (dist2(foot, p) <= tol * tol) {
                const bool ok = satin_.insertRailNode(
                    side == 0 ? QStringLiteral("A") : QStringLiteral("B"), static_cast<int>(i), t);
                note(satin_.message(), ok);
                return ok;
            }
        }
    }
    return false;
}

// --- Événements -------------------------------------------------------------------------------

bool DigitizeGestures::press(double x, double y, double pxPerMm, Qt::KeyboardModifiers mods) {
    if (mode_.isEmpty()) {
        return false;
    }
    scale_ = std::max(pxPerMm, 1e-6);
    mods_ = mods;
    anchor_ = current_ = toModel(x, y);
    travelled_ = false;
    additive_ = mods.testFlag(Qt::ControlModifier);
    stroke_.clear();
    grip_.reset();
    hitIndex_ = -1;
    pressObject_ = satinId();
    pressRevision_ = studio_.session().revision();

    if (mode_ == QLatin1String("points")) {
        if (points_.objectId().isEmpty()) {
            note(tr("Sélectionnez un objet à retoucher."), false);
            return false;
        }
        const double tol = kGripPx / scale_;
        const int hit = points_.pointAt(anchor_.x, anchor_.y, tol);
        if (mods.testFlag(Qt::AltModifier)) {
            // Insertion : après le point le plus proche (n'importe où, la souris donne la
            // position).
            const int near = points_.pointAt(anchor_.x, anchor_.y, 1e6);
            if (near >= 0) {
                const bool ok = points_.insertAfter(near, anchor_.x, anchor_.y);
                note(points_.message(), ok);
            }
            return false;
        }
        if (hit >= 0) {
            hitIndex_ = hit;
            const bool selected = points_.selection().contains(static_cast<std::size_t>(hit));
            if (mods.testFlag(Qt::ShiftModifier) && rangeAnchor_ >= 0) {
                points_.selectRange(rangeAnchor_, hit);
            } else if (additive_) {
                points_.selectPoint(hit, true);
            } else if (!selected) {
                points_.selectPoint(hit, false);
            }
            if (!mods.testFlag(Qt::ShiftModifier)) {
                rangeAnchor_ = hit;
            }
            drag_ = Drag::Points;
        } else {
            drag_ = Drag::Rect;
        }
    } else if (mode_ == QLatin1String("fade")) {
        fadeHandle_ = fadeHandleAt(anchor_);
        const int count = static_cast<int>(fade_.colors().size());
        const auto axis = fade_.axis();
        fadeOffset_ = {};
        if (fadeHandle_ == 0 || fadeHandle_ == 1) {
            // Le premier et le dernier fil SONT les extrémités de la transition.
            fade_.setSelectedThread(fadeHandle_ == 0 ? 0 : count - 1);
        } else if (fadeHandle_ != 3) {
            // Un fil du milieu ou la ligne : un clic désigne le fil, un glissé déplace le fondu.
            const int stop = fadeStopAt(anchor_);
            if (stop >= 0) {
                fade_.setSelectedThread(stop);
                note(tr("Fil %1 désigné : réglez-le dans l'inspecteur.").arg(stop + 1));
            } else if (fadeHandle_ != 2 && !nearFadeLine(axis, anchor_)) {
                return false;
            }
            fadeHandle_ = 2;
            const auto centre = fadeHandlePos(axis, 2);
            fadeOffset_ = {centre.x - anchor_.x, centre.y - anchor_.y};
        }
        fadePreview_ = fade_.axis();
        drag_ = Drag::Fade;
    } else if (mode_ == QLatin1String("satin")) {
        if (!satin_.isSatin()) {
            note(tr("Sélectionnez une colonne satin à rails."), false);
            return false;
        }
        if (mods.testFlag(Qt::AltModifier)) {
            insertRailNodeAt(anchor_);
            return false;
        }
        grip_ = gripAt(anchor_);
        if (!grip_) {
            return false;
        }
        if (mods.testFlag(Qt::ShiftModifier) && grip_->type == Grip::Type::RungEnd) {
            const bool ok = satin_.removeRung(grip_->index);
            note(satin_.message(), ok);
            grip_.reset();
            return false;
        }
        drag_ = grip_->type == Grip::Type::RailNode ? Drag::RailNode : Drag::RungEnd;
    } else if (isGuideMode() || mode_ == QLatin1String("break")) {
        if (points_.objectId().isEmpty() && satin_.objectId().isEmpty()) {
            note(tr("Sélectionnez un remplissage."), false);
            return false;
        }
        if (isFillGuideMode()) {
            const bool isBreak = mode_ == QLatin1String("break");
            if (mods.testFlag(Qt::ShiftModifier)) {
                // Maj+clic : retire le guide (ou la rupture) désigné, sans tracer.
                const int hit = satin_.fillGuideAt(isBreak, anchor_.x, anchor_.y, kGripPx / scale_);
                if (hit < 0) {
                    note(isBreak ? tr("Aucune ligne de rupture sous le pointeur.")
                                 : tr("Aucun guide sous le pointeur."),
                         false);
                    return false;
                }
                const bool ok = satin_.removeFillGuide(isBreak, hit);
                note(satin_.message(), ok);
                return false;
            }
            hoverGuide_ = satin_.fillGuideAt(isBreak, anchor_.x, anchor_.y, kGripPx / scale_);
            grip_ = gripAt(anchor_);
            if (grip_ && grip_->type == Grip::Type::FillGuideNode) {
                drag_ = Drag::GuideNode;
                rebuildDynamic();
                return true;
            }
            grip_.reset();
        } else if (isGuideMode()) {
            grip_ = gripAt(anchor_);
            if (grip_ && grip_->type == Grip::Type::GuideNode) {
                drag_ = Drag::GuideNode;
                rebuildDynamic();
                return true;
            }
            if (mode_ == QLatin1String("guided") && satin_.hasGuidedPath()) {
                return false;
            }
        }
        stroke_.push_back(anchor_);
        drag_ = Drag::Stroke;
    } else if (mode_ == QLatin1String("rungs")) {
        drag_ = Drag::NewRung;
    } else {
        return false;
    }
    rebuildDynamic();
    return true;
}

void DigitizeGestures::move(double x, double y, Qt::KeyboardModifiers mods) {
    if (drag_ == Drag::None) {
        return;
    }
    mods_ = mods;
    current_ = toModel(x, y);
    if (std::sqrt(dist2(current_, anchor_)) * scale_ >= kClickPx) {
        travelled_ = true;
    }
    if (drag_ == Drag::Fade) {
        const auto preview =
            fade_.previewHandle(fadeHandle_, current_.x + fadeOffset_.x, current_.y + fadeOffset_.y,
                                mods.testFlag(Qt::ShiftModifier));
        if (!preview.isEmpty()) {
            fadePreview_ = preview;
        }
    } else if (drag_ == Drag::Stroke) {
        const double step = 2.0 / scale_;
        if (dist2(stroke_.back(), current_) >= step * step) {
            stroke_.push_back(current_);
        }
    }
    rebuildDynamic();
}

void DigitizeGestures::release(double x, double y, Qt::KeyboardModifiers mods) {
    if (drag_ == Drag::None) {
        return;
    }
    mods_ = mods;
    current_ = toModel(x, y);
    if (std::sqrt(dist2(current_, anchor_)) * scale_ >= kClickPx) {
        travelled_ = true;
    }
    const auto drag = drag_;
    const auto anchor = anchor_;
    const auto current = current_;
    const bool travelled = travelled_;
    // Le geste est terminé avant de muter : la mutation relance la chaîne de signaux.
    drag_ = Drag::None;
    const auto grip = grip_;
    grip_.reset();
    auto stroke = std::move(stroke_);
    stroke_.clear();
    bool ok = true;
    // Une poignée désigne un nœud par indices : si le document ou la sélection a changé pendant
    // le geste (Ctrl+Z, sélection au clavier), ces indices ne désignent plus le même nœud.
    if (grip && (satinId() != pressObject_ || studio_.session().revision() != pressRevision_)) {
        note(tr("Le document a changé pendant le geste : déplacement annulé."), false);
        info_.clear();
        rebuildDynamic();
        rebuildStatic();
        return;
    }
    switch (drag) {
    case Drag::Points:
        if (travelled) {
            ok = points_.moveSelected(current.x - anchor.x, current.y - anchor.y);
            note(points_.message(), ok);
        } else if (!additive_ && !mods.testFlag(Qt::ShiftModifier) && hitIndex_ >= 0) {
            points_.selectPoint(hitIndex_, false); // clic simple : ce point seul
        }
        break;
    case Drag::Rect:
        if (travelled) {
            points_.selectRect(anchor.x, anchor.y, current.x, current.y, additive_);
        } else if (!additive_) {
            points_.clearPointSelection();
        }
        break;
    case Drag::Fade:
        if (travelled) {
            ok = fade_.commitHandle(fadeHandle_, current.x + fadeOffset_.x,
                                    current.y + fadeOffset_.y, mods.testFlag(Qt::ShiftModifier));
            note(fade_.message(), ok);
        }
        break;
    case Drag::RailNode:
        if (travelled && grip) {
            ok = satin_.moveRailNode(grip->side == 0 ? QStringLiteral("A") : QStringLiteral("B"),
                                     grip->index, current.x, current.y);
            note(satin_.message(), ok);
        }
        break;
    case Drag::RungEnd:
        if (travelled && grip) {
            const auto rungs = satinRungs(studio_.session().project(), satinId());
            if (grip->index >= 0 && static_cast<std::size_t>(grip->index) < rungs.size()) {
                auto a = rungs[static_cast<std::size_t>(grip->index)].first;
                auto b = rungs[static_cast<std::size_t>(grip->index)].second;
                (grip->side == 0 ? a : b) = current;
                ok = satin_.moveRung(grip->index, a.x, a.y, b.x, b.y);
                note(satin_.message(), ok);
            }
        }
        break;
    case Drag::GuideNode:
        if (travelled && grip && grip->type == Grip::Type::FillGuideNode) {
            ok = satin_.moveFillGuidePoint(mode_ == QLatin1String("break"), grip->side, grip->index,
                                           current.x, current.y);
            note(satin_.message(), ok);
        } else if (travelled && grip) {
            ok = satin_.moveAutoSatinGuidePoint(grip->side, grip->index, current.x, current.y);
            note(satin_.message(), ok);
        }
        break;
    case Drag::Stroke:
        if (stroke.size() >= 2) {
            QVariantList list;
            for (const auto& p : stroke) {
                list.push_back(QVariantMap{{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}});
            }
            ok = satin_.addGuide(list, mode_ == QLatin1String("break"));
            note(satin_.message(), ok);
        }
        break;
    case Drag::NewRung:
        if (travelled) {
            rungs_.emplace_back(anchor, current);
        }
        break;
    case Drag::None:
        break;
    }
    info_.clear();
    rebuildDynamic();
    rebuildStatic();
}

bool DigitizeGestures::keyPress(int key) {
    if (mode_.isEmpty()) {
        return false;
    }
    if (key == Qt::Key_Escape) {
        return cancel();
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && mode_ == QLatin1String("rungs")) {
        return finishRungs();
    }
    if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && mode_ == QLatin1String("points") &&
        drag_ == Drag::None) {
        const bool ok = points_.deleteSelected();
        note(points_.message(), ok);
        return true;
    }
    return false;
}

bool DigitizeGestures::cancel() {
    if (drag_ != Drag::None) {
        drag_ = Drag::None;
        stroke_.clear();
        grip_.reset();
        info_.clear();
        rebuildDynamic();
        return true;
    }
    if (!rungs_.empty()) {
        rungs_.clear();
        rebuildDynamic();
        return true;
    }
    return false;
}

bool DigitizeGestures::finishRungs() {
    if (rungs_.size() < 2) {
        note(tr("Posez au moins deux barreaux."), false);
        return false;
    }
    QVariantList list;
    for (const auto& [a, b] : rungs_) {
        list.push_back(QVariantMap{{QStringLiteral("ax"), a.x},
                                   {QStringLiteral("ay"), a.y},
                                   {QStringLiteral("bx"), b.x},
                                   {QStringLiteral("by"), b.y}});
    }
    const auto id = satin_.createFromRungs(list);
    note(satin_.message(), !id.isEmpty());
    if (id.isEmpty()) {
        return false;
    }
    rungs_.clear();
    rebuildDynamic();
    return true;
}

// --- Surcouche --------------------------------------------------------------------------------

void DigitizeGestures::rebuildStatic() {
    static_.clear();
    if (mode_ == QLatin1String("points") && points_.active()) {
        HandleItem dots{HandleItem::Kind::Dots, {}};
        HandleItem selected{HandleItem::Kind::Selected, {}};
        const auto& chosen = points_.selection();
        for (const auto& p : points_.view().points) {
            (chosen.contains(p.index) ? selected : dots).points.push_back(toScene(p.pos));
        }
        static_.push_back(std::move(dots));
        static_.push_back(std::move(selected));
    } else if (mode_ == QLatin1String("fade") && fade_.hasFade()) {
        const auto axis = fade_.axis();
        if (!axis.isEmpty()) {
            static_.push_back({HandleItem::Kind::Line,
                               {toScene(fadeHandlePos(axis, 0)), toScene(fadeHandlePos(axis, 1))}});
            static_.push_back({HandleItem::Kind::Line,
                               {toScene(fadeHandlePos(axis, 2)), toScene(fadeHandlePos(axis, 3))}});
            // Début et fin sont dessinés par les fils numérotés qui les occupent ; la ligne et
            // les fils du milieu déplacent le fondu, le carré au bout du bras le tourne.
            static_.push_back({HandleItem::Kind::Handle, {toScene(fadeHandlePos(axis, 3))}});
            const auto colours = fade_.colors();
            HandleItem stops{HandleItem::Kind::Stop, {}};
            stops.selected = fade_.selectedThread();
            const int count = static_cast<int>(colours.size());
            for (int i = 0; i < count; ++i) {
                stops.points.push_back(toScene(fadeStopPos(axis, i, count)));
                stops.colors.push_back(colours[i].toString());
            }
            static_.push_back(std::move(stops));
        }
    } else if (isFillGuideMode()) {
        // Guides (ou ruptures) existants : tracé aplati + poignée sur chaque nœud éditable.
        HandleItem handles{HandleItem::Kind::Handle, {}};
        const auto curves = satin_.fillGuideCurves(mode_ == QLatin1String("break"));
        if (hoverGuide_ >= curves.size()) {
            hoverGuide_ = -1; // guide retiré (annulation, Maj+clic)
        }
        for (int guide = 0; guide < curves.size(); ++guide) {
            const auto map = curves[guide].toMap();
            HandleItem line{HandleItem::Kind::Line, {}};
            for (const auto& rawPoint : map.value(QStringLiteral("curve")).toList()) {
                line.points.push_back(toScene(mapPoint(rawPoint.toMap())));
            }
            if (line.points.size() >= 2) {
                static_.push_back(std::move(line));
            }
            if (guide != hoverGuide_) {
                continue;
            }
            // Affichage seulement : une poignée trop proche (à l'écran) de la précédente n'est
            // pas dessinée ; le nœud reste saisissable (le plus proche du pointeur l'emporte).
            const double minGap = 1.5 * kGripPx / scale_;
            std::optional<PointMm> previous;
            for (const auto& rawPoint : map.value(QStringLiteral("nodes")).toList()) {
                const auto p = mapPoint(rawPoint.toMap());
                if (previous && dist2(*previous, p) < minGap * minGap) {
                    continue;
                }
                previous = p;
                handles.points.push_back(toScene(p));
            }
        }
        static_.push_back(std::move(handles));
    } else if (isGuideMode()) {
        const auto paths = satin_.autoSatinGuidePaths();
        HandleItem handles{HandleItem::Kind::Handle, {}};
        for (const auto& raw : paths) {
            HandleItem line{HandleItem::Kind::Line, {}};
            for (const auto& rawPoint : raw.toList()) {
                const auto p = mapPoint(rawPoint.toMap());
                line.points.push_back(toScene(p));
                handles.points.push_back(toScene(p));
            }
            if (line.points.size() >= 2) {
                static_.push_back(std::move(line));
            }
        }
        static_.push_back(std::move(handles));
    } else if (mode_ == QLatin1String("satin") && satin_.isSatin()) {
        const auto id = satinId();
        const auto& project = studio_.session().project();
        HandleItem nodes{HandleItem::Kind::Handle, {}};
        for (int side = 0; side < 2; ++side) {
            HandleItem rail{HandleItem::Kind::Line, {}};
            for (const auto& p :
                 satinRailNodes(project, id, side == 0 ? RailSide::A : RailSide::B)) {
                rail.points.push_back(toScene(p));
                nodes.points.push_back(toScene(p));
            }
            static_.push_back(std::move(rail));
        }
        for (const auto& [a, b] : satinRungs(project, id)) {
            static_.push_back({HandleItem::Kind::Ghost, {toScene(a), toScene(b)}});
        }
        static_.push_back(std::move(nodes));
    }
    emit overlayChanged();
}

void DigitizeGestures::rebuildDynamic() {
    dynamic_.clear();
    for (const auto& [a, b] : rungs_) {
        dynamic_.push_back({HandleItem::Kind::Line, {toScene(a), toScene(b)}});
    }
    if (drag_ == Drag::Points) {
        const double dx = current_.x - anchor_.x;
        const double dy = current_.y - anchor_.y;
        HandleItem ghost{HandleItem::Kind::Active, {}};
        for (const auto& p : points_.view().points) {
            if (points_.selection().contains(p.index)) {
                ghost.points.push_back(toScene({p.pos.x + dx, p.pos.y + dy}));
            }
        }
        dynamic_.push_back(std::move(ghost));
        info_ = QStringLiteral("%1 ; %2 mm").arg(dx, 0, 'f', 2).arg(dy, 0, 'f', 2);
    } else if (drag_ == Drag::Rect) {
        dynamic_.push_back({HandleItem::Kind::Closed,
                            {toScene({anchor_.x, anchor_.y}), toScene({current_.x, anchor_.y}),
                             toScene({current_.x, current_.y}), toScene({anchor_.x, current_.y})}});
    } else if (drag_ == Drag::Fade && !fadePreview_.isEmpty()) {
        dynamic_.push_back(
            {HandleItem::Kind::Ghost,
             {toScene(fadeHandlePos(fadePreview_, 0)), toScene(fadeHandlePos(fadePreview_, 1))}});
        dynamic_.push_back({HandleItem::Kind::Active, {toScene(current_)}});
    } else if ((drag_ == Drag::RailNode || drag_ == Drag::RungEnd || drag_ == Drag::GuideNode) &&
               grip_) {
        dynamic_.push_back({HandleItem::Kind::Active, {toScene(current_)}});
        dynamic_.push_back({HandleItem::Kind::Ghost, {toScene(grip_->pos), toScene(current_)}});
    } else if (drag_ == Drag::Stroke) {
        HandleItem line{HandleItem::Kind::Line, {}};
        for (const auto& p : stroke_) {
            line.points.push_back(toScene(p));
        }
        dynamic_.push_back(std::move(line));
    } else if (drag_ == Drag::NewRung) {
        dynamic_.push_back({HandleItem::Kind::Ghost, {toScene(anchor_), toScene(current_)}});
    }
    emit overlayChanged();
}

} // namespace openstitch::desktop_v2::studio
