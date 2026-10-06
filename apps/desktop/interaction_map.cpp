// SPDX-License-Identifier: Apache-2.0
#include "interaction_map.hpp"

#include <QCoreApplication>
#include <QKeySequence>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <array>
#include <atomic>

namespace openstitch::desktop {

namespace {

using K = Qt::KeyboardModifier;
constexpr Qt::KeyboardModifiers kNone{};
constexpr Qt::KeyboardModifiers kShift{K::ShiftModifier};
constexpr Qt::KeyboardModifiers kCtrl{K::ControlModifier};
constexpr Qt::KeyboardModifiers kAlt{K::AltModifier};
constexpr Qt::KeyboardModifiers kCtrlShift{K::ControlModifier | K::ShiftModifier};

constexpr Gesture click(Qt::MouseButton b, Qt::KeyboardModifiers m = kNone) {
    return {GestureKind::Click, b, m, Qt::Key(0)};
}
constexpr Gesture dblclick(Qt::MouseButton b, Qt::KeyboardModifiers m = kNone) {
    return {GestureKind::DoubleClick, b, m, Qt::Key(0)};
}
constexpr Gesture drag(Qt::MouseButton b, Qt::KeyboardModifiers m = kNone,
                       Qt::Key held = Qt::Key(0)) {
    return {GestureKind::Drag, b, m, held};
}
constexpr Gesture wheel(Qt::KeyboardModifiers m = kNone) {
    return {GestureKind::Wheel, Qt::NoButton, m, Qt::Key(0)};
}
constexpr Gesture key(Qt::Key k, Qt::KeyboardModifiers m = kNone) {
    return {GestureKind::Key, Qt::NoButton, m, k};
}
constexpr Gesture simple(GestureKind kind) {
    return {kind, Qt::NoButton, kNone, Qt::Key(0)};
}

constexpr Row row(const char* id, Context ctx, Gesture g, Intent intent, const char* label,
                  Origin origin, std::uint8_t presets, bool routed, bool planned, bool overrides,
                  bool hint) {
    return Row{id, ctx, g, intent, label, origin, presets, routed, planned, overrides, hint};
}

constexpr auto Both = kPresetBoth;
constexpr auto OsOnly = kPresetOpenStitch;
constexpr auto F = Origin::Fusion;
constexpr auto P = Origin::Proposal;
constexpr auto E = Origin::Existing;
constexpr auto L = Qt::LeftButton;
constexpr auto M = Qt::MiddleButton;
constexpr auto Rt = Qt::RightButton;
constexpr Context Gl = Context::Global;

// clang-format off
// Colonnes : id, contexte, geste, intention, libellé, origine, préréglages,
//            routed, planned, overridesGlobal, hint.
// Ordre = ordre d'affichage (aide, indications, documentation).
constexpr auto kRows = std::to_array<Row>({
    row("G1", Gl, wheel(), Intent::ZoomAtCursor, QT_TRANSLATE_NOOP("InteractionMap", "Zoom au curseur"), F, Both, true, false, false, true),
    row("G2", Gl, drag(M, kCtrlShift), Intent::ZoomDrag, QT_TRANSLATE_NOOP("InteractionMap", "Zoom continu"), F, OsOnly, true, false, false, false),
    row("G3", Gl, drag(M), Intent::PanView, QT_TRANSLATE_NOOP("InteractionMap", "Panoramique"), F, OsOnly, true, false, false, true),
    row("G4", Gl, dblclick(M), Intent::FitDesign, QT_TRANSLATE_NOOP("InteractionMap", "Cadrer le design"), P, OsOnly, true, false, false, false),
    row("G5", Gl, drag(L, kNone, Qt::Key_Space), Intent::PanView, QT_TRANSLATE_NOOP("InteractionMap", "Panoramique (sans clic molette)"), P, Both, true, false, false, true),
    row("G6", Gl, wheel(kShift), Intent::ScrollHorizontal, QT_TRANSLATE_NOOP("InteractionMap", "Défilement horizontal"), P, Both, true, false, false, false),
    row("G7", Gl, wheel(kAlt), Intent::ScrollVertical, QT_TRANSLATE_NOOP("InteractionMap", "Défilement vertical"), P, Both, true, false, false, false),
    row("G8", Gl, simple(GestureKind::PixelScroll), Intent::PanView, QT_TRANSLATE_NOOP("InteractionMap", "Panoramique"), P, Both, true, false, false, false),
    row("G9", Gl, simple(GestureKind::NativePinch), Intent::ZoomAtCursor, QT_TRANSLATE_NOOP("InteractionMap", "Zoom au curseur"), P, Both, true, false, false, false),
    row("G10", Context::Select, click(Rt), Intent::ContextMenu, QT_TRANSLATE_NOOP("InteractionMap", "Menu contextuel"), E, Both, false, false, false, false),
    row("G10b", Context::Move, click(Rt), Intent::ContextMenu, QT_TRANSLATE_NOOP("InteractionMap", "Menu contextuel"), E, Both, false, false, false, false),
    row("G10c", Context::NodeEdit, click(Rt), Intent::ContextMenu, QT_TRANSLATE_NOOP("InteractionMap", "Menu contextuel"), E, Both, false, false, false, false),
    row("G10d", Context::StitchEdit, click(Rt), Intent::ContextMenu, QT_TRANSLATE_NOOP("InteractionMap", "Menu contextuel"), E, Both, false, false, false, false),
    row("G11", Gl, key(Qt::Key_Delete), Intent::DeleteSelection, QT_TRANSLATE_NOOP("InteractionMap", "Supprimer la sélection"), E, Both, false, false, false, false),
    row("G12", Gl, key(Qt::Key_Escape), Intent::CancelTool, QT_TRANSLATE_NOOP("InteractionMap", "Annuler l'outil en cours"), E, Both, false, false, false, false),
    row("G13", Gl, wheel(kCtrl), Intent::ZoomAtCursor, QT_TRANSLATE_NOOP("InteractionMap", "Zoom au curseur"), P, Both, true, false, false, false),
    row("G14", Gl, key(Qt::Key_F), Intent::FitCanvas, QT_TRANSLATE_NOOP("InteractionMap", "Ajuster au canevas"), E, Both, false, false, false, false),
    row("G15", Gl, key(Qt::Key_Left), Intent::NudgeFine, QT_TRANSLATE_NOOP("InteractionMap", "Déplacer l'objet de 0,1 mm"), E, Both, false, false, false, false),
    row("G15b", Gl, key(Qt::Key_Left, kShift), Intent::NudgeCoarse, QT_TRANSLATE_NOOP("InteractionMap", "Déplacer l'objet de 1 mm"), E, Both, false, false, false, false),
    row("P1", Context::Pan, drag(L), Intent::PanView, QT_TRANSLATE_NOOP("InteractionMap", "Panoramique"), E, Both, true, false, false, true),
    row("S1", Context::Select, click(L), Intent::SelectReplace, QT_TRANSLATE_NOOP("InteractionMap", "Sélectionner (le vide désélectionne)"), E, Both, true, false, false, true),
    row("S2", Context::Select, click(L, kShift), Intent::SelectAdd, QT_TRANSLATE_NOOP("InteractionMap", "Ajouter à la sélection"), F, Both, true, false, false, true),
    row("S3", Context::Select, click(L, kCtrl), Intent::SelectToggle, QT_TRANSLATE_NOOP("InteractionMap", "Basculer dans la sélection"), F, Both, true, false, false, true),
    row("S3b", Context::Select, click(L, kCtrlShift), Intent::SelectToggle, QT_TRANSLATE_NOOP("InteractionMap", "Basculer dans la sélection"), P, Both, true, false, false, false),
    row("S4", Context::Select, {GestureKind::LongPress, L, kNone, Qt::Key(0)}, Intent::SelectBelow, QT_TRANSLATE_NOOP("InteractionMap", "Sélectionner dessous"), F, Both, true, false, false, false),
    row("S5", Context::Select, click(L, kAlt), Intent::SelectBelow, QT_TRANSLATE_NOOP("InteractionMap", "Sélectionner dessous"), P, Both, true, false, false, true),
    row("S6", Context::Select, drag(L), Intent::SelectRectangle, QT_TRANSLATE_NOOP("InteractionMap", "Sélection par rectangle (vers la droite : englobe, vers la gauche : croise)"), F, Both, true, false, false, false),
    row("S7", Context::Select, drag(L, kShift), Intent::SelectRectangle, QT_TRANSLATE_NOOP("InteractionMap", "Rectangle : ajouter à la sélection"), F, Both, true, false, false, false),
    row("S8", Context::Select, drag(L, kCtrl), Intent::SelectRectangle, QT_TRANSLATE_NOOP("InteractionMap", "Rectangle : basculer dans la sélection"), F, Both, true, false, false, false),
    row("S8b", Context::Select, drag(L, kCtrlShift), Intent::SelectRectangle, QT_TRANSLATE_NOOP("InteractionMap", "Rectangle : basculer dans la sélection"), P, Both, true, false, false, false),
    row("S9", Context::Select, key(Qt::Key_2), Intent::SelectModeFree, QT_TRANSLATE_NOOP("InteractionMap", "Mode de sélection libre"), F, Both, false, true, false, false),
    row("S9b", Context::Select, key(Qt::Key_3), Intent::SelectModeBrush, QT_TRANSLATE_NOOP("InteractionMap", "Mode de sélection au pinceau"), F, Both, false, true, false, false),
    row("S10", Context::Select, dblclick(L), Intent::EnterEdit, QT_TRANSLATE_NOOP("InteractionMap", "Entrer en édition de l'objet"), P, Both, false, false, false, false),
    row("S11", Context::Select, simple(GestureKind::Hover), Intent::HoverHighlight, QT_TRANSLATE_NOOP("InteractionMap", "Surbrillance de pré-sélection"), P, Both, true, false, false, false),
    row("M1", Context::Move, drag(L), Intent::MoveObject, QT_TRANSLATE_NOOP("InteractionMap", "Déplacer l'objet"), E, Both, false, false, false, true),
    row("M2", Context::Move, drag(L, kShift), Intent::AxisLock, QT_TRANSLATE_NOOP("InteractionMap", "Verrouiller l'axe"), P, Both, false, false, false, true),
    row("M3", Context::Move, drag(L, kCtrl), Intent::SuspendSnap, QT_TRANSLATE_NOOP("InteractionMap", "Suspendre l'accroche"), P, Both, false, false, false, true),
    row("M4", Context::Move, drag(L, kAlt), Intent::DuplicateOnMove, QT_TRANSLATE_NOOP("InteractionMap", "Dupliquer en déplaçant"), P, Both, false, false, false, true),
    row("D1", Context::DrawClicks, click(L), Intent::DrawPoint, QT_TRANSLATE_NOOP("InteractionMap", "Ajouter un point"), E, Both, false, false, false, true),
    row("D2", Context::DrawClicks, dblclick(L), Intent::FinishDraw, QT_TRANSLATE_NOOP("InteractionMap", "Terminer le tracé"), E, Both, false, false, false, true),
    row("D2b", Context::DrawClicks, key(Qt::Key_Return), Intent::FinishDraw, QT_TRANSLATE_NOOP("InteractionMap", "Terminer le tracé"), E, Both, false, false, false, false),
    row("D3", Context::DrawBox, key(Qt::Key(0), kShift), Intent::ConstrainShape, QT_TRANSLATE_NOOP("InteractionMap", "Contraindre la forme (carré, cercle)"), E, Both, false, false, false, true),
    row("D4", Context::DrawBox, key(Qt::Key(0), kAlt), Intent::DrawFromCenter, QT_TRANSLATE_NOOP("InteractionMap", "Dessiner depuis le centre"), P, Both, false, false, false, true),
    row("D5", Context::DrawClicks, key(Qt::Key(0), kCtrl), Intent::SuspendSnap, QT_TRANSLATE_NOOP("InteractionMap", "Suspendre l'accroche"), P, Both, false, false, false, false),
    row("D6", Context::DrawClicks, key(Qt::Key_Backspace), Intent::RemoveLastPoint, QT_TRANSLATE_NOOP("InteractionMap", "Retirer le dernier point"), E, Both, false, false, false, true),
    row("D7", Context::DrawClicks, key(Qt::Key_Escape), Intent::CancelTool, QT_TRANSLATE_NOOP("InteractionMap", "Annuler l'outil en cours"), E, Both, false, false, true, false),
    row("N1", Context::NodeEdit, drag(L), Intent::MoveNode, QT_TRANSLATE_NOOP("InteractionMap", "Déplacer le nœud"), E, Both, false, false, false, true),
    row("N2", Context::NodeEdit, drag(L, kShift), Intent::AxisLock, QT_TRANSLATE_NOOP("InteractionMap", "Verrouiller l'axe"), P, Both, false, false, false, true),
    row("N2b", Context::NodeEdit, drag(L, kCtrl), Intent::SuspendSnap, QT_TRANSLATE_NOOP("InteractionMap", "Suspendre l'accroche"), P, Both, false, false, false, true),
    row("N3", Context::NodeEdit, dblclick(L), Intent::InsertNode, QT_TRANSLATE_NOOP("InteractionMap", "Insérer un nœud sur le segment"), P, Both, false, true, false, false),
    row("N4", Context::NodeEdit, key(Qt::Key_Delete), Intent::DeleteNodes, QT_TRANSLATE_NOOP("InteractionMap", "Supprimer les nœuds sélectionnés"), E, Both, false, false, true, true),
});
// clang-format on

constexpr int kMaxHints = 6;

// Préréglage actif (aucun autre état global hors surcharge test du délai).
Preset& activePreset() {
    static Preset p = Preset::OpenStitch;
    return p;
}
std::atomic<int>& longPressOverride() {
    static std::atomic<int> v{-1};
    return v;
}

// Même couple organisation/application que app_theme.cpp. Le format est
// QSettings::defaultFormat() (= natif en production) pour qu'un test puisse
// tout rediriger vers un dossier temporaire sur toute plate-forme
// (QSettings::setDefaultFormat(IniFormat) + QSettings::setPath).
QSettings settings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenStitch"),
                     QStringLiteral("OpenStitch Studio"));
}

