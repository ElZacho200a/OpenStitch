// SPDX-License-Identifier: Apache-2.0
#include "inspector_controller.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "document_services.hpp"
#include "inspector_model.hpp"
#include "object_labels.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/stitch_preset_commands.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

constexpr double kEps = 1e-9;

QString u8(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString typeLabelOf(std::string_view typeId) {
    const std::string settable = settableIdOf(typeId);
    for (const auto& t : settableStitchTypes()) {
        if (t.id == settable) {
            return u8(t.label);
        }
    }
    if (typeId == "satin") {
        return QStringLiteral("Satin (colonnes)");
    }
    return u8(typeId);
}

// Parameters of a freshly created object of this type: the "default" of the reset button.
std::optional<document::StitchParams> defaultParamsOf(std::string_view typeId) {
    using namespace document;
    if (typeId == "running")
        return StitchParams{RunningStitchParams{}};
    if (typeId == "tatami")
        return StitchParams{TatamiParams{}};
    if (typeId == "directional")
        return StitchParams{DirectionalFillParams{}};
    if (typeId == "satin")
        return StitchParams{SatinParams{}};
    if (typeId == "spiral")
        return StitchParams{SpiralFillParams{}};
    if (typeId == "cross_stitch")
        return StitchParams{CrossStitchParams{}};
    if (typeId == "applique")
        return StitchParams{AppliqueParams{}};
    if (typeId == "auto_satin")
        return StitchParams{AutoSatinParams{}};
    if (typeId == "blanket")
        return StitchParams{BlanketStitchParams{}};
    if (typeId == "contour_fill")
        return StitchParams{ContourFillParams{}};
    if (typeId == "motif_run")
        return StitchParams{MotifRunParams{}};
    if (typeId == "motif_fill")
        return StitchParams{MotifFillParams{}};
    if (typeId == "radial")
        return StitchParams{RadialFillParams{}};
    if (typeId == "crosshatch")
        return StitchParams{CrosshatchFillParams{}};
    return std::nullopt;
}

std::optional<double> defaultOf(std::string_view typeId, std::string_view key) {
    const auto params = defaultParamsOf(typeId);
    if (!params) {
        return std::nullopt;
    }
    document::EmbroideryObject probe;
    probe.params = *params;
    return readParam(probe, key);
}

const char* kindName(ParamKind kind) {
    switch (kind) {
    case ParamKind::Number:
        return "number";
    case ParamKind::Integer:
        return "integer";
    case ParamKind::Bool:
        return "bool";
    case ParamKind::Choice:
        return "choice";
    case ParamKind::Point:
        return "point";
    case ParamKind::OptionalNumber:
        return "optional";
    case ParamKind::List:
        return "list";
    }
    return "number";
}

QString sectionOf(const ParamSpec& spec) {
    return u8(paramGroup(spec));
}

const char* levelName(ParamLevel level) {
    switch (level) {
    case ParamLevel::Essential:
        return "essential";
    case ParamLevel::Advanced:
        return "advanced";
    case ParamLevel::Expert:
        return "expert";
    }
    return "essential";
}

std::optional<ParamSpec> specOf(std::string_view typeId, std::string_view key) {
    for (const auto& spec : allParamSpecs(typeId)) {
        if (spec.key == key) {
            return spec;
        }
    }
    return std::nullopt;
}

QString formatValue(const ParamSpec& spec, double value) {
    switch (spec.kind) {
    case ParamKind::Bool:
        return value >= 0.5 ? QStringLiteral("Oui") : QStringLiteral("Non");
    case ParamKind::Integer:
        return QString::number(static_cast<long long>(std::llround(value)));
    case ParamKind::Choice: {
        const auto options = paramOptions(spec);
        const auto index = static_cast<std::size_t>(std::llround(value));
        return index < options.size() ? u8(options[index].second) : QString::number(value);
    }
    case ParamKind::List:
        return QString::number(static_cast<long long>(std::llround(value)));
    case ParamKind::Point:
        return value >= 0.5 ? QStringLiteral("Défini") : QStringLiteral("Automatique");
    case ParamKind::OptionalNumber:
    case ParamKind::Number:
        break;
    }
    return QString::number(value, 'g', 6);
}

const document::EmbroideryObject* findObject(const document::Project& project, std::uint64_t id) {
    const auto it = std::ranges::find_if(project.embroidery_objects,
                                         [&](const auto& o) { return o.id.value == id; });
    return it == project.embroidery_objects.end() ? nullptr : &*it;
}

QString refusalOf(const project_ops::BatchReport& report) {
    for (const auto& outcome : report.outcomes) {
        if (!outcome.ok && !outcome.message.empty()) {
            return u8(outcome.message);
        }
    }
    return report.error_message.empty() ? QStringLiteral("Modification refusée.")
                                        : u8(report.error_message);
}

} // namespace

