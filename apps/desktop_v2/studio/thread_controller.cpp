// SPDX-License-Identifier: Apache-2.0
#include "thread_controller.hpp"

#include <QColor>
#include <set>
#include <unordered_set>

#include "file_io.hpp"
#include "thread_palette_service.hpp"
#include "thread_plan.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}

QVariantMap choiceMap(const ThreadChoice& c) {
    return {{QStringLiteral("chartId"), q(c.chartId)},
            {QStringLiteral("code"), q(c.code)},
            {QStringLiteral("name"), q(c.name)},
            {QStringLiteral("brand"), q(c.brand)},
            {QStringLiteral("color"), QColor(c.rgb[0], c.rgb[1], c.rgb[2])},
            {QStringLiteral("distance"), c.distance}};
}

std::filesystem::path pathOf(const QUrl& url) {
    return std::filesystem::path(url.toLocalFile().toStdWString());
}
} // namespace

ThreadController::ThreadController(StudioController& studio, std::filesystem::path configDir,
                                   QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &ThreadController::changed);
    if (!configDir.empty()) {
        chartDir_ = configDir / "charts";
        for (const auto& problem : loadUserCharts(chartDir_)) {
            message_ = q(problem); // a refused file is reported, never silently dropped
            ok_ = false;
        }
    }
}

ThreadController::~ThreadController() = default;

bool ThreadController::report(const ServiceResult& result, const QString& success) {
    ok_ = result.ok;
    message_ = result.ok ? success : q(result.message);
    emit messageChanged();
    return result.ok;
}

QVariantList ThreadController::entries() const {
    QVariantList list;
    const auto& project = studio_.session().project();
    std::unordered_set<std::uint64_t> visible;
    for (const auto& object : project.embroidery_objects) {
        if (object.visible) {
            visible.insert(object.id.value);
        }
    }
    std::unordered_set<std::uint64_t> selected;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            selected.insert(target.id);
        }
    }
    int index = 0;
    for (const auto& entry : threadEntries(project)) {
        bool anyVisible = false;
        bool anySelected = false;
        for (const auto id : entry.objects) {
            anySelected = anySelected || selected.contains(id);
            anyVisible = anyVisible || visible.contains(id);
        }
        list.push_back(
            QVariantMap{{QStringLiteral("index"), index++},
                        {QStringLiteral("id"), QString::number(entry.objects.front())},
                        {QStringLiteral("label"), q(entry.label)},
                        {QStringLiteral("color"), QColor(entry.rgb[0], entry.rgb[1], entry.rgb[2])},
                        {QStringLiteral("objectCount"), static_cast<int>(entry.objects.size())},
                        {QStringLiteral("visible"), anyVisible},
                        {QStringLiteral("selected"), anySelected}});
    }
    return list;
}

int ThreadController::threadCount() const {
    return static_cast<int>(threadEntries(studio_.session().project()).size());
}

QVariantList ThreadController::sewingOrder() const {
    QVariantList list;
    int position = 0;
    for (const auto& row : orderRows(studio_.session().project())) {
        list.push_back(
            QVariantMap{{QStringLiteral("id"), QString::number(row.id)},
                        {QStringLiteral("name"), q(row.name)},
                        {QStringLiteral("color"), QColor(row.rgb[0], row.rgb[1], row.rgb[2])},
                        {QStringLiteral("position"), position++}});
    }
    return list;
}

QVariantList ThreadController::colorways() const {
    QVariantList list;
    for (const auto& row : colorwayRows(studio_.session().project())) {
        list.push_back(QVariantMap{{QStringLiteral("name"), q(row.name)},
                                   {QStringLiteral("active"), row.active}});
    }
    return list;
}

QVariantList ThreadController::charts() const {
    QVariantList list;
    for (const auto& chart : chartInfos()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), q(chart.id)},
                                   {QStringLiteral("name"), q(chart.name)},
                                   {QStringLiteral("source"), q(chart.source)},
                                   {QStringLiteral("count"), static_cast<int>(chart.count)},
                                   {QStringLiteral("user"), chart.user}});
    }
    return list;
}

