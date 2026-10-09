// SPDX-License-Identifier: Apache-2.0
#pragma once

// Table unique des gestes souris/clavier (lot L5, specs/plans/ui-interaction-model.md §1).
// Source de vérité du comportement routé du canevas, de l'aide, de la ligne
// d'indications et de la documentation générée. Qt Core + Gui seulement
// (QPainterPath / QRectF) : jamais QtWidgets, aucun QApplication requis.

#include <QList>
#include <QObject>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <Qt>

#include <cstdint>
#include <optional>
#include <span>

#include "tools.hpp"

namespace openstitch::desktop {

enum class Context : std::uint8_t {
    Global,     // repli : s'applique si le contexte actif n'a pas de ligne pour ce geste
    Select,     // outil Sélection, geste sur le vide ou sur un objet non déplacé
    Pan,        // outil « Déplacer la vue » : glisser gauche = panoramique
    Move,       // glisser un objet/forme sélectionné
    Crop,       // outil Rectangle / recadrage
    DrawBox,    // DrawRectangle, DrawEllipse, DrawPolygonRegular (cadre glissé)
    DrawClicks, // DrawPolygon, DrawSatinColumn, DrawDirectionGuide, DrawBreakLine
    DrawBezier,
    DrawFreeform,
    NodeEdit,  // poignées de nœuds / rails
    StitchEdit // édition des points de couture
};

enum class GestureKind : std::uint8_t {
    Click,
    DoubleClick,
    LongPress,
    Drag,
    Wheel,
    PixelScroll,
    NativePinch,
    Hover,
    Key
};

struct Gesture {
    GestureKind kind;
    Qt::MouseButton button{Qt::NoButton};
    Qt::KeyboardModifiers mods{}; // lus sur l'évènement, jamais l'état global du clavier
    Qt::Key key{Qt::Key(0)};      // Key ; ou touche MAINTENUE pour Drag (Espace + glisser)
    bool operator==(const Gesture&) const = default;
};

enum class Intent : std::uint8_t {
    ZoomAtCursor,
    ZoomDrag,
    PanView,
    FitDesign,
    ScrollHorizontal,
    ScrollVertical,
    SelectReplace,
    SelectAdd,
    SelectToggle,
    SelectBelow,
    SelectRectangle,
    EnterEdit,
    HoverHighlight,
    FitCanvas,
    NudgeFine,
    NudgeCoarse,
    MoveObject,
    AxisLock,
    SuspendSnap,
    DuplicateOnMove,
    DrawPoint,
    FinishDraw,
    ConstrainShape,
    DrawFromCenter,
    RemoveLastPoint,
    CancelTool,
    MoveNode,
    InsertNode,
    DeleteNodes,
    DeleteSelection,
    ContextMenu,
    SelectModeWindow,
    SelectModeFree,
    SelectModeBrush
};

enum class Origin : std::uint8_t { Fusion, Proposal, Existing };
enum class Preset : std::uint8_t { OpenStitch, Touchpad };
enum class SelectMode : std::uint8_t { Replace, Add, Toggle };

inline constexpr std::uint8_t kPresetOpenStitch = 1U << 0;
inline constexpr std::uint8_t kPresetTouchpad = 1U << 1;
inline constexpr std::uint8_t kPresetBoth = kPresetOpenStitch | kPresetTouchpad;

struct Row {
    const char* id; // « G3 » : stable, tag de test et ancre dans la doc
    Context context;
    Gesture gesture;
    Intent intent;
    const char* labelKey; // QT_TRANSLATE_NOOP("InteractionMap", "...")
    Origin origin;
    std::uint8_t presets; // masque : bit0 OpenStitch, bit1 Touchpad
    bool routed;          // true = CanvasView passe par resolve()
    bool planned;         // true = jamais listée / résolue tant que non livrée
    bool overridesGlobal; // true = peut masquer une ligne Global de même geste
    bool hint;            // true = dans la ligne d'indications sans modificateur tenu
};

struct Hint {
    QString gesture;
    QString label;
    bool operator==(const Hint&) const = default;
};

// Notification process-wide : émise par InteractionMap::setPreset quand le préréglage change
// (plusieurs fenêtres / dialogues partagent le même état global).
class PresetNotifier : public QObject {
    Q_OBJECT
signals:
    void presetChanged(openstitch::desktop::Preset preset);
};

class InteractionMap {
public:
    // Exact sur (kind, button, mods, key) : contexte spécifique puis Global.
    // Ignore le préréglage (il ne change que l'affichage) et les lignes planned.
    static std::optional<Intent> resolve(Context context, const Gesture& gesture);

    // Indications de la barre d'état. Sans modificateur tenu : lignes `hint` du
    // contexte puis Global (6 au plus, ordre de la table). Avec `held` non vide :
    // lignes dont les modificateurs contiennent `held`. Préréglage actif, jamais planned.
    static QList<Hint> hintsFor(Context context, Qt::KeyboardModifiers held);
    // Idem, plus les lignes du contexte Move (déplacement) quand `held` n'est pas vide, que le
    // contexte est Select et qu'un objet déplaçable est sélectionné.
    static QList<Hint> hintsFor(Context context, Qt::KeyboardModifiers held,
                                bool hasMovableSelection);

    // Lignes dans l'ordre de la table, filtrées par préréglage actif ; les
    // lignes planned n'y figurent que si `includePlanned`.
    static QList<const Row*> allRows(bool includePlanned = false);
    // Table brute, non filtrée (tests et outils internes).
    static std::span<const Row> rawRows();

    static QString describe(const Gesture& gesture); // « Ctrl + Maj + clic molette + glisser »
    static QString contextName(Context context);
    static QString label(const Row& row); // libellé traduit

    static Context contextFor(Tool tool, bool nodeEditActive, bool stitchEditActive);
    // Maj -> Add ; Ctrl -> Toggle ; Maj+Ctrl -> Toggle ; sinon Replace.
    static SelectMode selectModeFor(Qt::KeyboardModifiers mods);
    // Fenêtre : boundingRect entièrement dans `rect` ; croisement : la forme coupe `rect`.
    static bool rectSelects(const QRectF& rect, const QPainterPath& shape, bool crossing);

    static PresetNotifier& notifier();
    static Preset preset();
    static void setPreset(Preset preset);
    // Lit QSettings « navigation/preset » (openstitch|touchpad), l'applique et le retourne.
    static Preset loadPreset();
    static void savePreset(); // écrit le préréglage actif ; setPreset() n'écrit pas

    static constexpr int kLongPressMs = 500; // valeur proposée (non documentée côté Fusion)
    // QSettings « navigation/longPressMs » borné 300-1000, défaut kLongPressMs.
    static int longPressMs();
    static void setLongPressMsForTesting(int ms); // -1 = retour à la valeur normale
};

} // namespace openstitch::desktop