bool visibleInPreset(const Row& r) {
    const std::uint8_t bit =
        activePreset() == Preset::Touchpad ? kPresetTouchpad : kPresetOpenStitch;
    return (r.presets & bit) != 0;
}

QString tr(const char* key) {
    return QCoreApplication::translate("InteractionMap", key);
}

QString capitalized(QString s) {
    if (!s.isEmpty()) {
        s[0] = s[0].toUpper();
    }
    return s;
}

// « Sélectionner » -> « sélectionner » ; « DST... » reste intact.
QString lowerFirstWord(QString s) {
    if (s.size() >= 2 && s[0].isUpper() && s[1].isLower()) {
        s[0] = s[0].toLower();
    }
    return s;
}

QString keyName(Qt::Key k) {
    switch (k) {
    case Qt::Key_Delete:
        return tr("Suppr");
    case Qt::Key_Escape:
        return tr("Échap");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return tr("Entrée");
    case Qt::Key_Backspace:
        return tr("Retour arrière");
    case Qt::Key_Space:
        return tr("Espace");
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        return tr("Flèches");
    default:
        return QKeySequence(k).toString(QKeySequence::PortableText);
    }
}

// Le premier élément (modificateurs, touche) est capitalisé ; la description
// d'un geste commence donc toujours par une majuscule.
QString buttonPrefix(Qt::MouseButton b) {
    switch (b) {
    case Qt::MiddleButton:
        return tr("clic molette");
    case Qt::RightButton:
        return tr("clic droit");
    default:
        return tr("clic");
    }
}

