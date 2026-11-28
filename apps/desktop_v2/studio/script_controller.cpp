// SPDX-License-Identifier: Apache-2.0
#include "script_controller.hpp"

#include "task_pump.hpp"

#include <map>
#include <utility>

#include "param_spec.hpp"

#include "openstitch/scripting/selection_script.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

QString q(const std::string& s) {
    return QString::fromStdString(s);
}

struct Example {
    const char* title;
    const char* code;
};

// Exemples affichés dans le panneau : chacun garde les objets dont le type n'a pas le paramètre
// (un satin n'a pas d'écart de rangées), pour qu'il fonctionne sur n'importe quelle sélection.
// Lignes de 44 caractères au plus : elles tiennent dans l'éditeur du panneau (340 px) sans
// défilement horizontal.
const Example kExamples[] = {
    {QT_TRANSLATE_NOOP("ScriptController", "Densité selon la taille"),
     "# Grandes zones aérées, petites denses.\n"
     "for o in selection:\n"
     "    # un satin n'a pas d'écart de rangées\n"
     "    if \"row_spacing_mm\" not in o.params:\n"
     "        continue\n"
     "    if o.area_mm2 > 400:\n"
     "        o.set(row_spacing_mm=0.45)\n"
     "    elif o.area_mm2 < 50:\n"
     "        o.set(row_spacing_mm=0.35)\n"},
    {QT_TRANSLATE_NOOP("ScriptController", "Angle selon la position"),
     "# Gauche du cadre à 45°, droite à 135°.\n"
     "milieu = canvas[\"width_mm\"] / 2\n"
     "for o in selection:\n"
     "    if \"angle_deg\" in o.params:\n"
     "        a = 45 if o.x < milieu else 135\n"
     "        o.set(angle_deg=a)\n"},
    {QT_TRANSLATE_NOOP("ScriptController", "Selon la couleur"),
     "# Rouges et oranges vifs : plus serrés.\n"
     "for o in selection:\n"
     "    vif = o.color.saturation > 40\n"
     "    h = o.color.hue\n"
     "    rouge = h < 40 or h > 330\n"
     "    p = \"row_spacing_mm\" in o.params\n"
     "    if vif and rouge and p:\n"
     "        o.set(row_spacing_mm=0.38)\n"},
    {QT_TRANSLATE_NOOP("ScriptController", "Dégradé d'angle de gauche à droite"),
     "# Angle de 0° (gauche) à 90° (droite).\n"
     "xs = [o.x for o in selection]\n"
     "g, d = min(xs), max(xs)\n"
     "for o in selection:\n"
     "    t = (o.x - g) / (d - g) if d > g else 0\n"
     "    if \"angle_deg\" in o.params:\n"
     "        o.set(angle_deg=round(90 * t))\n"},
    {QT_TRANSLATE_NOOP("ScriptController", "Lister les objets et leurs paramètres"),
     "# Ne change rien : affiche ce qui est lu.\n"
     "for o in selection:\n"
     "    print(o.id, o.type, o.color,\n"
     "          f\"{o.area_mm2:.0f} mm²\",\n"
     "          f\"x={o.x:.1f} y={o.y:.1f}\")\n"
     "    for k, v in o.params.items():\n"
     "        if not isinstance(v, (list, dict)):\n"
     "            print(\"   \", k, \"=\", v)\n"},
};

// Libellé français d'un réglage (« Écart entre rangées (mm) ») à partir de son nom de champ
// `set_params`, tel que l'inspecteur l'affiche ; le nom brut si aucun type ne le décrit.
QString paramLabel(const std::string& key) {
    if (key == "color") {
        return QStringLiteral("Couleur");
    }
    static const std::map<std::string, QString> labels = [] {
        std::map<std::string, QString> out;
        for (const auto type : paramTypeIds()) {
            for (const auto& spec : allParamSpecs(type)) {
                const std::string k(spec.key);
                if (out.contains(k)) {
                    continue;
                }
                QString label = QString::fromUtf8(spec.label.data(), qsizetype(spec.label.size()));
                if (!spec.unit.empty()) {
                    label += QStringLiteral(" (%1)").arg(
                        QString::fromUtf8(spec.unit.data(), qsizetype(spec.unit.size())));
                }
                out.emplace(k, label);
            }
        }
        return out;
    }();
    const auto it = labels.find(key);
    return it != labels.end() ? it->second : QString::fromStdString(key);
}

