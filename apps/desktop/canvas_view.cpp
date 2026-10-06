// SPDX-License-Identifier: Apache-2.0
#include "canvas_view.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRubberBand>
#include <QScrollBar>
#include <QTextEdit>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"

namespace openstitch::desktop {

namespace {
constexpr double kZoomStep = 1.15;
constexpr double kMinPxPerMm = 0.2;    // motif de 1 m visible en entier
constexpr double kMaxPxPerMm = 400.0;  // 0,1 mm = 40 px : largement assez fin
constexpr double kMaxWheelSteps = 3.0; // plafonne un coup de molette violent
constexpr double kScrollPxPerNotch = 60.0;
constexpr double kZoomDragPerPixel = 0.01;
constexpr Qt::KeyboardModifiers kRelevantMods =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

// Modificateurs « effectifs » d'un évènement clavier : Qt 6.4/6.8 ne garantissent
// pas que l'appui sur une touche modificatrice la porte déjà dans modifiers().
Qt::KeyboardModifiers effectiveMods(const QKeyEvent* e) {
    Qt::KeyboardModifiers m = e->modifiers() & kRelevantMods;
    Qt::KeyboardModifier flag = Qt::NoModifier;
    switch (e->key()) {
    case Qt::Key_Shift:
        flag = Qt::ShiftModifier;
        break;
    case Qt::Key_Control:
        flag = Qt::ControlModifier;
        break;
    case Qt::Key_Alt:
        flag = Qt::AltModifier;
        break;
    case Qt::Key_Meta:
        flag = Qt::MetaModifier;
        break;
    default:
        return m;
    }
    return e->type() == QEvent::KeyPress ? (m | flag) : (m & ~Qt::KeyboardModifiers(flag));
}

bool isTextInput(const QWidget* w) {
    if (w == nullptr) {
        return false;
    }
    if (qobject_cast<const QLineEdit*>(w) != nullptr ||
        qobject_cast<const QAbstractSpinBox*>(w) != nullptr ||
        qobject_cast<const QTextEdit*>(w) != nullptr ||
        qobject_cast<const QPlainTextEdit*>(w) != nullptr) {
        return true;
    }
    const auto* combo = qobject_cast<const QComboBox*>(w);
    return combo != nullptr && combo->isEditable();
}

// Cadre élastique de sélection : double trait (halo + trait) et remplissage
// léger, couleurs issues des tokens (jamais codées en dur).
class SelectionBand : public QRubberBand {
public:
    explicit SelectionBand(QWidget* parent) : QRubberBand(QRubberBand::Rectangle, parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        const auto& t = AppTheme::instance().tokens();
        QPainter p(this);
        QColor fill = t.canvasSelectionRectLine;
        fill.setAlpha(36);
        p.fillRect(rect(), fill);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.canvasSelectionRectHalo, 3));
        p.drawRect(r);
        p.setPen(QPen(t.canvasSelectionRectLine, 1, Qt::DashLine));
        p.drawRect(r);
    }
};

// Curseur de modificateur 24x24 dessiné par code : flèche + symbole.
// `dpr` = devicePixelRatioF du viewport (écrans HiDPI : pixmap 2x, hotspot logique).
QCursor makeModifierCursor(QChar symbol, qreal dpr) {
    const auto& t = AppTheme::instance().tokens();
    QPixmap pm(QSize(24, 24) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPolygonF arrow;
    arrow << QPointF(1, 1) << QPointF(1, 15) << QPointF(5, 11) << QPointF(8, 17) << QPointF(10, 16)
          << QPointF(7, 10) << QPointF(12, 10);
    p.setPen(QPen(t.canvasSelectionRectHalo, 2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(arrow);
    p.setPen(QPen(t.canvasSelectionRectLine, 1));
    p.setBrush(t.canvasSelectionRectHalo);
    p.drawPolygon(arrow);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(t.canvasSelectionRectLine);
    p.drawText(QRect(12, 10, 12, 14), Qt::AlignCenter, QString(symbol));
    p.end();
    return QCursor(pm, 1, 1);
}

// Plus petit pas « rond » (en mm) dont la taille à l'écran atteint minPixels.
double niceStepMm(double pxPerMm, double minPixels) {
    static constexpr double steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (const double s : steps) {
        if (s * pxPerMm >= minPixels) {
            return s;
        }
    }
    return 1000.0;
}
} // namespace

// Filtre applicatif : Espace (panoramique) et Alt seul. Pas de setFocus dans
// enterEvent : le filtre ne réagit que si le curseur est sur le viewport et que
// le focus n'est pas dans un champ de saisie.
class SpaceKeyFilter : public QObject {
public:
    explicit SpaceKeyFilter(CanvasView* view) : QObject(view), view_(view) {}

    bool eventFilter(QObject* /*watched*/, QEvent* event) override {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
            return false;
        }
        return view_->filterKey(static_cast<QKeyEvent*>(event));
    }

private:
    CanvasView* view_;
};

CanvasView::CanvasView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor); // zoomAt() ancre à la main
    setFrameShape(QFrame::NoFrame);                   // le viewport s'aligne avec les règles
    setMouseTracking(true);
    // Sans focus, keyPressEvent ne reçoit jamais les flèches (nudge clavier)
    // -- StrongFocus inclut le focus au clic, déjà le geste naturel pour
    // sélectionner une forme avant de la déplacer au clavier.
    setFocusPolicy(Qt::StrongFocus);
    // Rendu de gros motifs : ne pas sauvegarder l'état du peintre entre items,
    // et ne repeindre que la zone modifiée plutôt que tout le viewport.
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);
    setBackgroundBrush(AppTheme::instance().tokens().canvasBackground);
    // Rejoue le fond et le contenu quand le thème change.
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] {
        setBackgroundBrush(AppTheme::instance().tokens().canvasBackground);
        resetCachedContent();
        viewport()->update();
        // Les pixmaps de curseur (+, ±) sont recolorés par les tokens : on force
        // leur reconstruction.
        cursorKind_ = -1;
        refreshCursor();
    });

    // Note : fromScenePoint/toScenePoint (paramètres du signal) accusent un
    // retard d'une étape sur viewportRect — ils reflètent la position du
    // glisser précédent, pas la courante (constaté par test, invisible en
    // usage réel où les mouvements sont pixel à pixel, mais faux pour un
    // glisser rapide à peu d'évènements). On reconvertit donc viewportRect
    // lui-même, toujours à jour.
    connect(this, &QGraphicsView::rubberBandChanged, this, [this](QRect viewportRect) {
        if (!viewportRect.isNull()) {
            lastRubberBandMm_ = mapToScene(viewportRect).boundingRect();
        }
    });

    longPressTimer_.setSingleShot(true);
    connect(&longPressTimer_, &QTimer::timeout, this, &CanvasView::fireLongPress);
    if (QCoreApplication::instance() != nullptr) {
        spaceFilter_ = new SpaceKeyFilter(this);
        QCoreApplication::instance()->installEventFilter(spaceFilter_);
    }
}

