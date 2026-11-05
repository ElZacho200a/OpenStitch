// SPDX-License-Identifier: Apache-2.0
#include "analysis_overlays.hpp"

#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QRectF>

#include <cstring>

#include "preview_canvas.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString overlayUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
QString tr(const char* text) {
    return QObject::tr(text);
}
} // namespace

bool AnalysisOverlays::densityShown() const {
    return host_.canvas.hasDensityOverlay();
}

void AnalysisOverlays::toggleDensity() {
    if (densityActive_) {
        hideDensity();
        host_.notice(tr("Carte de densité masquée."), false);
        return;
    }
    if (host_.session.project().embroidery_objects.empty()) {
        host_.notice(tr("Rien à analyser : le document ne contient aucun objet brodé."), true);
        return;
    }
    densityActive_ = true;
    requestDensity();
}

void AnalysisOverlays::hideDensity() {
    densityActive_ = false;
    densityRevision_ = 0;
    densityHotspots_.clear();
    if (densityTask_ != 0) {
        (void)host_.tasks.cancel(densityTask_);
        densityTask_ = 0;
    }
    host_.canvas.setDensityOverlay(QImage(), QRectF());
}

void AnalysisOverlays::requestDensity() {
    if (densityTask_ != 0) {
        (void)host_.tasks.cancel(densityTask_); // une carte périmée n'est jamais livrée
    }
    const auto snapshot = host_.session.snapshot();
    const auto revision = host_.session.revision();
    densityTask_ = host_.tasks.submit<DensityOverlay>(
        tr("Carte de densité").toStdString(),
        [snapshot, revision](const TaskContext& ctx) {
            return run_density(snapshot, revision, ctx);
        },
        [this](Result<DensityOverlay>&& r) {
            densityTask_ = 0;
            if (!densityActive_) {
                return;
            }
            if (!r) {
                densityActive_ = false;
                host_.notice(
                    tr("Carte de densité impossible : %1").arg(overlayUtf8(r.error().message)),
                    true);
                return;
            }
            if (r->revision != host_.session.revision()) {
                return; // le document a changé depuis : une autre carte est en route
            }
            applyDensity(*r);
        });
}

void AnalysisOverlays::applyDensity(const DensityOverlay& overlay) {
    densityRevision_ = overlay.revision;
    densityHotspots_ = overlay.hotspots;
    if (host_.frame.px_per_mm <= 0.0) {
        return; // pas d'aperçu : la carte sera posée au prochain aperçu
    }
    QImage image(overlay.columns, overlay.rows, QImage::Format_RGBA8888);
    for (int y = 0; y < overlay.rows; ++y) {
        std::memcpy(image.scanLine(y),
                    overlay.rgba.data() + static_cast<std::size_t>(y) * overlay.columns * 4,
                    static_cast<std::size_t>(overlay.columns) * 4);
    }
    // Repère modèle (Y vers le haut) -> pixels de l'aperçu (Y vers le bas).
    const QRectF rect(QPointF(host_.frame.mmToPxX(overlay.minXmm),
                              host_.frame.mmToPxY(-(overlay.minYmm + overlay.heightMm))),
                      QPointF(host_.frame.mmToPxX(overlay.minXmm + overlay.widthMm),
                              host_.frame.mmToPxY(-overlay.minYmm)));
    host_.canvas.setDensityOverlay(image, rect);
    const auto zones = static_cast<int>(overlay.hotspots.size());
    host_.notice(zones == 0 ? tr("Densité maximale %1 piqûres/mm² : aucune zone sur-dense.")
                                  .arg(overlay.maxPenetrationsPerMm2, 0, 'f', 1)
                            : tr("Densité maximale %1 piqûres/mm² : %2 zone(s) sur-dense(s) "
                                 "(seuil %3).")
                                  .arg(overlay.maxPenetrationsPerMm2, 0, 'f', 1)
                                  .arg(zones)
                                  .arg(overlay.thresholdPerMm2, 0, 'f', 1),
                 zones > 0);
}

bool AnalysisOverlays::fabricShown() const {
    return host_.canvas.hasFabricOverlay();
}

