// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "image_controller.hpp"
#include "view_controller.hpp"
#include <QObject>
#include <QTimer>

namespace openstitch::desktop_v2::studio {
// Display adapter: no document mutation, no second model; preparation runs on TaskManager.
class CompareController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool sourceVisible READ sourceVisible WRITE setSourceVisible NOTIFY changed)
    Q_PROPERTY(bool segmentationVisible READ segmentationVisible WRITE setSegmentationVisible NOTIFY
                   changed)
    Q_PROPERTY(bool vectorsVisible READ vectorsVisible WRITE setVectorsVisible NOTIFY changed)
    Q_PROPERTY(bool orderVisible READ orderVisible WRITE setOrderVisible NOTIFY changed)
    Q_PROPERTY(bool curtain READ curtain WRITE setCurtain NOTIFY changed)
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY changed)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY changed)
    Q_PROPERTY(bool hasSegmentation READ hasSegmentation NOTIFY changed)
    Q_PROPERTY(int buildCount READ buildCount NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
public:
    CompareController(StudioController& studio, ViewController& view, ImageController& images,
                      QObject* parent = nullptr);
    ~CompareController() override;
    bool sourceVisible() const { return display_.source; }
    bool segmentationVisible() const { return display_.segmentation; }
    bool vectorsVisible() const { return display_.vectors; }
    bool orderVisible() const { return display_.order; }
    bool curtain() const { return display_.curtain; }
    double opacity() const { return display_.opacity; }
    double position() const { return display_.position; }
    bool hasSource() const { return layers_ && !layers_->source.image.isNull(); }
    bool hasSegmentation() const { return layers_ && !layers_->segmentation.image.isNull(); }
    bool ready() const { return layers_ != nullptr; }
    int buildCount() const { return builds_; }
    void setSourceVisible(bool value);
    void setSegmentationVisible(bool value);
    void setVectorsVisible(bool value);
    void setOrderVisible(bool value);
    void setCurtain(bool value);
    void setOpacity(double value);
    void setPosition(double value);
    const std::shared_ptr<const ComparisonLayers>& layers() const { return layers_; }
signals:
    void changed();

private:
    void rebuild();
    void publish();
    StudioController& studio_;
    ViewController& view_;
    ImageController& images_;
    ComparisonDisplay display_;
    std::shared_ptr<const ComparisonLayers> layers_;
    std::uint64_t request_{0};
    std::uint64_t requestedDocument_{0};
    int requestedPreview_{-1};
    int builds_{0};
    QTimer pump_;
    QTimer refresh_;
    TaskManager tasks_;
};
} // namespace openstitch::desktop_v2::studio