InspectorController::InspectorController(StudioController& studio, QObject* parent)
    : QObject(parent), session_(studio.session()) {
    documentToken_ = session_.subscribe([this](ChangeKind) { bump(); });
    selectionToken_ = session_.selection().subscribe([this](const SelectionModel&) { bump(); });
}

void InspectorController::bump() {
    ++revision_;
    emit changed();
}

QVariantMap InspectorController::field(const QString& key) const {
    const auto s = selection();
    for (const auto& spec : specsOf(s)) {
        if (u8(spec.key) == key) {
            return fieldMap(s, spec);
        }
    }
    return {};
}

InspectorController::~InspectorController() {
    session_.unsubscribe(documentToken_);
    session_.selection().unsubscribe(selectionToken_);
}

InspectorController::Selection InspectorController::selection() const {
    const auto state = inspect(session_.project(), session_.selection().targets());
    Selection out;
    out.ids = state.ids;
    out.typeMixed = state.count > 0 && state.typeId.mixed;
    out.typeId = out.typeMixed || state.count == 0 ? std::string{} : state.typeId.value;
    for (const auto id : out.ids) {
        const auto* object = findObject(session_.project(), id);
        if (object == nullptr) {
            continue;
        }
        const std::string type(stitchTypeId(*object));
        auto it = std::ranges::find_if(out.groups, [&](const auto& g) { return g.first == type; });
        if (it == out.groups.end()) {
            out.groups.emplace_back(type, std::vector<std::uint64_t>{});
            it = out.groups.end() - 1;
        }
        it->second.push_back(id);
    }
    return out;
}

std::vector<ParamSpec> InspectorController::specsOf(const Selection& s) const {
    if (s.ids.empty()) {
        return {};
    }
    if (!s.typeMixed) {
        return allParamSpecs(s.typeId);
    }
    std::vector<ParamSpec> common;
    for (auto spec : allParamSpecs(s.groups.front().first)) {
        if (spec.kind == ParamKind::Point || spec.kind == ParamKind::List) {
            continue; // authored with their own editors, on one type at a time
        }
        bool shared = true;
        for (std::size_t i = 1; i < s.groups.size() && shared; ++i) {
            const auto other = specOf(s.groups[i].first, spec.key);
            shared = other && other->kind == spec.kind && other->options == spec.options;
            if (shared) {
                spec.min = std::max(spec.min, other->min);
                spec.max = std::min(spec.max, other->max);
                shared = spec.min <= spec.max;
            }
        }
        if (shared) {
            common.push_back(spec);
        }
    }
    return common;
}

int InspectorController::count() const {
    return static_cast<int>(selection().ids.size());
}

QString InspectorController::typeId() const {
    return u8(selection().typeId);
}

QString InspectorController::typeLabel() const {
    const auto s = selection();
    if (s.ids.empty()) {
        return {};
    }
    return s.typeMixed ? QStringLiteral("Types différents") : typeLabelOf(s.typeId);
}

bool InspectorController::typeMixed() const {
    return selection().typeMixed;
}

