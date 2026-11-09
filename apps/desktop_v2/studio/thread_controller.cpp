// SPDX-License-Identifier: Apache-2.0
#include "thread_controller.hpp"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <unordered_set>

#include "file_io.hpp"
#include "openstitch/commands/colorway_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/thread_palette/color_distance.hpp"
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
        stockPath_ = configDir / "my_threads.json";
        settingsPath_ = configDir / "thread_preferences.ini";
        for (const auto& problem : loadUserCharts(chartDir_)) {
            message_ = q(problem); // a refused file is reported, never silently dropped
            ok_ = false;
        }
        QSettings settings(QString::fromStdWString(settingsPath_.wstring()), QSettings::IniFormat);
        limitToStock_ = settings.value(QStringLiteral("limitToStock"), false).toBool();
        const QString filename = QString::fromStdWString(stockPath_.wstring());
        if (QFile::exists(filename)) {
            QFile file(filename);
            if (!file.open(QIODevice::ReadOnly)) {
                stockWritable_ = false;
                ok_ = false;
                message_ =
                    QStringLiteral("Impossible de lire Mes fils : %1").arg(file.errorString());
            } else {
                const auto bytes = file.readAll();
                const auto parsed = thread_palette::stock_from_json(
                    std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
                if (parsed) {
                    stock_ = std::move(*parsed);
                } else {
                    stockWritable_ = false; // never silently overwrite a damaged inventory
                    ok_ = false;
                    message_ = QStringLiteral("Mes fils : fichier invalide conservé (%1)")
                                   .arg(q(parsed.error().message));
                }
            }
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

bool ThreadController::persistStock(const thread_palette::ThreadStock& stock) {
    if (!stockWritable_) {
        const ServiceResult refusal{false,
                                    "Stock illisible : fichier conservé, corrigez-le avant de modifier Mes fils"};
        return report(refusal, {});
    }
    if (stockPath_.empty()) {
        return true; // stateless tests and embedded preview
    }
    const QString path = QString::fromStdWString(stockPath_.wstring());
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return report({false, "Impossible de créer le dossier de Mes fils"}, {});
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return report({false, "Impossible d'ouvrir le stock de fils pour écriture"}, {});
    }
    const auto json = thread_palette::stock_to_json(stock);
    if (file.write(json.data(), static_cast<qint64>(json.size())) !=
            static_cast<qint64>(json.size()) ||
        !file.commit()) {
        return report({false, "Écriture atomique du stock de fils impossible"}, {});
    }
    return true;
}

bool ThreadController::persistStockSetting(bool limit) {
    if (settingsPath_.empty()) {
        return true;
    }
    const QString path = QString::fromStdWString(settingsPath_.wstring());
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("limitToStock"), limit);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

void ThreadController::setLimitToStock(bool value) {
    if (limitToStock_ == value) {
        return;
    }
    if (!persistStockSetting(value)) {
        report({false, "Impossible d'enregistrer le filtre Mes fils"}, {});
        return;
    }
    limitToStock_ = value;
    emit stockChanged();
    report({true, {}}, value ? QStringLiteral("Recherche limitée à Mes fils")
                             : QStringLiteral("Recherche dans tout le nuancier"));
}

QVariantList ThreadController::ownedThreads() const {
    QVariantList list;
    for (const auto& choice : stockChoices()) {
        list.push_back(choiceMap(choice));
    }
    return list;
}

int ThreadController::stockCount() const {
    return static_cast<int>(stock_.size());
}

std::vector<ThreadChoice> ThreadController::stockChoices() const {
    std::vector<ThreadChoice> list;
    for (const auto& key : stock_.owned_keys()) {
        const auto matches = desktop_v2::searchThreads(key.chart_id, key.code);
        const auto exact = std::find_if(matches.begin(), matches.end(), [&](const auto& t) {
            return t.chartId == key.chart_id && t.code == key.code;
        });
        if (exact != matches.end()) {
            list.push_back(*exact);
        }
    }
    for (const auto& thread : stock_.personal_threads()) {
        ThreadChoice choice;
        choice.chartId = thread.key.chart_id;
        choice.code = thread.key.code;
        choice.name = thread.name;
        choice.brand = thread.brand;
        choice.rgb = thread.rgb;
        list.push_back(std::move(choice));
    }
    return list;
}

std::vector<ThreadChoice>
ThreadController::nearestChoices(std::array<std::uint8_t, 3> rgb, const QString& chartId,
                                 int topN) const {
    if (topN < 1) {
        return {};
    }
    if (!limitToStock_) {
        return nearestThreads(rgb, chartId.toStdString(), static_cast<std::size_t>(topN));
    }
    auto choices = stockChoices();
    const auto target = thread_palette::to_cielab(rgb);
    for (auto& choice : choices) {
        const auto actual = thread_palette::to_cielab(choice.rgb);
        choice.distance = thread_palette::ciede2000(target, actual);
    }
    const auto closer = [](const ThreadChoice& a, const ThreadChoice& b) {
        return a.distance < b.distance;
    };
    std::stable_sort(choices.begin(), choices.end(), closer);
    if (choices.size() > static_cast<std::size_t>(topN)) {
        choices.resize(static_cast<std::size_t>(topN));
    }
    return choices;
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
         nearestChoices(all[static_cast<std::size_t>(entry)].rgb, chartId, topN)) {
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
                const auto matches = nearestChoices(object.rgb, chartId, std::max(1, topN));
                for (const auto& choice : matches) {
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
    // One command for ALL assignments (V1 parity); the previous implementation
    // pushed one undo entry for each colour.
    const auto& project = studio_.session().project();
    std::unordered_set<std::uint64_t> selected;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            selected.insert(target.id);
        } else if (target.kind == TargetKind::VectorObject) {
            for (const auto& object : project.embroidery_objects) {
                if (object.source_vector.value == target.id) {
                    selected.insert(object.id.value);
                }
            }
        }
    }
    if (limitToStock_ && stockChoices().empty()) {
        report({false, "Mes fils est vide ou ses références ne sont plus disponibles"}, {});
        return 0;
    }
    std::vector<commands::ObjectThreadAssignment> assignments;
    for (const auto& object : project.embroidery_objects) {
        if (object.thread || (!selected.empty() && !selected.contains(object.id.value))) {
            continue;
        }
        const auto nearest = nearestChoices(object.rgb, chartId, 1);
        if (nearest.empty()) {
            report({false, "Nuancier inconnu ou vide"}, {});
            return 0;
        }
        const auto& choice = nearest.front();
        const thread_palette::ThreadKey key{choice.chartId, choice.code};
        assignments.push_back({object.id, key, choice.rgb});
    }
    if (assignments.empty()) {
        report({false, "Aucune couleur libre à associer"}, {});
        return 0;
    }
    const int count = static_cast<int>(assignments.size());
    studio_.session().execute(std::make_unique<commands::SetObjectThreadCommand>(
        std::move(assignments), "Couleurs libres vers fils proches"));
    report({true, {}}, QStringLiteral("%1 objet(s) associés aux fils les plus proches").arg(count));
    return count;
}

