// SPDX-License-Identifier: Apache-2.0
//
// Banc de réactivité de l'interface (headless, QT_QPA_PLATFORM=offscreen) --
// PAS un test CTest. Chronomètre ce que MainWindow recalcule après une
// mutation (refreshImage), un simple réaffichage (displayImage : sélection,
// bascule de visibilité), la couche points seule (renderStitches, chaque pas
// de simulation), et le coût de peinture de la scène. Protocole et résultats :
// docs/performance-audit.md.
//
// Usage : openstitch-bench-ui <projet.osp> [--reps N] [--quantize]
//
// Utilise le seam de test existant (`friend class MainWindowTest`, cf.
// main_window.hpp) : aucune API de production ajoutée pour ce banc.
#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;

namespace {

void measure(const char* label, int reps, const std::function<void()>& fn) {
    std::vector<double> ms;
    for (int r = 0; r < reps; ++r) {
        QElapsedTimer t;
        t.start();
        fn();
        ms.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
    }
    const double first = ms.front();
    std::vector<double> warm(ms.begin() + (reps > 1 ? 1 : 0), ms.end());
    std::sort(warm.begin(), warm.end());
    std::printf("%-44s froid %9.2f ms | chaud min %9.2f med %9.2f ms\n", label, first, warm.front(),
                warm[warm.size() / 2]);
    std::fflush(stdout);
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest {
public:
    static int run(const char* ospPath, int reps, bool quantize) {
        auto loaded = project_io::load_project(ospPath);
        if (!loaded) {
            std::fprintf(stderr, "projet illisible : %s\n", loaded.error().message.c_str());
            return 1;
        }
        if (quantize) {
            loaded->ops.push_back(image::QuantizeOp{8});
        }
        std::printf(
            "projet %s : %zu objets vectoriels, %zu objets brodés, image %dx%d, %zu op(s)\n",
            ospPath, loaded->vector_objects.size(), loaded->embroidery_objects.size(),
            loaded->original.width, loaded->original.height, loaded->ops.size());

        MainWindow window;
        window.resize(1600, 1000);
        measure("applyLoadedProject (chargement)", 1, [&] { window.applyLoadedProject(*loaded); });
        std::printf("  commandes affichées : %zu\n",
                    window.sequence_ ? window.sequence_->commands.size() : 0);

        measure("refreshImage (après toute mutation)", reps, [&] { window.refreshImage(); });
        measure("displayImage (sélection, visibilité)", reps,
                [&] { window.displayImage(window.processed_); });
        measure("renderStitches (couche points)", reps, [&] { window.renderStitches(); });

        QImage target(1600, 1000, QImage::Format_ARGB32_Premultiplied);
        measure("peinture de la scène (1600x1000)", reps, [&] {
            target.fill(Qt::white);
            QPainter painter(&target);
            window.scene_->render(&painter);
        });

        // Mutation locale typique : glisser d'une forme (commande undoable),
        // puis annulation -- chacune suivie du rafraîchissement de l'UI.
        if (!window.project_.vector_objects.empty()) {
            const ObjectId id = window.project_.vector_objects.front().id;
            measure("déplacer une forme + refreshImage", reps, [&] {
                window.undoStack_.execute(std::make_unique<commands::TranslateVectorObjectCommand>(
                                              id, Vec2um{Micrometers{100}, Micrometers{0}}),
                                          window.project_);
                window.refreshImage();
            });
            measure("annuler + refreshImage", reps, [&] {
                window.undoStack_.undo(window.project_);
                window.refreshImage();
            });
        }

        // Simulation complète : ~400 pas, chacun reconstruit la couche points
        // (cf. MainWindow::simTick).
        if (window.sequence_) {
            const int n = static_cast<int>(window.sequence_->commands.size());
            const int step = std::max(1, (n + 1) / 400);
            measure("simulation complète (~400 pas)", 1, [&] {
                for (int s = 0; s <= n; s += step) {
                    window.simStep_ = s;
                    window.renderStitches();
                }
            });
            window.simStep_ = -1;
        }
        return 0;
    }
};

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchBench"));
    QCoreApplication::setApplicationName(QStringLiteral("BenchUi"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    if (argc < 2) {
        std::fprintf(stderr, "usage: openstitch-bench-ui <projet.osp> [--reps N] [--quantize]\n");
        return 2;
    }
    int reps = 5;
    bool quantize = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc) {
            reps = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--quantize") == 0) {
            quantize = true;
        }
    }
    return openstitch::desktop::MainWindowTest::run(argv[1], reps, quantize);
}
