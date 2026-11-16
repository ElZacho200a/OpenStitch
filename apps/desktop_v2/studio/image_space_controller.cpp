// SPDX-License-Identifier: Apache-2.0
#include "image_space_controller.hpp"

#include <QColor>
#include <QImage>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "image_prep.hpp"
#include "openstitch/commands/command.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/ops.hpp"
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

// Identifiants de toutes les sources image du document (racine historique comprise), dans
// l'ordre du document : la base des opérations qui doivent garder les calques alignés.
std::vector<ObjectId> imageSourceIds(const document::Project& project) {
    std::vector<ObjectId> ids;
    if (!project.original.empty()) {
        ids.push_back(ObjectId{});
    }
    for (const auto& entry : project.image_sources) {
        if (!entry.source.original.empty()) {
            ids.push_back(entry.calque);
        }
    }
    return ids;
}

document::ImageSource& sourceOf(document::Project& project, ObjectId id) {
    return *const_cast<document::ImageSource*>(project.findImageSource(id));
}

// Change l'échelle physique (mm par pixel) de TOUTES les sources image d'un même facteur, pour
// que les calques d'une composition (un `.ora`, un lot importé ensemble) restent alignés. Les
// pixels, la segmentation et les traitements ne bougent pas ; son propre inverse par revert().
class SetMmPerPxCommand final : public commands::ICommand {
public:
    explicit SetMmPerPxCommand(double factor) : factor_(factor) {}
    void apply(document::Project& project) override {
        previous_.clear();
        for (const auto id : imageSourceIds(project)) {
            auto& source = sourceOf(project, id);
            previous_.emplace_back(id, source.mm_per_px.value);
            source.mm_per_px.value *= factor_;
        }
    }
    void revert(document::Project& project) override {
        for (const auto& [id, value] : previous_) {
            if (project.findImageSource(id) != nullptr) {
                sourceOf(project, id).mm_per_px.value = value;
            }
        }
    }
    [[nodiscard]] std::string name() const override { return "Redimensionner l'image"; }

private:
    double factor_;
    std::vector<std::pair<ObjectId, double>> previous_;
};

// Ajoute la même opération à plusieurs sources nommées (recadrage commun des calques alignés,
// ou une seule source). Les sources sont fixées à la construction : changer de calque actif
// entre l'action et son annulation ne déplace pas l'annulation sur un autre calque.
class AppendOpToSourcesCommand final : public commands::ICommand {
public:
    AppendOpToSourcesCommand(std::vector<ObjectId> sources, image::ImageOp op)
        : sources_(std::move(sources)), op_(std::move(op)) {}
    void apply(document::Project& project) override {
        stashed_.clear();
        for (const auto id : sources_) {
            auto& source = sourceOf(project, id);
            source.ops.push_back(op_);
            stashed_.push_back(std::move(source.segmentation));
            source.segmentation.reset();
        }
    }
    void revert(document::Project& project) override {
        for (std::size_t i = sources_.size(); i-- > 0;) {
            auto& source = sourceOf(project, sources_[i]);
            source.ops.pop_back();
            source.segmentation = std::move(stashed_[i]);
        }
        stashed_.clear();
    }
    [[nodiscard]] std::string name() const override {
        return sources_.size() > 1 ? image::op_name(op_) + " (tous les calques)"
                                   : image::op_name(op_);
    }
    [[nodiscard]] bool affectsEmbroidery() const override { return false; }

private:
    std::vector<ObjectId> sources_;
    image::ImageOp op_;
    std::vector<std::optional<segmentation::Segmentation>> stashed_;
};

// Retire l'opération `index` de la pile. Invalide la segmentation (l'image de travail change) et
// la restaure à l'annulation, comme AppendImageOpCommand.
class RemoveImageOpCommand final : public commands::ICommand {
public:
    explicit RemoveImageOpCommand(std::size_t index) : index_(index) {}
    void apply(document::Project& project) override {
        removed_ = project.imageSource().ops.at(index_);
        project.imageSource().ops.erase(project.imageSource().ops.begin() +
                                        static_cast<std::ptrdiff_t>(index_));
        stashed_ = std::move(project.imageSource().segmentation);
        project.imageSource().segmentation.reset();
    }
    void revert(document::Project& project) override {
        project.imageSource().ops.insert(
            project.imageSource().ops.begin() + static_cast<std::ptrdiff_t>(index_), removed_);
        project.imageSource().segmentation = std::move(stashed_);
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
        std::swap(project.imageSource().ops.at(a_), project.imageSource().ops.at(b_));
        stashed_ = std::move(project.imageSource().segmentation);
        project.imageSource().segmentation.reset();
    }
    void revert(document::Project& project) override {
        std::swap(project.imageSource().ops.at(a_), project.imageSource().ops.at(b_));
        project.imageSource().segmentation = std::move(stashed_);
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
        tool_.clear();
        lastOpCount_ = 0;
        emit toolChanged();
        refresh();
    });
    connect(&images_, &ImageController::imagesChanged, this, [this] { publishLayer(); });
    connect(&images_, &ImageController::previewChanged, this, [this] { publishLayer(); });
    trackSegmentation();
}