bool ThreadController::ownsThread(const QString& chartId, const QString& code) const {
    return stock_.owns({chartId.toStdString(), code.toStdString()});
}

bool ThreadController::setOwnedThread(const QString& chartId, const QString& code, bool owned) {
    const auto key = thread_palette::ThreadKey{chartId.toStdString(), code.toStdString()};
    if (key.chart_id.empty() || key.code.empty() ||
        key.chart_id == thread_palette::kPersonalChartId) {
        return report({false, "Choisissez une référence de nuancier valide"}, {});
    }
    if (owned) {
        const auto choices = desktop_v2::searchThreads(key.chart_id, key.code);
        if (std::none_of(choices.begin(), choices.end(), [&](const auto& t) {
                return t.chartId == key.chart_id && t.code == key.code;
            })) {
            return report({false, "Référence introuvable dans le nuancier"}, {});
        }
    }
    auto next = stock_;
    if (owned ? !next.own(key) : !next.disown(key)) {
        const char* reason = owned ? "Ce fil est déjà dans Mes fils"
                                   : "Ce fil n'est pas dans Mes fils";
        return report({false, reason}, {});
    }
    if (!persistStock(next)) {
        return false;
    }
    stock_ = std::move(next);
    emit stockChanged();
    return report({true, {}}, owned ? QStringLiteral("Fil ajouté à Mes fils")
                                    : QStringLiteral("Fil retiré de Mes fils"));
}

