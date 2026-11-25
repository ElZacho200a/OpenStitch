// SPDX-License-Identifier: Apache-2.0
#include "text_tool.hpp"

#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <vector>

#include "action_catalog.hpp"
#include "action_controller.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

constexpr double kMinCapHeightMm = 1.0; // bornes de `text.add` : cap_height_mm [1 ; 500]
constexpr double kMaxCapHeightMm = 500.0;
constexpr double kHitMarginMm = 1.0; // tolérance du clic autour de l'emprise d'un texte
constexpr double kLimitMm = 1000.0;  // |coord| <= 1000 pour `origin_mm`
// Fil par défaut d'un NOUVEAU texte : lisible à la fois sur le fond sombre de l'espace Vecteurs et
// sur le fond clair de l'espace Points (contraste de l'ordre de 4:1 et 3,7:1). Le noir d'`add_text`
// disparaissait sur le canevas Vecteurs ; ce choix ne change aucun texte existant ni la CLI/MCP.
constexpr const char* kDefaultThread = "#D2483C";

// Lettres (objets de broderie) du texte `textId`, dans l'ordre du document.
std::vector<std::uint64_t> lettersOf(const document::Project& project, std::uint64_t textId) {
    std::vector<std::uint64_t> out;
    for (const auto& emb : project.embroidery_objects) {
        const auto* vec = project.findObject(emb.source_vector);
        if (vec != nullptr && vec->text_owner && vec->text_owner->value == textId) {
            out.push_back(emb.id.value);
        }
    }
    return out;
}

QString qs(const std::string& s) {
    return QString::fromStdString(s);
}

} // namespace

TextTool::TextTool(StudioController& studio, QObject* parent) : QObject(parent), studio_(studio) {}

void TextTool::setCapHeightMm(double value) {
    if (!std::isfinite(value)) {
        return;
    }
    value = std::clamp(value, kMinCapHeightMm, kMaxCapHeightMm);
    if (std::abs(value - capHeightMm_) > 1e-9) {
        capHeightMm_ = value;
        emit capHeightChanged();
        emit changed(); // la place libre sous l'ancre (clearanceMm) en dépend
    }
}

std::optional<std::uint64_t> TextTool::textAt(double sceneX, double sceneY) const {
    const auto& scene = studio_.scene();
    if (!scene) {
        return std::nullopt;
    }
    const auto& project = studio_.session().project();
    std::optional<std::uint64_t> found;
    for (const auto& text : project.text_objects) {
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
        bool any = false;
        for (const auto id : lettersOf(project, text.id.value)) {
            const auto bounds = scene->boundsOf(id);
            if (!bounds) {
                continue;
            }
            x0 = any ? std::min(x0, bounds->x0) : bounds->x0;
            y0 = any ? std::min(y0, bounds->y0) : bounds->y0;
            x1 = any ? std::max(x1, bounds->x1) : bounds->x1;
            y1 = any ? std::max(y1, bounds->y1) : bounds->y1;
            any = true;
        }
        if (any && sceneX >= x0 - kHitMarginMm && sceneX <= x1 + kHitMarginMm &&
            sceneY >= y0 - kHitMarginMm && sceneY <= y1 + kHitMarginMm) {
            found = text.id.value; // le dernier de la liste est le plus haut dans la pile
        }
    }
    return found;
}