ImageSpaceController::~ImageSpaceController() = default;

bool ImageSpaceController::hasImage() const {
    return studio_.session().project().hasActiveImage();
}

double ImageSpaceController::mmPerPx() const {
    return studio_.session().project().imageSource().mm_per_px.value;
}

double ImageSpaceController::imageWidthMm() const {
    return images_.imageWidth() * mmPerPx();
}

double ImageSpaceController::imageHeightMm() const {
    return images_.imageHeight() * mmPerPx();
}

bool ImageSpaceController::canResize() const {
    const auto& project = studio_.session().project();
    return project.hasActiveImage() && project.embroidery_objects.empty();
}

QString ImageSpaceController::scaleImage(double percent) {
    if (!hasImage() || images_.imageWidth() <= 0) {
        const auto text = tr("Importez d'abord une image.");
        setMessage(text);
        return text;
    }
    if (!canResize()) {
        const auto text = tr("Des objets existent déjà : redimensionnez-les par la sélection.");
        setMessage(text);
        return text;
    }
    const double next = mmPerPx() * percent / 100.0;
    const double width = images_.imageWidth() * next;
    const double height = images_.imageHeight() * next;
    constexpr double kMinMm = 1.0;
    constexpr double kMaxMm = 5000.0;
    if (!(percent > 0.0) || std::min(width, height) < kMinMm || std::max(width, height) > kMaxMm) {
        const auto text = tr("Taille refusée : entre %1 et %2 mm.").arg(kMinMm).arg(kMaxMm);
        setMessage(text);
        return text;
    }
    if (std::abs(next - mmPerPx()) < 1e-12) {
        return {};
    }
    studio_.session().execute(std::make_unique<SetMmPerPxCommand>(next / mmPerPx()));
    publishLayer();
    emit changed();
    setMessage(tr("Image redimensionnée à %1 × %2 mm (Ctrl+Z pour annuler).")
                   .arg(width, 0, 'f', 1)
                   .arg(height, 0, 'f', 1));
    return {};
}

QString ImageSpaceController::setImageWidthMm(double widthMm) {
    if (images_.imageWidth() <= 0 || !(widthMm > 0.0)) {
        const auto text = tr("Largeur invalide.");
        setMessage(text);
        return text;
    }
    return scaleImage(widthMm / imageWidthMm() * 100.0);
}

QString ImageSpaceController::setImageHeightMm(double heightMm) {
    if (images_.imageHeight() <= 0 || !(heightMm > 0.0)) {
        const auto text = tr("Hauteur invalide.");
        setMessage(text);
        return text;
    }
    return scaleImage(heightMm / imageHeightMm() * 100.0);
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
                             .arg(studio_.session().project().imageSource().ops.size());
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
    const bool seg = project.imageSource().segmentation.has_value();
    const std::size_t count = project.imageSource().ops.size();
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
    const auto source = studio_.session().project().imageSourceId();
    if (observedImageSource_ != source) {
        observedImageSource_ = source;
        imageChanged_ = false;
        hadSegmentation_ = false;
        if (cropActive_) {
            cropActive_ = false;
            emit cropChanged();
        }
    }
    trackSegmentation();
    publishLayer(); // une annulation d'échelle change mm/pixel sans signal d'image
    emit changed();
}

void ImageSpaceController::setMessage(const QString& text) {
    message_ = text;
    emit messageChanged();
}

QString ImageSpaceController::removeOp(int index) {
    auto& session = studio_.session();
    const auto& ops = session.project().imageSource().ops;
    if (index < 0 || static_cast<std::size_t>(index) >= ops.size()) {
        const auto text = tr("Ce traitement n'existe plus.");
        setMessage(text);
        return text;
    }
    auto next = ops;
    next.erase(next.begin() + index);
    const auto& original = session.project().imageSource().original;
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
    const auto& ops = session.project().imageSource().ops;
    const int other = index + delta;
    if (index < 0 || other < 0 || static_cast<std::size_t>(index) >= ops.size() ||
        static_cast<std::size_t>(other) >= ops.size() || delta == 0) {
        const auto text = tr("Déplacement impossible.");
        setMessage(text);
        return text;
    }
    auto next = ops;
    std::swap(next[static_cast<std::size_t>(index)], next[static_cast<std::size_t>(other)]);
    const auto& original = session.project().imageSource().original;
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
    tool_.clear();
    emit cropChanged();
    emit toolChanged();
}

