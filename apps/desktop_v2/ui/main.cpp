// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <filesystem>

#include "openstitch/core/app_info.hpp"
#include "shell_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("OpenStitch"));
    QApplication::setApplicationName(QStringLiteral("OpenStitch Desktop V2"));
    QApplication::setApplicationVersion(QString::fromUtf8(openstitch::kAppVersion));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenStitch Desktop V2 (aperçu technique)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("fichier"),
                                 QStringLiteral("Image ou projet .osp à ouvrir."));
    QCommandLineOption height(QStringList{QStringLiteral("height-mm")},
                              QStringLiteral("Hauteur visée du motif (mm)."), QStringLiteral("mm"),
                              QStringLiteral("0"));
    parser.addOption(height);
    // Recette visuelle reproductible : ouvre le fichier, attend la fin des calculs, enregistre une
    // capture de la fenêtre puis quitte (utilisable avec QT_QPA_PLATFORM=offscreen).
    QCommandLineOption shot(QStringList{QStringLiteral("screenshot")},
                            QStringLiteral("Enregistre une capture PNG puis quitte."),
                            QStringLiteral("png"));
    parser.addOption(shot);
    QCommandLineOption shotSize(QStringList{QStringLiteral("screenshot-size")},
                                QStringLiteral("Dimensions de la capture (largeur x hauteur)."),
                                QStringLiteral("WxH"), QStringLiteral("1280x720"));
    parser.addOption(shotSize);
    QCommandLineOption select(QStringList{QStringLiteral("select-objects")},
                              QStringLiteral("Recette : sélectionne les N premiers objets."),
                              QStringLiteral("n"), QStringLiteral("0"));
    parser.addOption(select);
    QCommandLineOption fresh(QStringList{QStringLiteral("new")},
                             QStringLiteral("Démarre sur un projet vierge."));
    parser.addOption(fresh);
    QCommandLineOption demo(
        QStringList{QStringLiteral("demo-shapes")},
        QStringLiteral("Recette : dessine un rectangle, une ellipse et un polygone."));
    parser.addOption(demo);
    QCommandLineOption tool(QStringList{QStringLiteral("tool")},
                            QStringLiteral("Recette : active l'outil d'identifiant donné."),
                            QStringLiteral("id"));
    parser.addOption(tool);
    QCommandLineOption size(QStringList{QStringLiteral("size")},
                            QStringLiteral("Recette : taille de la fenêtre, LARGEURxHAUTEUR."),
                            QStringLiteral("LxH"));
    parser.addOption(size);
    QCommandLineOption theme(QStringList{QStringLiteral("theme")},
                             QStringLiteral("Recette : thème light, dark ou system."),
                             QStringLiteral("mode"));
    parser.addOption(theme);
    QCommandLineOption workspace(
        QStringList{QStringLiteral("workspace")},
        QStringLiteral("Recette : espace design, digitize, zones, optimize ou produce."),
        QStringLiteral("id"));
    parser.addOption(workspace);
    QCommandLineOption command(QStringList{QStringLiteral("command")},
                               QStringLiteral("Recette : exécute une commande de la palette."),
                               QStringLiteral("id"));
    parser.addOption(command);
    parser.process(app);

    openstitch::desktop_v2::ShellWindow window;
    if (parser.isSet(size)) {
        const auto parts = parser.value(size).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts[0].toInt() > 0 && parts[1].toInt() > 0) {
            window.resize(parts[0].toInt(), parts[1].toInt());
        }
    }
    if (parser.isSet(shot)) {
        const QStringList dimensions = parser.value(shotSize).split(QLatin1Char('x'));
        if (dimensions.size() != 2) {
            return 2;
        }
        bool widthOk = false;
        bool heightOk = false;
        const int width = dimensions[0].toInt(&widthOk);
        const int heightPx = dimensions[1].toInt(&heightOk);
        if (!widthOk || !heightOk || width < 640 || heightPx < 480) {
            return 2;
        }
        // `--size` (recette libre) l'emporte sur la taille de capture par défaut de `main`.
        if (!parser.isSet(size)) {
            window.resize(width, heightPx);
        }
    }
    window.show();
    // Une capture ne doit jamais modifier les préférences ni la récupération de l'utilisateur.
    QTemporaryDir recipeDir;
    const auto stateDir = parser.isSet(shot) && recipeDir.isValid()
                              ? std::filesystem::path(recipeDir.path().toStdU16String())
                              : std::filesystem::path(QStandardPaths::writableLocation(
                                                          QStandardPaths::AppLocalDataLocation)
                                                          .toStdU16String());
    window.setStateDirectory(stateDir);
    if (window.hasRecovery() && parser.positionalArguments().isEmpty() && !parser.isSet(shot)) {
        const auto answer = QMessageBox::question(
            &window, QObject::tr("Récupération"),
            QObject::tr("Une session précédente s'est terminée sans enregistrer. "
                        "Récupérer le document ?"));
        if (answer == QMessageBox::Yes) {
            (void)window.recoverAutosave();
        } else {
            window.discardRecovery();
        }
    }
    if (!parser.positionalArguments().isEmpty()) {
        const QString file = parser.positionalArguments().front();
        const std::filesystem::path path(file.toStdU16String());
        if (file.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive)) {
            window.openProject(path);
        } else {
            window.openImage(path, parser.value(height).toDouble());
        }
    }
    if (parser.isSet(fresh) || parser.isSet(demo)) {
        window.newProject();
    }
    if (parser.isSet(demo)) {
        using namespace openstitch::desktop_v2;
        auto& session = window.session();
        (void)session.applyBatch(batchAddRect({-20, 10}, 30, 20), "Rectangle");
        (void)session.applyBatch(batchAddEllipse({18, 12}, 26, 26), "Ellipse");
        (void)session.applyBatch(
            batchAddPolygon({{-30, -30}, {0, -12}, {30, -30}, {10, -8}, {-10, -8}}), "Polygone");
    }
    if (parser.isSet(theme)) {
        window.setThemeMode(parser.value(theme));
    }
    if (parser.isSet(workspace)) {
        window.waitForIdle();
        (void)window.runCommand(QStringLiteral("workspace.") + parser.value(workspace));
    }
    if (parser.isSet(command)) {
        window.waitForIdle();
        (void)window.runCommand(parser.value(command));
    }
    if (parser.isSet(tool)) {
        window.waitForIdle();
        (void)window.runCommand(QStringLiteral("tool.") + parser.value(tool));
    }
    if (parser.isSet(shot)) {
        window.waitForIdle();
        std::vector<openstitch::desktop_v2::Target> picked;
        const auto& objects = window.session().project().embroidery_objects;
        for (int i = 0; i < parser.value(select).toInt() && i < static_cast<int>(objects.size());
             ++i) {
            picked.push_back({openstitch::desktop_v2::TargetKind::EmbroideryObject,
                              objects[static_cast<std::size_t>(i)].id.value});
        }
        window.session().selection().replace(std::move(picked));
        return window.grab().save(parser.value(shot)) ? 0 : 1;
    }
    return app.exec();
}