void AnalysisOverlays::toggleFabricRisk() {
    if (fabricActive_) {
        hideFabricRisk();
        host_.notice(tr("Risque de plissement masqué."), false);
        return;
    }
    if (host_.session.project().embroidery_objects.empty()) {
        host_.notice(tr("Rien à analyser : le document ne contient aucun objet brodé."), true);
        return;
    }
    fabricActive_ = true;
    requestFabricRisk();
}

void AnalysisOverlays::hideFabricRisk() {
    fabricActive_ = false;
    fabricRisk_.reset();
    if (fabricTask_ != 0) {
        (void)host_.tasks.cancel(fabricTask_);
        fabricTask_ = 0;
    }
    host_.canvas.setFabricOverlay(QImage(), QRectF());
    host_.canvas.setFabricArrows({});
}

void AnalysisOverlays::requestFabricRisk() {
    if (fabricTask_ != 0) {
        (void)host_.tasks.cancel(fabricTask_); // un résultat périmé n'est jamais livré
    }
    const auto snapshot = host_.session.snapshot();
    const auto revision = host_.session.revision();
    fabricTask_ = host_.tasks.submit<FabricRisk>(
        tr("Déformation du tissu").toStdString(),
        [snapshot, revision](const TaskContext& ctx) {
            return runFabricRisk(snapshot, revision, ctx);
        },
        [this](Result<FabricRisk>&& r) {
            fabricTask_ = 0;
            if (!fabricActive_) {
                return;
            }
            if (!r) {
                fabricActive_ = false;
                host_.notice(
                    tr("Analyse du tissu impossible : %1").arg(overlayUtf8(r.error().message)),
                    true);
                return;
            }
            if (r->revision != host_.session.revision()) {
                return; // le document a changé : un autre calcul est en route
            }
            applyFabricRisk(*r);
        });
}

void AnalysisOverlays::applyFabricRisk(const FabricRisk& risk) {
    fabricRisk_ = risk;
    if (host_.frame.px_per_mm <= 0.0) {
        return; // pas d'aperçu : la carte sera posée au prochain aperçu
    }
    QImage image(risk.columns, risk.rows, QImage::Format_RGBA8888);
    for (int y = 0; y < risk.rows; ++y) {
        std::memcpy(image.scanLine(y),
                    risk.rgba.data() + static_cast<std::size_t>(y) * risk.columns * 4,
                    static_cast<std::size_t>(risk.columns) * 4);
    }
    const QRectF rect(QPointF(host_.frame.mmToPxX(risk.minXmm),
                              host_.frame.mmToPxY(-(risk.minYmm + risk.heightMm))),
                      QPointF(host_.frame.mmToPxX(risk.minXmm + risk.widthMm),
                              host_.frame.mmToPxY(-risk.minYmm)));
    host_.canvas.setFabricOverlay(image, rect);
    // Flèches : le déplacement réel (< 1 mm) est invisible tel quel, il est amplifié ×20 et le
    // message le dit : jamais une déformation présentée comme à l'échelle.
    constexpr double kAmplify = 20.0;
    QVector<QLineF> arrows;
    for (const auto& a : risk.arrows) {
        const QPointF from(host_.frame.mmToPxX(a.xMm), host_.frame.mmToPxY(-a.yMm));
        const QPointF to(host_.frame.mmToPxX(a.xMm + a.dxMm * kAmplify),
                         host_.frame.mmToPxY(-(a.yMm + a.dyMm * kAmplify)));
        arrows.push_back(QLineF(from, to));
    }
    host_.canvas.setFabricArrows(arrows);
    host_.notice(overlayUtf8(risk.verdict()) + tr(" Flèches amplifiées ×20."),
                 risk.maxWrinkleIndex >= 1.0);
}

void AnalysisOverlays::onDocumentChanged() {
    if (fabricActive_ && fabricRisk_ && fabricRisk_->revision != host_.session.revision()) {
        requestFabricRisk(); // la carte affichée date d'avant la dernière modification
    }
}

void AnalysisOverlays::onPreviewReady() {
    if (densityActive_ && densityRevision_ != host_.session.revision()) {
        requestDensity(); // la carte affichée date d'avant la dernière modification
    }
}

} // namespace openstitch::desktop_v2