bool hasShadowingRow(Context context, const Gesture& g) {
    return std::any_of(kRows.begin(), kRows.end(), [&](const Row& r) {
        return !r.planned && r.context == context && r.gesture == g;
    });
}

} // namespace

std::optional<Intent> InteractionMap::resolve(Context context, const Gesture& gesture) {
    for (const Context c : {context, Context::Global}) {
        for (const Row& r : kRows) {
            if (!r.planned && r.context == c && r.gesture == gesture) {
                return r.intent;
            }
        }
        if (context == Context::Global) {
            break;
        }
    }
    return std::nullopt;
}

QList<Hint> InteractionMap::hintsFor(Context context, Qt::KeyboardModifiers held) {
    QList<Hint> out;
    const bool noModifier = held == Qt::KeyboardModifiers{};
    for (const Context c : {context, Context::Global}) {
        for (const Row& r : kRows) {
            if (r.planned || r.context != c || !visibleInPreset(r)) {
                continue;
            }
            // Une ligne Global masquée par une ligne spécifique de même geste n'est pas listée.
            if (c == Context::Global && context != Context::Global &&
                hasShadowingRow(context, r.gesture)) {
                continue;
            }
            if (noModifier ? !r.hint : !r.gesture.mods.testFlags(held)) {
                continue;
            }
            if (noModifier && out.size() >= kMaxHints) {
                return out;
            }
            out.push_back({describe(r.gesture), lowerFirstWord(label(r))});
        }
        if (context == Context::Global) {
            break;
        }
    }
    return out;
}

