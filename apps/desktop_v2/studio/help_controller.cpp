// SPDX-License-Identifier: Apache-2.0
#include "help_controller.hpp"

#include <QCoreApplication>
#include <QVariantMap>

#include <vector>

#include "action_controller.hpp"
#include "tool_registry.hpp"

namespace openstitch::desktop_v2::studio {

HelpController::HelpController(QObject* parent) : QObject(parent) {}

QVariantList HelpController::shortcuts() const {
    QVariantList groups;
    for (const auto& command : studioCommands()) {
        if (command.shortcut.empty()) {
            continue;
        }
        QVariantMap holder;
        int index = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (groups[i].toMap().value(QStringLiteral("group")).toString().toStdString() ==
                command.group) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            holder.insert(QStringLiteral("group"), QString::fromStdString(command.group));
            holder.insert(QStringLiteral("items"), QVariantList{});
            groups.push_back(holder);
            index = static_cast<int>(groups.size()) - 1;
        }
        auto entry = groups[index].toMap();
        auto items = entry.value(QStringLiteral("items")).toList();
        items.push_back(
            QVariantMap{{QStringLiteral("label"), QString::fromStdString(command.label)},
                        {QStringLiteral("shortcut"), QString::fromStdString(command.shortcut)}});
        entry.insert(QStringLiteral("items"), items);
        groups[index] = entry;
    }
    return groups;
}

QVariantList HelpController::gestures() const {
    const auto row = [](const char* label, const char* gesture) {
        return QVariantMap{{QStringLiteral("label"), QString::fromUtf8(label)},
                           {QStringLiteral("gesture"), QString::fromUtf8(gesture)}};
    };
    return {row("Sélectionner un objet", "Clic"),
            row("Ajouter ou retirer de la sélection", "Ctrl + clic"),
            row("Sélection par rectangle", "Glisser sur le vide"),
            row("Zoomer", "Molette"),
            row("Déplacer la vue", "Espace + glisser, ou outil Main"),
            row("Déplacer la sélection de 1 mm", "Flèches"),
            row("Déplacer la sélection de 5 mm", "Maj + flèches"),
            row("Annuler un geste en cours", "Échap"),
            row("Menu contextuel de l'objet", "Clic droit"),
            row("Nœud lisse / anguleux (outil Nœuds)", "S / C"),
            row("Palette de commandes", "Ctrl + K")};
}

QVariantList HelpController::guide() const {
    const auto step = [](const char* title, const char* text) {
        return QVariantMap{{QStringLiteral("title"), QString::fromUtf8(title)},
                           {QStringLiteral("text"), QString::fromUtf8(text)}};
    };
    return {step("1. Préparer",
                 "Ouvrez un projet .osp ou importez une image, un SVG ou une broderie "
                 "machine (Ctrl+O, ou le bouton Fichier). Réglez l'image et la palette."),
            step("2. Numériser",
                 "Segmentez l'image, créez les objets de broderie et réglez leurs "
                 "points dans l'inspecteur. Chaque modification est annulable (Ctrl+Z)."),
            step("3. Contrôler", "Lancez l'analyse : densité, risques de déformation du tissu, "
                                 "coupes de fil. Zoomez pour observer la structure des points."),
            step("4. Produire",
                 "Vérifiez le cadre et les couleurs, puis exportez le fichier machine "
                 "depuis le menu Fichier."),
            step("Compagnon IA", "Ctrl+J ouvre la discussion. Toute modification proposée par l'IA "
                                 "s'affiche en avant / après et attend votre accord.")};
}

QString HelpController::aboutText() const {
    return QStringLiteral("OpenStitch Studio %1\nNumérisation de broderie machine libre — licence "
                          "Apache-2.0.\nAucun code GPL ni AGPL.")
        .arg(QCoreApplication::applicationVersion().isEmpty()
                 ? QStringLiteral("(développement)")
                 : QCoreApplication::applicationVersion());
}

QString HelpController::toolHelp(const QString& toolId, const QString& hint) const {
    const auto registry = ToolRegistry::builtin();
    const auto* tool = registry.find(toolId.toStdString());
    if (tool == nullptr) {
        return {};
    }
    QString text = QString::fromStdString(tool->label);
    if (!tool->shortcut.empty()) {
        text += QStringLiteral(" (") + QString::fromStdString(tool->shortcut) + QLatin1Char(')');
    }
    if (!hint.isEmpty()) {
        text += QStringLiteral(" — ") + hint;
    }
    return text;
}

} // namespace openstitch::desktop_v2::studio
