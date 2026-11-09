// SPDX-License-Identifier: Apache-2.0
#include "image_space_controller.hpp"

#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "image_prep.hpp"
#include "openstitch/commands/command.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

constexpr int kMaxTextureSide = 8192;

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Dimensions après chaque opération : vide si une opération (recadrage) sort de l'image. Évite de
// rejouer le pipeline (coûteux) pour valider une suppression ou un déplacement d'étape.
std::optional<std::pair<int, int>> dimensionsAfter(const std::vector<image::ImageOp>& ops, int w,
                                                   int h) {
    for (const auto& op : ops) {
        if (const auto* crop = std::get_if<image::CropOp>(&op)) {
            if (crop->width <= 0 || crop->height <= 0 || crop->x < 0 || crop->y < 0 ||
                crop->x + crop->width > w || crop->y + crop->height > h) {
                return std::nullopt;
            }
            w = crop->width;
            h = crop->height;
        } else if (const auto* turn = std::get_if<image::Rotate90Op>(&op)) {
            if (turn->quarter_turns % 2 != 0) {
                std::swap(w, h);
            }
        }
    }
    return std::pair{w, h};
}

// Retire l'opération `index` de la pile. Invalide la segmentation (l'image de travail change) et
// la restaure à l'annulation, comme AppendImageOpCommand.
class RemoveImageOpCommand final : public commands::ICommand {
public:
    explicit RemoveImageOpCommand(std::size_t index) : index_(index) {}
    void apply(document::Project& project) override {
        removed_ = project.ops.at(index_);
        project.ops.erase(project.ops.begin() + static_cast<std::ptrdiff_t>(index_));
        stashed_ = std::move(project.segmentation);
        project.segmentation.reset();
    }
    void revert(document::Project& project) override {
        project.ops.insert(project.ops.begin() + static_cast<std::ptrdiff_t>(index_), removed_);
        project.segmentation = std::move(stashed_);
        stashed_.reset();
    }
    [[nodiscard]] std::string name() const override {
        return "Supprimer le traitement : " + image::op_name(removed_);
    }

private:
    std::size_t index_;
    image::ImageOp removed_;
    std::optional<segmentation::Segmentation> stashed_;
};

// Échange l'opération `index` avec la suivante (ou la précédente). Son propre inverse.
class SwapImageOpsCommand final : public commands::ICommand {
public:
    SwapImageOpsCommand(std::size_t a, std::size_t b, std::string label)
        : a_(a), b_(b), label_(std::move(label)) {}
    void apply(document::Project& project) override {
        std::swap(project.ops.at(a_), project.ops.at(b_));
        stashed_ = std::move(project.segmentation);
        project.segmentation.reset();
    }
    void revert(document::Project& project) override {
        std::swap(project.ops.at(a_), project.ops.at(b_));
        project.segmentation = std::move(stashed_);
        stashed_.reset();
    }
    [[nodiscard]] std::string name() const override {
        return "Réordonner le traitement : " + label_;
    }

private:
    std::size_t a_, b_;
    std::string label_;
    std::optional<segmentation::Segmentation> stashed_;
};

} // namespace

ImageSpaceController::ImageSpaceController(StudioController& studio, ImageController& images,
                                           QObject* parent)
    : QObject(parent), studio_(studio), images_(images) {
    connect(&studio_, &StudioController::changed, this, [this] { refresh(); });
    connect(&studio_, &StudioController::documentReplaced, this, [this] {
        imageChanged_ = false;
        hadSegmentation_ = false;
        cropActive_ = false;
        lastOpCount_ = 0;
        refresh();
    });
    connect(&images_, &ImageController::imagesChanged, this, [this] { publishLayer(); });
    connect(&images_, &ImageController::previewChanged, this, [this] { publishLayer(); });
    trackSegmentation();
}

ImageSpaceController::~ImageSpaceController() = default;

bool ImageSpaceController::hasImage() const {
    return studio_.session().project().hasImage();
}

double ImageSpaceController::mmPerPx() const {
    return studio_.session().project().mm_per_px.value;
}

int ImageSpaceController::imageWidthPx() const {
    return images_.imageWidth();
}

int ImageSpaceController::imageHeightPx() const {
    return images_.imageHeight();
}

void ImageSpaceController::setCompareMode(int mode) {
    const auto next = static_cast<SourceCompare>(std::clamp(mode, 0, 2));
    if (next == view_.mode) {
        return;
    }
    view_.mode = next;
    ++view_.viewRevision;
    if (canvas_) {
        canvas_->setSourceImage(view_);
    }
    emit viewChanged();
}

void ImageSpaceController::setCurtain(double fraction) {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    if (!std::isfinite(clamped) || clamped == view_.curtain) {
        return;
    }
    view_.curtain = clamped;
    ++view_.viewRevision;
    if (canvas_) {
        canvas_->setSourceImage(view_);
    }
    emit viewChanged();
}

