// SPDX-License-Identifier: Apache-2.0
// Ergonomie transversale de la fenêtre principale : ouverture par chemin et glisser-déposer,
// raccourcis dont l'activation dépend du contexte, disposition de l'interface, accessibilité
// et indicateur d'enregistrement (audit ergonomique « UX globale », docs/audit-ui-ergonomie.md).
// Aucune logique métier ici : uniquement du câblage UI.
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

#include <algorithm>

#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "main_window.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {

namespace {

// Version du format de `ui/windowState` : à incrémenter quand un dock ou une barre est
// ajouté/retiré, pour qu'un état enregistré par une version antérieure ne laisse pas le
// nouvel élément caché (restoreState refuse un état de version différente). v2 : docks en
// onglets (HP-UX-024) : un état v1 pouvait laisser un dock (Filtres) flottant hors du cadre.
constexpr int kUiLayoutVersion = 2;

[[nodiscard]] bool isImageSuffix(const QString& suffix) {
    static const QStringList kImages{QStringLiteral("png"),  QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                     QStringLiteral("tif"),  QStringLiteral("tiff"),
                                     QStringLiteral("gif"),  QStringLiteral("webp")};
    return kImages.contains(suffix);
}

// Formats de broderie machine lisibles (registre de formats : dst, pes, jef, exp).
[[nodiscard]] bool isMachineSuffix(const QString& suffix) {
    const auto* format = formats::find_format_for_extension(suffix.toStdString());
    return format != nullptr && format->can_read && format->decode != nullptr;
}

[[nodiscard]] bool isOpenableSuffix(const QString& suffix) {
    return suffix == QLatin1String("osp") || isMachineSuffix(suffix) ||
           suffix == QLatin1String("svg") || isImageSuffix(suffix);
}

} // namespace

// ---------------------------------------------------------------------------
// Ouverture par chemin / glisser-déposer
// ---------------------------------------------------------------------------

void MainWindow::openPath(const QString& path) {
    // Au lancement, la récupération d'autosave peut déjà afficher un dialogue modal : on
    // n'ouvre pas un second document par-dessus, on réessaie une fois celui-ci refermé.
    if (QApplication::activeModalWidget() != nullptr) {
        QTimer::singleShot(300, this, [this, path] { openPath(path); });
        return;
    }
    const QFileInfo info(path);
    if (!info.isFile()) {
        statusBar()->showMessage(tr("Fichier introuvable : %1").arg(info.fileName()), 8000);
        return;
    }
    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("osp")) {
        openRecentFile(
            info.absoluteFilePath()); // garde « modifications non enregistrées » + ouverture
    } else if (isMachineSuffix(suffix)) {
        importDstFile(info.absoluteFilePath());
    } else if (suffix == QLatin1String("svg") || isImageSuffix(suffix)) {
        openImageFile(info.absoluteFilePath());
    } else {
        statusBar()->showMessage(
            tr("Format non pris en charge : « .%1 ». Formats ouvrables : .osp, .dst, .svg, "
               "PNG, JPEG, BMP, TIFF.")
                .arg(suffix),
            10000);
    }
}

