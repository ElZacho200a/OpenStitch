// SPDX-License-Identifier: Apache-2.0
#pragma once

// Journal de performance de Studio, ACTIVÉ SEULEMENT par la variable d'environnement
// OPENSTITCH_PERF_LOG=<fichier> : une ligne JSON par événement (reconstruction de scène, calque
// vectoriel...) avec sa durée réelle. Sans la variable, `perfLog` ne coûte qu'un test booléen.
// Sert à mesurer sur un VRAI projet où le temps part (aucune promesse de gain sans ces données).

#include <QElapsedTimer>
#include <QString>
#include <QVariantMap>

namespace openstitch::desktop_v2::studio {

[[nodiscard]] bool perfLogEnabled();
void perfLog(const QString& event, const QVariantMap& fields);

// Chronomètre de portée : écrit un événement `name` avec `ms` (temps passé sur le fil appelant,
// donc sur le fil d'interface pour les contrôleurs) à la destruction. Inactif sans la variable.
class PerfScope {
public:
    explicit PerfScope(const char* name) : name_(name), on_(perfLogEnabled()) {
        if (on_) {
            timer_.start();
        }
    }
    ~PerfScope() {
        if (on_) {
            perfLog(QString::fromLatin1(name_),
                    {{QStringLiteral("ms"), static_cast<double>(timer_.nsecsElapsed()) / 1e6}});
        }
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    const char* name_;
    bool on_;
    QElapsedTimer timer_;
};

} // namespace openstitch::desktop_v2::studio
