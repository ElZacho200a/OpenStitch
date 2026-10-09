// SPDX-License-Identifier: Apache-2.0
//
// Captures d'écran headless de l'interface (QT_QPA_PLATFORM=offscreen) pour les audits
// d'ergonomie : PAS un test CTest. Charge un projet .osp et enregistre, dans le dossier
// donné, la fenêtre principale dans plusieurs états (aucune sélection, objet auto-satin
// sélectionné, tatami sélectionné, petite fenêtre).
//
// Usage : openstitch-ui-shots <projet.osp> <dossier-sortie>
//
// Utilise le seam de test existant (`friend class MainWindowTest`, cf. main_window.hpp).
#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#include "main_window.hpp"
#include "openstitch/project_io/project_io.hpp"

namespace openstitch::desktop {

class MainWindowTest {
public:
    static int run(const char* ospPath, const QString& outDir) {
        auto loaded = project_io::load_project(ospPath);
        if (!loaded) {
            std::fprintf(stderr, "projet illisible : %s\n", loaded.error().message.c_str());
            return 1;
        }
        QDir().mkpath(outDir);
        MainWindow window;
        window.resize(1600, 1000);
        window.applyLoadedProject(*loaded);
        window.show();
        QApplication::processEvents();
        const auto shot = [&](const char* name) {
            QApplication::processEvents();
            const QString path = outDir + "/" + name + ".png";
            window.grab().save(path);
            std::printf("%s\n", path.toStdString().c_str());
        };
        shot("01-aucune-selection");

        const auto selectKind = [&](auto pred, const char* name) {
            for (const auto& emb : window.project_.embroidery_objects) {
                if (pred(emb)) {
                    window.setSelection(
                        {.region = std::nullopt, .embroidery = emb.id, .objects = {}});
                    window.updateActions();
                    shot(name);
                    return;
                }
            }
        };
        selectKind([](const auto& e) { return e.is_auto_satin(); }, "02-auto-satin-selectionne");
        selectKind([](const auto& e) { return e.is_tatami(); }, "03-tatami-selectionne");

        window.resize(1024, 640);
        QApplication::processEvents();
        shot("04-petite-fenetre-1024x640");
        return 0;
    }
};

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchShots"));
    QCoreApplication::setApplicationName(QStringLiteral("UiShots"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    if (argc < 3) {
        std::fprintf(stderr, "usage: openstitch-ui-shots <projet.osp> <dossier-sortie>\n");
        return 2;
    }
    return openstitch::desktop::MainWindowTest::run(argv[1], QString::fromLocal8Bit(argv[2]));
}
