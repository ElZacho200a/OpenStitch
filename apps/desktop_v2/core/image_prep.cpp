// SPDX-License-Identifier: Apache-2.0
#include "image_prep.hpp"

#include <cmath>

#include "document_session.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/image/ops.hpp"

namespace openstitch::desktop_v2 {

namespace {

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

bool inRange(double v, double lo, double hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}

} // namespace

PixelSize workingImageSize(const document::Project& project) {
    if (!project.hasImage()) {
        return {};
    }
    const auto img = image::apply_pipeline(project.original, project.ops);
    if (!img) {
        return {};
    }
    return {img->width, img->height};
}

std::vector<std::string> imagePrepNames(const document::Project& project) {
    std::vector<std::string> names;
    for (const auto& op : project.ops) {
        names.push_back(image::op_name(op));
    }
    return names;
}

ServiceResult applyImagePrep(DocumentSession& session, const PrepRequest& r) {
    if (!session.project().hasImage()) {
        return refuse("Le projet n'a pas d'image source.");
    }
    image::ImageOp op;
    switch (r.kind) {
    case PrepKind::BrightnessContrast:
        if (!inRange(r.a, -100, 100) || !inRange(r.b, -100, 100)) {
            return refuse("Luminosité et contraste doivent être compris entre -100 et 100.");
        }
        if (std::abs(r.a) < 1e-9 && std::abs(r.b) < 1e-9) {
            return refuse("Luminosité et contraste sont à zéro : rien à appliquer.");
        }
        op = image::BrightnessContrastOp{r.a, r.b};
        break;
    case PrepKind::FlipHorizontal:
        op = image::FlipOp{true};
        break;
    case PrepKind::FlipVertical:
        op = image::FlipOp{false};
        break;
    case PrepKind::RotateRight:
        op = image::Rotate90Op{1};
        break;
    case PrepKind::Rotate180:
        op = image::Rotate90Op{2};
        break;
    case PrepKind::RotateLeft:
        op = image::Rotate90Op{3};
        break;
    case PrepKind::Grayscale:
        op = image::GrayscaleOp{};
        break;
    case PrepKind::Sharpen:
        if (!inRange(r.a, 0.05, 3.0)) {
            return refuse("La netteté doit être comprise entre 0,05 et 3.");
        }
        op = image::SharpenOp{r.a};
        break;
    case PrepKind::Clahe:
        if (!inRange(r.a, 1.0, 8.0)) {
            return refuse("La limite de contraste local doit être comprise entre 1 et 8.");
        }
        op = image::ClaheOp{r.a};
        break;
    case PrepKind::Crop: {
        const auto size = workingImageSize(session.project());
        if (size.width == 0) {
            return refuse("L'image de travail est illisible.");
        }
        if (r.w < 8 || r.h < 8 || r.x < 0 || r.y < 0 || r.x + r.w > size.width ||
            r.y + r.h > size.height) {
            return refuse("Le recadrage doit rester dans l'image (" + std::to_string(size.width) +
                          " × " + std::to_string(size.height) + " px) et mesurer 8 px au moins.");
        }
        if (r.x == 0 && r.y == 0 && r.w == size.width && r.h == size.height) {
            return refuse("Le recadrage couvre toute l'image : rien à faire.");
        }
        op = image::CropOp{r.x, r.y, r.w, r.h};
        break;
    }
    }
    session.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