QList<const Row*> InteractionMap::allRows(bool includePlanned) {
    QList<const Row*> out;
    for (const Row& r : kRows) {
        if (visibleInPreset(r) && (includePlanned || !r.planned)) {
            out.push_back(&r);
        }
    }
    return out;
}

std::span<const Row> InteractionMap::rawRows() {
    return {kRows.data(), kRows.size()};
}

QString InteractionMap::label(const Row& row) {
    return tr(row.labelKey);
}

QString InteractionMap::describe(const Gesture& g) {
    QStringList parts;
    if (g.mods.testFlag(Qt::ControlModifier)) {
        parts << tr("Ctrl");
    }
    if (g.mods.testFlag(Qt::ShiftModifier)) {
        parts << tr("Maj");
    }
    if (g.mods.testFlag(Qt::AltModifier)) {
        parts << tr("Alt");
    }
    switch (g.kind) {
    case GestureKind::Click:
        parts << buttonPrefix(g.button);
        break;
    case GestureKind::DoubleClick:
        parts << (g.button == Qt::MiddleButton ? tr("double-clic molette") : tr("double-clic"));
        break;
    case GestureKind::LongPress:
        parts << tr("appui long");
        break;
    case GestureKind::Drag:
        if (g.key != Qt::Key(0)) {
            parts << keyName(g.key);
        }
        if (g.button == Qt::MiddleButton) {
            parts << tr("clic molette");
        } else if (g.button == Qt::RightButton) {
            parts << tr("clic droit");
        }
        parts << tr("glisser");
        break;
    case GestureKind::Wheel:
        parts << tr("molette");
        break;
    case GestureKind::PixelScroll:
        parts << tr("défilement à deux doigts");
        break;
    case GestureKind::NativePinch:
        parts << tr("pincement");
        break;
    case GestureKind::Hover:
        parts << tr("survol");
        break;
    case GestureKind::Key:
        if (g.key != Qt::Key(0)) {
            parts << keyName(g.key);
        }
        break;
    }
    return capitalized(parts.join(QStringLiteral(" + ")));
}

