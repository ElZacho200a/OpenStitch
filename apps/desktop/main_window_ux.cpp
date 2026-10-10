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
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QUrl>

#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "main_window.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {

namespace {

// Version du format de `ui/windowState` : à incrémenter quand un dock ou une barre est
// ajouté/retiré, pour qu'un état enregistré par une version antérieure ne laisse pas le
// nouvel élément caché (restoreState refuse un état de version différente).
constexpr int kUiLayoutVersion = 1;

[[nodiscard]] bool isImageSuffix(const QString& suffix) {
    static const QStringList kImages{QStringLiteral("png"),  QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                     QStringLiteral("tif"),  QStringLiteral("tiff")};
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
    // Différé : ouvrir un dialogue (garde d'enregistrement, import) pendant le glisser bloquerait
    // l'application source (l'Explorateur) tant qu'il reste ouvert.
    QTimer::singleShot(0, this, [this, path] { openPath(path); });
}

// ---------------------------------------------------------------------------
// Touches : ShortcutOverride et raccourcis contextuels
// ---------------------------------------------------------------------------

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
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
            if (plain && ((letter && listLike) || (editKey && editorLike))) {
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
    }
    fitWindowToScreen();
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
    } else {
        savedLabel_->setText(
            tr("Enregistré à %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))));
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ModifiedChange) {
        updateSavedIndicator();
    }
}

} // namespace openstitch::desktop
