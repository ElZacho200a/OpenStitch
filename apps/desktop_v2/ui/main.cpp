// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QCommandLineParser>
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
    parser.process(app);

    openstitch::desktop_v2::ShellWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        const QString file = parser.positionalArguments().front();
        const std::filesystem::path path(file.toStdU16String());
        if (file.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive)) {
            window.openProject(path);
        } else {
            window.openImage(path, parser.value(height).toDouble());
        }
    }
    if (parser.isSet(shot)) {
        window.waitForIdle();
        window.grab().save(parser.value(shot));
        return 0;
    }
    return app.exec();
}