CanvasView::~CanvasView() {
    if (spaceFilter_ != nullptr) {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->removeEventFilter(spaceFilter_);
        }
        delete spaceFilter_;
        spaceFilter_ = nullptr;
    }
}

Context CanvasView::currentContext() const {
    if (cropMode_) {
        return Context::Crop;
    }
    if (boxDrawMode_) {
        return Context::DrawBox;
    }
    if (freeformDrawMode_) {
        return Context::DrawFreeform;
    }
    if (bezierDrawMode_) {
        return Context::DrawBezier;
    }
    if (polygonDrawMode_ || satinPairDrawMode_) {
        return Context::DrawClicks;
    }
    return baseContext_;
}

void CanvasView::setBaseContext(Context context) {
    baseContext_ = context;
    inputModelEnabled_ = true;
    cancelSelectionPress();
    updateDragMode();
    refreshCursor();
}

void CanvasView::setSelectionRectangleEnabled(bool enabled) {
    selectionRectEnabled_ = enabled;
    if (!enabled) {
        cancelSelectionPress();
    }
}

void CanvasView::applyModeCursor(Qt::CursorShape shape) {
    // Un curseur de mode explicite remplace tout curseur transitoire.
    transientCursor_ = false;
    cursorKind_ = 0;
    setCursor(shape);
    if (viewport() != nullptr) {
        viewport()->setCursor(shape);
    }
    refreshCursor();
}

// Recalcule dragMode() à partir de TOUS les booléens de mode courants (pas
// seulement celui qu'on vient de changer) : défaut trouvé par revue (retour
// utilisateur : rectangle/ellipse "ne fonctionnent pas au clic-glisser").
// MainWindow::setTool() appelle les six méthodes set*Mode ci-dessous à la
// suite, une seule à `true` (l'outil actif) et les cinq autres à `false` --
// quand chacune fixait dragMode() en ne regardant QUE son propre booléen
// (ex. `setDragMode(enabled ? RubberBandDrag : ScrollHandDrag)`), la
// dernière méthode appelée écrasait systématiquement le mode posé par les
// précédentes, quel que soit l'outil réellement actif (setBoxDrawMode(true)
// posait RubberBandDrag, puis setPolygonDrawMode(false) l'écrasait aussitôt
// en ScrollHandDrag). Aucun test ne l'a repéré car tous injectent
// directement le signal Qt final (boxDrawnMm, etc.), jamais la mécanique
// souris réelle bout en bout. En recalculant depuis l'état COMPLET à chaque
// appel, l'ordre d'appel n'a plus d'importance et chaque méthode reste
// correcte utilisée seule (comme le font les tests de CanvasView isolé).
void CanvasView::updateDragMode() {
    if (cropMode_ || boxDrawMode_) {
        setDragMode(QGraphicsView::RubberBandDrag);
    } else if (polygonDrawMode_ || freeformDrawMode_ || satinPairDrawMode_ || bezierDrawMode_) {
        setDragMode(QGraphicsView::NoDrag);
    } else {
        // Vue isolée (aucun setBaseContext) : comportement historique.
        // Modèle activé : NoDrag (panoramique = clic molette / Espace / outil Pan), SAUF en
        // édition de nœuds / de points où glisser dans le vide déplace toujours la vue
        // (comportement historique : pas de rectangle de sélection dans ces contextes).
        const bool editContext =
            baseContext_ == Context::NodeEdit || baseContext_ == Context::StitchEdit;
        setDragMode(inputModelEnabled_ && !editContext ? QGraphicsView::NoDrag
                                                       : QGraphicsView::ScrollHandDrag);
    }
}

