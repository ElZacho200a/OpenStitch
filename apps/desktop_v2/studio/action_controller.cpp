// SPDX-License-Identifier: Apache-2.0
#include "action_controller.hpp"

#include <QHash>

#include <algorithm>

#include "command_catalog.hpp"
#include "edit_batch.hpp"
#include "tool_registry.hpp"

namespace openstitch::desktop_v2::studio {

namespace {
QString qs(const std::string& text) {
    return QString::fromStdString(text);
}

QString kindName(FieldKind kind) {
    switch (kind) {
    case FieldKind::Number:
        return QStringLiteral("number");
    case FieldKind::Integer:
        return QStringLiteral("integer");
    case FieldKind::Bool:
        return QStringLiteral("bool");
    case FieldKind::Text:
        return QStringLiteral("text");
    case FieldKind::Choice:
        return QStringLiteral("choice");
    case FieldKind::File:
        return QStringLiteral("file");
    }
    return QStringLiteral("text");
}
} // namespace

const std::vector<StudioCommand>& studioCommands() {
    static const std::vector<StudioCommand> table = [] {
        std::vector<StudioCommand> out = {
            {"cmd.file.new", "Nouveau projet", "Fichier", "Ctrl+N", "idle"},
            {"cmd.file.open", "Ouvrir un projet…", "Fichier", "Ctrl+O", "idle"},
            {"cmd.file.save", "Enregistrer", "Fichier", "Ctrl+S", "idle"},
            {"cmd.file.save-as", "Enregistrer sous…", "Fichier", "Ctrl+Shift+S", "idle"},
            {"cmd.file.import-image", "Importer une image ou un SVG…", "Fichier", "", "idle"},
            {"cmd.file.import-machine", "Importer une broderie machine…", "Fichier", "", "idle"},
            {"cmd.edit.undo", "Annuler", "Édition", "Ctrl+Z", "undo"},
            {"cmd.edit.redo", "Rétablir", "Édition", "Ctrl+Y", "redo"},
            {"cmd.edit.duplicate", "Dupliquer la sélection", "Édition", "Ctrl+D", "selection"},
            {"cmd.edit.delete", "Supprimer la sélection", "Édition", "Suppr", "selection"},
            {"cmd.edit.group", "Grouper la sélection", "Édition", "Ctrl+G", "multi"},
            {"cmd.edit.ungroup", "Dissocier le groupe", "Édition", "Ctrl+Shift+G", "selection"},
            {"cmd.edit.rotate-cw", "Pivoter de 90° (horaire)", "Édition", "Ctrl+R", "selection"},
            {"cmd.edit.rotate-ccw", "Pivoter de 90° (anti-horaire)", "Édition", "Ctrl+Shift+R",
             "selection"},
            {"cmd.edit.mirror-h", "Miroir horizontal", "Édition", "Ctrl+Shift+H", "selection"},
            {"cmd.edit.mirror-v", "Miroir vertical", "Édition", "Ctrl+Shift+V", "selection"},
            {"cmd.edit.eyedropper", "Pipette de couleur", "Édition", "I", "selection"},
            {"cmd.select.all", "Tout sélectionner", "Sélection", "Ctrl+A", ""},
            {"cmd.select.invert", "Inverser la sélection", "Sélection", "Ctrl+I", ""},
            {"cmd.select.clear", "Effacer la sélection", "Sélection", "Échap", "selection"},
            {"cmd.arrange.forward", "Avancer d'un plan", "Ordre", "PgUp", "selection"},
            {"cmd.arrange.backward", "Reculer d'un plan", "Ordre", "PgDown", "selection"},
            {"cmd.arrange.front", "Premier plan", "Ordre", "Fin", "selection"},
            {"cmd.arrange.back", "Arrière-plan", "Ordre", "Début", "selection"},
            {"cmd.workspace.0", "Espace : Préparer", "Espace", "Ctrl+1", ""},
            {"cmd.workspace.1", "Espace : Numériser", "Espace", "Ctrl+2", ""},
            {"cmd.workspace.2", "Espace : Contrôler", "Espace", "Ctrl+3", ""},
            {"cmd.workspace.3", "Espace : Produire", "Espace", "Ctrl+4", ""},
            {"cmd.view.fit", "Ajuster le motif à la fenêtre", "Vue", "F", ""},
            {"cmd.view.zoom-in", "Zoom avant", "Vue", "", ""},
            {"cmd.view.zoom-out", "Zoom arrière", "Vue", "", ""},
            {"cmd.view.zoom-selection", "Zoom sur la sélection", "Vue", "", "selection"},
            {"cmd.prefs.open", "Préférences…", "Application", "Ctrl+,", ""},
            {"cmd.companion.open", "Compagnon IA", "Application", "Ctrl+J", ""},
            {"cmd.companion.toggle", "Compagnon IA : activer ou couper le pont", "Application",
             "Ctrl+Shift+J", ""},
            {"cmd.help.shortcuts", "Raccourcis clavier et gestes", "Aide", "F1", ""},
            {"cmd.help.guide", "Guide de prise en main", "Aide", "", ""},
            {"cmd.help.tool", "Aide sur l'outil actif", "Aide", "Shift+F1", ""},
            {"cmd.help.about", "À propos d'OpenStitch Studio", "Aide", "", ""},
        };
        const auto registry = ToolRegistry::builtin();
        for (const auto& tool : registry.tools()) {
            out.push_back(
                {"cmd.tool." + tool.id, "Outil : " + tool.label, "Outil", tool.shortcut, ""});
        }
        return out;
    }();
    return table;
}

ActionController::ActionController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    token_ = studio_.session().subscribe([this](ChangeKind) { emit actionsChanged(); });
}

