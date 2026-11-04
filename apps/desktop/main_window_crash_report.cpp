// SPDX-License-Identifier: Apache-2.0
// Action « Aide > Envoyer un rapport de plantage… » et proposition au démarrage (HP-QA-002).
// Jamais d'envoi silencieux : le navigateur s'ouvre sur un ticket prérempli que l'utilisateur
// relit et valide ; aucune donnée de projet n'est lue.
#include <QAction>
#include <QDesktopServices>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QUrl>

#include "crash_report.hpp"
#include "crash_report_ticket.hpp"
#include "openstitch/core/log.hpp"

namespace openstitch::desktop {

namespace {

QString toQString(const std::filesystem::path& p) {
    return QString::fromStdWString(p.wstring());
}

QStringList logTail() {
    QStringList out;
    for (const std::string& l : openstitch::read_log_tail(openstitch::user_log_directory(), 30)) {
        out << QString::fromUtf8(l.data(), static_cast<qsizetype>(l.size()));
    }
    return out;
}

void runReportDialog(QWidget* parent, bool startup) {
    const auto dumpDir = crashDumpDirectory();
    const auto dump = findLatestDump(dumpDir);
    QMessageBox box(parent);
    box.setObjectName(QStringLiteral("crashReportDialog"));
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QObject::tr("Rapport de plantage"));
    box.setText(startup ? QObject::tr("Le dernier lancement s'est terminé par un plantage.")
                        : QObject::tr("Envoyer un rapport de plantage"));
    box.setInformativeText(
        QObject::tr("Un ticket GitHub prérempli (version, système, dernières lignes du journal "
                    "sans chemins de fichiers) va s'ouvrir dans votre navigateur. Rien n'est "
                    "envoyé tant que vous ne validez pas le ticket vous-même ; aucune donnée de "
                    "projet n'est incluse.\n\nDossier du minidump (à joindre manuellement si "
                    "vous le souhaitez) :\n%1")
            .arg(toQString(dumpDir)));
    QPushButton* open = box.addButton(QObject::tr("Ouvrir le ticket"), QMessageBox::AcceptRole);
    QPushButton* folder =
        box.addButton(QObject::tr("Afficher le dossier du dump"), QMessageBox::ActionRole);
    box.addButton(startup ? QObject::tr("Plus tard") : QObject::tr("Annuler"),
                  QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == open) {
        QDesktopServices::openUrl(crashReportUrl(logTail(), dump.has_value()));
    } else if (box.clickedButton() == folder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(dumpDir)));
    }
}

} // namespace

void installCrashReportAction(QMainWindow* window, QMenu* helpMenu) {
    QAction* act = helpMenu->addAction(QObject::tr("Envoyer un rapport de &plantage…"));
    act->setObjectName(QStringLiteral("action_crash_report"));
    act->setToolTip(QObject::tr("Ouvre un ticket GitHub prérempli, sans aucun envoi automatique"));
    act->setStatusTip(act->toolTip());
    QObject::connect(act, &QAction::triggered, window,
                     [window] { runReportDialog(window, false); });
}

void proposeCrashReportIfRecent(QWidget* parent) {
    const auto dump = findRecentUnseenDump(crashDumpDirectory());
    if (!dump.has_value()) {
        return;
    }
    markDumpSeen(*dump); // proposé une seule fois
    runReportDialog(parent, true);
}

} // namespace openstitch::desktop
