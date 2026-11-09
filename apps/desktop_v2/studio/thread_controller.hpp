// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <filesystem>
#include <optional>

#include "openstitch/stitch_analysis/thread_reduction.hpp"
#include "openstitch/thread_palette/thread_stock.hpp"

#include "document_services.hpp"
#include "studio_controller.hpp"
#include "thread_palette_service.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter over the thread services (thread plan, sewing order, colourways, charts). Every
// mutation is one ICommand through DocumentSession (one undo step), so the canvas, the DST and
// the production sheet follow without any state kept here. No engine logic lives in this class.
class ThreadController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
    // [{index, id, label, color, objectCount, visible, selected}] in order of first sewing.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY changed)
    // [{id, name, color, position}] in sewing order, background first.
    Q_PROPERTY(QVariantList sewingOrder READ sewingOrder NOTIFY changed)
    // [{name, active}]
    Q_PROPERTY(QVariantList colorways READ colorways NOTIFY changed)
    // [{id, name, source, count, user}]
    Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
    Q_PROPERTY(QVariantList ownedThreads READ ownedThreads NOTIFY stockChanged)
    Q_PROPERTY(int stockCount READ stockCount NOTIFY stockChanged)
    Q_PROPERTY(bool limitToStock READ limitToStock WRITE setLimitToStock NOTIFY stockChanged)
    Q_PROPERTY(QVariantList colorFilm READ colorFilm NOTIFY changed)
    Q_PROPERTY(int filmChanges READ filmChanges NOTIFY changed)
    Q_PROPERTY(QVariantMap fabric READ fabric NOTIFY changed)
public:
    // `configDir` receives the imported charts (reloaded at the next start); empty = none.
    explicit ThreadController(StudioController& studio, std::filesystem::path configDir = {},
                              QObject* parent = nullptr);
    ~ThreadController() override;

    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] int threadCount() const;
    [[nodiscard]] QVariantList sewingOrder() const;
    [[nodiscard]] QVariantList colorways() const;
    [[nodiscard]] QVariantList charts() const;
    [[nodiscard]] QVariantList ownedThreads() const;
    [[nodiscard]] int stockCount() const;
    [[nodiscard]] bool limitToStock() const { return limitToStock_; }
    void setLimitToStock(bool value);
    [[nodiscard]] QVariantList colorFilm() const;
    [[nodiscard]] int filmChanges() const;
    [[nodiscard]] QVariantMap fabric() const;

    // --- Thread plan ---------------------------------------------------------------------------
    Q_INVOKABLE bool moveThread(int from, int to);
    Q_INVOKABLE bool mergeThreads(int from, int into);
    Q_INVOKABLE bool recolorThread(int entry, const QString& hex);
    Q_INVOKABLE bool setThreadVisible(int entry, bool visible);
    Q_INVOKABLE bool selectThreadObjects(int entry);
    Q_INVOKABLE bool moveInOrder(const QString& objectId, int toPosition);

    // --- Charts ----------------------------------------------------------------------------------
    // [{chartId, code, name, brand, color, distance}]
    Q_INVOKABLE [[nodiscard]] QVariantList searchThreads(const QString& chartId,
                                                         const QString& query) const;
    Q_INVOKABLE [[nodiscard]] QVariantList nearestToEntry(int entry, const QString& chartId,
                                                          int topN = 8) const;
    Q_INVOKABLE [[nodiscard]] QVariantList nearestToSelection(const QString& chartId,
                                                              int topN = 8) const;
    Q_INVOKABLE bool assignChartThread(int entry, const QString& chartId, const QString& code);
    // Gives every free colour (no chart thread) its nearest thread in `chartId`: one undo step
    // per colour changed (reported).
    Q_INVOKABLE int snapFreeColours(const QString& chartId);
    Q_INVOKABLE bool setOwnedThread(const QString& chartId, const QString& code, bool owned);
    Q_INVOKABLE bool addPersonalThread(const QString& code, const QString& name,
                                       const QString& hexColor);
    Q_INVOKABLE bool removeOwnedThread(const QString& chartId, const QString& code);
    Q_INVOKABLE bool assignOwnedThread(int entry, const QString& chartId, const QString& code);
    Q_INVOKABLE bool ownsThread(const QString& chartId, const QString& code) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap previewReduction(int maxThreads);
    Q_INVOKABLE bool applyReduction();
    Q_INVOKABLE bool selectFilmBlock(int index);
    Q_INVOKABLE bool moveFilmBlock(int from, int to);
    Q_INVOKABLE bool groupFilmByColor();
    Q_INVOKABLE bool importChart(const QUrl& file);
    Q_INVOKABLE bool removeChart(const QString& chartId);
    Q_INVOKABLE bool exportThreadList(const QUrl& file);

    // --- Colourways ------------------------------------------------------------------------------
    Q_INVOKABLE bool addColorway(const QString& name);
    Q_INVOKABLE bool activateColorway(int index);
    Q_INVOKABLE bool removeColorway(int index);
    Q_INVOKABLE bool renameColorway(int index, const QString& name);
    Q_INVOKABLE bool setFabric(const QString& hexColor, const QString& name,
                               const QString& material, const QString& stabilizer,
                               double grainAngleDeg, double weftSoftness);
signals:
    void changed();
    void chartsChanged();
    void messageChanged();
    void stockChanged();

private:
    bool report(const ServiceResult& result, const QString& success);
    [[nodiscard]] bool persistStock(const thread_palette::ThreadStock& stock);
    [[nodiscard]] bool persistStockSetting(bool limit);
    using Choices = std::vector<ThreadChoice>;
    [[nodiscard]] Choices stockChoices() const;
    [[nodiscard]] auto nearestChoices(Rgb rgb, const QString& chart, int n) const -> Choices;
    using ColorFilm = std::vector<stitch_analysis::ColorFilmBlock>;
    [[nodiscard]] const std::optional<ColorFilm>& currentFilm() const;
    [[nodiscard]] bool commitSewingOrder(const std::vector<ObjectId>& ids, const QString& label);
    StudioController& studio_;
    std::filesystem::path chartDir_;
    std::filesystem::path stockPath_;
    std::filesystem::path settingsPath_;
    thread_palette::ThreadStock stock_;
    bool stockWritable_{true};
    bool limitToStock_{false};
    mutable std::optional<ColorFilm> cachedFilm_;
    mutable std::uint64_t cachedFilmRevision_{0};
    mutable std::shared_ptr<const StudioSceneSnapshot> cachedFilmScene_;
    std::optional<stitch_analysis::ThreadMergePlan> reduction_;
    std::uint64_t reductionRevision_{0};
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