void CanvasView::setCropMode(bool enabled) {
    cropMode_ = enabled;
    lastRubberBandMm_ = QRectF();
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setBoxDrawMode(bool enabled) {
    boxDrawMode_ = enabled;
    lastRubberBandMm_ = QRectF();
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setPolygonDrawMode(bool enabled) {
    polygonDrawMode_ = enabled;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setFreeformDrawMode(bool enabled) {
    freeformDrawMode_ = enabled;
    freeformActive_ = false;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setSatinPairDrawMode(bool enabled) {
    satinPairDrawMode_ = enabled;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setBezierDrawMode(bool enabled) {
    bezierDrawMode_ = enabled;
    bezierPressActive_ = false;
    updateDragMode();
    applyModeCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasView::setCanvasSizeMm(QSizeF sizeMm) {
    canvasMm_ = sizeMm;
    // Marge de travail autour du canevas : la moitié de sa taille de chaque côté.
    const QRectF canvas(-sizeMm.width() / 2.0, -sizeMm.height() / 2.0, sizeMm.width(),
                        sizeMm.height());
    scene()->setSceneRect(canvas.adjusted(-sizeMm.width() / 2.0, -sizeMm.height() / 2.0,
                                          sizeMm.width() / 2.0, sizeMm.height() / 2.0));
    viewport()->update();
    emit viewChanged();
}

double CanvasView::pixelsPerMm() const {
    return transform().m11();
}

void CanvasView::zoomIn() {
    applyCenterZoom(kZoomStep);
}

void CanvasView::zoomOut() {
    applyCenterZoom(1.0 / kZoomStep);
}

void CanvasView::fitCanvas() {
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    fitInView(canvas.adjusted(-5, -5, 5, 5), Qt::KeepAspectRatio);
    emit viewChanged();
}

void CanvasView::applyCenterZoom(double factor) {
    const double current = pixelsPerMm();
    const double target = std::clamp(current * factor, kMinPxPerMm, kMaxPxPerMm);
    factor = target / current;
    if (factor == 1.0) {
        return;
    }
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    emit viewChanged();
}

void CanvasView::zoomAt(double factor, QPointF viewportPos) {
    if (!(factor > 0.0)) {
        return;
    }
    const double current = pixelsPerMm();
    const double target = std::clamp(current * factor, kMinPxPerMm, kMaxPxPerMm);
    factor = target / current;
    if (factor == 1.0) {
        return;
    }
    bool invertible = false;
    const QTransform inv = viewportTransform().inverted(&invertible);
    if (!invertible) {
        return;
    }
    const QPointF scenePos = inv.map(viewportPos);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(factor, factor);
    // Le point de scène doit revenir sous `viewportPos` : on compense par les barres.
    const QPointF after = viewportTransform().map(scenePos);
    const QPointF shift = after - viewportPos;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() +
                                    static_cast<int>(std::lround(shift.x())));
    verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                  static_cast<int>(std::lround(shift.y())));
    emit viewChanged();
}

void CanvasView::scrollBy(int dx, int dy) {
    if (dx != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    }
    if (dy != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
    }
}

void CanvasView::wheelEvent(QWheelEvent* event) {
    updateModifiers(event->modifiers());
    const Qt::KeyboardModifiers mods = event->modifiers() & kRelevantMods;
    const QPoint pixel = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    event->accept();
    if ((mods & Qt::AltModifier) != 0) {
        altUsedInGesture_ = true;
    }
    Gesture g{GestureKind::Wheel, Qt::NoButton, mods, Qt::Key(0)};
    if (!pixel.isNull() && mods == Qt::KeyboardModifiers{}) {
        g.kind = GestureKind::PixelScroll;
    }
    auto intent = InteractionMap::resolve(currentContext(), g);
    if (!intent && (mods & Qt::AltModifier) == 0) {
        // Ctrl+Maj+molette, Meta+molette... : non définis par la table mais zoomaient
        // avant L5 ; on ne régresse pas silencieusement. (Alt + autre modificateur :
        // ignoré, comme Alt+Maj/Ctrl + clic.)
        intent = Intent::ZoomAtCursor;
    }
    if (!intent) {
        return;
    }
    // Qt échange x/y avec Alt sur certaines plates-formes : repli sur x.
    const int delta = angle.y() != 0 ? angle.y() : angle.x();
    switch (*intent) {
    case Intent::ZoomAtCursor: {
        if (angle.y() == 0) {
            break;
        }
        // Deltas fractionnaires (< 120, pavés tactiles Windows) lissés, plafonnés.
        const double steps = std::clamp(angle.y() / 120.0, -kMaxWheelSteps, kMaxWheelSteps);
        zoomAt(std::pow(kZoomStep, steps), event->position());
        break;
    }
    case Intent::ScrollHorizontal:
        scrollBy(-static_cast<int>(std::lround(delta / 120.0 * kScrollPxPerNotch)), 0);
        break;
    case Intent::ScrollVertical:
        scrollBy(0, -static_cast<int>(std::lround(delta / 120.0 * kScrollPxPerNotch)));
        break;
    case Intent::PanView:
        scrollBy(-pixel.x(), -pixel.y());
        break;
    default:
        break;
    }
}

bool CanvasView::viewportEvent(QEvent* event) {
    switch (event->type()) {
    case QEvent::Enter:
        cursorOverViewport_ = true;
        break;
    case QEvent::Leave:
        // Le curseur quitte le viewport : le cache de modificateurs (et donc le
        // curseur « + » / « ± ») ne doit pas survivre à l'état clavier qu'on ne voit plus.
        cursorOverViewport_ = false;
        updateModifiers(Qt::NoModifier);
        emit cursorLeftViewport();
        break;
    case QEvent::NativeGesture: {
        const auto* g = static_cast<QNativeGestureEvent*>(event);
        switch (g->gestureType()) {
        case Qt::ZoomNativeGesture:
            zoomAt(1.0 + g->value(), g->position());
            break;
        case Qt::PanNativeGesture:
            scrollBy(-static_cast<int>(std::lround(g->delta().x())),
                     -static_cast<int>(std::lround(g->delta().y())));
            break;
        case Qt::SmartZoomNativeGesture:
            fitCanvas();
            break;
        default:
            return QGraphicsView::viewportEvent(event);
        }
        event->accept();
        return true;
    }
    default:
        break;
    }
    return QGraphicsView::viewportEvent(event);
}

// --- états transitoires : modificateurs, curseur, panoramique -------------------

void CanvasView::updateModifiers(Qt::KeyboardModifiers mods) {
    mods &= kRelevantMods;
    if (mods == lastModifiers_) {
        return;
    }
    lastModifiers_ = mods;
    refreshCursor();
    emit modifiersChanged(mods);
}

void CanvasView::refreshCursor() {
    if (viewport() == nullptr) {
        return;
    }
    // 1 main fermée, 2 main ouverte, 3 « + », 4 « ± », 5 copie.
    int kind = 0;
    const Context ctx = currentContext();
    if (panning_ || zoomDragging_) {
        kind = 1;
    } else if (spaceHeld_ || (inputModelEnabled_ && ctx == Context::Pan)) {
        kind = 2;
    } else if (inputModelEnabled_ && ctx == Context::Select && selectionRectEnabled_) {
        const bool shift = (lastModifiers_ & Qt::ShiftModifier) != 0;
        const bool ctrl = (lastModifiers_ & Qt::ControlModifier) != 0;
        kind = ctrl ? 4 : (shift ? 3 : 0);
    } else if (inputModelEnabled_ && ctx == Context::Move &&
               (lastModifiers_ & Qt::AltModifier) != 0) {
        kind = 5;
    }
    if (kind == cursorKind_) {
        return;
    }
    cursorKind_ = kind;
    if (kind == 0) {
        if (transientCursor_) {
            transientCursor_ = false;
            if (hadViewportCursor_) {
                viewport()->setCursor(savedViewportCursor_);
            } else {
                viewport()->unsetCursor();
            }
        }
        return;
    }
    if (!transientCursor_) {
        transientCursor_ = true;
        hadViewportCursor_ = viewport()->testAttribute(Qt::WA_SetCursor);
        savedViewportCursor_ = viewport()->cursor();
    }
    switch (kind) {
    case 1:
        viewport()->setCursor(Qt::ClosedHandCursor);
        break;
    case 2:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;
    case 3:
        viewport()->setCursor(makeModifierCursor(QChar(u'+'), viewport()->devicePixelRatioF()));
        break;
    case 4:
        viewport()->setCursor(makeModifierCursor(QChar(0x00B1), viewport()->devicePixelRatioF()));
        break;
    default:
        viewport()->setCursor(Qt::DragCopyCursor);
        break;
    }
}

void CanvasView::setSpaceHeld(bool held) {
    if (spaceHeld_ == held) {
        return;
    }
    spaceHeld_ = held;
    refreshCursor();
}

void CanvasView::resetTransientInput(bool cursorLeft) {
    if (cursorLeft) {
        cursorOverViewport_ = false;
    }
    middleDoubleClickPending_ = false;
    spaceConsumed_ = false;
    setSpaceHeld(false);
    cancelSelectionPress();
    if (panning_ || zoomDragging_) {
        endGesture();
    }
}

void CanvasView::startPan(const QPoint& viewportPos, Qt::MouseButton button) {
    panning_ = true;
    zoomDragging_ = false;
    gestureButton_ = button;
    gestureLastPos_ = viewportPos;
    refreshCursor();
}

void CanvasView::startZoomDrag(const QPoint& viewportPos, Qt::MouseButton button) {
    zoomDragging_ = true;
    panning_ = false;
    gestureButton_ = button;
    gestureLastPos_ = viewportPos;
    zoomAnchorPos_ = viewportPos;
    refreshCursor();
}

void CanvasView::endGesture() {
    panning_ = false;
    zoomDragging_ = false;
    gestureButton_ = Qt::NoButton;
    refreshCursor();
}

void CanvasView::cancelSelectionPress() {
    longPressTimer_.stop();
    selectionPress_ = false;
    rectActive_ = false;
    altBodyPress_ = false;
    longPressFired_ = false;
    if (rubberBand_ != nullptr) {
        rubberBand_->hide();
    }
}

void CanvasView::ensureRubberBand() {
    if (rubberBand_ == nullptr) {
        rubberBand_ = new SelectionBand(viewport());
    }
}

void CanvasView::fireLongPress() {
    if (!selectionPress_ || rectActive_) {
        return;
    }
    const QPointF posMm = mapToScene(pressViewportPos_);
    const QPoint globalPos = pressGlobalPos_;
    // État de sélection soldé AVANT l'émission : le relâchement qui suit (souvent
    // capté par le menu ouvert) n'émet ni clic ni rectangle, et un appui suivant
    // n'est pas bloqué par un état périmé.
    cancelSelectionPress();
    queueSelectBelow(posMm, globalPos);
}

// Émission différée : le slot ouvre en général un QMenu::exec(), dont la boucle
// d'évènements ne doit pas tourner au milieu de notre gestionnaire souris (état
// de sélection à moitié nettoyé, appui long encore armé).
void CanvasView::queueSelectBelow(QPointF posMm, QPoint globalPos) {
    QMetaObject::invokeMethod(
        this,
        [this, posMm, globalPos] {
            emit selectBelowRequested(posMm, globalPos, SelectMode::Replace);
        },
        Qt::QueuedConnection);
}

void CanvasView::emitSelectionClick(const QPoint& viewportPos, const QPoint& globalPos,
                                    Qt::KeyboardModifiers mods) {
    const QPointF pos = mapToScene(viewportPos);
    const Qt::KeyboardModifiers m =
        mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
    if ((m & Qt::AltModifier) != 0) {
        const Gesture g{GestureKind::Click, Qt::LeftButton, m, Qt::Key(0)};
        if (InteractionMap::resolve(Context::Select, g) == Intent::SelectBelow) {
            queueSelectBelow(pos, globalPos);
        }
        // Alt + Maj/Ctrl + clic : non défini par la table, ignoré (comme Alt + glisser).
        return;
    }
    const SelectMode mode = InteractionMap::selectModeFor(m);
    if (mode == SelectMode::Replace) {
        emit canvasClickedMm(pos);
    } else {
        emit selectionClickedMm(pos, mode);
    }
}

// Filtre applicatif (voir SpaceKeyFilter). true = évènement consommé.
bool CanvasView::filterKey(QKeyEvent* event) {
    const bool press = event->type() == QEvent::KeyPress;
    const int key = event->key();
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Meta ||
        key == Qt::Key_Alt) {
        if (cursorOverViewport_ && isVisible()) {
            updateModifiers(effectiveMods(event));
        }
        if (key == Qt::Key_Alt) {
            if (press && !event->isAutoRepeat()) {
                altUsedInGesture_ = false;
            } else if (!press && altUsedInGesture_ && cursorOverViewport_ && isVisible() &&
                       isActiveWindow()) {
                // Alt seul activerait la barre de menus (Windows) au relâchement.
                // (Sous Linux, un gestionnaire de fenêtres qui réserve Alt+clic pour
                // déplacer les fenêtres ne livre pas ce clic à l'application : c'est
                // hors de notre portée, l'Alt+clic doit y être reconfiguré.)
                altUsedInGesture_ = false;
                return true;
            }
        }
        return false;
    }
    if (key != Qt::Key_Space) {
        return false;
    }
    if (press) {
        if (!spaceConsumed_) {
            // Jamais pendant un menu/popup, une boîte modale ou si la fenêtre n'est
            // pas active : Espace y appartient à l'autre widget (activer un menu,
            // valider un bouton de dialogue).
            if (!cursorOverViewport_ || !isVisible() || !isActiveWindow() ||
                QApplication::activePopupWidget() != nullptr ||
                QApplication::activeModalWidget() != nullptr ||
                (event->modifiers() & ~Qt::KeypadModifier) != 0 ||
                isTextInput(QApplication::focusWidget())) {
                return false;
            }
            spaceConsumed_ = true;
        }
        if (!event->isAutoRepeat()) {
            setSpaceHeld(true);
        }
        return true;
    }
    if (spaceConsumed_) {
        if (!event->isAutoRepeat()) {
            spaceConsumed_ = false;
            setSpaceHeld(false);
        }
        return true;
    }
    return false;
}

void CanvasView::mousePressEvent(QMouseEvent* event) {
    updateModifiers(event->modifiers());
    cursorOverViewport_ = true;
    // Un glisser (pan/zoom) ou une sélection est en cours : un autre bouton ne
    // démarre rien (pas d'état à moitié possédé, pas d'appui long armé).
    if (panning_ || zoomDragging_ || selectionPress_) {
        event->accept();
        return;
    }
    const Context ctx = currentContext();
    const QPoint viewportPos = event->position().toPoint();
    const Qt::KeyboardModifiers mods = event->modifiers() & kRelevantMods;
    if ((mods & Qt::AltModifier) != 0) {
        altUsedInGesture_ = true;
    }
    // Panoramique / zoom continu : disponibles dans tout contexte, AVANT les
    // branches de dessin ; ni clic ni base (aucun point de tracé).
    if (event->button() == Qt::MiddleButton) {
        const auto intent = InteractionMap::resolve(
            ctx, Gesture{GestureKind::Drag, Qt::MiddleButton, mods, Qt::Key(0)});
        if (intent == Intent::PanView) {
            startPan(viewportPos, Qt::MiddleButton);
        } else if (intent == Intent::ZoomDrag) {
            startZoomDrag(viewportPos, Qt::MiddleButton);
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool spacePan =
            spaceHeld_ &&
            InteractionMap::resolve(ctx, Gesture{GestureKind::Drag, Qt::LeftButton, mods,
                                                 Qt::Key_Space}) == Intent::PanView;
        const bool panTool =
            ctx == Context::Pan &&
            InteractionMap::resolve(ctx, Gesture{GestureKind::Drag, Qt::LeftButton, mods,
                                                 Qt::Key(0)}) == Intent::PanView;
        if (spacePan || panTool) {
            startPan(viewportPos, Qt::LeftButton);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && boxDrawMode_) {
        boxPressMm_ = mapToScene(viewportPos); // vrai point d'appui (Alt = depuis le centre)
    }
    if (event->button() == Qt::LeftButton && freeformDrawMode_) {
        freeformActive_ = true;
        emit freeformPointMm(mapToScene(viewportPos));
        QGraphicsView::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && bezierDrawMode_) {
        bezierPressActive_ = true;
        bezierAnchorMm_ = mapToScene(viewportPos);
        QGraphicsView::mousePressEvent(event);
        return;
    }
    // Un clic sur un élément interactif (poignée de nœud) ne doit pas
    // déclencher la sélection : la scène serait reconstruite en plein drag.
    QGraphicsItem* item = itemAt(viewportPos);
    const bool onInteractiveItem =
        item != nullptr && (item->flags() & QGraphicsItem::ItemIsMovable);
    // Modèle de sélection : on regarde TOUS les items sous le point, pas seulement
    // le plus haut (un item de survol/surbrillance au-dessus ne doit pas masquer une
    // poignée). Les poignées ignorent la transformation de vue ; le corps d'un objet
    // vectoriel (déplaçable) non. NB (T4) : les items de recouvrement/survol doivent
    // avoir setAcceptedMouseButtons(Qt::NoButton), sinon ils captent le press.
    bool anyMovable = onInteractiveItem;
    bool onHandle =
        onInteractiveItem && (item->flags() & QGraphicsItem::ItemIgnoresTransformations);
    if (inputModelEnabled_ && selectionRectEnabled_ && ctx == Context::Select) {
        for (const QGraphicsItem* it : items(viewportPos)) {
            if ((it->flags() & QGraphicsItem::ItemIsMovable) != 0) {
                anyMovable = true;
                if ((it->flags() & QGraphicsItem::ItemIgnoresTransformations) != 0) {
                    onHandle = true;
                }
            }
        }
    }
    if (event->button() == Qt::LeftButton && inputModelEnabled_ && selectionRectEnabled_ &&
        ctx == Context::Select && !onHandle) {
        const bool selectionMods =
            (mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)) != 0;
        // Alt SEUL sur le corps d'un objet déjà sélectionné : le press est transmis à l'item
        // (glisser = déplacer ; avec Alt = dupliquer en déplaçant, M4). Un Alt + clic sans
        // mouvement ouvre « Sélectionner dessous » au relâchement.
        if (anyMovable && mods == Qt::AltModifier) {
            altBodyPress_ = true;
            pressViewportPos_ = viewportPos;
            pressGlobalPos_ = event->globalPosition().toPoint();
            QGraphicsView::mousePressEvent(event);
            event->accept();
            return;
        }
        // Corps déplaçable sans modificateur : glisser d'objet (M1), inchangé.
        if (!anyMovable || selectionMods) {
            selectionPress_ = true;
            rectActive_ = false;
            longPressFired_ = false;
            pressViewportPos_ = viewportPos;
            pressGlobalPos_ = event->globalPosition().toPoint();
            pressMods_ = mods;
            if (mods == Qt::KeyboardModifiers{} &&
                InteractionMap::resolve(ctx, Gesture{GestureKind::LongPress, Qt::LeftButton,
                                                     Qt::NoModifier, Qt::Key(0)})) {
                longPressTimer_.start(InteractionMap::longPressMs());
            }
            if (anyMovable) {
                event->accept(); // Maj/Ctrl/Alt sur un corps : sélection, pas de glisser
            } else {
                QGraphicsView::mousePressEvent(event);
                event->accept(); // le geste est pris en charge par la vue
            }
            return;
        }
    }
    if (event->button() == Qt::LeftButton && !cropMode_ && !boxDrawMode_ && !onInteractiveItem) {
        emit canvasClickedMm(mapToScene(viewportPos));
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        // Séquence Qt : press / release / dblclick / release. Le release final
        // est avalé (pas de pan fantôme ni de second ajustement).
        if (InteractionMap::resolve(currentContext(),
                                    Gesture{GestureKind::DoubleClick, Qt::MiddleButton,
                                            event->modifiers() & kRelevantMods, Qt::Key(0)}) ==
            Intent::FitDesign) {
            fitCanvas();
        }
        middleDoubleClickPending_ = true;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && !cropMode_ && !boxDrawMode_) {
        emit canvasDoubleClickedMm(mapToScene(event->position().toPoint()));
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void CanvasView::contextMenuEvent(QContextMenuEvent* event) {
    if (cropMode_ || boxDrawMode_ || polygonDrawMode_ || freeformDrawMode_ || satinPairDrawMode_ ||
        bezierDrawMode_) {
        return; // pas de menu contextuel pendant un recadrage/dessin
    }
    emit canvasContextMenu(mapToScene(event->pos()), event->globalPos());
    event->accept();
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    updateModifiers(event->modifiers());
    cursorOverViewport_ = true;
    const QPoint viewportPos = event->position().toPoint();
    if (panning_ || zoomDragging_) {
        if (panning_) {
            const QPoint d = viewportPos - gestureLastPos_;
            scrollBy(-d.x(), -d.y());
        } else {
            const int dy = viewportPos.y() - gestureLastPos_.y();
            zoomAt(std::exp(-dy * kZoomDragPerPixel), zoomAnchorPos_);
        }
        gestureLastPos_ = viewportPos;
        const QPointF p = mapToScene(viewportPos);
        emit cursorMovedMm(QPointF(p.x(), -p.y()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
    const QPointF scenePos = mapToScene(viewportPos);
    // Passage scène (Y bas) -> repère physique (Y haut).
    emit cursorMovedMm(QPointF(scenePos.x(), -scenePos.y()));
    if (freeformActive_) {
        emit freeformPointMm(scenePos);
    }
    if (bezierPressActive_) {
        emit bezierPointDraggingMm(bezierAnchorMm_, scenePos);
    }
    if (selectionPress_ && !longPressFired_) {
        if (!rectActive_ && (viewportPos - pressViewportPos_).manhattanLength() >
                                QApplication::startDragDistance()) {
            longPressTimer_.stop();
            if ((pressMods_ & Qt::AltModifier) != 0) {
                cancelSelectionPress(); // Alt + glisser : aucun geste défini
            } else {
                rectActive_ = true;
                ensureRubberBand();
                rubberBand_->show();
            }
        }
        if (rectActive_) {
            rubberBand_->setGeometry(QRect(pressViewportPos_, viewportPos).normalized());
        }
    }
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    updateModifiers(event->modifiers());
    if (panning_ || zoomDragging_) {
        if (event->button() == gestureButton_) {
            endGesture();
        }
        event->accept(); // le relâchement d'un autre bouton est ignoré
        return;
    }
    if (event->button() == Qt::MiddleButton && middleDoubleClickPending_) {
        middleDoubleClickPending_ = false;
        event->accept();
        return;
    }
    if (altBodyPress_ && event->button() == Qt::LeftButton) {
        altBodyPress_ = false;
        const QPoint pressPos = pressViewportPos_;
        const QPoint global = pressGlobalPos_;
        const QPoint releasePos = event->position().toPoint();
        QGraphicsView::mouseReleaseEvent(event);
        if ((releasePos - pressPos).manhattanLength() <= QApplication::startDragDistance()) {
            queueSelectBelow(mapToScene(pressPos), global);
        }
        return;
    }
    if (selectionPress_ && event->button() == Qt::LeftButton) {
        const bool wasRect = rectActive_;
        const bool fired = longPressFired_;
        const QPoint pressPos = pressViewportPos_;
        const QPoint releasePos = event->position().toPoint();
        const Qt::KeyboardModifiers mods = pressMods_;
        const QPoint global = pressGlobalPos_;
        QGraphicsView::mouseReleaseEvent(event);
        cancelSelectionPress();
        if (fired) {
            return; // l'appui long consommé n'émet ni clic ni rectangle
        }
        if (wasRect) {
            const QRectF rectMm =
                mapToScene(QRect(pressPos, releasePos).normalized()).boundingRect();
            emit selectionRectangleMm(rectMm, InteractionMap::selectModeFor(mods),
                                      releasePos.x() < pressPos.x());
            return;
        }
        emitSelectionClick(pressPos, global, mods);
        return;
    }
    // Capturé AVANT QGraphicsView::mouseReleaseEvent (qui peut, selon le mode
    // de glisser, déclencher un traitement interne) : les modificateurs de CET
    // évènement, pas une relecture différée de l'état clavier global
    // (défaut trouvé par revue — pas fiable à rejouer dans un test QTest
    // offscreen, cf. Maj = cercle).
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    QGraphicsView::mouseReleaseEvent(event);
    if (freeformActive_ && event->button() == Qt::LeftButton) {
        freeformActive_ = false;
        emit freeformStrokeFinished();
    }
    if (bezierPressActive_ && event->button() == Qt::LeftButton) {
        bezierPressActive_ = false;
        emit bezierPointCommittedMm(bezierAnchorMm_, mapToScene(event->position().toPoint()));
    }
    if ((cropMode_ || boxDrawMode_) && lastRubberBandMm_.isValid() &&
        !lastRubberBandMm_.isEmpty()) {
        const QRectF rect = lastRubberBandMm_;
        lastRubberBandMm_ = QRectF();
        if (cropMode_) {
            emit cropSelectedMm(rect);
        } else {
            emit boxDrawnMm(rect, modifiers);
        }
    }
}

void CanvasView::keyPressEvent(QKeyEvent* event) {
    updateModifiers(effectiveMods(event));
    if (event->key() == Qt::Key_Space && (event->modifiers() & ~Qt::KeypadModifier) == 0) {
        if (!event->isAutoRepeat()) {
            setSpaceHeld(true);
        }
        event->accept();
        return;
    }
    // Pas de coordonnée réelle en jeu (juste un pas fixe), donc pertinent
    // même hors des modes de dessin où les autres évènements sont filtrés —
    // seul l'appelant (MainWindow) décide si un objet est sélectionnable au
    // clavier en ce moment (mode Sélection, un objet sélectionné).
    constexpr double kStepMm = 0.1;    // pas normal : 0,1 mm
    constexpr double kBigStepMm = 1.0; // Maj : 1 mm
    const double step = (event->modifiers() & Qt::ShiftModifier) ? kBigStepMm : kStepMm;
    switch (event->key()) {
    case Qt::Key_Left:
        emit nudgeRequestedMm(QPointF(-step, 0.0));
        return;
    case Qt::Key_Right:
        emit nudgeRequestedMm(QPointF(step, 0.0));
        return;
    case Qt::Key_Up:
        emit nudgeRequestedMm(QPointF(0.0, -step));
        return;
    case Qt::Key_Down:
        emit nudgeRequestedMm(QPointF(0.0, step));
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event) {
    updateModifiers(effectiveMods(event));
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            spaceConsumed_ = false;
            setSpaceHeld(false);
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent* event) {
    // Le focus part, pas forcément le curseur : le drapeau de survol est conservé.
    resetTransientInput(false);
    QGraphicsView::focusOutEvent(event);
}

void CanvasView::hideEvent(QHideEvent* event) {
    resetTransientInput();
    QGraphicsView::hideEvent(event);
}

void CanvasView::changeEvent(QEvent* event) {
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        resetTransientInput();
    }
}

void CanvasView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    emit viewChanged();
}

void CanvasView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewChanged();
}

void CanvasView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);

    // Zone du canevas en blanc.
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    painter->fillRect(canvas, Qt::white);

    // Grille adaptative : pas fin >= 8 px à l'écran, pas majeur = 5x ou 10x.
    const double pxPerMm = pixelsPerMm();
    const double fine = niceStepMm(pxPerMm, 8.0);
    const double major = niceStepMm(pxPerMm, 40.0);

    const auto drawGrid = [&](double step, const QColor& color) {
        QPen pen(color);
        pen.setCosmetic(true);
        painter->setPen(pen);
        const double x0 = std::floor(rect.left() / step) * step;
        const double y0 = std::floor(rect.top() / step) * step;
        for (double x = x0; x <= rect.right(); x += step) {
            painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (double y = y0; y <= rect.bottom(); y += step) {
            painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
        }
    };
    const QColor gridMajor = AppTheme::instance().tokens().canvasGrid;
    QColor gridFine = gridMajor;
    gridFine.setAlpha(gridMajor.alpha() / 2);
    drawGrid(fine, gridFine);
    drawGrid(major, gridMajor);

    // Axes du repère (origine au centre du canevas).
    QPen axisPen(AppTheme::instance().tokens().canvasAxis);
    axisPen.setCosmetic(true);
    painter->setPen(axisPen);
    painter->drawLine(QLineF(rect.left(), 0.0, rect.right(), 0.0));
    painter->drawLine(QLineF(0.0, rect.top(), 0.0, rect.bottom()));
}

void CanvasView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);

    // Cadre (limite physique de broderie), toujours visible au-dessus du contenu.
    const QRectF canvas(-canvasMm_.width() / 2.0, -canvasMm_.height() / 2.0, canvasMm_.width(),
                        canvasMm_.height());
    QPen pen(AppTheme::instance().tokens().canvasHoop);
    pen.setCosmetic(true);
    pen.setWidth(2);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(canvas);
}

} // namespace openstitch::desktop
