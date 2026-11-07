// SPDX-License-Identifier: Apache-2.0
#include "thread_inventory_controller.hpp"

#include <QColor>
#include <optional>

#include "preview_service.hpp"
#include "thread_plan.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
std::optional<Rgb> colorOf(const QString& text) {
    const QColor color(text);
    if (!color.isValid() || color.alpha() != 255)
        return std::nullopt;
    return Rgb{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
               static_cast<std::uint8_t>(color.blue())};
}
QColor colorOf(Rgb color) {
    return QColor(color[0], color[1], color[2]);
}
QVariantMap choice(const ThreadChoice& thread) {
    return {{QStringLiteral("chartId"), QString::fromStdString(thread.chartId)},
            {QStringLiteral("code"), QString::fromStdString(thread.code)},
            {QStringLiteral("name"), QString::fromStdString(thread.name)},
            {QStringLiteral("brand"), QString::fromStdString(thread.brand)},
            {QStringLiteral("color"), colorOf(thread.rgb)},
            {QStringLiteral("personal"), thread.chartId == thread_palette::kPersonalChartId},
            {QStringLiteral("distance"), thread.distance}};
}
} // namespace

ThreadInventoryController::ThreadInventoryController(StudioController& studio,
                                                     ThreadController& threads,
                                                     std::filesystem::path configDir,
                                                     QObject* parent)
    : QObject(parent), studio_(studio), threads_(threads),
      file_(configDir.empty() ? std::filesystem::path{} : configDir / "thread-stock.json"),
      seenRevision_(studio.session().revision()) {
    connect(&threads_, &ThreadController::chartsChanged, this, [this] {
        emit chartsChanged();
        emit stockChanged();
    });
    token_ = studio_.session().subscribe([this](ChangeKind) {
        if (seenRevision_ != studio_.session().revision()) {
            seenRevision_ = studio_.session().revision();
            clearPreview();
        }
        emit variantsChanged();
    });
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    reload();
}

ThreadInventoryController::~ThreadInventoryController() {
    pump_.stop();
    studio_.session().unsubscribe(token_);
    tasks_.cancelAll();
}

bool ThreadInventoryController::say(bool ok, const QString& message) {
    ok_ = ok;
    message_ = message;
    emit messageChanged();
    return ok;
}

bool ThreadInventoryController::reload() {
    if (file_.empty())
        return say(false, tr("Dossier de configuration indisponible"));
    auto result = loadThreadStock(file_);
    loaded_ = result.has_value();
    if (!result) {
        emit stockChanged();
        return say(false, QString::fromStdString(result.error().message));
    }
    stock_ = std::move(*result);
    emit stockChanged();
    return say(true, {});
}

QVariantList ThreadInventoryController::stock() const {
    QVariantList result;
    for (const auto& thread : stockChoices(stock_))
        result.push_back(choice(thread));
    return result;
}

QVariantList ThreadInventoryController::colorways() const {
    return threads_.colorways();
}

bool ThreadInventoryController::commit(thread_palette::ThreadStock stock) {
    if (!loaded_)
        return say(false, tr("Stock non chargé : corrigez le fichier puis rechargez"));
    const auto saved = saveThreadStock(file_, stock);
    if (!saved)
        return say(false, QString::fromStdString(saved.error().message));
    stock_ = std::move(stock);
    emit stockChanged();
    return say(true, tr("Stock enregistré"));
}

bool ThreadInventoryController::owns(const QString& chart, const QString& code) const {
    return stock_.owns({chart.toStdString(), code.toStdString()});
}

bool ThreadInventoryController::setOwned(const QString& chart, const QString& code, bool owned) {
    auto copy = stock_;
    const thread_palette::ThreadKey key{chart.toStdString(), code.toStdString()};
    const bool changed = owned ? copy.own(key) : copy.disown(key);
    if (!changed)
        return say(false, tr("Référence vide, personnelle ou déjà dans cet état"));
    return commit(std::move(copy));
}

bool ThreadInventoryController::addPersonal(const QString& code, const QString& name,
                                            const QString& color, const QString& brand) {
    const auto rgb = colorOf(color);
    if (!rgb)
        return say(false, tr("Choisissez une couleur RGB opaque valide"));
    auto copy = stock_;
    thread_palette::Thread thread;
    thread.key.code = code.trimmed().toStdString();
    thread.name = name.trimmed().toStdString();
    thread.brand = brand.trimmed().toStdString();
    thread.rgb = *rgb;
    const auto result = copy.add_personal(std::move(thread));
    if (!result)
        return say(false, QString::fromStdString(result.error().message));
    return commit(std::move(copy));
}