QString InteractionMap::contextName(Context context) {
    switch (context) {
    case Context::Global:
        return tr("Partout");
    case Context::Select:
        return tr("Sélection");
    case Context::Pan:
        return tr("Déplacer la vue");
    case Context::Move:
        return tr("Déplacement");
    case Context::Crop:
        return tr("Recadrage");
    case Context::DrawBox:
        return tr("Dessin (cadre)");
    case Context::DrawClicks:
        return tr("Dessin (clics)");
    case Context::DrawBezier:
        return tr("Dessin (courbes)");
    case Context::DrawFreeform:
        return tr("Dessin (main levée)");
    case Context::NodeEdit:
        return tr("Édition de nœuds");
    case Context::StitchEdit:
        return tr("Édition de points");
    }
    return {};
}

Context InteractionMap::contextFor(Tool tool, bool nodeEditActive, bool stitchEditActive) {
    if (stitchEditActive) {
        return Context::StitchEdit;
    }
    if (nodeEditActive) {
        return Context::NodeEdit;
    }
    switch (tool) {
    case Tool::Select:
        return Context::Select;
    case Tool::Pan:
        return Context::Pan;
    case Tool::Rect:
        return Context::Crop;
    case Tool::DrawRectangle:
    case Tool::DrawEllipse:
    case Tool::DrawPolygonRegular:
        return Context::DrawBox;
    case Tool::DrawPolygon:
    case Tool::DrawSatinColumn:
    case Tool::DrawDirectionGuide:
    case Tool::DrawBreakLine:
        return Context::DrawClicks;
    case Tool::DrawBezier:
    case Tool::DrawSatinCutLine:
        return Context::DrawBezier;
    case Tool::DrawFreeform:
        return Context::DrawFreeform;
    }
    return Context::Select;
}

SelectMode InteractionMap::selectModeFor(Qt::KeyboardModifiers mods) {
    if (mods.testFlag(Qt::ControlModifier)) {
        return SelectMode::Toggle;
    }
    if (mods.testFlag(Qt::ShiftModifier)) {
        return SelectMode::Add;
    }
    return SelectMode::Replace;
}

bool InteractionMap::rectSelects(const QRectF& rect, const QPainterPath& shape, bool crossing) {
    if (shape.isEmpty()) {
        return false;
    }
    return crossing ? shape.intersects(rect) : rect.contains(shape.boundingRect());
}

Preset InteractionMap::preset() {
    return activePreset();
}

void InteractionMap::setPreset(Preset p) {
    activePreset() = p;
}

Preset InteractionMap::loadPreset() {
    const QString v = settings().value(QStringLiteral("navigation/preset")).toString();
    setPreset(v == QLatin1String("touchpad") ? Preset::Touchpad : Preset::OpenStitch);
    return activePreset();
}

void InteractionMap::savePreset() {
    QSettings s = settings();
    s.setValue(QStringLiteral("navigation/preset"), activePreset() == Preset::Touchpad
                                                        ? QStringLiteral("touchpad")
                                                        : QStringLiteral("openstitch"));
    s.sync();
}

int InteractionMap::longPressMs() {
    if (const int o = longPressOverride().load(); o >= 0) {
        return o;
    }
    bool ok = false;
    const int v = settings().value(QStringLiteral("navigation/longPressMs")).toInt(&ok);
    return ok ? std::clamp(v, 300, 1000) : kLongPressMs;
}

void InteractionMap::setLongPressMsForTesting(int ms) {
    longPressOverride().store(ms < 0 ? -1 : ms);
}

} // namespace openstitch::desktop