bool TextTool::begin(double sceneX, double sceneY, PointMm model) {
    if (!std::isfinite(model.x) || !std::isfinite(model.y) || std::abs(model.x) > kLimitMm ||
        std::abs(model.y) > kLimitMm) {
        emit message(tr("Position du texte hors limites"));
        return false;
    }
    cancel();
    const auto& project = studio_.session().project();
    if (const auto existing = textAt(sceneX, sceneY)) {
        const auto* text = project.findText(ObjectId{*existing});
        if (text != nullptr) {
            // L'éditeur se pose SOUS le texte (coin bas-gauche), pour ne pas le masquer ; ses
            // lettres sont sélectionnées pour que l'inspecteur et « Supprimer » parlent du même
            // texte.
            double x0 = 0.0;
            double y1 = 0.0;
            bool any = false;
            std::vector<Target> targets;
            for (const auto id : lettersOf(project, *existing)) {
                targets.push_back({TargetKind::EmbroideryObject, id});
                if (const auto bounds =
                        studio_.scene() ? studio_.scene()->boundsOf(id) : std::nullopt) {
                    x0 = any ? std::min(x0, bounds->x0) : bounds->x0;
                    y1 = any ? std::max(y1, bounds->y1) : bounds->y1;
                    any = true;
                }
            }
            textId_ = *existing;
            anchor_ = any ? screenToModel(x0, y1) : model;
            draft_ = qs(text->text);
            if (!targets.empty()) {
                studio_.session().selection().replace(std::move(targets));
            }
            editing_ = true;
            error_.clear();
            emit changed();
            return true;
        }
    }
    textId_ = 0;
    anchor_ = model;
    draft_.clear();
    editing_ = true;
    error_.clear();
    emit changed();
    return true;
}

void TextTool::close() {
    editing_ = false;
    textId_ = 0;
    draft_.clear();
    error_.clear();
    emit changed();
}

bool TextTool::cancel() {
    if (!editing_) {
        return false;
    }
    close();
    return true;
}

QString TextTool::commit(const QString& text, QObject* actions) {
    if (!editing_) {
        return {};
    }
    auto* controller = qobject_cast<ActionController*>(actions);
    if (controller == nullptr) {
        error_ = tr("Le texte ne peut pas être validé ici.");
        emit changed();
        return error_;
    }
    // Les retours à la ligne et espaces de fin (Entrée tapé avant de valider) ne font pas partie du
    // texte : « Anne⏎ » doit rester « Anne ».
    const QString clean = text.trimmed();
    const bool blank = clean.isEmpty();
    if (textId_ == 0) {
        if (blank) { // rien de saisi : aucun objet fantôme
            close();
            return {};
        }
        // Le point cliqué est le CENTRE du texte : `text.add` pose le texte (aligné au centre) sur
        // sa ligne de base, on descend donc de la demi-hauteur des capitales.
        const QVariantMap values{
            {QStringLiteral("text"), clean},
            {QStringLiteral("cap_height_mm"), QString::number(capHeightMm_, 'g', 6)},
            {QStringLiteral("rgb"), QString::fromLatin1(kDefaultThread)},
            {QStringLiteral("origin_x_mm"), QString::number(anchor_.x, 'f', 3)},
            {QStringLiteral("origin_y_mm"),
             QString::number(anchor_.y - capHeightMm_ / 2.0, 'f', 3)}};
        const auto refusal = controller->run(QStringLiteral("text.add"), values);
        if (!refusal.isEmpty()) {
            error_ = refusal;
            emit changed();
            return refusal;
        }
        close();
        return {};
    }
    // Édition : un texte existant ne devient jamais vide en silence (Suppr le supprime).
    if (blank) {
        error_ =
            tr("Un texte ne peut pas être vide. Échap garde l'ancien texte ; pour le supprimer, "
               "faites Échap puis Suppr.");
        emit changed();
        return error_;
    }
    const auto& project = studio_.session().project();
    if (project.findText(ObjectId{textId_}) == nullptr) {
        close(); // le texte a disparu entre-temps (annulation, suppression)
        return {};
    }
    if (clean == draft_) { // inchangé : pas d'étape d'annulation vide
        close();
        return {};
    }
    // Les réglages actuels du texte sont conservés : seul le contenu change.
    QVariantMap values;
    for (const auto& [key, value] : textEditValues(project, textId_)) {
        values.insert(qs(key), qs(value));
    }
    values.insert(QStringLiteral("text"), clean);
    std::vector<Target> targets;
    for (const auto id : lettersOf(project, textId_)) {
        targets.push_back({TargetKind::EmbroideryObject, id});
    }
    studio_.session().selection().replace(std::move(targets));
    const auto refusal = controller->run(QStringLiteral("text.edit"), values);
    if (!refusal.isEmpty()) {
        error_ = refusal;
        emit changed();
        return refusal;
    }
    close();
    return {};
}

} // namespace openstitch::desktop_v2::studio