bool ThreadController::addPersonalThread(const QString& code, const QString& name,
                                         const QString& hexColor) {
    const QColor color(hexColor);
    if (!color.isValid() || code.trimmed().isEmpty()) {
        return report({false, "Référence ou couleur personnelle invalide"}, {});
    }
    thread_palette::Thread thread;
    thread.key = {thread_palette::kPersonalChartId, code.trimmed().toStdString()};
    thread.name = name.trimmed().toStdString();
    using Byte = std::uint8_t;
    thread.rgb = {Byte(color.red()), Byte(color.green()), Byte(color.blue())};
    auto next = stock_;
    auto added = next.add_personal(thread);
    if (!added) {
        return report({false, added.error().message}, {});
    }
    if (!persistStock(next)) {
        return false;
    }
    stock_ = std::move(next);
    emit stockChanged();
    return report({true, {}}, QStringLiteral("Fil personnel %1 enregistré").arg(code));
}

bool ThreadController::removeOwnedThread(const QString& chartId, const QString& code) {
    auto next = stock_;
    if (!next.disown({chartId.toStdString(), code.toStdString()})) {
        return report({false, "Ce fil n'est pas dans Mes fils"}, {});
    }
    if (!persistStock(next)) {
        return false;
    }
    stock_ = std::move(next);
    emit stockChanged();
    return report({true, {}}, QStringLiteral("Fil retiré du stock"));
}

