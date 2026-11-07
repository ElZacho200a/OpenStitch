// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <filesystem>

#include "document_services.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter over the thread services (thread plan, sewing order, colourways, charts). Every
// mutation is one ICommand through DocumentSession (one undo step), so the canvas, the DST and
// the production sheet follow without any state kept here. No engine logic lives in this class.
class ThreadController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY messageChanged)
    // [{index, label, color, objectCount, visible, selected}] in order of first sewing.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY changed)
    // [{id, name, color, position}] in sewing order, background first.
    Q_PROPERTY(QVariantList sewingOrder READ sewingOrder NOTIFY changed)
    // [{name, active}]
    Q_PROPERTY(QVariantList colorways READ colorways NOTIFY changed)
    // [{id, name, source, count, user}]
    Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
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

    // --- Thread plan ---------------------------------------------------------------------------
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
    Q_INVOKABLE bool importChart(const QUrl& file);
    Q_INVOKABLE bool removeChart(const QString& chartId);
    Q_INVOKABLE bool exportThreadList(const QUrl& file);

    // --- Colourways ------------------------------------------------------------------------------
    Q_INVOKABLE bool addColorway(const QString& name);
    Q_INVOKABLE bool activateColorway(int index);
    Q_INVOKABLE bool removeColorway(int index);
    Q_INVOKABLE bool renameColorway(int index, const QString& name);
signals:
    void changed();
    void chartsChanged();
    void messageChanged();

private:
    bool report(const ServiceResult& result, const QString& success);
    StudioController& studio_;
    std::filesystem::path chartDir_;
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