bool ThreadInventoryController::removeStock(const QString& chart, const QString& code) {
    return setOwned(chart, code, false);
}

QVariantList ThreadInventoryController::search(const QString& chart, const QString& query) const {
    QVariantList result;
    for (const auto& thread : desktop_v2::searchThreads(chart.toStdString(), query.toStdString()))
        result.push_back(choice(thread));
    return result;
}

QVariantMap ThreadInventoryController::compareColors(const QString& a, const QString& b,
                                                     double ratioB) {
    const auto rgbA = colorOf(a);
    const auto rgbB = colorOf(b);
    if (!rgbA || !rgbB) {
        say(false, tr("Deux couleurs RGB opaques valides sont nécessaires"));
        return {};
    }
    const auto result = compareThreadColors(*rgbA, *rgbB, ratioB);
    if (!result) {
        say(false, QString::fromStdString(result.error().message));
        return {};
    }
    say(true, tr("Estimation en lumière linéaire ; vérifier sur tissu"));
    return {{QStringLiteral("mixed"), colorOf(result->mixed)},
            {QStringLiteral("difference"), result->difference}};
}

QVariantMap ThreadInventoryController::suggest(const QString& target, const QString& chart) {
    const auto rgb = colorOf(target);
    if (!rgb) {
        say(false, tr("Couleur cible invalide"));
        return {};
    }
    const auto result = suggestBlend(*rgb, chart.toStdString());
    if (!result) {
        say(false, tr("Au moins deux fils sont nécessaires dans le nuancier"));
        return {};
    }
    say(true, QString::fromStdString(result->summary()));
    return {{QStringLiteral("a"), choice(result->a)},
            {QStringLiteral("b"), choice(result->b)},
            {QStringLiteral("mixed"), colorOf(result->mixed)},
            {QStringLiteral("ratioB"), result->ratioB},
            {QStringLiteral("mixedError"), result->mixedError},
            {QStringLiteral("singleError"), result->singleError}};
}

bool ThreadInventoryController::importChart(const QUrl& file) {
    const bool ok = threads_.importChart(file);
    return say(ok, threads_.message());
}

bool ThreadInventoryController::removeChart(const QString& chart) {
    const bool ok = threads_.removeChart(chart);
    return say(ok, threads_.message());
}

void ThreadInventoryController::clearPreview() {
    ++request_;
    tasks_.cancelAll();
    busy_ = false;
    preview_ = QUrl{};
    previewIndex_ = -1;
    emit previewChanged();
}

void ThreadInventoryController::previewColorway(int index) {
    clearPreview();
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto request = request_;
    busy_ = true;
    emit previewChanged();
    tasks_.submit<PreviewImage>(
        "studio.colorway-preview",
        [snapshot, revision, index](const TaskContext& task) -> Result<PreviewImage> {
            auto project = colorwayPreviewProject(*snapshot, index);
            if (!project)
                return std::unexpected(project.error());
            return render_preview(std::make_shared<const document::Project>(std::move(*project)),
                                  revision, 3.0, task);
        },
        [this, request, revision, index](Result<PreviewImage>&& result) {
            if (request != request_ || revision != studio_.session().revision())
                return;
            busy_ = false;
            if (!result) {
                say(false, QString::fromStdString(result.error().message));
            } else {
                const QByteArray png(reinterpret_cast<const char*>(result->png.data()),
                                     static_cast<qsizetype>(result->png.size()));
                preview_ = QUrl(QStringLiteral("data:image/png;base64,") +
                                QString::fromLatin1(png.toBase64()));
                previewIndex_ = index;
                say(true, tr("Aperçu seul : le document n'a pas changé"));
            }
            emit previewChanged();
        });
}

bool ThreadInventoryController::activatePreview() {
    if (busy_ || preview_.isEmpty() || previewIndex_ < 0)
        return say(false, tr("Prévisualisez d'abord un coloris du document courant"));
    const auto result = desktop_v2::activateColorway(studio_.session(), previewIndex_);
    return say(result.ok, QString::fromStdString(result.message));
}

} // namespace openstitch::desktop_v2::studio