bool ThreadController::assignOwnedThread(int entry, const QString& chartId,
                                         const QString& code) {
    if (entry < 0) {
        return report({false, "Sélectionnez un fil du projet"}, {});
    }
    const auto all = threadEntries(studio_.session().project());
    if (static_cast<std::size_t>(entry) >= all.size()) {
        return report({false, "Fil du projet inconnu"}, {});
    }
    const auto matches = stockChoices();
    const auto it = std::find_if(matches.begin(), matches.end(), [&](const auto& t) {
        return t.chartId == chartId.toStdString() && t.code == code.toStdString();
    });
    if (it == matches.end()) {
        return report({false, "Fil absent de Mes fils"}, {});
    }
    std::vector<ObjectId> ids;
    for (const auto id : all[static_cast<std::size_t>(entry)].objects) {
        ids.push_back(ObjectId{id});
    }
    const thread_palette::ThreadKey key{it->chartId, it->code};
    studio_.session().execute(std::make_unique<commands::SetObjectThreadCommand>(
        ids, key, it->rgb, "Assigner un fil possédé"));
    return report({true, {}}, QStringLiteral("Fil %1 assigné").arg(code));
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

std::optional<ThreadController::ColorFilm> ThreadController::currentFilm() const {
    const auto& scene = studio_.scene();
    if (!scene || !scene->effectiveSequence) {
        return std::nullopt;
    }
    return stitch_analysis::color_film(studio_.session().project(), *scene->effectiveSequence);
}

QVariantList ThreadController::colorFilm() const {
    QVariantList list;
    const auto film = currentFilm();
    if (!film) {
        return list;
    }
    for (std::size_t i = 0; i < film->size(); ++i) {
        const auto& block = (*film)[i];
        QString label;
        if (block.identity.key) {
            label = q(block.identity.key->chart_id + " " + block.identity.key->code);
        } else {
            const auto& rgb = block.identity.rgb;
            label = QColor(rgb[0], rgb[1], rgb[2]).name().toUpper();
        }
        const auto& rgb = block.identity.rgb;
        QVariantMap item;
        item.insert(QStringLiteral("index"), static_cast<int>(i));
        item.insert(QStringLiteral("label"), label);
        item.insert(QStringLiteral("color"), QColor(rgb[0], rgb[1], rgb[2]));
        item.insert(QStringLiteral("objectCount"), static_cast<int>(block.objects.size()));
        item.insert(QStringLiteral("stitches"), static_cast<int>(block.stitch_count));
        item.insert(QStringLiteral("lengthMm"), block.length_mm);
        item.insert(QStringLiteral("locked"), block.locked);
        list.push_back(std::move(item));
    }
    return list;
}

int ThreadController::filmChanges() const {
    const auto film = currentFilm();
    return film ? static_cast<int>(stitch_analysis::color_change_count(*film)) : 0;
}

bool ThreadController::selectFilmBlock(int index) {
    const auto film = currentFilm();
    if (!film || index < 0 || static_cast<std::size_t>(index) >= film->size()) {
        return report({false, "Passage de fil inconnu"}, {});
    }
    std::vector<Target> targets;
    for (const auto id : (*film)[static_cast<std::size_t>(index)].objects) {
        targets.push_back({TargetKind::EmbroideryObject, id.value});
    }
    if (targets.empty()) {
        return report({false, "Aucun objet dans ce passage"}, {});
    }
    studio_.session().selection().replace(std::move(targets));
    return report({true, {}}, QStringLiteral("Objets de ce passage sélectionnés"));
}

bool ThreadController::commitSewingOrder(const std::vector<ObjectId>& ids,
                                         const QString& label) {
    const auto& project = studio_.session().project();
    if (ids.size() != project.embroidery_objects.size()) {
        return report({false, "Ordre incomplet : déplacement refusé"}, {});
    }
    std::unordered_set<std::uint64_t> seen;
    std::size_t previousRank = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto* object = project.findEmbroidery(ids[i]);
        if (!object || !seen.insert(ids[i].value).second) {
            return report({false, "Ordre invalide : identifiants manquants ou répétés"}, {});
        }
        const auto rank = project.calqueRankOf(*object);
        if (rank < previousRank) {
            return report({false, "Déplacement entre calques interdit"}, {});
        }
        previousRank = rank;
        if (project.embroidery_objects[i].locked && project.embroidery_objects[i].id != ids[i]) {
            return report({false, "Un objet a son ordre de couture figé"}, {});
        }
    }
    bool different = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        different = different || ids[i] != project.embroidery_objects[i].id;
    }
    if (!different) {
        return report({false, "L'ordre de couture est déjà identique"}, {});
    }
    studio_.session().execute(std::make_unique<commands::ReorderEmbroideryCommand>(ids));
    return report({true, {}}, label);
}

bool ThreadController::moveFilmBlock(int from, int to) {
    const auto film = currentFilm();
    if (!film || from < 0 || to < 0 || static_cast<std::size_t>(from) >= film->size() ||
        static_cast<std::size_t>(to) >= film->size()) {
        return report({false, "Passage de fil ou destination invalide"}, {});
    }
    const auto& project = studio_.session().project();
    const auto source = static_cast<std::size_t>(from);
    const auto dest = static_cast<std::size_t>(to);
    const auto order = stitch_analysis::reorder_film_blocks(project, *film, source, dest);
    return order && commitSewingOrder(*order, QStringLiteral("Passage de fil déplacé"));
}

bool ThreadController::groupFilmByColor() {
    const auto film = currentFilm();
    if (!film || film->size() < 2) {
        return report({false, "Pas assez de passages de fil à grouper"}, {});
    }
    return commitSewingOrder(
        stitch_analysis::merge_same_thread_blocks(studio_.session().project(), *film),
        QStringLiteral("Passages regroupés par couleur (empilement modifié)"));
}