QVariantList InspectorController::types() const {
    const auto s = selection();
    const std::string current = settableIdOf(s.typeId);
    QVariantList out;
    for (const auto& t : settableStitchTypes()) {
        out.push_back(QVariantMap{{QStringLiteral("id"), u8(t.id)},
                                  {QStringLiteral("label"), u8(t.label)},
                                  {QStringLiteral("current"), !s.typeMixed && t.id == current}});
    }
    return out;
}

QVariantMap InspectorController::fieldMap(const Selection& s, const ParamSpec& spec) const {
    bool mixed = false;
    double value = 0.0;
    bool first = true;
    bool anySet = false;
    bool allDefault = true;
    double pointX = 0.0;
    double pointY = 0.0;
    for (const auto id : s.ids) {
        const auto* object = findObject(session_.project(), id);
        const auto v = object ? readParam(*object, spec.key) : std::nullopt;
        if (!v) {
            continue;
        }
        if (const auto own = defaultOf(stitchTypeId(*object), spec.key);
            own && std::abs(*v - *own) > kEps) {
            allDefault = false;
        }
        const bool set = paramIsSet(*object, spec.key);
        if (set && !anySet) {
            anySet = true;
            if (const auto pt = readPointMm(*object, spec.key)) {
                pointX = pt->first;
                pointY = pt->second;
            }
        }
        if (first) {
            value = *v;
            first = false;
        } else if (std::abs(*v - value) > kEps) {
            mixed = true;
        }
    }
    const auto def =
        defaultOf(s.groups.empty() ? s.typeId : s.groups.front().first, spec.key).value_or(value);
    const bool optionalKind = spec.kind == ParamKind::Point ||
                              spec.kind == ParamKind::OptionalNumber ||
                              spec.kind == ParamKind::List;
    const bool atDefault = optionalKind ? !anySet : allDefault;
    QVariantList choices;
    if (spec.kind == ParamKind::Choice) {
        int index = 0;
        for (const auto& option : paramOptions(spec)) {
            choices.push_back(QVariantMap{{QStringLiteral("value"), index++},
                                          {QStringLiteral("label"), u8(option.second)}});
        }
    }
    return {{QStringLiteral("key"), u8(spec.key)},
            {QStringLiteral("label"), u8(spec.label)},
            {QStringLiteral("kind"), QString::fromLatin1(kindName(spec.kind))},
            {QStringLiteral("min"), spec.min},
            {QStringLiteral("max"), spec.max},
            {QStringLiteral("step"), spec.step},
            {QStringLiteral("unit"), u8(spec.unit)},
            {QStringLiteral("level"), QString::fromLatin1(levelName(paramLevel(spec)))},
            {QStringLiteral("section"), sectionOf(spec)},
            {QStringLiteral("value"), value},
            {QStringLiteral("text"),
             mixed ? QStringLiteral("—")
             : spec.kind == ParamKind::Point
                 ? (anySet ? QStringLiteral("%1 ; %2 mm").arg(pointX).arg(pointY)
                           : QStringLiteral("Automatique"))
             : spec.kind == ParamKind::OptionalNumber && !anySet ? QStringLiteral("Automatique")
                                                                 : formatValue(spec, value)},
            {QStringLiteral("mixed"), mixed},
            {QStringLiteral("isDefault"), atDefault},
            {QStringLiteral("isSet"), anySet},
            {QStringLiteral("pointX"), pointX},
            {QStringLiteral("pointY"), pointY},
            {QStringLiteral("defaultValue"), def},
            {QStringLiteral("defaultText"), formatValue(spec, def)},
            {QStringLiteral("choices"), choices}};
}

std::vector<ParamSpec> InspectorController::visibleSpecs(const Selection& s) const {
    std::vector<ParamSpec> out;
    const std::string needle = query_.toStdString();
    for (const auto& spec : specsOf(s)) {
        // A search looks through every level; without search the level toggle decides.
        const auto level = paramLevel(spec);
        const bool byLevel = level == ParamLevel::Essential ||
                             (level == ParamLevel::Advanced && (showAdvanced_ || showExpert_)) ||
                             (level == ParamLevel::Expert && showExpert_);
        const bool visible = needle.empty() ? byLevel : paramMatches(spec, needle);
        if (visible) {
            out.push_back(spec);
        }
    }
    return out;
}

