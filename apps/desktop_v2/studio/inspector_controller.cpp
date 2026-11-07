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
    }
    return "number";
}

QString sectionOf(const ParamSpec& spec) {
    const std::string_view key = spec.key;
    if (key.find("underlay") != std::string_view::npos || key == "hidden_underpath") {
        return QStringLiteral("Sous-couche");
    }
    if (key.find("pull") != std::string_view::npos) {
        return QStringLiteral("Compensation de tirage");
    }
    return QStringLiteral("Points");
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
    if (s.ids.empty() || s.typeMixed) {
        return {};
    }
    for (const auto& spec : paramSpecs(s.typeId)) {
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
    return out;
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
    for (const auto id : s.ids) {
        const auto* object = findObject(session_.project(), id);
        const auto v = object ? readParam(*object, spec.key) : std::nullopt;
        if (!v) {
            continue;
        }
        if (first) {
            value = *v;
            first = false;
        } else if (std::abs(*v - value) > kEps) {
            mixed = true;
        }
    }
    const auto def = defaultOf(s.typeId, spec.key).value_or(value);
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
            {QStringLiteral("level"), paramLevel(spec) == ParamLevel::Essential
                                          ? QStringLiteral("essential")
                                          : QStringLiteral("advanced")},
            {QStringLiteral("section"), sectionOf(spec)},
            {QStringLiteral("value"), value},
            {QStringLiteral("text"), mixed ? QStringLiteral("—") : formatValue(spec, value)},
            {QStringLiteral("mixed"), mixed},
            {QStringLiteral("isDefault"), !mixed && std::abs(value - def) <= kEps},
            {QStringLiteral("defaultValue"), def},
            {QStringLiteral("defaultText"), formatValue(spec, def)},
            {QStringLiteral("choices"), choices}};
}

std::vector<ParamSpec> InspectorController::visibleSpecs(const Selection& s) const {
    std::vector<ParamSpec> out;
    if (s.ids.empty() || s.typeMixed) {
        return out;
    }
    const std::string needle = query_.toStdString();
    for (const auto& spec : paramSpecs(s.typeId)) {
        // A search looks through every level; without search the level toggle decides.
        const bool visible = needle.empty()
                                 ? (showAdvanced_ || paramLevel(spec) == ParamLevel::Essential)
                                 : paramMatches(spec, needle);
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
    return s.ids.empty() || s.typeMixed ? 0 : static_cast<int>(paramSpecs(s.typeId).size());
}

int InspectorController::hiddenAdvancedCount() const {
    const auto s = selection();
    if (s.ids.empty() || s.typeMixed || showAdvanced_ || !query_.isEmpty()) {
        return 0;
    }
    const auto specs = paramSpecs(s.typeId);
    return static_cast<int>(std::ranges::count_if(
        specs, [](const auto& spec) { return paramLevel(spec) == ParamLevel::Advanced; }));
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
    if (s.typeMixed) {
        return QStringLiteral(
            "La sélection mélange plusieurs types de points : convertissez-la d'abord.");
    }
    // Every value is validated and applied on a COPY with the same operation as Claude and the
    // CLI (`set_params`); the live document only receives the final result, in one command.
    document::Project scratch = session_.project();
    for (const auto& [key, value] : pairs) {
        const auto specs = paramSpecs(s.typeId);
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
        if (it->kind != ParamKind::Number && std::abs(value - std::round(value)) > kEps) {
            return name + QStringLiteral(" : une valeur entière est attendue.");
        }
        const auto json = batchSetParam(s.ids, s.typeId, key, value);
        if (json.empty()) {
            return name + QStringLiteral(" : valeur refusée.");
        }
        const auto report = project_ops::apply_batch(scratch, json);
        if (!report.ok || !report.applied) {
            return name + QStringLiteral(" : ") + refusalOf(report);
        }
    }
    auto composite = std::make_unique<commands::CompositeCommand>(label.toStdString());
    for (const auto id : s.ids) {
        const auto* before = findObject(session_.project(), id);
        const auto* after = findObject(scratch, id);
        if (before == nullptr || after == nullptr || before->params == after->params) {
            continue;
        }
        composite->add(std::make_unique<commands::SetStitchParamsCommand>(
            ObjectId{id}, after->params, pairs.size() == 1 ? pairs.front().first : std::string{}));
    }
    if (composite->empty()) {
        return {}; // already at that value: nothing to record
    }
    std::string key;
    if (pairs.size() == 1) {
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
    const auto specs = paramSpecs(s.typeId);
    const auto it =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    const QString label = it == specs.end() ? key : u8(it->label);
    return finish(applyPairs(s, {{key.toStdString(), value}}, QStringLiteral("Modifier : ") + label,
                             key.toStdString()),
                  label + QStringLiteral(" modifié"));
}

QString InspectorController::setParamText(const QString& key, const QString& text) {
    const auto s = selection();
    const auto specs = paramSpecs(s.typeId);
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

QString InspectorController::resetParam(const QString& key) {
    const auto s = selection();
    const auto def = defaultOf(s.typeId, key.toStdString());
    if (!def) {
        return finish(QStringLiteral("Aucune valeur par défaut pour ce réglage."));
    }
    const auto specs = paramSpecs(s.typeId);
    const auto it =
        std::ranges::find_if(specs, [&](const auto& sp) { return sp.key == key.toStdString(); });
    const QString label = it == specs.end() ? key : u8(it->label);
    return finish(applyPairs(s, {{key.toStdString(), *def}},
                             QStringLiteral("Réinitialiser : ") + label, key.toStdString()),
                  label + QStringLiteral(" réinitialisé"));
}

QString InspectorController::resetAll() {
    const auto s = selection();
    std::vector<std::pair<std::string, double>> pairs;
    for (const auto& spec : paramSpecs(s.typeId)) {
        if (const auto def = defaultOf(s.typeId, spec.key)) {
            pairs.emplace_back(std::string(spec.key), *def);
        }
    }
    if (pairs.empty()) {
        return finish(QStringLiteral("Ce type de points n'a pas de réglages à réinitialiser."));
    }
    return finish(applyPairs(s, pairs, QStringLiteral("Réinitialiser les réglages de points"), {}),
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
    // One undo step: save under the new name (appended), then drop the old entry.
    auto composite = std::make_unique<commands::CompositeCommand>("Renommer le préréglage");
    composite->add(std::make_unique<commands::SaveStitchPresetCommand>(clean, old.params));
    composite->add(std::make_unique<commands::RemoveStitchPresetCommand>(index));
    session_.execute(std::move(composite));
    return finish({}, QStringLiteral("Préréglage renommé"));
}

QString InspectorController::removePreset(int index) {
    const auto result = desktop_v2::removePreset(session_, index);
    return finish(result.ok ? QString() : u8(result.message),
                  QStringLiteral("Préréglage supprimé"));
}

} // namespace openstitch::desktop_v2::studio