// Valeur JSON compacte rendue lisible : chaînes sans guillemets, décimales à la française.
QString readable(const std::string& json) {
    QString text = QString::fromStdString(json);
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        return text.mid(1, text.size() - 2);
    }
    if (text == QStringLiteral("true")) {
        return QStringLiteral("oui");
    }
    if (text == QStringLiteral("false")) {
        return QStringLiteral("non");
    }
    if (text == QStringLiteral("null")) {
        return QStringLiteral("—");
    }
    bool number = false;
    (void)text.toDouble(&number);
    return number ? text.replace(QLatin1Char('.'), QLatin1Char(',')) : text;
}

} // namespace

ScriptController::ScriptController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio), source_(QString::fromUtf8(kExamples[0].code)) {
    // Sélection ou document modifiés : le compteur et l'état « périmé » de l'aperçu changent.
    connect(&studio_, &StudioController::changed, this, [this] { emit stateChanged(); });
    attachTaskPump(tasks_, this);
}

ScriptController::~ScriptController() {
    tasks_.cancelAll();
}

void ScriptController::setSource(const QString& source) {
    if (source == source_) {
        return;
    }
    source_ = source;
    emit sourceChanged();
    emit stateChanged();
}

std::vector<ObjectId> ScriptController::selectedIds() const {
    std::vector<ObjectId> ids;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            ids.push_back(ObjectId{target.id});
        }
    }
    return ids;
}

int ScriptController::selectionCount() const {
    return static_cast<int>(selectedIds().size());
}

bool ScriptController::previewStale() const {
    return preview_ && (preview_->revision != studio_.session().revision() ||
                        preview_->selection != selectedIds() || preview_->source != source_);
}

QVariantList ScriptController::changes() const {
    return preview_ ? preview_->changes : QVariantList{};
}

QVariantList ScriptController::examples() const {
    QVariantList list;
    for (const auto& example : kExamples) {
        list.push_back(QVariantMap{{QStringLiteral("title"), tr(example.title)},
                                   {QStringLiteral("code"), QString::fromUtf8(example.code)}});
    }
    return list;
}

bool ScriptController::modified() const {
    return source_ != QString::fromUtf8(kExamples[exampleIndex_].code);
}

void ScriptController::loadExample(int index) {
    if (index < 0 || index >= static_cast<int>(std::size(kExamples))) {
        return;
    }
    if (modified() && !source_.trimmed().isEmpty()) {
        savedSource_ = source_;
    }
    exampleIndex_ = index;
    const QString code = QString::fromUtf8(kExamples[index].code);
    if (code == source_) {
        emit sourceChanged();
        return;
    }
    setSource(code);
}

void ScriptController::restoreSource() {
    if (savedSource_.isEmpty()) {
        return;
    }
    const QString restored = savedSource_;
    savedSource_.clear();
    setSource(restored);
}

void ScriptController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

void ScriptController::setRunning(bool running) {
    if (running_ != running) {
        running_ = running;
        emit busyChanged();
    }
}

void ScriptController::cancel() {
    // Le script lui-même s'arrête à son délai (5 s au plus) ; son résultat est ignoré.
    tasks_.cancelAll();
    setRunning(false);
    say(true, tr("Résultat ignoré : le document ne sera pas modifié"));
}

void ScriptController::preview() {
    run(false);
}

