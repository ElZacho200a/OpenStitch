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
    if (ext == ".png")
        return "PNG";
    if (ext == ".jpg" || ext == ".jpeg")
        return "JPEG";
    if (ext == ".bmp")
        return "BMP";
    if (ext == ".gif")
        return "GIF";
    if (ext == ".webp")
        return "WebP";
    if (ext == ".tif" || ext == ".tiff")
        return "TIFF";
    return ext.empty() ? "inconnu" : ext.substr(1);
}

// HP-AUTO-014 : orientation EXIF (1..8) d'un JPEG, 1 si absente ou illisible. Analyse minimale
// des segments APP1 « Exif » ; aucune dépendance (cv::imdecode avec IMREAD_UNCHANGED ignore
// l'orientation).
int exif_orientation(const std::vector<char>& data) {
    const auto at = [&](std::size_t i) { return static_cast<std::uint8_t>(data[i]); };
    const std::size_t n = data.size();
    if (n < 4 || at(0) != 0xFF || at(1) != 0xD8) {
        return 1;
    }
    std::size_t pos = 2;
    while (pos + 4 <= n && at(pos) == 0xFF) {
        const std::uint8_t marker = at(pos + 1);
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        const std::size_t len = (static_cast<std::size_t>(at(pos + 2)) << 8) | at(pos + 3);
        if (len < 2 || pos + 2 + len > n) {
            break;
        }
        const std::size_t seg = pos + 4;
        const std::size_t segEnd = pos + 2 + len;
        if (marker == 0xE1 && segEnd - seg >= 14 && data[seg] == 'E' && data[seg + 1] == 'x' &&
            data[seg + 2] == 'i' && data[seg + 3] == 'f' && at(seg + 4) == 0 && at(seg + 5) == 0) {
            const std::size_t tiff = seg + 6;
            const bool le = data[tiff] == 'I' && data[tiff + 1] == 'I';
            if (!le && !(data[tiff] == 'M' && data[tiff + 1] == 'M')) {
                return 1;
            }
            const auto u16 = [&](std::size_t o) -> std::size_t {
                return le ? (at(o) | (static_cast<std::size_t>(at(o + 1)) << 8))
                          : ((static_cast<std::size_t>(at(o)) << 8) | at(o + 1));
            };
            const auto u32 = [&](std::size_t o) -> std::size_t {
                return le ? (u16(o) | (u16(o + 2) << 16)) : ((u16(o) << 16) | u16(o + 2));
            };
            if (tiff + 8 > segEnd) {
                return 1;
            }
            const std::size_t ifd = tiff + u32(tiff + 4);
            if (ifd + 2 > segEnd) {
                return 1;
            }
            const std::size_t count = u16(ifd);
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t e = ifd + 2 + i * 12;
                if (e + 12 > segEnd) {
                    break;
                }
                if (u16(e) == 0x0112) {
                    const std::size_t v = u16(e + 8);
                    return v >= 1 && v <= 8 ? static_cast<int>(v) : 1;
                }
            }
            return 1;
        }
        pos = segEnd;
    }
    return 1;
}

void apply_orientation(cv::Mat& mat, int orientation) {
    switch (orientation) {
    case 2:
        cv::flip(mat, mat, 1);
        break;
    case 3:
        cv::flip(mat, mat, -1);
        break;
    case 4:
        cv::flip(mat, mat, 0);
        break;
    case 5:
        cv::transpose(mat, mat);
        break;
    case 6:
        cv::rotate(mat, mat, cv::ROTATE_90_CLOCKWISE);
        break;
    case 7:
        cv::transpose(mat, mat);
        cv::flip(mat, mat, -1);
        break;
    case 8:
        cv::rotate(mat, mat, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    default:
        break;
    }
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
    apply_orientation(mat, exif_orientation(bytes));
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
    case 2: { // niveaux de gris + alpha (PNG "GA")
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

} // namespace

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

Result<std::vector<std::uint8_t>> encode_png(const Image& image) {
    if (image.empty()) {
        return fail(ErrorCategory::Internal, "Image vide : rien à encoder");
    }
    // Image interne RGBA -> BGRA pour OpenCV, puis imencode en PNG.
    const cv::Mat rgba(image.height, image.width, CV_8UC4,
                       const_cast<std::uint8_t*>(image.rgba.data()));
    cv::Mat bgra;
    cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
    std::vector<std::uint8_t> out;
    if (!cv::imencode(".png", bgra, out)) {
        return fail(ErrorCategory::Internal, "Échec de l'encodage PNG");
    }
    return out;
}

Result<Image> decode_image(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return fail(ErrorCategory::InvalidFile, "Données d'image vides");
    }
    const cv::Mat raw(1, static_cast<int>(bytes.size()), CV_8U,
                      const_cast<std::uint8_t*>(bytes.data()));
    cv::Mat mat = cv::imdecode(raw, cv::IMREAD_UNCHANGED);
    if (mat.empty()) {
        return fail(ErrorCategory::InvalidFile, "Image intégrée illisible");
    }
    const bool had_alpha = (mat.channels() == 4 || mat.channels() == 2);
    auto rgba = to_rgba8(mat);
    if (!rgba) {
        return std::unexpected(rgba.error());
    }
    Image img;
    img.width = rgba->cols;
    img.height = rgba->rows;
    img.source_had_alpha = had_alpha;
    img.rgba.resize(static_cast<std::size_t>(rgba->cols) * static_cast<std::size_t>(rgba->rows) *
                    4);
    for (int row = 0; row < rgba->rows; ++row) {
        const auto* src = rgba->ptr<std::uint8_t>(row);
        std::copy_n(src, static_cast<std::size_t>(rgba->cols) * 4,
                    img.rgba.data() +
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(rgba->cols) * 4);
    }
    return img;
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
    img.rgba.resize(static_cast<std::size_t>(rgba->cols) * static_cast<std::size_t>(rgba->rows) *
                    4);
    // cv::Mat peut avoir un padding de ligne : copie ligne par ligne.
    for (int row = 0; row < rgba->rows; ++row) {
        const auto* src = rgba->ptr<std::uint8_t>(row);
        std::copy_n(src, static_cast<std::size_t>(rgba->cols) * 4,
                    img.rgba.data() +
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(rgba->cols) * 4);
    }
    return img;
}

} // namespace openstitch::image