ActionController::~ActionController() {
    studio_.session().unsubscribe(token_);
}

const ActionDef* ActionController::find(const QString& id) const {
    const auto key = id.toStdString();
    if (const auto* special = contextAction(key)) {
        return special;
    }
    return findAction(key);
}

ActionContext ActionController::context() const {
    ActionContext ctx;
    const auto& session = studio_.session();
    double x0 = 0;
    double y0 = 0;
    double x1 = 0;
    double y1 = 0;
    bool any = false;
    const auto& scene = studio_.scene();
    for (const auto& target : session.selection().targets()) {
        if (target.kind != TargetKind::EmbroideryObject) {
            continue;
        }
        ctx.ids.push_back(target.id);
        if (!scene) {
            continue;
        }
        const auto found = scene->objectBounds.find(target.id);
        if (found == scene->objectBounds.end()) {
            continue;
        }
        const auto& b = found->second;
        x0 = any ? std::min(x0, b.x0) : b.x0;
        y0 = any ? std::min(y0, b.y0) : b.y0;
        x1 = any ? std::max(x1, b.x1) : b.x1;
        y1 = any ? std::max(y1, b.y1) : b.y1;
        any = true;
    }
    // Scene space is Y down; the catalog works in model space (Y up).
    ctx.centre = any ? PointMm{(x0 + x1) / 2.0, -(y0 + y1) / 2.0} : PointMm{0, 0};
    ctx.project = &session.project();
    return ctx;
}

QVariantList ActionController::search(const QString& query) const {
    const bool hasSelection = studio_.hasSelection();
    QVariantList list;
    const auto needle = normalizeForSearch(query.toStdString());
    for (const auto& def : actionCatalog()) {
        if (!needle.empty() &&
            normalizeForSearch(def.label + " " + def.group + " " + def.id).find(needle) ==
                std::string::npos) {
            continue;
        }
        list.push_back(
            QVariantMap{{QStringLiteral("id"), qs(def.id)},
                        {QStringLiteral("label"), qs(def.label)},
                        {QStringLiteral("group"), qs(def.group)},
                        {QStringLiteral("needsSelection"), def.needsSelection},
                        {QStringLiteral("hasFields"), !def.fields.empty()},
                        {QStringLiteral("enabled"), !def.needsSelection || hasSelection}});
    }
    return list;
}

QString ActionController::commandReason(const StudioCommand& command) const {
    if (command.needs == "idle" && studio_.busy()) {
        return QStringLiteral("opération en cours");
    }
    if (command.needs == "undo" && !studio_.canUndo()) {
        return QStringLiteral("rien à annuler");
    }
    if (command.needs == "redo" && !studio_.canRedo()) {
        return QStringLiteral("rien à rétablir");
    }
    if (command.needs == "selection" && !studio_.hasSelection()) {
        return QStringLiteral("sélection requise");
    }
    if (command.needs == "multi") {
        int selected = 0;
        for (const auto& entry : studio_.objects()) {
            selected += entry.toMap().value(QStringLiteral("selected")).toBool() ? 1 : 0;
        }
        if (selected < 2) {
            return QStringLiteral("deux objets au moins");
        }
    }
    return {};
}

QVariantList ActionController::commands() const {
    QVariantList list;
    for (const auto& command : studioCommands()) {
        const auto reason = commandReason(command);
        list.push_back(QVariantMap{{QStringLiteral("id"), qs(command.id)},
                                   {QStringLiteral("label"), qs(command.label)},
                                   {QStringLiteral("group"), qs(command.group)},
                                   {QStringLiteral("shortcut"), qs(command.shortcut)},
                                   {QStringLiteral("kind"), QStringLiteral("command")},
                                   {QStringLiteral("enabled"), reason.isEmpty()},
                                   {QStringLiteral("reason"), reason},
                                   {QStringLiteral("needsSelection"), command.needs == "selection"},
                                   {QStringLiteral("hasFields"), false}});
    }
    return list;
}