void ScriptController::apply() {
    if (preview_ && !previewStale()) {
        (void)applyPreview();
        return;
    }
    run(true);
}

void ScriptController::run(bool applyAfter) {
    auto ids = selectedIds();
    if (ids.empty()) {
        say(false, tr("Sélectionnez d'abord un ou plusieurs objets brodés"));
        return;
    }
    if (running_) {
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    if (!snapshot) {
        return;
    }
    Preview pending;
    pending.revision = studio_.session().revision();
    pending.selection = ids;
    pending.source = source_;
    const std::string code = source_.toStdString();
    setRunning(true);
    say(true, tr("Exécution du script…"));
    tasks_.submit<scripting::ScriptApplication>(
        "studio.selection_script",
        [snapshot, ids = std::move(ids), code](const TaskContext&) {
            document::Project scratch = *snapshot; // copie : la simulation ne touche pas le vivant
            return scripting::apply_selection_script(scratch, ids, code, {}, /*dry_run=*/true);
        },
        [this, pending = std::move(pending),
         applyAfter](Result<scripting::ScriptApplication>&& result) mutable {
            setRunning(false);
            if (!result) {
                preview_.reset();
                output_ = q(result.error().detail);
                emit previewChanged();
                const auto& e = result.error();
                say(false, e.hint.empty() ? q(e.message)
                                          : tr("%1\nPiste : %2").arg(q(e.message), q(e.hint)));
                return;
            }
            for (const auto& change : result->script.changes) {
                pending.changes.push_back(QVariantMap{
                    {QStringLiteral("object"), QVariant::fromValue(change.object.value)},
                    {QStringLiteral("name"),
                     change.object_name.empty()
                         ? QStringLiteral("Objet %1").arg(change.object.value)
                         : q(change.object_name)},
                    {QStringLiteral("key"), q(change.key)},
                    {QStringLiteral("label"), paramLabel(change.key)},
                    {QStringLiteral("before"), readable(change.before)},
                    {QStringLiteral("after"), readable(change.after)}});
            }
            pending.batch = std::move(result->script.batch_json);
            pending.operations = result->script.operation_count;
            output_ = q(result->script.output);
            preview_ = std::move(pending);
            emit previewChanged();
            emit stateChanged();
            if (preview_->changes.isEmpty()) {
                say(true, tr("Le script ne change aucune valeur : aucun objet sélectionné ne "
                             "remplit ses conditions (seuils de taille, de couleur…)"));
                return;
            }
            if (applyAfter) {
                (void)applyPreview();
                return;
            }
            const auto n = preview_->changes.size();
            say(true, n == 1 ? tr("1 changement prêt : vérifiez puis « Appliquer »")
                             : tr("%1 changements prêts : vérifiez puis « Appliquer »").arg(n));
        });
}

bool ScriptController::applyPreview() {
    if (!preview_ || previewStale()) {
        say(false, tr("Le document ou le script a changé : relancez l'aperçu"));
        return false;
    }
    const int count = static_cast<int>(preview_->changes.size());
    if (preview_->operations == 0) {
        say(true, tr("Le script ne change aucune valeur"));
        return false;
    }
    const auto result = studio_.session().applyBatch(preview_->batch, "Script sur la sélection");
    if (!result.report.ok || !result.report.applied) {
        QString reason = q(result.report.error_message);
        for (const auto& outcome : result.report.outcomes) {
            if (!outcome.ok) {
                reason = q(outcome.message);
                break;
            }
        }
        say(false, tr("Lot refusé : %1").arg(reason));
        return false;
    }
    preview_.reset(); // consommé : le document a changé
    emit previewChanged();
    emit stateChanged();
    say(true, count == 1 ? tr("1 changement appliqué — Ctrl+Z pour annuler")
                         : tr("%1 changements appliqués — Ctrl+Z pour annuler").arg(count));
    emit applied(count);
    return true;
}

} // namespace openstitch::desktop_v2::studio