QVariantList InspectorController::fields() const {
    const auto s = selection();
    QVariantList out;
    for (const auto& spec : visibleSpecs(s)) {
        out.push_back(fieldMap(s, spec));
    }
    return out;
}

QVariantList InspectorController::sections() const {
    const auto s = selection();
    std::vector<QString> order;
    std::vector<QVariantList> groups;
    for (const auto& spec : visibleSpecs(s)) {
        const QString title = sectionOf(spec);
        auto it = std::ranges::find(order, title);
        if (it == order.end()) {
            order.push_back(title);
            groups.emplace_back();
            it = order.end() - 1;
        }
        groups[static_cast<std::size_t>(it - order.begin())].push_back(fieldMap(s, spec));
    }
    QVariantList out;
    for (std::size_t i = 0; i < order.size(); ++i) {
        out.push_back(QVariantMap{{QStringLiteral("title"), order[i]},
                                  {QStringLiteral("fields"), groups[i]}});
    }
    return out;
}

int InspectorController::totalFieldCount() const {
    const auto s = selection();
    return static_cast<int>(specsOf(s).size());
}

int InspectorController::hiddenAdvancedCount() const {
    const auto s = selection();
    if (s.ids.empty() || showAdvanced_ || showExpert_ || !query_.isEmpty()) {
        return 0;
    }
    const auto specs = specsOf(s);
    return static_cast<int>(std::ranges::count_if(
        specs, [](const auto& spec) { return paramLevel(spec) == ParamLevel::Advanced; }));
}

int InspectorController::hiddenExpertCount() const {
    const auto s = selection();
    if (s.ids.empty() || showExpert_ || !query_.isEmpty()) {
        return 0;
    }
    const auto specs = specsOf(s);
    return static_cast<int>(std::ranges::count_if(
        specs, [](const auto& spec) { return paramLevel(spec) == ParamLevel::Expert; }));
}

void InspectorController::setShowExpert(bool show) {
    if (show != showExpert_) {
        showExpert_ = show;
        bump();
    }
}

void InspectorController::setQuery(const QString& query) {
    if (query != query_) {
        query_ = query;
        bump();
    }
}

void InspectorController::setShowAdvanced(bool show) {
    if (show != showAdvanced_) {
        showAdvanced_ = show;
        bump();
    }
}

void InspectorController::setAllPresets(bool all) {
    if (all != allPresets_) {
        allPresets_ = all;
        bump();
    }
}

QVariantList InspectorController::presets() const {
    const auto s = selection();
    QVariantList out;
    int index = 0;
    for (const auto& preset : session_.project().stitch_presets) {
        document::EmbroideryObject probe;
        probe.params = preset.params;
        const std::string id(stitchTypeId(probe));
        const bool matches = !s.typeMixed && !s.ids.empty() && id == s.typeId;
        if (allPresets_ || matches) {
            out.push_back(QVariantMap{{QStringLiteral("index"), index},
                                      {QStringLiteral("name"), u8(preset.name)},
                                      {QStringLiteral("typeId"), u8(id)},
                                      {QStringLiteral("typeLabel"), typeLabelOf(id)},
                                      {QStringLiteral("matches"), matches}});
        }
        ++index;
    }
    return out;
}

QString InspectorController::finish(const QString& refusal, const QString& success) {
    message_ = refusal.isEmpty() ? success : refusal;
    ok_ = refusal.isEmpty();
    emit messageChanged();
    return refusal;
}

