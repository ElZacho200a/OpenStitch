// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- outils qui travaillent sur l'IMAGE SOURCE du projet : clic-pour-broder (une zone de
// couleur devient un objet brodé) et zones de détail (masque qui préserve les petites formes à la
// segmentation). Chaque action est UNE `ICommand` annulable. Sans Qt.
//
// Repère image : centrée sur l'origine du modèle, `project.mm_per_px` millimètres par pixel, Y vers
// le bas (c'est celui des mm « écran » de l'aperçu) -- le même que l'ancien desktop.

#include <cstdint>
#include <optional>
#include <vector>

#include "document_services.hpp"
#include "openstitch/image/image.hpp"

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
ServiceResult clearDetail(DocumentSession& session);
// Valeur du masque (0..255) sous un point ; 0 si pas de masque ou hors image.
[[nodiscard]] int detailWeightAt(const document::Project& project, const image::Image& processed,
                                 double xScreenMm, double yScreenMm);

} // namespace openstitch::desktop_v2