bool ThreadController::moveThread(int from, int to) {
    if (from < 0 || to < 0) {
        return report({false, "Choisissez deux fils différents."}, {});
    }
    return report(desktop_v2::moveThread(studio_.session(), static_cast<std::size_t>(from),
                                         static_cast<std::size_t>(to)),
                  tr("Ordre des fils modifié"));
}

bool ThreadController::mergeThreads(int from, int into) {
    if (from < 0 || into < 0) {
        return report({false, "Choisissez deux fils à fusionner"}, {});
    }
    return report(desktop_v2::mergeThreads(studio_.session(), static_cast<std::size_t>(from),
                                           static_cast<std::size_t>(into)),
                  QStringLiteral("Fils fusionnés"));
}

bool ThreadController::recolorThread(int entry, const QString& hex) {
    const QColor colour(hex);
    if (entry < 0 || !colour.isValid()) {
        return report({false, "Fil ou couleur invalide"}, {});
    }
    return report(desktop_v2::recolorThread(studio_.session(), static_cast<std::size_t>(entry),
                                            {static_cast<std::uint8_t>(colour.red()),
                                             static_cast<std::uint8_t>(colour.green()),
                                             static_cast<std::uint8_t>(colour.blue())}),
                  QStringLiteral("Fil recoloré en %1").arg(colour.name()));
}

bool ThreadController::setThreadVisible(int entry, bool visible) {
    if (entry < 0) {
        return report({false, "Choisissez un fil"}, {});
    }
    return report(
        desktop_v2::setThreadVisible(studio_.session(), static_cast<std::size_t>(entry), visible),
        visible ? QStringLiteral("Fil affiché") : QStringLiteral("Fil masqué"));
}

bool ThreadController::selectThreadObjects(int entry) {
    const auto all = threadEntries(studio_.session().project());
    if (entry < 0 || static_cast<std::size_t>(entry) >= all.size()) {
        return report({false, "Fil inconnu"}, {});
    }
    std::vector<Target> targets;
    for (const auto id : all[static_cast<std::size_t>(entry)].objects) {
        targets.push_back({TargetKind::EmbroideryObject, id});
    }
    studio_.session().selection().replace(std::move(targets));
    return report({true, {}}, QStringLiteral("%1 objet(s) de ce fil sélectionné(s)")
                                  .arg(all[static_cast<std::size_t>(entry)].objects.size()));
}

bool ThreadController::moveInOrder(const QString& objectId, int toPosition) {
    bool parsed = false;
    const auto id = objectId.toULongLong(&parsed);
    if (!parsed || toPosition < 0) {
        return report({false, "Objet ou position invalide"}, {});
    }
    return report(
        desktop_v2::moveInOrder(studio_.session(), id, static_cast<std::size_t>(toPosition)),
        QStringLiteral("Ordre de couture modifié"));
}

QVariantList ThreadController::searchThreads(const QString& chartId, const QString& query) const {
    QVariantList list;
    for (const auto& choice :
         desktop_v2::searchThreads(chartId.toStdString(), query.toStdString())) {
        list.push_back(choiceMap(choice));
    }
    return list;
}

QVariantList ThreadController::nearestToEntry(int entry, const QString& chartId, int topN) const {
    QVariantList list;
    const auto all = threadEntries(studio_.session().project());
    if (entry < 0 || static_cast<std::size_t>(entry) >= all.size() || topN < 1) {
        return list;
    }
    for (const auto& choice :
         nearestThreads(all[static_cast<std::size_t>(entry)].rgb, chartId.toStdString(),
                        static_cast<std::size_t>(topN))) {
        list.push_back(choiceMap(choice));
    }
    return list;
}