QVariantList ImageSpaceController::opStack() const {
    QVariantList list;
    const auto names = imagePrepNames(studio_.session().project());
    for (std::size_t i = 0; i < names.size(); ++i) {
        list.push_back(QVariantMap{{QStringLiteral("index"), static_cast<int>(i)},
                                   {QStringLiteral("name"), fromUtf8(names[i])}});
    }
    return list;
}

QString ImageSpaceController::stageState() const {
    if (!hasImage()) {
        return QStringLiteral("empty");
    }
    return imageChanged_ ? QStringLiteral("stale") : QStringLiteral("upToDate");
}

QString ImageSpaceController::stageDetail() const {
    if (!hasImage()) {
        return tr("Aucune image source");
    }
    const QString size = tr("%1 × %2 px, %3 traitement(s)")
                             .arg(images_.imageWidth())
                             .arg(images_.imageHeight())
                             .arg(studio_.session().project().ops.size());
    return imageChanged_ ? tr("L'image a changé : segmentation à refaire. ") + size : size;
}

void ImageSpaceController::attachCanvas(QObject* canvas) {
    canvas_ = qobject_cast<GpuCanvas*>(canvas);
    publishLayer();
    // Si les octets n'ont pas changé, publishLayer évite volontairement de les convertir ;
    // un nouveau canevas doit néanmoins recevoir le snapshot déjà construit.
    if (canvas_ && view_.data) {
        canvas_->setSourceImage(view_);
    }
}

void ImageSpaceController::detachCanvas() {
    canvas_.clear();
}

void ImageSpaceController::fit() {
    const auto& after = view_.data ? view_.data->after : QImage();
    if (!canvas_ || !view_.data || after.isNull()) {
        return;
    }
    const auto rect = sourceImageRectMm(after, view_.data->mmPerPx);
    canvas_->zoomToSceneRect(rect.left(), rect.top(), rect.right(), rect.bottom());
}

