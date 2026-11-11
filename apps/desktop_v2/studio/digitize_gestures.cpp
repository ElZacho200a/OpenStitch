// SPDX-License-Identifier: Apache-2.0
#include "digitize_gestures.hpp"

#include <algorithm>
#include <cmath>

#include "satin_column.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
constexpr double kGripPx = 9.0;
constexpr double kClickPx = 3.0;

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
    connect(&satin_, &SatinController::changed, this, [this] {
        if (mode_ == QLatin1String("satin") || mode_ == QLatin1String("guide")) {
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
                                   QStringLiteral("break"),
                                   QStringLiteral("rungs")};
    if (mode == mode_ || !known.contains(mode)) {
        return;
    }
    cancel();
    rungs_.clear();
    if (mode_ == QLatin1String("points") && pointsTurnedOn_) {
        points_.setActive(false);
        pointsTurnedOn_ = false;
    }
    mode_ = mode;
    if (mode_ == QLatin1String("points") && !points_.active()) {
        points_.setActive(true);
        pointsTurnedOn_ = true;
    }
    emit modeChanged();
    rebuildStatic();
}

QString DigitizeGestures::hint() const {
    if (mode_ == QLatin1String("points")) {
        return tr("Cliquez un point, glissez pour déplacer ; rectangle pour sélectionner ; Maj : "
                  "plage ; Ctrl : ajouter ; Alt+clic : insérer ; Suppr : supprimer.");
    }
    if (mode_ == QLatin1String("fade")) {
        return tr(
            "Glissez les poignées de l'axe : début, fin, centre ou rotation (Maj : symétrique).");
    }
    if (mode_ == QLatin1String("satin")) {
        return tr("Glissez un nœud de rail ou une extrémité de barreau ; Alt+clic sur un rail : "
                  "insérer un nœud ; Maj+clic sur un barreau : le retirer.");
    }
    if (mode_ == QLatin1String("guide")) {
        return tr("Tracez un guide sur le remplissage ; sur le satin guidé, glissez ses nœuds "
                  "pour remodeler la ligne centrale.");
    }
    if (mode_ == QLatin1String("break")) {
        return tr("Tracez une ligne de rupture à travers le remplissage.");
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
    if (mode_ == QLatin1String("guide")) {
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
        if (fadeHandle_ < 0) {
            return false;
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
    } else if (mode_ == QLatin1String("guide") || mode_ == QLatin1String("break")) {
        if (points_.objectId().isEmpty() && satin_.objectId().isEmpty()) {
            note(tr("Sélectionnez un remplissage."), false);
            return false;
        }
        if (mode_ == QLatin1String("guide")) {
            grip_ = gripAt(anchor_);
            if (grip_ && grip_->type == Grip::Type::GuideNode) {
                drag_ = Drag::GuideNode;
                rebuildDynamic();
                return true;
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
        const auto preview = fade_.previewHandle(fadeHandle_, current_.x, current_.y,
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
            ok = fade_.commitHandle(fadeHandle_, current.x, current.y,
                                    mods.testFlag(Qt::ShiftModifier));
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
        if (travelled && grip) {
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
            HandleItem handles{HandleItem::Kind::Handle, {}};
            for (int h = 0; h < 4; ++h) {
                handles.points.push_back(toScene(fadeHandlePos(axis, h)));
            }
            static_.push_back(std::move(handles));
        }
    } else if (mode_ == QLatin1String("guide")) {
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
