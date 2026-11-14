// SPDX-License-Identifier: Apache-2.0
#pragma once

// Adaptateur Qt Quick des fondus de deux fils (famille G) : création / modification /
// suppression, axe de transition manipulable par poignées (aperçu sans mutation, validation en UNE
// commande annulable) et courbe de transition. Aucune logique métier : services `fade_service` et
// `fade_axis` du coeur. Coordonnées en mm « modèle » (Y vers le haut).

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <cstdint>
#include <vector>

#include "fade_axis.hpp"
#include "fade_service.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

class FadeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString objectId READ objectId NOTIFY changed)
    Q_PROPERTY(bool hasFade READ hasFade NOTIFY changed)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    Q_PROPERTY(QString incompatibility READ incompatibility NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(bool complementary READ complementary NOTIFY changed)
    Q_PROPERTY(int texture READ texture NOTIFY changed)
    Q_PROPERTY(int geometry READ geometry NOTIFY changed)
    Q_PROPERTY(int method READ method NOTIFY changed)
    Q_PROPERTY(double satinDensityMm READ satinDensityMm NOTIFY changed)
    Q_PROPERTY(int crosshatchLayers READ crosshatchLayers NOTIFY changed)
    Q_PROPERTY(double crosshatchAngleDeg READ crosshatchAngleDeg NOTIFY changed)
    Q_PROPERTY(double angle READ angle NOTIFY changed)
    Q_PROPERTY(bool multiFade READ multiFade NOTIFY changed)
    Q_PROPERTY(bool satinTarget READ satinTarget NOTIFY changed)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY changed)
    Q_PROPERTY(int curve READ curve NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantMap axis READ axis NOTIFY changed)
    Q_PROPERTY(QVariantList curves READ curves CONSTANT)
    Q_PROPERTY(QVariantList methods READ methods CONSTANT)
    Q_PROPERTY(QVariantList curveProfile READ curveProfile NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    explicit FadeController(StudioController& studio, QObject* parent = nullptr);
    ~FadeController() override;

    [[nodiscard]] QString objectId() const;
    [[nodiscard]] bool hasFade() const {
        return settings_.has_value() || multiSettings_.has_value();
    }
    [[nodiscard]] bool multiFade() const { return multiSettings_.has_value(); }
    [[nodiscard]] bool satinTarget() const;
    [[nodiscard]] bool canCreate() const { return id_ != 0 && !hasFade() && incompat_.isEmpty(); }
    [[nodiscard]] QString incompatibility() const { return incompat_; }
    [[nodiscard]] QColor foreground() const;
    [[nodiscard]] QColor background() const;
    [[nodiscard]] bool complementary() const {
        return multiSettings_ ? multiSettings_->complementary
                              : settings_ && settings_->complementary;
    }
    [[nodiscard]] int texture() const {
        return multiSettings_ ? multiSettings_->texture : settings_ ? settings_->texture : 0;
    }
    [[nodiscard]] int geometry() const {
        return multiSettings_ ? multiSettings_->geometry : settings_ ? settings_->geometry : 0;
    }
    [[nodiscard]] int method() const { return multiSettings_ ? multiSettings_->method : 0; }
    [[nodiscard]] double satinDensityMm() const {
        return multiSettings_ ? multiSettings_->satinDensityMm : 0.4;
    }
    [[nodiscard]] int crosshatchLayers() const {
        return multiSettings_ ? multiSettings_->crosshatchLayers : 2;
    }
    [[nodiscard]] double crosshatchAngleDeg() const {
        return multiSettings_ ? multiSettings_->crosshatchAngleDeg : 0.0;
    }
    [[nodiscard]] double angle() const;
    [[nodiscard]] QVariantList colors() const;
    [[nodiscard]] int curve() const;
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QVariantMap axis() const { return axisMap(axis_); }
    [[nodiscard]] QVariantList curves() const;
    [[nodiscard]] QVariantList methods() const;
    [[nodiscard]] QVariantList curveProfile() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    Q_INVOKABLE bool create(const QColor& foreground, bool complementary);
    Q_INVOKABLE bool createWithTexture(const QColor& foreground, bool complementary, int texture);
    Q_INVOKABLE bool setTexture(int texture);
    Q_INVOKABLE bool setGeometry(int geometry);
    Q_INVOKABLE bool setAngle(double degrees);
    Q_INVOKABLE bool createMulticolor(const QVariantList& colors, bool complementary, int texture,
                                      int geometry);
    Q_INVOKABLE bool editMulticolor(const QVariantList& colors, bool complementary, int texture,
                                    int geometry);
    Q_INVOKABLE bool createMulticolorAdvanced(const QVariantList& colors, bool complementary,
                                              int texture, int geometry, int method,
                                              double satinDensityMm, int crosshatchLayers,
                                              double crosshatchAngleDeg);
    Q_INVOKABLE bool editMulticolorAdvanced(const QVariantList& colors, bool complementary,
                                            int texture, int geometry, int method,
                                            double satinDensityMm, int crosshatchLayers,
                                            double crosshatchAngleDeg);
    Q_INVOKABLE bool edit(const QColor& foreground, bool complementary);
    Q_INVOKABLE bool remove();
    // handle : 0 Start, 1 End, 2 Move, 3 Rotate. Aperçu = géométrie pure, aucun point régénéré.
    Q_INVOKABLE QVariantMap previewHandle(int handle, double xMm, double yMm, bool symmetric) const;
    Q_INVOKABLE bool commitHandle(int handle, double xMm, double yMm, bool symmetric);
    Q_INVOKABLE bool setCurve(int curve);
    Q_INVOKABLE int layerCurve(int layer) const;
    Q_INVOKABLE bool setLayerCurve(int layer, int curve);
    // Inverse le sens du fondu (début <-> fin) : une étape d'annulation.
    Q_INVOKABLE bool reverse();
signals:
    void changed();
    void messageChanged();

private:
    void refresh();
    bool report(const ServiceResult& result, const QString& success);
    bool refuse(const QString& why);
    [[nodiscard]] static QVariantMap axisMap(const std::optional<FadeAxisView>& view);

    StudioController& studio_;
    std::uint64_t lastSeenRevision_{~std::uint64_t{0}};
    std::vector<Target> lastSeenSelection_;
    std::uint64_t id_{0};
    std::optional<FadeSettings> settings_;
    std::optional<MultiFadeSettings> multiSettings_;
    int multiCurve_{0};
    std::optional<FadeAxisView> axis_;
    QString incompat_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
