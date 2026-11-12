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
    Q_PROPERTY(int curve READ curve NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantMap axis READ axis NOTIFY changed)
    Q_PROPERTY(QVariantList curves READ curves CONSTANT)
    Q_PROPERTY(QVariantList curveProfile READ curveProfile NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
public:
    explicit FadeController(StudioController& studio, QObject* parent = nullptr);
    ~FadeController() override;

    [[nodiscard]] QString objectId() const;
    [[nodiscard]] bool hasFade() const { return settings_.has_value(); }
    [[nodiscard]] bool canCreate() const { return id_ != 0 && !settings_ && incompat_.isEmpty(); }
    [[nodiscard]] QString incompatibility() const { return incompat_; }
    [[nodiscard]] QColor foreground() const;
    [[nodiscard]] QColor background() const;
    [[nodiscard]] bool complementary() const { return settings_ && settings_->complementary; }
    [[nodiscard]] int texture() const { return settings_ ? settings_->texture : 0; }
    [[nodiscard]] int curve() const { return axis_ ? static_cast<int>(axis_->curve) : 0; }
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QVariantMap axis() const { return axisMap(axis_); }
    [[nodiscard]] QVariantList curves() const;
    [[nodiscard]] QVariantList curveProfile() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }

    Q_INVOKABLE bool create(const QColor& foreground, bool complementary);
    Q_INVOKABLE bool createWithTexture(const QColor& foreground, bool complementary, int texture);
    Q_INVOKABLE bool setTexture(int texture);
    Q_INVOKABLE bool edit(const QColor& foreground, bool complementary);
    Q_INVOKABLE bool remove();
    // handle : 0 Start, 1 End, 2 Move, 3 Rotate. Aperçu = géométrie pure, aucun point régénéré.
    Q_INVOKABLE QVariantMap previewHandle(int handle, double xMm, double yMm, bool symmetric) const;
    Q_INVOKABLE bool commitHandle(int handle, double xMm, double yMm, bool symmetric);
    Q_INVOKABLE bool setCurve(int curve);
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
    std::optional<FadeAxisView> axis_;
    QString incompat_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
