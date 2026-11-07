// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <memory>
#include <vector>

#include "source_image_tools.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

// Adaptateur des outils image source. Le masque d'aperçu et l'image sont immuables ; les calculs
// d'image / croissance sont exécutés par TaskManager, les commandes viennent de core/.
class DetailController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY changed)
    Q_PROPERTY(double radiusMm READ radiusMm WRITE setRadiusMm NOTIFY changed)
    Q_PROPERTY(int tolerance READ tolerance WRITE setTolerance NOTIFY changed)
    Q_PROPERTY(int stitchKind READ stitchKind WRITE setStitchKind NOTIFY changed)
    Q_PROPERTY(bool replaceSelected READ replaceSelected WRITE setReplaceSelected NOTIFY changed)
    Q_PROPERTY(bool confirmationPending READ confirmationPending NOTIFY changed)
    Q_PROPERTY(bool showMask READ showMask WRITE setShowMask NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString imageUrl READ imageUrl NOTIFY changed)
    Q_PROPERTY(QString maskUrl READ maskUrl NOTIFY changed)
    Q_PROPERTY(QSizeF imageSizeMm READ imageSizeMm NOTIFY changed)
public:
    explicit DetailController(StudioController& studio, QObject* parent = nullptr);
    ~DetailController() override;
    [[nodiscard]] bool ready() const { return processed_ && !processed_->empty(); }
    [[nodiscard]] bool busy() const { return clickTask_ != 0; }
    [[nodiscard]] bool gestureActive() const { return !stroke_.empty(); }
    [[nodiscard]] double radiusMm() const { return radius_; }
    [[nodiscard]] int tolerance() const { return tolerance_; }
    [[nodiscard]] int stitchKind() const { return kind_; }
    [[nodiscard]] bool replaceSelected() const { return replace_; }
    [[nodiscard]] bool confirmationPending() const { return pending_; }
    [[nodiscard]] bool showMask() const { return showMask_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QString imageUrl() const { return imageUrl_; }
    [[nodiscard]] QString maskUrl() const { return maskUrl_; }
    [[nodiscard]] QSizeF imageSizeMm() const;
    void setRadiusMm(double value);
    void setTolerance(int value);
    void setStitchKind(int value);
    void setReplaceSelected(bool value);
    void setShowMask(bool value);
    bool press(const QString& tool, double x, double y);
    void move(double x, double y);
    void release(double x, double y);
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE void clearMask();
    Q_INVOKABLE void confirmReplacement(bool discardRetouches);
signals:
    void changed();

private:
    void refresh();
    void preview();
    void showCommittedMask();
    void click(double x, double y, bool confirmed = false);
    void inform(QString text);
    StudioController& studio_;
    DocumentSession::Token token_{};
    QTimer pump_;
    QTimer refreshTimer_;
    std::shared_ptr<const image::Image> processed_;
    std::vector<std::pair<double, double>> stroke_;
    double radius_{2.0};
    int tolerance_{20};
    int kind_{0};
    bool erase_{false};
    bool clickStroke_{false};
    bool replace_{false};
    bool showMask_{true};
    bool pending_{false};
    double pendingX_{0}, pendingY_{0};
    std::uint64_t pendingRevision_{0};
    std::uint64_t pendingObject_{0};
    QString message_;
    QString imageUrl_, maskUrl_;
    TaskId refreshTask_{0}, previewTask_{0}, clickTask_{0};
    std::uint64_t previewSerial_{0};
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