QVariantList ActionController::searchAll(const QString& query) const {
    // Un seul filtre pour tout : celui du catalogue de commandes (mots multiples, sans accents).
    std::vector<CommandEntry> entries;
    QHash<QString, QVariantMap> byId;
    const auto add = [&](QVariantMap map) {
        const auto key = map.value(QStringLiteral("id")).toString();
        entries.push_back({key.toStdString(),
                           map.value(QStringLiteral("label")).toString().toStdString(),
                           map.value(QStringLiteral("group")).toString().toStdString(),
                           map.value(QStringLiteral("shortcut")).toString().toStdString(),
                           map.value(QStringLiteral("enabled")).toBool()});
        byId.insert(key, std::move(map));
    };
    for (const auto& item : search({})) {
        auto map = item.toMap();
        map[QStringLiteral("kind")] = QStringLiteral("action");
        map[QStringLiteral("shortcut")] = QString();
        map[QStringLiteral("reason")] = map.value(QStringLiteral("enabled")).toBool()
                                            ? QString()
                                            : QStringLiteral("sélection requise");
        add(std::move(map));
    }
    for (const auto& item : commands()) {
        add(item.toMap());
    }
    QVariantList list;
    for (const auto* entry : filterCommands(entries, query.toStdString())) {
        list.push_back(byId.value(qs(entry->id)));
    }
    return list;
}

QString ActionController::runCommand(const QString& id) {
    const auto& table = studioCommands();
    const auto found = std::ranges::find(table, id.toStdString(), &StudioCommand::id);
    if (found == table.end()) {
        return QStringLiteral("Commande inconnue");
    }
    if (const auto reason = commandReason(*found); !reason.isEmpty()) {
        return qs(found->label) + QStringLiteral(" : ") + reason;
    }
    emit commandRequested(id);
    return {};
}

QVariantList ActionController::fields(const QString& id) const {
    QVariantList list;
    const auto* def = find(id);
    if (!def) {
        return list;
    }
    auto values = defaultValues(*def);
    if (def->id == "text.edit" || def->id == "text.glyph") { // current settings of the selection
        const auto ctx = context();
        if (def->id == "text.edit") {
            if (const auto owner = selectedTextOwner(*ctx.project, ctx.ids)) {
                values = textEditValues(*ctx.project, *owner);
            }
        } else {
            const auto target = glyphTargetOf(*ctx.project, ctx.ids);
            if (target.ok) {
                values = glyphEditValues(*ctx.project, target);
            }
        }
    }
    for (const auto& field : def->fields) {
        QVariantList choices;
        for (const auto& choice : field.choices) {
            choices.push_back(QVariantMap{{QStringLiteral("value"), qs(choice.value)},
                                          {QStringLiteral("label"), qs(choice.label)}});
        }
        const auto found = values.find(field.key);
        list.push_back(QVariantMap{
            {QStringLiteral("key"), qs(field.key)},
            {QStringLiteral("label"), qs(field.label)},
            {QStringLiteral("kind"), kindName(field.kind)},
            {QStringLiteral("min"), field.min},
            {QStringLiteral("max"), field.max},
            {QStringLiteral("value"), qs(found != values.end() ? found->second : field.def)},
            {QStringLiteral("unit"), qs(field.unit)},
            {QStringLiteral("choices"), choices},
            {QStringLiteral("required"), field.required}});
    }
    return list;
}

QString ActionController::finish(const ActionDef& def, bool ok, const QString& message) {
    ok_ = ok;
    message_ = qs(def.label) + (ok ? QStringLiteral(" : fait.") : QStringLiteral(" : ") + message);
    message_.remove(QStringLiteral("…"));
    emit resultChanged();
    return ok ? QString() : message;
}

QString ActionController::run(const QString& id, const QVariantMap& values) {
    const auto* def = find(id);
    if (!def) {
        return QStringLiteral("Action inconnue");
    }
    Values typed;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        typed[it.key().toStdString()] = it.value().toString().toStdString();
    }
    auto& session = studio_.session();
    const auto ctx = context();
    std::string error;
    if (def->run) { // ICommand-backed action
        if (!validateAction(*def, typed, ctx, error)) {
            return finish(*def, false, qs(error));
        }
        const auto result = def->run(session, typed, ctx);
        return finish(*def, result.ok, qs(result.message));
    }
    const auto batch = buildAction(*def, typed, ctx, error);
    if (batch.empty()) {
        return finish(*def, false, qs(error));
    }
    const auto result = session.applyBatch(batch, def->label);
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        return finish(*def, false,
                      outcomes.empty() ? QStringLiteral("opération invalide")
                                       : qs(outcomes.back().message));
    }
    if (def->id == "text.remove") {
        session.selection().clear(); // removed letters can no longer be selected
    } else {
        std::vector<Target> created;
        for (const auto newId : createdObjectIds(result.report)) {
            if (session.project().findText(ObjectId{newId}) != nullptr) {
                for (const auto& emb : session.project().embroidery_objects) {
                    const auto* vec = session.project().findObject(emb.source_vector);
                    if (vec && vec->text_owner && vec->text_owner->value == newId) {
                        created.push_back({TargetKind::EmbroideryObject, emb.id.value});
                    }
                }
            } else {
                created.push_back({TargetKind::EmbroideryObject, newId});
            }
        }
        if (!created.empty()) {
            session.selection().replace(std::move(created));
        }
    }
    return finish(*def, true, {});
}

} // namespace openstitch::desktop_v2::studio
