// SPDX-License-Identifier: Apache-2.0
#include "fix_controller.hpp"

#include <algorithm>

namespace openstitch::desktop_v2::studio {

namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}
} // namespace

FixController::FixController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    // Une proposition décrit UNE révision : tout changement du document la rend périmée à l'écran.
    connect(&studio_, &StudioController::changed, this, [this] {
        if (plan_ || reduction_) {
            emit plansChanged();
        }
    });
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
}

FixController::~FixController() {
    pump_.stop();
    tasks_.cancelAll();
}

void FixController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

void FixController::begin() {
    ++running_;
    emit busyChanged();
}

void FixController::end() {
    running_ = std::max(0, running_ - 1);
    emit busyChanged();
}

void FixController::cancel() {
    tasks_.cancelAll();
    running_ = 0;
    emit busyChanged();
    say(true, QStringLiteral("Calcul annulé"));
}

// --- « Corriger » -----------------------------------------------------------------------------

void FixController::analyze() {
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    begin();
    tasks_.submit<FixPlan>(
        "studio.fix_plan",
        [snapshot, revision](const TaskContext& task) {
            return plan_fixes(snapshot, revision, task);
        },
        [this](Result<FixPlan>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            plan_ = std::move(*result);
            emit plansChanged();
            say(true, plan_->proposals.empty()
                          ? QStringLiteral("Aucune correction automatique à proposer")
                          : QStringLiteral("%1 correction(s) proposée(s), %2 sûre(s)")
                                .arg(plan_->proposals.size())
                                .arg(plan_->safeCount));
        });
}

bool FixController::planStale() const {
    return plan_ && plan_->revision != studio_.session().revision();
}

QVariantList FixController::proposals() const {
    QVariantList list;
    if (!plan_) {
        return list;
    }
    const auto snapshot = studio_.session().snapshot();
    int index = 0;
    for (const auto& p : plan_->proposals) {
        list.push_back(QVariantMap{
            {QStringLiteral("index"), index++},
            {QStringLiteral("label"), q(p.label)},
            {QStringLiteral("message"), q(p.message)},
            {QStringLiteral("preview"), snapshot ? q(describe_fix(*snapshot, p)) : QString()},
            {QStringLiteral("safe"), p.safe}});
    }
    return list;
}

bool FixController::applyIndices(const std::vector<std::size_t>& indices, const QString& label) {
    if (!plan_) {
        say(false, QStringLiteral("Aucune proposition : lancez d'abord l'analyse"));
        return false;
    }
    if (planStale()) {
        say(false, QStringLiteral("Le document a changé depuis l'analyse : relancez-la"));
        return false;
    }
    const auto snapshot = studio_.session().snapshot();
    auto build = build_fix_command(*snapshot, *plan_, indices, label.toStdString());
    if (!build.command) {
        say(false, QStringLiteral("Aucune correction applicable"));
        return false;
    }
    studio_.session().execute(std::move(build.command)); // UNE étape d'annulation
    plan_.reset();                                       // consommée : le résultat est périmé
    emit plansChanged();
    say(true, QStringLiteral("%1 correction(s) appliquée(s) en une étape (annulable)")
                  .arg(build.applied));
    return true;
}

bool FixController::applyFix(int index) {
    if (index < 0) {
        say(false, QStringLiteral("Correction inconnue"));
        return false;
    }
    return applyIndices({static_cast<std::size_t>(index)}, QStringLiteral("Corriger"));
}

bool FixController::applyAllSafe() {
    return applyIndices({}, QStringLiteral("Corriger tout ce qui est sûr"));
}

void FixController::discardPlan() {
    plan_.reset();
    emit plansChanged();
}

// --- Réduire les fils -------------------------------------------------------------------------

void FixController::previewReduction(int maxThreads) {
    if (maxThreads < 1) {
        say(false, QStringLiteral("Le nombre de fils visé doit être au moins 1"));
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto limit = static_cast<std::size_t>(maxThreads);
    begin();
    tasks_.submit<ThreadReductionPreview>(
        "studio.thread_reduction",
        [snapshot, revision, limit](const TaskContext& task) {
            return plan_thread_reduction(snapshot, revision, limit, task);
        },
        [this](Result<ThreadReductionPreview>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            reduction_ = std::move(*result);
            emit plansChanged();
            say(!reduction_->plan.empty(),
                reduction_->plan.empty()
                    ? QStringLiteral("Le design utilise déjà ce nombre de fils ou moins")
                    : QStringLiteral("Aperçu : %1 fil(s) au lieu de %2")
                          .arg(reduction_->plan.after)
                          .arg(reduction_->plan.before));
        });
}

bool FixController::reductionStale() const {
    return reduction_ && reduction_->revision != studio_.session().revision();
}

int FixController::threadsBefore() const {
    return reduction_ ? static_cast<int>(reduction_->plan.before) : 0;
}

int FixController::threadsAfter() const {
    return reduction_ ? static_cast<int>(reduction_->plan.after) : 0;
}

int FixController::objectsRecoloured() const {
    return reduction_ ? static_cast<int>(reduction_->plan.reassignments.size()) : 0;
}

bool FixController::applyReduction() {
    if (!reduction_) {
        say(false, QStringLiteral("Aucun aperçu : choisissez d'abord le nombre de fils"));
        return false;
    }
    if (reductionStale()) {
        say(false, QStringLiteral("Le document a changé depuis l'aperçu : recalculez-le"));
        return false;
    }
    auto command = build_thread_reduction_command(*reduction_, "Réduire les fils");
    if (!command) {
        say(false, QStringLiteral("Rien à réduire"));
        return false;
    }
    const auto after = reduction_->plan.after;
    studio_.session().execute(std::move(command));
    reduction_.reset();
    emit plansChanged();
    say(true, QStringLiteral("Design réduit à %1 fil(s) en une étape (annulable)").arg(after));
    return true;
}

void FixController::discardReduction() {
    reduction_.reset();
    emit plansChanged();
}

} // namespace openstitch::desktop_v2::studio