void ImageSpaceController::cancelCrop() {
    if (!cropActive_) {
        return;
    }
    cropActive_ = false;
    emit cropChanged();
    emit toolChanged();
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
    // Validation commune (bornes, cadre plein) par le service, sur le calque actif.
    const int x = static_cast<int>(cx_), y = static_cast<int>(cy_);
    const int w = static_cast<int>(cw_), h = static_cast<int>(ch_);
    const int iw = images_.imageWidth(), ih = images_.imageHeight();
    if (w < 8 || h < 8 || x < 0 || y < 0 || x + w > iw || y + h > ih) {
        const auto text = tr("Le recadrage doit rester dans l'image (%1 × %2 px) et mesurer 8 px "
                             "au moins.")
                              .arg(iw)
                              .arg(ih);
        setMessage(text);
        return text;
    }
    if (x == 0 && y == 0 && w == iw && h == ih) {
        const auto text = tr("Le recadrage couvre toute l'image : rien à faire.");
        setMessage(text);
        return text;
    }
    // Calques alignés : même taille de travail que le calque actif (toile commune d'un `.ora`).
    auto& project = studio_.session().project();
    const auto active = project.imageSourceId();
    std::vector<ObjectId> targets;
    for (const auto id : imageSourceIds(project)) {
        const auto& source = *project.findImageSource(id);
        const auto size =
            dimensionsAfter(source.ops, source.original.width, source.original.height);
        if (id == active || (size && size->first == iw && size->second == ih)) {
            targets.push_back(id);
        }
    }
    studio_.session().execute(
        std::make_unique<AppendOpToSourcesCommand>(targets, image::CropOp{x, y, w, h}));
    cropActive_ = false;
    emit cropChanged();
    emit toolChanged();
    setMessage(targets.size() > 1
                   ? tr("Recadrage appliqué aux %1 calques alignés (Ctrl+Z pour annuler).")
                         .arg(targets.size())
                   : tr("Image recadrée (Ctrl+Z pour annuler)."));
    return {};
}

QString ImageSpaceController::tool() const {
    return cropActive_ ? QStringLiteral("crop") : tool_;
}

void ImageSpaceController::setTool(const QString& tool) {
    const QString next = tool == QLatin1String("crop") || tool == QLatin1String("pick") ||
                                 tool == QLatin1String("measure")
                             ? tool
                             : QString();
    if (next == this->tool()) {
        return;
    }
    tool_ = next == QLatin1String("crop") ? QString() : next;
    if (next == QLatin1String("crop")) {
        beginCrop();
    } else {
        cancelCrop();
    }
    emit toolChanged();
}

void ImageSpaceController::setPickTolerance(int tolerance) {
    const int next = std::clamp(tolerance, 0, 255);
    if (next != pickTolerance_) {
        pickTolerance_ = next;
        emit toolChanged();
    }
}

void ImageSpaceController::setPickContiguous(bool contiguous) {
    if (contiguous != pickContiguous_) {
        pickContiguous_ = contiguous;
        emit toolChanged();
    }
}

int ImageSpaceController::imageLayerCount() const {
    return static_cast<int>(imageSourceIds(studio_.session().project()).size());
}

QString ImageSpaceController::colorAt(double sx, double sy) const {
    const auto after = images_.images()->get(QStringLiteral("after"));
    const auto p = screenToImage(sx, sy);
    const int iw = images_.imageWidth();
    const int ih = images_.imageHeight();
    if (after.isNull() || iw <= 0 || ih <= 0 || p.x() < 0.0 || p.y() < 0.0 || p.x() >= iw ||
        p.y() >= ih) {
        return {};
    }
    // L'image publiée peut être réduite (très grandes images) : on y échantillonne au prorata.
    const int x = std::min(after.width() - 1, static_cast<int>(p.x() * after.width() / iw));
    const int y = std::min(after.height() - 1, static_cast<int>(p.y() * after.height() / ih));
    return after.pixelColor(x, y).name(QColor::HexRgb);
}

QString ImageSpaceController::makeTransparentAt(double sx, double sy) {
    if (!hasImage()) {
        const auto text = tr("Importez d'abord une image.");
        setMessage(text);
        return text;
    }
    const auto hex = colorAt(sx, sy);
    if (hex.isEmpty()) {
        const auto text = tr("Cliquez dans l'image pour choisir la couleur du fond.");
        setMessage(text);
        return text;
    }
    const QColor color(hex);
    const auto p = screenToImage(sx, sy);
    const image::ColorToAlphaOp op{static_cast<std::uint8_t>(color.red()),
                                   static_cast<std::uint8_t>(color.green()),
                                   static_cast<std::uint8_t>(color.blue()),
                                   pickTolerance_,
                                   pickContiguous_,
                                   static_cast<int>(std::floor(p.x())),
                                   static_cast<int>(std::floor(p.y()))};
    const auto active = studio_.session().project().imageSourceId();
    studio_.session().execute(
        std::make_unique<AppendOpToSourcesCommand>(std::vector<ObjectId>{active}, op));
    setMessage(tr("%1 rendu transparent sur ce calque (Ctrl+Z pour annuler).").arg(hex));
    return {};
}

QString ImageSpaceController::scaleFromMeasure(double lengthPx, double realMm) {
    if (!(lengthPx >= 1.0) || !(realMm > 0.0) || !std::isfinite(lengthPx + realMm)) {
        const auto text = tr("Mesurez un segment dans l'image puis indiquez sa longueur réelle.");
        setMessage(text);
        return text;
    }
    if (mmPerPx() <= 0.0) {
        return tr("Importez d'abord une image.");
    }
    return scaleImage(realMm / lengthPx / mmPerPx() * 100.0);
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