QVariantList ThreadController::nearestToSelection(const QString& chartId, int topN) const {
    const auto& project = studio_.session().project();
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        for (const auto& object : project.embroidery_objects) {
            if (object.id.value == target.id) {
                QVariantList list;
                for (const auto& choice :
                     nearestThreads(object.rgb, chartId.toStdString(),
                                    static_cast<std::size_t>(std::max(1, topN)))) {
                    list.push_back(choiceMap(choice));
                }
                return list;
            }
        }
    }
    return {};
}

bool ThreadController::assignChartThread(int entry, const QString& chartId, const QString& code) {
    if (entry < 0) {
        return report({false, "Choisissez un fil du plan"}, {});
    }
    return report(desktop_v2::assignChartThread(studio_.session(), static_cast<std::size_t>(entry),
                                                chartId.toStdString(), code.toStdString()),
                  QStringLiteral("Fil %1 %2 assigné").arg(chartId, code));
}

int ThreadController::snapFreeColours(const QString& chartId) {
    int changed = 0;
    std::set<std::array<std::uint8_t, 3>> attempted;
    for (;;) {
        const auto all = threadEntries(studio_.session().project());
        std::optional<std::size_t> target;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].label.rfind('#', 0) == 0 && !attempted.contains(all[i].rgb)) {
                target = i;
                break;
            }
        }
        if (!target) {
            break;
        }
        attempted.insert(all[*target].rgb);
        const auto nearest = nearestThreads(all[*target].rgb, chartId.toStdString(), 1);
        if (nearest.empty()) {
            report({false, "Nuancier inconnu ou vide"}, {});
            return changed;
        }
        if (desktop_v2::assignChartThread(studio_.session(), *target, nearest.front().chartId,
                                          nearest.front().code)
                .ok) {
            ++changed;
        }
    }
    report({changed > 0, "Aucune couleur libre à associer"},
           QStringLiteral("%1 couleur(s) associée(s) au fil le plus proche").arg(changed));
    return changed;
}

bool ThreadController::importChart(const QUrl& file) {
    if (!file.isLocalFile()) {
        return report({false, "Choisissez un fichier .csv ou .json local"}, {});
    }
    if (chartDir_.empty()) {
        return report({false, "Dossier de configuration indisponible : import impossible"}, {});
    }
    const auto result = importThreadChart(pathOf(file), chartDir_);
    if (!result) {
        return report({false, result.error().message}, {});
    }
    emit chartsChanged();
    return report({true, {}}, QStringLiteral("Nuancier « %1 » importé (%2 fils)")
                                  .arg(q(result->name))
                                  .arg(result->count));
}

bool ThreadController::removeChart(const QString& chartId) {
    if (!removeUserChart(chartId.toStdString(), chartDir_)) {
        return report({false, "Seuls les nuanciers importés peuvent être retirés"}, {});
    }
    emit chartsChanged();
    return report({true, {}}, QStringLiteral("Nuancier retiré"));
}

bool ThreadController::exportThreadList(const QUrl& file) {
    if (!file.isLocalFile()) {
        return report({false, "Choisissez un fichier local"}, {});
    }
    std::size_t rows = 0;
    const auto result = exportThreadListCsv(studio_.session().project(), pathOf(file), &rows);
    return report(result, QStringLiteral("%1 fil(s) exporté(s)").arg(rows));
}

bool ThreadController::addColorway(const QString& name) {
    return report(desktop_v2::addColorway(studio_.session(), name.toStdString()),
                  QStringLiteral("Coloris « %1 » créé").arg(name));
}

bool ThreadController::activateColorway(int index) {
    return report(desktop_v2::activateColorway(studio_.session(), index),
                  QStringLiteral("Coloris activé"));
}

bool ThreadController::removeColorway(int index) {
    return report(desktop_v2::removeColorway(studio_.session(), index),
                  QStringLiteral("Coloris supprimé"));
}

bool ThreadController::renameColorway(int index, const QString& name) {
    return report(desktop_v2::renameColorway(studio_.session(), index, name.toStdString()),
                  QStringLiteral("Coloris renommé"));
}

} // namespace openstitch::desktop_v2::studio