QString InspectorController::applyPairs(const Selection& s,
                                        const std::vector<std::pair<std::string, double>>& pairs,
                                        const QString& label, const std::string& mergeKey) {
    if (s.ids.empty()) {
        return QStringLiteral("Sélectionnez au moins un objet.");
    }
    std::vector<std::string> batches;
    const auto specs = specsOf(s);
    for (const auto& [key, value] : pairs) {
        const auto it = std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key; });
        if (it == specs.end()) {
            return u8("Réglage inconnu pour ce type de points : " + key);
        }
        const QString name = u8(it->label);
        const QString unit = it->unit.empty() ? QString() : QStringLiteral(" ") + u8(it->unit);
        if (!std::isfinite(value)) {
            return name + QStringLiteral(" : valeur non finie.");
        }
        if (value < it->min - kEps || value > it->max + kEps) {
            return name + QStringLiteral(" : %1%2 est hors plage [%3 ; %4]%2.")
                              .arg(formatValue(*it, value), unit, formatValue(*it, it->min),
                                   formatValue(*it, it->max));
        }
        if (it->kind != ParamKind::Number && it->kind != ParamKind::OptionalNumber &&
            std::abs(value - std::round(value)) > kEps) {
            return name + QStringLiteral(" : une valeur entière est attendue.");
        }
        if (it->kind == ParamKind::Point || it->kind == ParamKind::List) {
            return name + QStringLiteral(" : ce réglage se modifie par son propre éditeur.");
        }
        for (const auto& [type, ids] : s.groups) {
            const auto json = batchSetParam(ids, type, key, value);
            if (json.empty()) {
                return name + QStringLiteral(" : valeur refusée.");
            }
            batches.push_back(json);
        }
    }
    return commitBatches(s, batches, label, mergeKey);
}

QString InspectorController::commitBatches(const Selection& s,
                                           const std::vector<std::string>& batches,
                                           const QString& label, const std::string& mergeKey) {
    // Every batch is applied on a COPY with the same operation as Claude and the CLI
    // (`set_params`); the live document only receives the final result, in one command.
    document::Project scratch = session_.project();
    for (const auto& json : batches) {
        const auto report = project_ops::apply_batch(scratch, json);
        if (!report.ok || !report.applied) {
            return refusalOf(report);
        }
    }
    auto composite = std::make_unique<commands::CompositeCommand>(label.toStdString());
    for (const auto id : s.ids) {
        const auto* before = findObject(session_.project(), id);
        const auto* after = findObject(scratch, id);
        if (before == nullptr || after == nullptr || before->params == after->params) {
            continue;
        }
        composite->add(std::make_unique<commands::SetStitchParamsCommand>(ObjectId{id},
                                                                          after->params, mergeKey));
    }
    if (composite->empty()) {
        return {}; // already at that value: nothing to record
    }
    std::string key;
    if (!mergeKey.empty()) {
        key = "inspector:" + mergeKey;
        for (const auto id : s.ids) {
            key += ":" + std::to_string(id);
        }
    }
    composite->setMergeKey(key);
    session_.execute(std::move(composite));
    return {};
}

QString InspectorController::setParam(const QString& key, double value) {
    const auto s = selection();
    const auto specs = specsOf(s);
    const auto it =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    const QString label = it == specs.end() ? key : u8(it->label);
    return finish(applyPairs(s, {{key.toStdString(), value}}, QStringLiteral("Modifier : ") + label,
                             key.toStdString()),
                  label + QStringLiteral(" modifié"));
}

QString InspectorController::setParamText(const QString& key, const QString& text) {
    const auto s = selection();
    const auto specs = specsOf(s);
    const auto it =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    QString cleaned = text.trimmed();
    if (it != specs.end() && !it->unit.empty() && cleaned.endsWith(u8(it->unit))) {
        cleaned.chop(static_cast<qsizetype>(u8(it->unit).size()));
    }
    cleaned = cleaned.trimmed();
    cleaned.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = cleaned.toDouble(&ok);
    if (!ok) {
        return finish(QStringLiteral("« %1 » n'est pas un nombre.").arg(text.trimmed()));
    }
    return setParam(key, value);
}

