// SPDX-License-Identifier: Apache-2.0
// Générateur de la table des gestes pour la documentation utilisateur.
// Usage : openstitch_gesture_table --markdown
// Lié à interaction_map.cpp seul (Qt Core/Gui, aucun QApplication) ; la sortie
// est déterministe (préréglage OpenStitch par défaut, QSettings jamais lu) et
// comparée octet pour octet à docs/source/user-guide.md par `docs_gestures_in_sync`.

#include <QByteArray>
#include <QString>

#include <cstdio>
#include <cstring>

#include "interaction_map.hpp"

using openstitch::desktop::InteractionMap;

namespace {

QString cell(QString s) {
    return s.replace(QLatin1Char('|'), QStringLiteral("\\|"));
}

QString markdown() {
    QString out;
    out += QStringLiteral("| Réf. | Contexte | Geste | Action |\n");
    out += QStringLiteral("|---|---|---|---|\n");
    for (const auto* r : InteractionMap::allRows(false)) {
        out +=
            QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                .arg(QString::fromLatin1(r->id), cell(InteractionMap::contextName(r->context)),
                     cell(InteractionMap::describe(r->gesture)), cell(InteractionMap::label(*r)));
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--markdown") == 0) {
        const QByteArray bytes = markdown().toUtf8();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
        return 0;
    }
    std::fputs("usage: openstitch_gesture_table --markdown\n", stderr);
    return 2;
}
