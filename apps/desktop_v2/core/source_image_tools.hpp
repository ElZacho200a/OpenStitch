// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- outils qui travaillent sur l'IMAGE SOURCE du projet : clic-pour-broder (une zone de
// couleur devient un objet brodé) et zones de détail (masque qui préserve les petites formes à la
// segmentation). Chaque action est UNE `ICommand` annulable. Sans Qt.
//
// Repère image : centrée sur l'origine du modèle, `project.imageSource().mm_per_px` millimètres par
// pixel, Y vers le bas (c'est celui des mm « écran » de l'aperçu) -- le même que l'ancien desktop.

#include <cstdint>
#include <optional>
#include <vector>

#include "document_services.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop_v2 {

struct PixelPoint {
    int x{0};
    int y{0};
};

enum class ClickKind { Auto, Fill, Satin, Contour };

// Image de travail : l'original avec la pile de prétraitements (`image::apply_pipeline`). Vide /
// erreur si le projet n'a pas d'image (projet créé à partir de zéro).
[[nodiscard]] Result<image::Image> processedImage(const document::Project& project);

// Pixel sous un point en mm « écran » (Y vers le bas) ; rien hors de l'image.
[[nodiscard]] std::optional<PixelPoint> pixelUnder(const image::Image& image, double mmPerPx,
                                                   double xScreenMm, double yScreenMm);
// Centre d'un pixel en mm « écran » (réciproque de `pixelUnder`).
[[nodiscard]] std::pair<double, double> pixelCentreMm(const image::Image& image, double mmPerPx,
                                                      PixelPoint pixel);

// Zone de couleur connexe autour du point (tolérance CIELAB 0..100, comblement des trous isolés)
// -> vectorisation -> objet brodé. Retourne l'identifiant de l'objet brodé créé, ou le refus du
// moteur en clair (zone vide, trop petite…).
struct ClickResult {
    bool ok{false};
    std::string message;
    std::uint64_t objectId{0};
};
ClickResult clickToStitch(DocumentSession& session, const image::Image& processed, double xScreenMm,
                          double yScreenMm, int tolerance, ClickKind kind);

// Zones de détail : peint (ou gomme) un disque de rayon `radiusMm` en chaque centre (mm « écran »),
// UN trait = UNE commande.
ServiceResult paintDetail(DocumentSession& session, const image::Image& processed,
                          const std::vector<std::pair<double, double>>& centresMm, double radiusMm,
                          bool erase);
// Même chose en pixels de l'image de travail (`width` x `height`) : c'est ce que manipule l'IHM,
// qui connaît déjà le pixel sous le curseur. Le rayon reste en mm (réglage indépendant de la
// résolution).
ServiceResult paintDetailPixels(DocumentSession& session, int width, int height, double mmPerPx,
                                const std::vector<std::pair<int, int>>& pixels, double radiusMm,
                                bool erase, document::MaskKind kind = document::MaskKind::Detail);
ServiceResult clearDetail(DocumentSession& session,
                          document::MaskKind kind = document::MaskKind::Detail);

// Bornes du rayon du pinceau de détail (mm).
inline constexpr double kMinDetailRadiusMm = 0.2;
inline constexpr double kMaxDetailRadiusMm = 50.0;

// Masque de détail utilisable avec une image de `width` x `height` : le masque du projet, ou
// nullptr s'il est absent, vide de détail ou d'une autre taille (image recadrée depuis). Le
// pointeur vit autant que `source`.
[[nodiscard]] const segmentation::DetailMask* usableDetailMask(const document::ImageSource& source,
                                                               int width, int height);
// Valeur du masque (0..255) sous un point ; 0 si pas de masque ou hors image.
[[nodiscard]] int detailWeightAt(const document::Project& project, const image::Image& processed,
                                 double xScreenMm, double yScreenMm);

} // namespace openstitch::desktop_v2