QVariantMap ThreadController::previewReduction(int maxThreads) {
    reduction_.reset();
    if (maxThreads < 1 || maxThreads > 64) {
        report({false, "Nombre de fils demandé hors limites (1 à 64)"}, {});
        return {};
    }
    const auto& scene = studio_.scene();
    if (!scene || !scene->effectiveSequence || studio_.busy()) {
        report({false, "Générez d'abord des points et attendez la fin du rendu"}, {});
        return {};
    }
    const auto& project = studio_.session().project();
    const auto usage = stitch_analysis::thread_usage(project, *scene->effectiveSequence);
    auto plan = stitch_analysis::plan_reduce_threads(usage, static_cast<std::size_t>(maxThreads));
    if (plan.empty()) {
        report({false, "Le motif utilise déjà ce nombre de fils ou moins"}, {});
        return {};
    }
    QVariantMap result;
    result.insert(QStringLiteral("before"), static_cast<int>(plan.before));
    result.insert(QStringLiteral("after"), static_cast<int>(plan.after));
    result.insert(QStringLiteral("affected"), static_cast<int>(plan.reassignments.size()));
    reduction_ = std::move(plan);
    reductionRevision_ = studio_.session().revision();
    report({true, {}}, QStringLiteral("Aperçu de réduction prêt : aucune modification appliquée"));
    return result;
}

bool ThreadController::applyReduction() {
    if (!reduction_ || reductionRevision_ != studio_.session().revision()) {
        reduction_.reset();
        return report({false, "Aperçu périmé : recalculez la réduction"}, {});
    }
    std::vector<commands::ObjectThreadAssignment> assignments;
    for (const auto& change : reduction_->reassignments) {
        assignments.push_back({change.object, change.thread, change.rgb});
    }
    reduction_.reset();
    if (assignments.empty()) {
        return report({false, "Aucun fil à réduire"}, {});
    }
    studio_.session().execute(std::make_unique<commands::SetObjectThreadCommand>(
        std::move(assignments), "Réduire la palette de fils"));
    return report({true, {}}, QStringLiteral("Palette réduite en une étape annulable"));
}

QVariantMap ThreadController::fabric() const {
    const auto& f = studio_.session().project().fabric;
    return {{QStringLiteral("color"), QColor(f.color[0], f.color[1], f.color[2])},
            {QStringLiteral("name"), q(f.name)},
            {QStringLiteral("material"), q(f.material)},
            {QStringLiteral("stabilizer"), q(f.stabilizer)},
            {QStringLiteral("grainAngleDeg"), f.grain_angle.radians * 180.0 / std::numbers::pi},
            {QStringLiteral("weftSoftness"), f.weft_softness}};
}

bool ThreadController::setFabric(const QString& hexColor, const QString& name,
                                 const QString& material, const QString& stabilizer,
                                 double grainAngleDeg, double weftSoftness) {
    const QColor color(hexColor);
    const bool invalidAngle = !std::isfinite(grainAngleDeg) || std::abs(grainAngleDeg) > 360.0;
    const bool invalidSoftness = !std::isfinite(weftSoftness) ||
                                 weftSoftness < 0.25 || weftSoftness > 4.0;
    if (!color.isValid() || invalidAngle || invalidSoftness) {
        return report({false, "Tissu invalide : couleur, angle ou souplesse hors limites"}, {});
    }
    document::FabricSettings next;
    using Byte = std::uint8_t;
    next.color = {Byte(color.red()), Byte(color.green()), Byte(color.blue())};
    next.name = name.trimmed().toStdString();
    next.material = material.trimmed().toStdString();
    next.stabilizer = stabilizer.trimmed().toStdString();
    next.grain_angle = Angle{grainAngleDeg * std::numbers::pi / 180.0};
    next.weft_softness = weftSoftness;
    if (next == studio_.session().project().fabric) {
        return report({false, "Réglages du tissu inchangés"}, {});
    }
    studio_.session().execute(std::make_unique<commands::SetFabricCommand>(next));
    return report({true, {}}, QStringLiteral("Tissu du projet mis à jour"));
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
