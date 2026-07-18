// SPDX-License-Identifier: Apache-2.0
#include "openstitch/image/image.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace openstitch::image {

namespace {

std::string format_from_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png") return "PNG";
    if (ext == ".jpg" || ext == ".jpeg") return "JPEG";
    if (ext == ".bmp") return "BMP";
    if (ext == ".tif" || ext == ".tiff") return "TIFF";
    return ext.empty() ? "inconnu" : ext.substr(1);
}

// Lecture des octets puis cv::imdecode : contrairement à cv::imread, ce
// chemin gère les chemins Windows non-ASCII (std::filesystem ouvre le
// fichier, OpenCV ne voit que des octets).
Result<cv::Mat> decode_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Fichier introuvable ou illisible : " + path.string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier est vide : " + path.string());
    }

    const cv::Mat raw(1, static_cast<int>(bytes.size()), CV_8U, bytes.data());
    cv::Mat mat = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (mat.empty()) {
        return fail(ErrorCategory::InvalidFile,
                    "Format d'image non reconnu ou fichier corrompu : " + path.string(),
                    "cv::imdecode a renvoyé une matrice vide");
    }
    return mat;
}

// Normalise n'importe quelle matrice décodée en RGBA 8 bits.
Result<cv::Mat> to_rgba8(cv::Mat mat) {
    if (mat.depth() == CV_16U) {
        cv::Mat mat8;
        mat.convertTo(mat8, CV_8U, 255.0 / 65535.0);
        mat = mat8;
    } else if (mat.depth() != CV_8U) {
        return fail(ErrorCategory::UnsupportedFormat,
                    "Profondeur de couleur non supportée (attendu 8 ou 16 bits par canal)");
    }

    cv::Mat rgba;
    switch (mat.channels()) {
    case 1:
        cv::cvtColor(mat, rgba, cv::COLOR_GRAY2RGBA);
        break;
    case 2: {  // niveaux de gris + alpha (PNG "GA")
        std::vector<cv::Mat> ga;
        cv::split(mat, ga);
        cv::Mat rgb;
        cv::cvtColor(ga[0], rgb, cv::COLOR_GRAY2RGB);
        std::vector<cv::Mat> planes;
        cv::split(rgb, planes);
        planes.push_back(ga[1]);
        cv::merge(planes, rgba);
        break;
    }
    case 3:
        cv::cvtColor(mat, rgba, cv::COLOR_BGR2RGBA);
        break;
    case 4:
        cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
        break;
    default:
        return fail(ErrorCategory::UnsupportedFormat,
                    "Nombre de canaux non supporté : " + std::to_string(mat.channels()));
    }
    return rgba;
}

}  // namespace

Result<ImageInfo> read_image_info(const std::filesystem::path& path) {
    auto mat = decode_file(path);
    if (!mat) {
        return std::unexpected(mat.error());
    }
    ImageInfo info;
    info.width_px = mat->cols;
    info.height_px = mat->rows;
    info.channels = mat->channels();
    info.has_alpha = (mat->channels() == 4 || mat->channels() == 2);
    info.format = format_from_extension(path);
    return info;
}

Result<Image> load_image(const std::filesystem::path& path) {
    auto mat = decode_file(path);
    if (!mat) {
        return std::unexpected(mat.error());
    }
    const bool had_alpha = (mat->channels() == 4 || mat->channels() == 2);

    auto rgba = to_rgba8(*mat);
    if (!rgba) {
        return std::unexpected(rgba.error());
    }

    Image img;
    img.width = rgba->cols;
    img.height = rgba->rows;
    img.source_had_alpha = had_alpha;
    img.rgba.resize(static_cast<std::size_t>(rgba->cols) * static_cast<std::size_t>(rgba->rows) * 4);
    // cv::Mat peut avoir un padding de ligne : copie ligne par ligne.
    for (int row = 0; row < rgba->rows; ++row) {
        const auto* src = rgba->ptr<std::uint8_t>(row);
        std::copy_n(src, static_cast<std::size_t>(rgba->cols) * 4,
                    img.rgba.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(rgba->cols) * 4);
    }
    return img;
}

}  // namespace openstitch::image