void ImageSpaceController::publishLayer() {
    const auto before = images_.images()->get(QStringLiteral("before"));
    const auto after = images_.images()->get(images_.prepPreviewActive() ? QStringLiteral("preview")
                                                                         : QStringLiteral("after"));
    const double mm = mmPerPx();
    const bool had = view_.data != nullptr;
    if (!hasImage() || after.isNull() || mm <= 0.0) {
        imageKeysSet_ = false;
        if (had) {
            view_.data.reset();
            ++view_.dataRevision;
            ++view_.viewRevision;
            ++layerRevision_;
            if (canvas_) {
                canvas_->setSourceImage(view_);
            }
            emit layerChanged();
        }
        return;
    }
    // Plusieurs signaux (preview/images) peuvent désigner exactement les mêmes pixels.
    // Ne pas réduire deux images ni réallouer des textures dans ce cas.
    if (imageKeysSet_ && beforeKey_ == before.cacheKey() && afterKey_ == after.cacheKey() &&
        imageMmPerPx_ == mm) {
        return;
    }
    beforeKey_ = before.cacheKey();
    afterKey_ = after.cacheKey();
    imageMmPerPx_ = mm;
    imageKeysSet_ = true;
    auto data = std::make_shared<SourceImageData>();
    data->mmPerPx = mm;
    const int side = std::max({after.width(), after.height(), before.width(), before.height()});
    if (side > kMaxTextureSide) {
        // Texture bornée : même facteur pour l'avant et l'après, donc même emprise physique.
        const double k = static_cast<double>(kMaxTextureSide) / side;
        const auto shrink = [k](const QImage& img) {
            return img.isNull() ? img
                                : img.scaled(std::max(1, static_cast<int>(img.width() * k)),
                                             std::max(1, static_cast<int>(img.height() * k)),
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        };
        data->after = shrink(after);
        data->before = shrink(before);
        data->mmPerPx = mm / k;
    } else {
        data->after = after;
        data->before = before;
    }
    view_.data = std::move(data);
    ++view_.dataRevision;
    ++view_.viewRevision;
    ++layerRevision_;
    const bool first = !had;
    if (canvas_) {
        canvas_->setSourceImage(view_);
        if (first) {
            fit();
        }
    }
    emit layerChanged();
}

void ImageSpaceController::trackSegmentation() {
    const auto& project = studio_.session().project();
    const bool seg = project.segmentation.has_value();
    const std::size_t count = project.ops.size();
    if (seg) {
        imageChanged_ = false;
    } else if (hadSegmentation_ && count != lastOpCount_) {
        imageChanged_ = true;
    }
    hadSegmentation_ = seg;
    lastOpCount_ = count;
}

void ImageSpaceController::refresh() {
    const auto revision = studio_.session().revision();
    if (observedRevision_ == revision) {
        return; // sélection, progression et statut : les opérations d'image n'ont pas changé
    }
    observedRevision_ = revision;
    trackSegmentation();
    emit changed();
}

void ImageSpaceController::setMessage(const QString& text) {
    message_ = text;
    emit messageChanged();
}

QString ImageSpaceController::removeOp(int index) {
    auto& session = studio_.session();
    const auto& ops = session.project().ops;
    if (index < 0 || static_cast<std::size_t>(index) >= ops.size()) {
        const auto text = tr("Ce traitement n'existe plus.");
        setMessage(text);
        return text;
    }
    auto next = ops;
    next.erase(next.begin() + index);
    const auto& original = session.project().original;
    if (!dimensionsAfter(next, original.width, original.height)) {
        const auto text = tr("Impossible : un recadrage suivant sortirait de l'image.");
        setMessage(text);
        return text;
    }
    session.execute(std::make_unique<RemoveImageOpCommand>(static_cast<std::size_t>(index)));
    setMessage(tr("Traitement supprimé (Ctrl+Z pour annuler)."));
    return {};
}

QString ImageSpaceController::moveOp(int index, int delta) {
    auto& session = studio_.session();
    const auto& ops = session.project().ops;
    const int other = index + delta;
    if (index < 0 || other < 0 || static_cast<std::size_t>(index) >= ops.size() ||
        static_cast<std::size_t>(other) >= ops.size() || delta == 0) {
        const auto text = tr("Déplacement impossible.");
        setMessage(text);
        return text;
    }
    auto next = ops;
    std::swap(next[static_cast<std::size_t>(index)], next[static_cast<std::size_t>(other)]);
    const auto& original = session.project().original;
    if (!dimensionsAfter(next, original.width, original.height)) {
        const auto text = tr("Impossible : l'ordre demandé ferait sortir un recadrage de l'image.");
        setMessage(text);
        return text;
    }
    const auto label = image::op_name(ops[static_cast<std::size_t>(index)]);
    session.execute(std::make_unique<SwapImageOpsCommand>(static_cast<std::size_t>(index),
                                                          static_cast<std::size_t>(other), label));
    setMessage(tr("Traitement déplacé (Ctrl+Z pour annuler)."));
    return {};
}

void ImageSpaceController::beginCrop() {
    if (!hasImage() || images_.imageWidth() <= 0) {
        return;
    }
    const double w = images_.imageWidth();
    const double h = images_.imageHeight();
    // Cadre initial : l'image moins une marge, pour que les poignées soient saisissables.
    cx_ = std::round(w * 0.1);
    cy_ = std::round(h * 0.1);
    cw_ = std::round(w * 0.8);
    ch_ = std::round(h * 0.8);
    cropActive_ = true;
    emit cropChanged();
}

void ImageSpaceController::cancelCrop() {
    if (!cropActive_) {
        return;
    }
    cropActive_ = false;
    emit cropChanged();
}

void ImageSpaceController::setCrop(double x, double y, double w, double h) {
    const double iw = images_.imageWidth();
    const double ih = images_.imageHeight();
    if (!cropActive_ || iw <= 0 || ih <= 0 || !std::isfinite(x + y + w + h)) {
        return;
    }
    const double nx = std::clamp(std::round(x), 0.0, iw - 1);
    const double ny = std::clamp(std::round(y), 0.0, ih - 1);
    cx_ = nx;
    cy_ = ny;
    cw_ = std::clamp(std::round(w), 1.0, iw - nx);
    ch_ = std::clamp(std::round(h), 1.0, ih - ny);
    emit cropChanged();
}

QString ImageSpaceController::applyCrop() {
    if (!cropActive_) {
        return tr("Aucun cadre de recadrage.");
    }
    const auto refusal = images_.applyPrep(QStringLiteral("crop"), {{QStringLiteral("x"), cx_},
                                                                    {QStringLiteral("y"), cy_},
                                                                    {QStringLiteral("w"), cw_},
                                                                    {QStringLiteral("h"), ch_}});
    if (refusal.isEmpty()) {
        cropActive_ = false;
        emit cropChanged();
    }
    return refusal;
}

QPointF ImageSpaceController::imageToScreen(double px, double py) const {
    if (!canvas_ || mmPerPx() <= 0.0) {
        return {};
    }
    // Pixels de l'image de travail (non réduite), convertis avec la résolution du projet.
    const double wMm = images_.imageWidth() * mmPerPx();
    const double hMm = images_.imageHeight() * mmPerPx();
    const auto p =
        canvas_->camera().worldToScreen({-wMm / 2.0 + px * mmPerPx(), -hMm / 2.0 + py * mmPerPx()});
    return {p.x, p.y};
}

QPointF ImageSpaceController::screenToImage(double sx, double sy) const {
    if (!canvas_ || mmPerPx() <= 0.0) {
        return {};
    }
    const auto w = canvas_->camera().screenToWorld({sx, sy});
    const double wMm = images_.imageWidth() * mmPerPx();
    const double hMm = images_.imageHeight() * mmPerPx();
    return {(w.x + wMm / 2.0) / mmPerPx(), (w.y + hMm / 2.0) / mmPerPx()};
}

} // namespace openstitch::desktop_v2::studio
