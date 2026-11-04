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
        window.grab().save(parser.value(shot));
        return 0;
    }
    return app.exec();
}