QString MainWindow::firstOpenableLocalFile(const QList<QUrl>& urls) {
    for (const QUrl& url : urls) {
        if (url.isLocalFile() &&
            isOpenableSuffix(QFileInfo(url.toLocalFile()).suffix().toLower())) {
            return url.toLocalFile();
        }
    }
    return {};
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() != nullptr && event->mimeData()->hasUrls() &&
        !firstOpenableLocalFile(event->mimeData()->urls()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QString path = event->mimeData() != nullptr
                             ? firstOpenableLocalFile(event->mimeData()->urls())
                             : QString();
    if (path.isEmpty()) {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    // HP-FILE-007 : Maj + dépôt d'un .osp / fichier machine = insérer dans le projet ouvert
    // plutôt que le remplacer.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if ((event->modifiers() & Qt::ShiftModifier) != 0 &&
        (suffix == QLatin1String("osp") || isMachineSuffix(suffix))) {
        QTimer::singleShot(0, this, [this, path] { (void)insertDesignFile(path); });
        return;
    }
    // Différé : ouvrir un dialogue (garde d'enregistrement, import) pendant le glisser bloquerait
    // l'application source (l'Explorateur) tant qu'il reste ouvert.
    QTimer::singleShot(0, this, [this, path] { openPath(path); });
}

// ---------------------------------------------------------------------------
// Touches : ShortcutOverride et raccourcis contextuels
// ---------------------------------------------------------------------------

namespace {

// Hauteur de fenêtre (px) en deçà de laquelle la palette d'outils passe sur deux colonnes.
constexpr int kTwoColumnPaletteMaxHeightPx = 520;
// Largeur de fenêtre (px) en deçà de laquelle le mode portable est proposé une fois.
constexpr int kPortableModeProposalMaxWidthPx = 1000;

// Répartit les actions de la palette sur une ou deux colonnes (HP-UX-026). Les actions sont
// déplacées (jamais copiées) : l'ordre est conservé, la coupure tombe de préférence sur un
// séparateur proche du milieu. La seconde barre est créée à la première demande.
void setPaletteColumns(QMainWindow* window, QToolBar* first, bool twoColumns) {
    auto* second = window->findChild<QToolBar*>(QStringLiteral("toolPalette2"));
    const bool split = first->property("paletteSplit").toBool();
    if (twoColumns == split) {
        return;
    }
    first->setProperty("paletteSplit", twoColumns);
    if (twoColumns) {
        if (second == nullptr) {
            second = new QToolBar(first->windowTitle(), window);
            second->setObjectName(QStringLiteral("toolPalette2"));
            second->setMovable(false);
            second->setIconSize(first->iconSize());
            window->addToolBarBreak(Qt::LeftToolBarArea);
            window->addToolBar(Qt::LeftToolBarArea, second);
        }
        const QList<QAction*> actions = first->actions();
        int cut = static_cast<int>(actions.size() + 1) / 2;
        for (int offset = 0; offset <= 3; ++offset) {
            const int below = cut - offset;
            const int above = cut + offset;
            if (below > 0 && actions.at(below)->isSeparator()) {
                cut = below + 1;
                break;
            }
            if (above < actions.size() && actions.at(above)->isSeparator()) {
                cut = above + 1;
                break;
            }
        }
        for (int i = cut; i < actions.size(); ++i) {
            first->removeAction(actions.at(i));
            second->addAction(actions.at(i));
        }
        second->show();
    } else if (second != nullptr) {
        const QList<QAction*> moved = second->actions();
        for (QAction* action : moved) {
            second->removeAction(action);
            first->addAction(action);
        }
        second->hide();
    }
}

} // namespace

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == this && event->type() == QEvent::Resize) {
        if (toolPalette_ != nullptr) {
            setPaletteColumns(this, toolPalette_, height() <= kTwoColumnPaletteMaxHeightPx);
        }
        // Petite fenêtre : le mode portable est proposé une seule fois (préférence mémorisée).
        auto* portable = findChild<QAction*>(QStringLiteral("action_portableMode"));
        if (isVisible() && portable != nullptr && !portable->isChecked() &&
            width() < kPortableModeProposalMaxWidthPx) {
            QSettings settings;
            if (!settings.value(QStringLiteral("ui/portableHintShown"), false).toBool()) {
                settings.setValue(QStringLiteral("ui/portableHintShown"), true);
                statusBar()->showMessage(tr("Fenêtre étroite : le menu Affichage ▸ Panneaux ▸ "
                                            "« Mode portable » rend de la place au canevas."),
                                         12000);
            }
        }
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget != nullptr && widget == QApplication::focusWidget() &&
            widget->window() == this && widget != view_ && widget != view_->viewport()) {
            const Qt::KeyboardModifiers mods = keyEvent->modifiers() & ~Qt::KeypadModifier;
            const bool plain = mods == Qt::NoModifier || mods == Qt::ShiftModifier;
            const int key = keyEvent->key();
            const bool letter = key >= Qt::Key_A && key <= Qt::Key_Z;
            const bool editKey = key == Qt::Key_Return || key == Qt::Key_Enter ||
                                 key == Qt::Key_Backspace || key == Qt::Key_Escape;
            const bool listLike = qobject_cast<QComboBox*>(widget) != nullptr ||
                                  qobject_cast<QAbstractItemView*>(widget) != nullptr;
            const bool editorLike = listLike ||
                                    qobject_cast<QAbstractSpinBox*>(widget) != nullptr ||
                                    qobject_cast<QLineEdit*>(widget) != nullptr ||
                                    qobject_cast<QTextEdit*>(widget) != nullptr;
            // Une lettre seule dans une liste ou une liste déroulante sert à la recherche par
            // frappe : elle n'active pas l'outil du même nom (« S » = colonne satin...).
            // Entrée / Retour arrière dans un contrôle d'édition valident / effacent.
            // Ctrl+K est un geste d'édition dans certains contrôles de texte :
            // ne pas lancer la palette au milieu d'une saisie.
            if ((mods == Qt::ControlModifier && key == Qt::Key_K && editorLike) ||
                (plain && ((letter && listLike) || (editKey && editorLike)))) {
                event->accept();
                return false;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateShortcutsState() {
    if (drawReturnShortcut_ == nullptr) {
        return;
    }
    // Entrée / Retour arrière : seulement pendant un tracé multi-clics ; le reste du temps ils
    // appartiennent aux champs et aux listes (valider une saisie, activer un élément).
    // Échap reste armé en permanence : il sert aussi à abandonner un geste du canevas (cadre
    // élastique, glisser) ; il est rendu aux champs d'édition par le filtre ShortcutOverride.
    const bool drawing = finishDrawAct_ != nullptr && finishDrawAct_->isEnabled();
    drawReturnShortcut_->setEnabled(drawing);
    drawEnterShortcut_->setEnabled(drawing);
    drawBackspaceShortcut_->setEnabled(drawing);
}

// ---------------------------------------------------------------------------
// Disposition de l'interface
// ---------------------------------------------------------------------------

void MainWindow::fitWindowToScreen(double fraction) {
    const QScreen* sc = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (sc == nullptr || isMaximized() || isFullScreen()) {
        return;
    }
    const QRect avail = sc->availableGeometry();
    const QSize limit(static_cast<int>(avail.width() * fraction),
                      static_cast<int>(avail.height() * fraction));
    if (width() > limit.width() || height() > limit.height()) {
        resize(size().boundedTo(limit));
    }
    // Une position enregistrée sur un écran débranché ne doit pas laisser la fenêtre hors de vue.
    if (isVisible() && !avail.intersects(frameGeometry())) {
        move(avail.center() - rect().center());
    }
}

void MainWindow::saveUiLayout() {
    // Le mode « Masquer les panneaux » cache tous les docks : enregistré tel quel, le prochain
    // lancement démarrerait sans aucun panneau. On les ré-affiche (la bascule restaure ceux qui
    // étaient visibles) avant de capturer l'état.
    if (hidePanelsAct_ != nullptr && hidePanelsAct_->isChecked()) {
        hidePanelsAct_->setChecked(false);
    }
    // QSettings() par défaut : lit l'organisation/application déjà posées sur QCoreApplication
    // (main.cpp). Un test peut ainsi rediriger vers un fichier temporaire sans toucher au
    // registre réel de l'utilisateur.
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/windowState"), saveState(kUiLayoutVersion));
    if (const auto* portable = findChild<QAction*>(QStringLiteral("action_portableMode"))) {
        s.setValue(QStringLiteral("ui/portableMode"), portable->isChecked());
    }
}

void MainWindow::restoreUiLayout() {
    QSettings s;
    if (s.contains(QStringLiteral("ui/geometry"))) {
        restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    }
    if (s.contains(QStringLiteral("ui/windowState"))) {
        const QByteArray state = s.value(QStringLiteral("ui/windowState")).toByteArray();
        // Version courante, sinon format historique (sans version) ; en dernier recours,
        // la disposition par défaut plutôt qu'un état corrompu ou périmé.
        const bool ok = restoreState(state, kUiLayoutVersion) || restoreState(state);
        if (!ok && !defaultWindowState_.isEmpty()) {
            restoreState(defaultWindowState_);
        }
        // L'état restauré fixe déjà la place des panneaux : seule la case du menu est mise à
        // jour, sans rejouer le déplacement.
        if (auto* portable = findChild<QAction*>(QStringLiteral("action_portableMode"))) {
            const QSignalBlocker blocker(portable);
            portable->setChecked(ok && s.value(QStringLiteral("ui/portableMode")).toBool());
        }
    }
    fitWindowToScreen();
    // Un dock flottant mémorisé sur un écran débranché ou plus petit (dock Fils ouvert, par
    // exemple) ne doit jamais démarrer hors de vue ni plus grand que l'écran (HP-UX-024).
    const QScreen* sc = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (sc != nullptr) {
        const QRect avail = sc->availableGeometry();
        for (QDockWidget* dock : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly)) {
            if (!dock->isFloating()) {
                continue;
            }
            QRect frame = dock->frameGeometry();
            frame.setSize(frame.size().boundedTo(avail.size()));
            if (!avail.contains(frame)) {
                const int maxLeft = avail.left() + avail.width() - frame.width();
                const int maxTop = avail.top() + avail.height() - frame.height();
                frame.moveTopLeft(QPoint(std::clamp(frame.left(), avail.left(), maxLeft),
                                         std::clamp(frame.top(), avail.top(), maxTop)));
            }
            dock->setGeometry(frame);
        }
    }
}

// ---------------------------------------------------------------------------
// Espaces de travail non destructifs (HP-UX-049)
// ---------------------------------------------------------------------------

void MainWindow::buildWorkspacesMenu() {
    if (panelsMenu_ == nullptr) {
        return;
    }
    QMenu* menu = panelsMenu_->addMenu(tr("Espaces de travail"));
    menu->setObjectName(QStringLiteral("menu_workspaces"));

    // Les presets agissent uniquement sur la disposition de l'IHM,
    // jamais sur la sélection, les calques ou le contenu du projet.
    const auto apply = [this](int mode) {
        QSettings settings;
        const QString previousKey = QStringLiteral("ui/workspaces/previous");
        if (!settings.contains(previousKey)) {
            settings.setValue(previousKey, saveState(kUiLayoutVersion));
        }
        const QList<QDockWidget*> docks{documentDock_, propertiesDock_, workflowDock_,
                                        orderDock_,    filterDock_,     analysisDock_,
                                        historyDock_,  threadDock_,     colorsDock_};
        QList<QDockWidget*> visible{documentDock_};
        QDockWidget* front = nullptr;
        QString presetName;
        switch (mode) {
        case 0: // Numériser
            visible << propertiesDock_ << workflowDock_ << colorsDock_;
            front = propertiesDock_;
            presetName = tr("Numériser");
            break;
        case 1: // Retoucher
            visible << propertiesDock_ << historyDock_;
            front = propertiesDock_;
            presetName = tr("Retoucher");
            break;
        case 2: // Points
            visible << propertiesDock_ << orderDock_ << threadDock_;
            front = propertiesDock_;
            presetName = tr("Points");
            break;
        case 3: // Vérifier / Exporter
            visible << orderDock_ << analysisDock_ << threadDock_;
            front = analysisDock_;
            presetName = tr("Vérifier / Exporter");
            break;
        default:
            return;
        }
        for (QDockWidget* dock : docks) {
            if (dock != nullptr) {
                dock->setVisible(visible.contains(dock));
            }
        }
        if (front != nullptr) {
            front->raise();
        }
        settings.setValue(QStringLiteral("ui/workspaces/active"), mode);
        statusBar()->showMessage(tr("Espace de travail activé : %1").arg(presetName), 4000);
    };

    const auto addPreset = [this, menu, &apply](const QString& name, const char* objectName,
                                                int mode) {
        QAction* action = menu->addAction(name);
        action->setObjectName(QLatin1String(objectName));
        action->setStatusTip(tr("Adapte les panneaux au travail, sans modifier le motif."));
        connect(action, &QAction::triggered, this, [apply, mode] { apply(mode); });
    };
    addPreset(tr("Numériser"), "action_workspaceDigitize", 0);
    addPreset(tr("Retoucher"), "action_workspaceRetouch", 1);
    addPreset(tr("Points"), "action_workspaceStitches", 2);
    addPreset(tr("Vérifier / Exporter"), "action_workspaceQuality", 3);
    menu->addSeparator();

    QAction* restore = menu->addAction(tr("Restaurer la disposition précédente"));
    restore->setObjectName(QStringLiteral("action_workspaceRestore"));
    restore->setStatusTip(tr("Revient aux panneaux avant le premier changement d'espace."));
    connect(restore, &QAction::triggered, this, [this] {
        QSettings settings;
        const QString key = QStringLiteral("ui/workspaces/previous");
        const QByteArray previous = settings.value(key).toByteArray();
        if (previous.isEmpty()) {
            statusBar()->showMessage(tr("Aucune disposition précédente enregistrée."), 4000);
            return;
        }
        if (!restoreState(previous, kUiLayoutVersion)) {
            statusBar()->showMessage(tr("Disposition précédente incompatible."), 4000);
            return;
        }
        settings.remove(key);
        settings.remove(QStringLiteral("ui/workspaces/active"));
        statusBar()->showMessage(tr("Disposition précédente restaurée."), 4000);
    });
}

// ---------------------------------------------------------------------------
// Accessibilité, ordre de tabulation, indicateur d'enregistrement
// ---------------------------------------------------------------------------

void MainWindow::applyAccessibility() {
    if (polygonSidesSpin_ != nullptr) {
        polygonSidesSpin_->setAccessibleName(tr("Nombre de côtés du polygone régulier"));
    }
    // Le canevas est atteignable au clavier (Tab) et laisse le dépôt de fichiers à la fenêtre.
    view_->setFocusPolicy(Qt::StrongFocus);
    view_->setAcceptDrops(false);
    view_->viewport()->setAcceptDrops(false);
    if (analysisList_ != nullptr) {
        analysisList_->setAccessibleName(tr("Problèmes détectés"));
    }
    // Ordre de tabulation : canevas, puis les panneaux dans l'ordre de lecture.
    QWidget* previous = view_;
    for (QWidget* w : std::initializer_list<QWidget*>{
             static_cast<QWidget*>(documentPanel_), static_cast<QWidget*>(propertiesPanel_),
             static_cast<QWidget*>(analysisDock_ != nullptr ? analysisDock_->widget() : nullptr)}) {
        if (w != nullptr) {
            setTabOrder(previous, w);
            previous = w;
        }
    }
}

void MainWindow::updateSavedIndicator() {
    if (savedLabel_ == nullptr) {
        return;
    }
    if (isWindowModified()) {
        savedLabel_->setText(tr("Non enregistré"));
    } else if (currentProjectPath_.isEmpty()) {
        savedLabel_->setText(tr("Nouveau document"));
    } else if (!savedAt_.isValid()) {
        // Un projet ouvert depuis le disque n'a pas été « enregistré à l'heure
        // courante » par cette session. Ne pas inventer une heure de sauvegarde.
        savedLabel_->setText(tr("Projet ouvert"));
    } else {
        savedLabel_->setText(tr("Enregistré à %1").arg(savedAt_.toString(QStringLiteral("HH:mm"))));
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ModifiedChange) {
        updateSavedIndicator();
    }
}

} // namespace openstitch::desktop
