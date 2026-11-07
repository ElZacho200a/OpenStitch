// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "task_manager.hpp"
#include "thread_controller.hpp"
#include "thread_palette_service.hpp"

namespace openstitch::desktop_v2::studio {

// Profile data is independent of the document. Colorway previews use an immutable copy on a
// worker; the only document edit is explicit activation through the existing core command.
class ThreadInventoryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList stock READ stock NOTIFY stockChanged)
    Q_PROPERTY(QVariantList charts READ charts NOTIFY chartsChanged)
    Q_PROPERTY(QVariantList colorways READ colorways NOTIFY variantsChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageOk READ messageOk NOTIFY messageChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY stockChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY previewChanged)
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY previewChanged)
    Q_PROPERTY(int previewIndex READ previewIndex NOTIFY previewChanged)
public:
    ThreadInventoryController(StudioController& studio, ThreadController& threads,
                              std::filesystem::path configDir, QObject* parent = nullptr);
    ~ThreadInventoryController() override;
    [[nodiscard]] QVariantList stock() const;
    [[nodiscard]] QVariantList charts() const { return threads_.charts(); }
    [[nodiscard]] QVariantList colorways() const;
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] bool messageOk() const { return ok_; }
    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] QUrl previewUrl() const { return preview_; }
    [[nodiscard]] int previewIndex() const { return previewIndex_; }
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool owns(const QString& chart, const QString& code) const;
    Q_INVOKABLE bool setOwned(const QString& chart, const QString& code, bool owned);
    Q_INVOKABLE bool addPersonal(const QString& code, const QString& name, const QString& color,
                                 const QString& brand = {});
    Q_INVOKABLE bool removeStock(const QString& chart, const QString& code);
    Q_INVOKABLE QVariantList search(const QString& chart, const QString& query) const;
    Q_INVOKABLE QVariantMap compareColors(const QString& a, const QString& b, double ratioB);
    Q_INVOKABLE QVariantMap suggest(const QString& target, const QString& chart);
    Q_INVOKABLE bool importChart(const QUrl& file);
    Q_INVOKABLE bool removeChart(const QString& chart);
    Q_INVOKABLE void previewColorway(int index);
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE bool activatePreview();
signals:
    void stockChanged();
    void chartsChanged();
    void variantsChanged();
    void messageChanged();
    void previewChanged();

private:
    bool say(bool ok, const QString& message);
    bool commit(thread_palette::ThreadStock stock);
    StudioController& studio_;
    ThreadController& threads_;
    std::filesystem::path file_;
    thread_palette::ThreadStock stock_;
    DocumentSession::Token token_{};
    std::uint64_t seenRevision_{};
    std::uint64_t request_{};
    bool loaded_{false};
    bool ok_{true};
    bool busy_{false};
    QString message_;
    QUrl preview_;
    int previewIndex_{-1};
    QTimer pump_;
    TaskManager tasks_;
};

} // namespace openstitch::desktop_v2::studio