QString InspectorController::setPoint(const QString& key, double xMm, double yMm) {
    const auto s = selection();
    if (s.ids.empty() || s.typeMixed) {
        return finish(QStringLiteral("Sélectionnez des objets d'un même type de points."));
    }
    const auto spec = specOf(s.typeId, key.toStdString());
    if (!spec || spec->kind != ParamKind::Point) {
        return finish(QStringLiteral("Ce réglage n'est pas un point."));
    }
    const auto json = batchSetPoint(s.ids, s.typeId, key.toStdString(), xMm, yMm);
    if (json.empty()) {
        return finish(u8(std::string(spec->label)) +
                      QStringLiteral(" : coordonnées refusées (entre -5000 et 5000 mm)."));
    }
    const QString label = u8(spec->label);
    return finish(
        commitBatches(s, {json}, QStringLiteral("Modifier : ") + label, key.toStdString()),
        label + QStringLiteral(" modifié"));
}

QString InspectorController::clearParam(const QString& key) {
    const auto s = selection();
    if (s.ids.empty()) {
        return finish(QStringLiteral("Sélectionnez au moins un objet."));
    }
    const auto specs = specsOf(s);
    const auto spec =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    if (spec == specs.end()) {
        return finish(QStringLiteral("Réglage inconnu pour ce type de points."));
    }
    std::vector<std::string> batches;
    for (const auto& [type, ids] : s.groups) {
        const auto json = batchClearParam(ids, type, key.toStdString());
        if (json.empty()) {
            return finish(QStringLiteral("Ce réglage ne s'efface pas."));
        }
        batches.push_back(json);
    }
    const QString label = u8(spec->label);
    return finish(commitBatches(s, batches, QStringLiteral("Effacer : ") + label, {}),
                  label + QStringLiteral(" effacé"));
}

QString InspectorController::resetParam(const QString& key) {
    const auto s = selection();
    const auto specs = specsOf(s);
    const auto found =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    const ParamSpec* spec = found == specs.end() ? nullptr : &*found;
    if (spec && !isScalarKind(spec->kind)) {
        return clearParam(key); // a point / a list / an optional goes back to « absent »
    }
    const QString label = spec ? u8(spec->label) : key;
    // Each type goes back to ITS OWN default (they can differ between types).
    std::vector<std::string> batches;
    for (const auto& [type, ids] : s.groups) {
        const auto def = defaultOf(type, key.toStdString());
        if (!def) {
            return finish(QStringLiteral("Aucune valeur par défaut pour ce réglage."));
        }
        const auto json = batchSetParam(ids, type, key.toStdString(), *def);
        if (json.empty()) {
            return finish(label + QStringLiteral(" : valeur refusée."));
        }
        batches.push_back(json);
    }
    return finish(
        commitBatches(s, batches, QStringLiteral("Réinitialiser : ") + label, key.toStdString()),
        label + QStringLiteral(" réinitialisé"));
}

QString InspectorController::resetAll() {
    const auto s = selection();
    if (s.ids.empty()) {
        return finish(QStringLiteral("Sélectionnez au moins un objet."));
    }
    std::vector<std::string> batches;
    for (const auto& spec : specsOf(s)) {
        for (const auto& [type, ids] : s.groups) {
            if (isScalarKind(spec.kind)) {
                if (const auto def = defaultOf(type, spec.key)) {
                    batches.push_back(batchSetParam(ids, type, spec.key, *def));
                }
            } else if (spec.kind != ParamKind::List) {
                // Guides, break lines and rungs are authored on the canvas: never wiped.
                batches.push_back(batchClearParam(ids, type, spec.key));
            }
        }
    }
    std::erase_if(batches, [](const std::string& b) { return b.empty(); });
    if (batches.empty()) {
        return finish(QStringLiteral("Ce type de points n'a pas de réglages à réinitialiser."));
    }
    return finish(
        commitBatches(s, batches, QStringLiteral("Réinitialiser les réglages de points"), {}),
        QStringLiteral("Réglages réinitialisés"));
}

QString InspectorController::setStitchType(const QString& id) {
    const auto s = selection();
    if (s.ids.empty()) {
        return finish(QStringLiteral("Sélectionnez au moins un objet."));
    }
    const std::string type = id.toStdString();
    if (!s.typeMixed && settableIdOf(s.typeId) == type) {
        return finish({}, QStringLiteral("Déjà de ce type."));
    }
    if (type == "auto_satin") {
        // As in the Widgets inspector: the first method that builds columns wins; a refused
        // attempt leaves the document intact. Only the last refusal is reported.
        const auto last = satinAttemptCount() - 1;
        for (std::size_t attempt = 0; attempt < last; ++attempt) {
            if (session_.applyBatch(batchAutoSatin(s.ids, attempt), "Type : satin").committed) {
                return finish({}, QStringLiteral("Converti en satin"));
            }
        }
        const auto result = session_.applyBatch(batchAutoSatin(s.ids, last), "Type : satin");
        return finish(result.committed ? QString() : refusalOf(result.report),
                      QStringLiteral("Converti en satin"));
    }
    const auto json = batchSetStitchType(s.ids, type);
    if (json.empty()) {
        return finish(QStringLiteral("Type de points inconnu."));
    }
    const auto result = session_.applyBatch(json, "Type de points");
    return finish(result.committed ? QString() : refusalOf(result.report),
                  QStringLiteral("Type de points modifié"));
}

QString InspectorController::savePreset(const QString& name) {
    const auto s = selection();
    if (s.ids.empty()) {
        return finish(QStringLiteral("Sélectionnez l'objet dont les réglages deviennent le "
                                     "préréglage."));
    }
    const auto result = desktop_v2::savePreset(session_, name.toStdString(), s.ids.front());
    return finish(result.ok ? QString() : u8(result.message),
                  QStringLiteral("Préréglage « %1 » enregistré").arg(name.trimmed()));
}

QString InspectorController::applyPreset(int index) {
    const auto s = selection();
    const auto result = desktop_v2::applyPreset(session_, index, s.ids);
    return finish(result.ok ? QString() : u8(result.message),
                  QStringLiteral("Préréglage appliqué"));
}

QString InspectorController::renamePreset(int index, const QString& name) {
    const auto& list = session_.project().stitch_presets;
    const std::string clean = name.trimmed().toStdString();
    if (index < 0 || static_cast<std::size_t>(index) >= list.size()) {
        return finish(QStringLiteral("Préréglage inconnu."));
    }
    if (clean.empty()) {
        return finish(QStringLiteral("Donnez un nom au préréglage."));
    }
    if (clean.size() > 64) {
        return finish(QStringLiteral("Nom trop long (64 caractères au plus)."));
    }
    const auto& old = list[static_cast<std::size_t>(index)];
    if (old.name == clean) {
        return finish({}, QStringLiteral("Même nom."));
    }
    if (std::ranges::any_of(list, [&](const auto& p) { return p.name == clean; })) {
        return finish(QStringLiteral("Un préréglage porte déjà ce nom."));
    }
    // One undo step, ORDER PRESERVED: the entries from `index` on are removed (last first), then
    // re-saved in their original order with the new name at the renamed slot.
    const auto first = static_cast<std::size_t>(index);
    auto composite = std::make_unique<commands::CompositeCommand>("Renommer le préréglage");
    for (std::size_t i = list.size(); i > first; --i) {
        composite->add(
            std::make_unique<commands::RemoveStitchPresetCommand>(static_cast<int>(i - 1)));
    }
    for (std::size_t i = first; i < list.size(); ++i) {
        composite->add(std::make_unique<commands::SaveStitchPresetCommand>(
            i == first ? clean : list[i].name, list[i].params));
    }
    session_.execute(std::move(composite));
    return finish({}, QStringLiteral("Préréglage renommé"));
}

QString InspectorController::removePreset(int index) {
    const auto result = desktop_v2::removePreset(session_, index);
    return finish(result.ok ? QString() : u8(result.message),
                  QStringLiteral("Préréglage supprimé"));
}

} // namespace openstitch::desktop_v2::studio
