// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <QTime>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

#include "auto_satin_strategy_ui.hpp"
#include "interaction_map.hpp"
#include "view_display_state.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/document/object_fragment.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/segmentation/color_merge.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/thread_palette/thread_library.hpp"
#include "realistic_view_state.hpp"
#include "tools.hpp"

class QGraphicsScene;
class QAbstractGraphicsShapeItem;
class QGraphicsEllipseItem;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsPixmapItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsPolygonItem;
class QLabel;
class QToolButton;
class QAction;
class QListWidget;
class QPushButton;
class QDockWidget;
class QSlider;
class QThread;
class QTimer;
class QToolBar;
class QComboBox;
class QCheckBox;
class QVBoxLayout;
class QDoubleSpinBox;
class QSpinBox;
class QMenu;
class QActionGroup;
class QShortcut;
class QDragEnterEvent;
class QDropEvent;
class QListWidgetItem;
class QUrl;

namespace openstitch::commands {
class CompositeCommand;
}

namespace openstitch::desktop {

class SimulationNavBar;

class CanvasView;
struct DensityRaster; // density_overlay.hpp
class GesturesDialog;
class QuickStartDialog;
class ManualDialog;
class PropertiesPanel;
class DocumentPanel;
class SegmentationColorsPanel;
class SegmentationDialog;
class ThreadPanel;
class ColorwayDialog;
class ThreadStrip;
class WorkflowPanel;
class EmptyStateWidget;
struct MachineExportRequest;
// Seam de test unique (déclaré ici pour le friend ci-dessous) : donne à
// MainWindowTest (tests/unit/desktop/test_main_window.cpp) accès à
// applyLoadedProject() sans exposer de méthode de chargement dans l'API
// publique de production.
class MainWindowTest;
class CreationFlowTest; // tests/unit/desktop/test_creation_flow.cpp (HP-UX-002/028/029/038,
                        // HP-PERF-009)

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — chargement (libs/image), placement (libs/document), transformations
// (libs/image::ops) et undo/redo (libs/commands) viennent des bibliothèques
// cœur ; cette classe câble, affiche et recalcule l'aperçu.
class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class MainWindowTest;
    friend class CreationFlowTest;
    friend class HelpManualTest;

public:
    MainWindow();
    ~MainWindow() override;

    // Ouvre un fichier désigné par chemin (argument de ligne de commande, glisser-déposer
    // depuis l'Explorateur) en le routant par extension : .osp (projet), .dst (import
    // machine), .svg et images. Passe par les mêmes gardes « modifications non
    // enregistrées » que les menus ; reporté tant qu'un dialogue modal est ouvert.
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    // Filtre d'application : ShortcutOverride (touches simples non volées aux listes et
    // listes déroulantes) ; voir main_window_ux.cpp.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
private slots:
    // Document vierge (Ctrl+N, HP-FILE-001) : garde des modifications non
    // enregistrées, puis remplacement complet du document par un `Project`
    // par défaut via `applyLoadedProject` — un seul chemin de remplacement de
    // document, donc jamais un mode d'édition ou un tracé en cours qui
    // survivrait au nouveau document.
    void newProject();
    void openImage();
    void openAny();
    void undo();
    void redo();
    void adjustBrightnessContrast();
    void quantizeColors();
    void onCropSelected(QRectF rectMm);
    void segmentImage();
    void onCanvasClicked(QPointF posMm);
    // Modèle d'interaction L5 (signaux de CanvasView, specs/plans/ui-interaction-model.md §2.5).
    // Maj/Ctrl + clic : sélection par point (Add/Toggle) ; Replace passe par
    // onCanvasClicked (comportement historique inchangé).
    void onSelectionClicked(QPointF posMm, SelectMode mode);
    void onSelectionRectangle(QRectF rectMm, SelectMode mode, bool crossing);
    void onSelectBelow(QPointF posMm, QPoint globalPos, SelectMode mode);
    void onCanvasContextMenu(QPointF posMm, QPoint globalPos);
    // Rectangle/ellipse dessiné (outils DrawRectangle/DrawEllipse) : interprété
    // selon `currentTool_`. Maj enfoncée + DrawEllipse = cercle contraint.
    void onBoxDrawn(QRectF rectMm, Qt::KeyboardModifiers modifiers);
    // Double-clic : clôt un polygone en cours de tracé (outil DrawPolygon).
    void onCanvasDoubleClicked(QPointF posMm);
    void deleteSelectedRegion();
    // Suppr universel (action_deleteRegion, QKeySequence::Delete) : selon le
    // contexte, supprime la région (comportement historique), l'objet de
    // broderie ou l'ensemble des objets vectoriels sélectionnés -- un seul pas
    // d'annulation (CompositeCommand) pour la multi-sélection.
    void deleteSelection();
    void recolorSelectedRegion();
    void vectorizeSelectedRegion(bool askDetail = true);
    [[nodiscard]] std::optional<int> askVectorizeDetail(qsizetype regionCount);
    void autoDigitize();
    void segmentWithAi();
    // Rend visibles les rejets internes de l'auto-satin (ex. branche de
    // squelette trop large pour du satin) : la zone concernée reçoit un
    // remplissage tatami de repli (jamais laissée sans point), mais
    // l'utilisateur doit savoir POURQUOI cette zone diffère du reste de la
    // région (défaut trouvé en usage réel, cf. autodigitize.hpp
    // `AutoResult::warnings`).
    void warnAboutSkippedAutoSatinBranches(const std::vector<std::string>& warnings);
    // Même principe pour l'import SVG (openSvg) : fonctionnalités
    // rencontrées mais non prises en charge (texte, dégradés, filtres...) --
    // jamais silencieuses (openstitch/formats/svg_import.hpp), jamais non
    // plus bloquantes pour le reste de l'import.
    void warnAboutSkippedSvgFeatures(const std::vector<std::string>& warnings);
    void openAiPreferences();
    void openSettings(); // onglet « Paramètres » (EditSettings, fichier INI)
    // HP-UX-007/008 : préférences générales (unités, grille, autosave, dossier de travail).
    void openGeneralPreferences();
    // HP-FILE-007 : insère un .osp / une broderie machine dans le projet ouvert (une ICommand).
    void insertDesign();
    bool insertDesignFile(const QString& file);
    // HP-UX-002 : création DIRECTE (sans dialogue) d'un objet par forme sélectionnée, au dernier
    // réglage mémorisé (`ui_memory::creation`). Les variantes `…WithSettings` gardent le dialogue
    // de saisie (« Créer avec réglages… »).
    void createRunningStitchObject();
    void createTatamiObject();
    void createSatinObject();
    void createRunningStitchObjectWithSettings();
    void createTatamiObjectWithSettings();
    void createSatinObjectWithSettings();
    void autoConvertToSatin();
    void changeFillAngle();
    void convertSatinsToTatami();
    // Change le type de points d'un objet de broderie (contour/tatami/satin).
    // Le type satin exige des rails, construits depuis le contour source.
    void setStitchType(ObjectId embroideryId, int type);
    // Paramètres et libellé du type demandé (0 contour, 1 tatami, 2 satin, 3 directionnel) pour
    // la forme source de `emb` ; faux + `error` si impossible. Satin : `restoredContour` reçoit le
    // contour brut de la région quand il diffère du contour actuel.
    bool stitchParamsForType(const document::EmbroideryObject& emb, int type,
                             document::StitchParams& params, std::string& label,
                             std::optional<std::vector<geometry::PathSet>>& restoredContour,
                             QString& error);
    // Donne ce type de points à TOUTES les formes sélectionnées, en un seul pas d'annulation :
    // une forme déjà cousue est convertie (toutes ses sections), une forme sans couture reçoit un
    // nouvel objet de broderie. `tweak` ajuste les paramètres (espacement, angle) avant usage.
    // Les formes qui ne peuvent pas recevoir ce type (satin impossible) sont ignorées et listées.
    void setStitchTypeForSelection(int type,
                                   const std::function<void(document::StitchParams&)>& tweak = {});
    void showStatistics();
    void setHoopSize();
    void exportDst();
    void exportDstQuick(); // Ctrl+Maj+E : derniers réglages, nom de fichier seulement
    bool runMachineExport(const MachineExportRequest& request); // choix du fichier puis écriture
    // Résumé chiffré de fin de numérisation (points, durée, fils, problèmes) en barre d'état avec
    // un bouton « Voir », puis dock Analyse ouvert (HP-UX-030/031) : aucune fenêtre modale.
    void announceAutoDigitizeResult(const QString& baseMessage);
    void openQualityDock(); // dock Analyse ouvert sur une analyse à jour (HP-UX-031, étape 1)
    void importDst();
    // Interopérabilité esquisses avec la CAO tierce (ex. Fusion 360, cf.
    // openstitch/formats/dxf.hpp) : import AJOUTE des objets vectoriels au
    // document courant (comme addVectorPrimitive), contrairement à l'import
    // DST qui REMPLACE tout le document -- un DXF est un tracé éditable à
    // combiner avec le travail en cours, pas un motif de broderie fini.
    void importDxf();
    void exportDxf();
    // Enregistre sur le chemin courant sans rien demander (Ctrl+S) ; bascule
    // sur « Enregistrer sous » tant qu'aucun chemin n'est connu (HP-FILE-002).
    void saveProject();
    // Demande toujours un chemin, puis l'adopte comme chemin courant
    // (Ctrl+Maj+S).
    void saveProjectAs();
    void loadProject();
    void runAnalysis();
    void toggleSimulation();
    void onSimSliderMoved(int value);
    void onSimTick();
    void exitSimulation();
    // Saut au début du bloc de couleur suivant (+1) ou précédent (-1) (HP-UX-036).
    void jumpSimulationColor(int direction);
    [[nodiscard]] std::vector<int> colorBlockStarts() const;
    void moveObjectUp();
    void moveObjectDown();
    void toggleObjectLock();
    void applyOrderStrategy();
    // Bascule le mode d'édition des points générés (Lot 8.2) : entrée/sortie
    // propre (capture/relâche l'objet cible, purge l'aperçu de vue brute mis
    // en cache), jamais de mutation du document ici (une seule commande par
    // glisser, construite ailleurs — cf. renderBase).
    void onStitchEditModeToggled(bool on);
    void onSatinGuideModeToggled(bool on);
    void addSatinGuide();
    void removeSelectedSatinGuide();
    // Mode remodelage des rails d'une colonne satin (nœuds de rail_a/rail_b,
    // distinct du mode guides ci-dessus qui édite les barreaux).
    void onSatinRailEditModeToggled(bool on);
    // Abandonne les retouches manuelles d'un objet (confirmation explicite,
    // DiscardOverridesCommand annulable) — appelée depuis l'inspecteur ou la
    // barre contextuelle, jamais de mutation directe hors commande.
    void discardOverrides(ObjectId id);
    // Remplissage directionnel (implémentation : main_window_directional.cpp).
    // Conversion d'un objet en remplissage directionnel (paramètres
    // équivalents calculés par le cœur, ConvertFillGroupCommand annulable).
    void convertToDirectional(ObjectId embroideryId);
    // Remplissage concentrique / contour (HP-STI-007, main_window_contour.cpp) : conversion
    // d'un remplissage en anneaux concentriques, ConvertFillGroupCommand annulable.
    void convertToContourFill(ObjectId embroideryId);
    // Radial (0), spirale (1), croisé (2) : HP-STI-008 / 009 / 014 (main_window_contour.cpp).
    void convertToFillKind(ObjectId embroideryId, int kind);
    // Moteur de points (main_window_engine.cpp) : satin de bordure (HP-STI-004) et
    // entrée/sortie automatiques par objet (HP-ENG-010), commandes annulables.
    void createBorderSatin(ObjectId vectorId, double widthMm, int side, int corner);
    void editBorderSatin(ObjectId id, double widthMm, int side, int corner);
    void setJoinMode(ObjectId id, int mode);
    void setConnectorMode(ObjectId id, int mode);
    void setDensityGradient(ObjectId embroideryId, bool enabled, double angleDeg,
                            double endSpacingMm);
    void applyTwoColorBlend();
    // « Ajouter une couche… » : demande le type (contour, tatami, satin, directionnel) puis la
    // couleur de fil, et empile une nouvelle couche sur la forme de l'objet de broderie `baseId`.
    void addLayerInteractive(ObjectId baseId);
    // Ajout en un geste (bouton « + Couche », menu de ligne) : même famille de points que `baseId`,
    // fil complémentaire, aucun dialogue. Un seul pas d'annulation.
    bool addLayerQuick(ObjectId baseId);
    // Copie la couche `id` (mêmes réglages, même forme) juste au-dessus d'elle. Un pas
    // d'annulation.
    bool duplicateLayer(ObjectId id);
    // Calques du document (HP-OBJ-024) : chaque geste = une commande annulable.
    void addCalque();
    void removeCalque(ObjectId id);
    void renameCalqueInteractive(ObjectId id);
    void moveSelectionToCalque(ObjectId id);
    // Échange le rang de couture de la couche `id` avec sa voisine de la même forme (+1 = vers le
    // dessus). Faux au bout de la pile. Un pas d'annulation.
    bool moveLayer(ObjectId id, int direction);
    // Dialogue de nom puis RenameEmbroideryCommand.
    void renameLayerInteractive(ObjectId id);
    // Ajoute à `composite` une copie de chaque objet de broderie de `sourceId`, rattachée à la
    // forme `copyId` (nouveaux ids, ordre conservé). Renvoie le nombre de couches copiées.
    int addEmbroideryCopies(commands::CompositeCommand& composite, ObjectId sourceId,
                            ObjectId copyId);
    // « <racine> – couche N » avec N = premier numéro libre parmi les couches de la même forme.
    [[nodiscard]] QString nextLayerName(const document::EmbroideryObject& base) const;
    // Construit la couche (paramètres par défaut du type, nom « <base> – couche N », même forme
    // source, fil `rgb`) et la pousse via AddLayerCommand, juste après la base. Faux si impossible.
    bool addLayer(ObjectId baseId, int type, const std::array<std::uint8_t, 3>& rgb);
    // Couleur proposée pour une couche : distincte de celle de la base.
    [[nodiscard]] static std::array<std::uint8_t, 3>
    defaultLayerColor(const std::array<std::uint8_t, 3>& base);
    // Mode « Guides de direction » : affiche l'aperçu du champ, les guides et
    // les lignes de rupture de l'objet ciblé, avec poignées déplaçables.
    void onDirectionGuideModeToggled(bool on);
    void generateDirectionGuideFromShape();
    void finishDirectionGuide();
    void cancelDirectionGuideDraw();
    void removeLastDirectionGuidePoint();

    // HP-FILE-004 — sauvegarde automatique et récupération après plantage.
    // Tick périodique (QTimer, ~120 s) : écrit un instantané de secours
    // (autosave.hpp) si le document est modifié et non vide, sans jamais
    // toucher le fichier utilisateur ni `currentProjectPath_`.
    void onAutosaveTick();
    // Appelé une fois, différé après le premier passage de la boucle
    // d'évènements qui suit la construction (après `window.show()`) : propose
    // de récupérer chaque créneau autosave orphelin laissé par un arrêt
    // anormal précédent.
    void checkAutosaveRecovery();

private:
    // --- UX globale (main_window_ux.cpp) ---
    void openImageFile(const QString& file);
    void importDstFile(const QString& file);
    // Chemin local du premier fichier déposé que l'application sait ouvrir (vide sinon).
    [[nodiscard]] static QString firstOpenableLocalFile(const QList<QUrl>& urls);
    // Disposition de l'interface : enregistrée à la fermeture (panneaux ré-affichés avant),
    // restaurée au lancement (version, repli sur la disposition par défaut, bornée à l'écran).
    void saveUiLayout();
    void restoreUiLayout();
    void fitWindowToScreen(double fraction = 1.0);
    // Noms accessibles, ordre de tabulation, politique de focus.
    void applyAccessibility();
    // Entrée / Retour arrière / Échap : actifs seulement quand un tracé ou un mode est en cours.
    void updateShortcutsState();
    void updateSavedIndicator();
    // Analyse (main_window_analysis.cpp).
    void rebuildAnalysisList();
    void markAnalysisStale();
    void runAnalysisInternal(bool explicitRequest);
    // Mesures de qualité de libs/project_ops (mêmes rapports que la CLI `project …`).
    bool requireEmbroideryForReport(const QString& title);
    void showFidelityReport();
    void showDensityReport();
    void showFabricRiskReport();
    void showDiagnoseReport();
    void exportRealisticPng();
    void activateAnalysisItem(QListWidgetItem* item, bool selectObject);
    // Simulation (rendu incrémental, cf. main_window.cpp).
    struct SimWalkState {
        bool hasPos{false};
        QPointF last;
        std::uint64_t lastSource{0};
        QPointF needle;
        bool hasNeedle{false};
        int next{0}; // prochain index de commande à parcourir
    };
    struct StitchMaps;
    struct StitchPaths;
    [[nodiscard]] StitchMaps buildStitchMaps() const;
    void walkStitches(const StitchMaps& maps, int from, int to, SimWalkState& state, bool drawDots,
                      StitchPaths& out) const;
    void addStitchItems(const StitchPaths& paths, bool drawDots, bool skipEmpty);
    void appendSimulation(int newLimit);
    void updateSimulationInfo(int step);
    void placeSimulationMarker();
    // Applique un projet déjà construit (charge depuis un fichier ou fixture
    // de test) : remplace le document, réinitialise undo/sélection, rafraîchit.
    void applyLoadedProject(document::Project project);
    // Remet à zéro tout l'état d'édition attaché au document COURANT (pile
    // d'annulation, séquence en cache, sélections, modes d'édition exclusifs,
    // tracés en cours, simulation) et invalide les commandes différées en vol.
    // Ne touche jamais `project_` : l'appelant l'a déjà remplacé (ou est sur le
    // point de le faire). Point unique de réinitialisation partagé par tous les
    // chemins « nouveau document » (Nouveau, ouvrir image/SVG/projet, import
    // DST) — chacun oubliait auparavant une partie de l'état (sélection de
    // broderie, modes satin/guides, polygone en cours…).
    void resetDocumentState();
    // Garde commune aux actions destructrices de document (Nouveau, Quitter) :
    // `true` si l'on peut continuer (document propre, enregistré à la demande,
    // ou abandon explicite), `false` si l'utilisateur annule — ou si
    // l'enregistrement demandé n'a pas abouti.
    [[nodiscard]] bool confirmDiscardChanges(const QString& question);
    // Écrit le document dans `file` (écriture atomique côté project_io) et,
    // en cas de succès, adopte ce chemin comme cible d'enregistrement.
    // `false` si l'écriture a échoué (message déjà affiché).
    [[nodiscard]] bool saveProjectToPath(const QString& file);
    // Charge un `.osp` déjà désigné (sans dialogue) : remplace le document et
    // adopte le chemin comme cible d'enregistrement. Seam de test du chemin
    // « ouvrir un projet », et futur point d'entrée des fichiers récents /
    // de l'ouverture par ligne de commande (HP-FILE-003, HP-FILE-005).
    bool openProjectFile(const QString& file);
    // Chemin `.osp` du document courant (vide = document jamais enregistré) ;
    // met le titre de la fenêtre en phase. Remis à vide par
    // resetDocumentState() : tout remplacement de document oublie la cible.
    void setCurrentProjectPath(const QString& file);
    // Garde commune (confirmDiscardChanges) + ouverture d'un item Récents,
    // partagée par le sous-menu Fichier ▸ Récents et l'écran d'accueil pour
    // qu'une évolution de la confirmation ou de l'enchaînement ne se fasse
    // qu'en un seul endroit.
    void openRecentFile(const QString& path);
    // Resynchronise recentFiles_ avec pruneMissingRecentFiles(loadRecentFiles())
    // -- seul endroit qui applique la purge des entrées disparues (AD-S11-2,
    // HP-FILE-003) -- puis reconstruit, après un cycle d'évènements
    // (QTimer::singleShot), le sous-menu Fichier ▸ Récents et la liste de
    // l'écran d'accueil à partir de recentFiles_. Seule la reconstruction de
    // l'UI est reportée : détruire, pendant l'exécution de son propre
    // gestionnaire de clic, le QAction du menu Récents ou le QPushButton de
    // l'écran d'accueil qui vient de déclencher cet appel serait une
    // réentrance. La resynchronisation de recentFiles_, elle, reste
    // synchrone : setCurrentProjectPath() et le constructeur le lisent et le
    // persistent sans attendre de cycle d'évènements, avant qu'aucun appel
    // différé n'ait pu s'exécuter.
    // `prune` : applique aussi la purge des fichiers disparus (QFileInfo::exists,
    // potentiellement bloquant sur réseau) -- réservé à l'ouverture du menu et
    // à un échec d'ouverture, jamais à chaque enregistrement.
    void refreshRecentFilesUi(bool prune = false);
    // Reconstruit synchroniquement sous-menu Récents + écran d'accueil.
    void rebuildRecentUi();
    void updateWindowTitle();
    // Chemin proposé dans un dialogue d'enregistrement/export : nom du projet
    // (ou « sans-titre ») + `suffix`, dans le dossier du projet, sinon le dernier
    // dossier utilisé, sinon Documents.
    [[nodiscard]] QString suggestedFilePath(const QString& suffix) const;
    // Mémorise le dossier de `file` comme dernier dossier utilisé.
    void rememberLastDirectory(const QString& file);
    // Message d'état + bouton temporaire « Ouvrir le dossier » après un export.
    void offerRevealInFolder(const QString& file, const QString& message);
    // Prévient avant de supprimer la segmentation (opération image, resegmentation) ;
    // `true` si l'on peut continuer (aucune segmentation, ou « Continuer »).
    [[nodiscard]] bool confirmDestroySegmentation(const QString& action);
    // Supprime le créneau autosave conservé après une récupération (cf. checkAutosaveRecovery).
    void discardPendingRecoverySlot();
    // Objet de broderie ciblé par la sélection courante (broderie choisie
    // dans l'ordre de couture, sinon remplissage rattaché à l'objet vectoriel
    // sélectionné au canevas ; nullptr sinon). Résolution partagée par
    // updateContextToolbar/updateInspector/syncDocumentSelection.
    [[nodiscard]] document::EmbroideryObject* resolveSelectedEmbroidery();
    // Etat Clean/ManuallyEdited/Dirty (Lot 8.2) de l'objet `id`, depuis le
    // cache `editStates_` rafraîchi à chaque `refreshImage()` (Clean si absent
    // du cache : c'est l'état implicite, cf. `classify_all_edit_states`).
    // Ne recalcule jamais rien elle-même (pas de logique métier ici) — lecture
    // seule d'un résultat déjà produit par le cœur.
    [[nodiscard]] stitch_generation::ObjectEditState editStateOf(ObjectId id) const;
    void buildMenus();
    void buildWorkspacesMenu(); // HP-UX-049 : dispositions de panneaux non destructives
    void buildHelpMenu();
    void buildMainToolbar();
    void buildContextToolbar();
    // Reconstruit la barre contextuelle selon la sélection (actions rapides).
    void updateContextToolbar();
    void buildToolPalette();
    // Change le mode d'interaction du canevas (Sélection / Déplacer / Rectangle),
    // synchronise les cases d'outils, le curseur et la barre d'état.
    void setTool(Tool tool);
    void buildAnalysisPanel();
    void buildSimulationToolbar();
    void buildOrderPanel();
    void refreshOrderPanel();
    // Panneau Fils / film couleur (HP-THR-004/005) : tout est dans main_window_threads.cpp.
    void buildThreadPanel();
    void refreshThreadPanel();
    // Passe la couleur `from` à la position `to` de la liste des fils (tous ses objets, un seul
    // pas d'annulation) : appelé par le panneau Fils et par la barre des fils.
    void moveThreadColour(int from, int to);
    // Barre des fils (bas de fenêtre) : pastilles rondes des fils du motif, dans
    // main_window_threads.cpp.
    void buildThreadStrip();
    void refreshThreadStrip();
    void refreshThreadSelectionInfo();
    [[nodiscard]] std::vector<ObjectId> selectedEmbroideryIds() const;
    void buildFilterPanel();
    void refreshFilterPanel();
    // Raccourcis des filtres d'affichage : n'afficher que ce type (index filtre 0..2) / cette
    // couleur (0xRRGGBB), ou tout réafficher. Mettent à jour le panneau et le rendu.
    void isolateFilterType(int filterType);
    void isolateFilterColor(std::uint32_t colorKey);
    void showAllFilters();
    // Couleurs distinctes des formes de l'image (première apparition), pour le sélecteur de fil.
    [[nodiscard]] std::vector<QColor> imageColors() const;
    // Sélecteur de couleur de fil : les couleurs de l'image y sont proposées en « Custom colors ».
    [[nodiscard]] QColor pickThreadColor(const QColor& initial, const QString& title);
    // Affiche/masque un dock sur ordre d'un rafraîchissement, sans défaire « Masquer les
    // panneaux » : en mode canevas seul, le dock reste caché et n'est (ré)affiché qu'à la sortie.
    static bool debugMenuEnabled(); // OPENSTITCH_DEBUG=1 ou QSettings « debug/menu »
    void setDockAutoVisible(QDockWidget* dock, bool visible, bool force = false);
    void buildWorkflowPanel();
    void refreshWorkflow();
    void buildDocumentPanel();
    void refreshDocumentPanel();
    // Reflète la sélection courante (broderie/région) dans le panneau Document.
    void syncDocumentSelection();
    void buildPropertiesPanel();
    // Met l'inspecteur en phase avec la sélection courante (broderie > vecteur >
    // région). Ne reconstruit le formulaire que si la sélection a changé, pour ne
    // pas interrompre une édition en cours.
    void updateInspector();
    // Un objet passe-t-il les filtres d'affichage (type, couleur, taille) ?
    [[nodiscard]] bool objectPassesFilter(const document::EmbroideryObject& object) const;
    // Une forme vectorielle passe-t-elle les mêmes filtres ? Taille : sa propre aire ; type et
    // couleur : au moins une de ses broderies doit les satisfaire (une forme sans broderie
    // n'est filtrée que par sa taille). Ne regarde pas `visible` (géré par l'appelant).
    [[nodiscard]] bool vectorPassesFilter(const document::VectorObject& object) const;
    // Aire d'une forme vectorielle (mm², trous déduits).
    [[nodiscard]] static double vectorAreaMm2(const document::VectorObject& object);
    // Aire de la région source d'un objet (mm²) ; 0 si introuvable.
    [[nodiscard]] double regionAreaMm2(const document::EmbroideryObject& object) const;
    // Index de type : 0 contour, 1 tatami, 2 satin, 3 remplissage directionnel.
    [[nodiscard]] static int stitchTypeIndex(const document::EmbroideryObject& object);
    void updateSimulationRange();
    // Remplissage tatami dont l'orientation est éditable : celui choisi dans
    // l'ordre de couture, ou à défaut le tatami rattaché à l'objet vectoriel
    // sélectionné au canevas. nullptr si aucun tatami n'est visé.
    [[nodiscard]] document::EmbroideryObject* currentFillObject();
    // Objet vectoriel visible sous un point (mm, scène). Le dernier dessiné gagne.
    [[nodiscard]] std::optional<ObjectId> objectAt(QPointF posMm) const;
    // Le calque de la forme est affiché (HP-OBJ-024) ; faux = ni dessinée, ni saisissable.
    [[nodiscard]] bool calqueShowsShape(const document::VectorObject& object) const;
    // Objet de broderie satin dont le RUBAN (rail_a + rail_b) contient le
    // point donné — repli utilisé quand `objectAt` ne trouve rien : un satin
    // manuel a un objet vectoriel source délibérément invisible (cf. §
    // colonne satin manuelle), donc pas cliquable via `objectAt` seul.
    [[nodiscard]] document::EmbroideryObject* satinEmbroideryAt(QPointF posMm);
    // Formate EXHAUSTIVEMENT les données d'un objet de broderie (identité,
    // paramètres, géométrie source, séquence générée, retouches, analyse) en
    // texte lisible — outil de débogage (menu contextuel canevas).
    [[nodiscard]] QString buildDebugDump(ObjectId embroideryId) const;
    // Affiche `buildDebugDump` dans une boîte de dialogue (copier/enregistrer).
    void showDebugDump(ObjectId embroideryId);
    // Objet de broderie rattaché à un objet vectoriel : la couche du DESSUS s'il y en a plusieurs
    // (nullptr si aucun).
    [[nodiscard]] document::EmbroideryObject* embroideryForVector(ObjectId vectorId);
    // Ouverture d'un SVG comme NOUVEAU document, directement en objets
    // vectoriels éditables -- saute entièrement image/segmentation/
    // vectorisation (demande utilisateur, 2026-09-11 : "évite la
    // segmentation" quand le tracé existe déjà). Même point d'entrée que
    // `openImage()` (menu "Ouvrir une image", état vide) -- branché sur
    // l'extension du fichier choisi, jamais une action séparée : ce qui
    // compte pour l'utilisateur est "ouvrir mon dessin", pas la distinction
    // interne image/vecteur.
    void openSvg(const QString& file);
    // Centre représentatif d'un objet de broderie (pour l'estimation du coût).
    [[nodiscard]] Vec2um embroideryCentroid(const document::EmbroideryObject& object) const;
    // Création manuelle de formes (rectangle/ellipse/polygone), en écho au
    // dessin de formes de base dans les logiciels de digitalisation du
    // marché : crée un `VectorObject` autonome (sans région source) que
    // l'utilisateur convertit ensuite en objet de broderie via les actions
    // « Créer un… » existantes — aucun nouveau chemin de création côté
    // broderie, tout le pipeline aval est réutilisé tel quel.
    void addVectorPrimitive(geometry::Path path, const QString& name);
    // Suit le tracé d'un polygone en cours (poignée sur le curseur -> aperçu
    // mis à jour) ; ferme (>= 3 sommets), sinon annule silencieusement.
    void updatePolygonPreview(QPointF cursorSceneMm);
    void finishPolygon();
    void cancelPolygonDraw();
    void removeLastPolygonVertex();
    // Accroche façon Fusion 360 (audit ergonomie esquisse demandé par
    // l'utilisateur) : cherche le point le plus proche du curseur parmi les
    // extrémités/milieux/centres des objets vectoriels existants, dans un
    // rayon fixe à l'écran (donc constant quel que soit le zoom). Utilisé par
    // le polygone et la colonne satin manuelle (clic = pose réelle du point,
    // donc l'accroche affichée correspond exactement à ce qui est posé) ;
    // délibérément PAS branché sur Bézier (l'ancre y est capturée dans
    // CanvasView au press, avant que ce code ait pu intervenir — brancher
    // seulement l'aperçu aurait affiché une accroche qui ne se produit pas
    // réellement au clic, pire qu'aucune accroche) ni sur le rectangle/ellipse
    // (glisser natif RubberBandDrag de Qt, pas d'aperçu personnalisable en
    // cours de glisser ; seuls les coins finaux sont accrochés à la fin).
    // `excludeObject` (optionnel) : objet dont les points ne sont pas candidats. Le glisser de
    // nœud n'utilise PLUS cette accroche (voir findNodeSnapMm, sommets seulement).
    [[nodiscard]] std::optional<QPointF>
    findSnapPointMm(QPointF cursorSceneMm,
                    std::optional<ObjectId> excludeObject = std::nullopt) const;
    // Affiche/masque le repère visuel d'accroche (cercle) au point donné.
    void updateSnapIndicator(std::optional<QPointF> snapSceneMm);
    // Miroir de ce qui précède pour le tracé à main levée (outil
    // DrawFreeform) : un point par évènement CanvasView::freeformPointMm
    // (pas de segment élastique jusqu'au curseur, contrairement au polygone —
    // chaque position déjà captée EST le tracé).
    void onFreeformPointAdded(QPointF posMm);
    void finishFreeform();
    void cancelFreeformDraw();
    // Lettrage (outil Texte, HP-TXT-*) : menu Texte, dialogue, création/édition/suppression
    // d'un texte en UN pas d'annulation (main_window_lettering.cpp). Aucune logique de
    // lettrage ici : tout vient de libs/lettering.
    void buildTextMenu();
    void updateTextActions();
    void placeTextAt(QPointF posMm);
    void editSelectedText();
    void removeSelectedText();
    // Texte propriétaire de la sélection (lettre = objet vectoriel ou de broderie), s'il existe.
    [[nodiscard]] std::optional<ObjectId> selectedTextId() const;
    // Génère les lettres de `text` puis applique la commande ; false (message affiché) si la
    // police est illisible ou si le texte ne produit aucune lettre.
    bool applyText(document::TextObject text, bool isNew);
    bool openTextDialog(document::TextObject initial, bool isNew);
    // Formes vectorielles : unir / soustraire / intersecter / séparer / couteau
    // (main_window_shapes.cpp).
    void buildShapeMenu();
    // Manipulation d'objets (main_window_objects.cpp, roadmap HP-OBJ-*).
    void buildObjectEditActions(QMenu* editMenu);
    void updateObjectEditActions();
    // Formes visées par une action d'objet : la sélection, ou la forme source de la broderie
    // sélectionnée dans l'ordre de couture.
    [[nodiscard]] std::vector<ObjectId> transformTargets() const;
    // Objets de broderie portés par ces formes.
    [[nodiscard]] std::vector<ObjectId> targetEmbroideryIds() const;
    void copySelection();
    void cutSelection();
    void pasteClipboard(bool inPlace);
    void duplicateSelectionWithStitches();
    void applyPastedFragment(const document::ObjectFragment& fragment, Vec2um delta,
                             const QString& label, const QString& done);
    void selectAllObjects();
    void selectObjectsWithSameColor();
    void setEmbroideryColorForSelection(std::array<std::uint8_t, 3> rgb);
    void runTransform(const std::vector<ObjectId>& ids, const commands::AffineTransform& transform,
                      const QString& label, const QString& done);
    void rotateSelection(double degrees); // positif : anti-horaire
    // `pivot` absent : centre de la boîte des objets. Une seule commande annulable.
    void rotateObjects(const std::vector<ObjectId>& ids, double degrees,
                       std::optional<Vec2um> pivot = std::nullopt);
    // Poignée de rotation + pivot déplaçable de la sélection (main_window_rotation.cpp).
    void addRotationHandles();
    [[nodiscard]] Vec2um rotationPivotFor(const std::vector<ObjectId>& ids,
                                          const commands::BoundsUm& box) const;
    void showRotationPreview(const commands::BoundsUm& box, Vec2um pivot, double degrees);
    void hideRotationPreview();
    std::optional<Vec2um> rotationPivot_; // pivot choisi, valable pour rotationPivotTargets_
    std::vector<ObjectId> rotationPivotTargets_;
    QGraphicsPolygonItem* rotationPreview_{nullptr};
    QGraphicsRectItem* commonBox_{nullptr}; // cadre commun d'une sélection multiple
    // Angle (degrés, positif = anti-horaire) d'un glisser de la poignée de rotation : de
    // `handleScene` à `releasedScene` autour de `centerScene` (repère scène, Y vers le bas).
    // `snap` (Maj) arrondit au multiple de 15° ; un mouvement négligeable donne 0.
    [[nodiscard]] static double rotationDragDegrees(QPointF centerScene, QPointF handleScene,
                                                    QPointF releasedScene, bool snap);
    void mirrorSelection(bool horizontal);
    void centerDesignInHoop();
    void scaleDesign(double factor);
    void fitDesignToHoop();
    void transformDesign(int kind); // 1 : rotation 90° horaire, 2 : miroir horizontal, 3 : vertical
    void updateShapeActions();
    void runBooleanOp(commands::BooleanOp op);
    void breakApartSelected();
    void blendSelectedZones();
    [[nodiscard]] const document::EmbroideryObject* blendFillFor(ObjectId vector) const;
    void createColorBlend(ObjectId baseId, std::optional<ObjectId> secondId);
    void removeHolesOfSelection(double maxAreaMm2); // 0 : tous les trous
    // Combler les vides d'un bloc uni : demande l'écart et l'aire maximale, puis
    // `fillBlockHolesWith`.
    void fillBlockHoles();
    // Détecte les poches sans fil depuis les points cousus de la sélection (écart de fermeture en
    // mm ; aire maximale en mm², 0 = sans limite) et les comble en UN pas d'annulation.
    void fillBlockHolesWith(double closingGapMm, double maxAreaMm2);
    void finishCut();
    // Exécute la commande (un pas d'annulation), sélectionne le premier de `keep` encore présent
    // et annonce `done` ; en cas d'échec, affiche la raison dans la barre d'état.
    void applyShapeCommand(commands::VectorOpResult result, const QString& done,
                           const std::vector<ObjectId>& keep);
    // Colonne satin manuelle (outil DrawSatinColumn) : mêmes principes que le
    // polygone (aperçu élastique, terminé par double-clic/Entrée/bouton,
    // annulé par Échap, dernier point retirable par Retour arrière), mais
    // chaque clic alterne entre rail A et rail B (paires Ai-Bi -> barreaux).
    void updateSatinColumnPreview(QPointF cursorSceneMm);
    void finishSatinColumn();
    void cancelSatinColumnDraw();
    void removeLastSatinColumnPoint();
    // Courbes de Bézier (outil DrawBezier, "plume") : clics successifs comme
    // le polygone, mais chaque clic peut être glissé pour poser un nœud Lisse
    // à poignées symétriques au lieu d'un Coin — mêmes déclencheurs de
    // finalisation/annulation que le polygone et la colonne satin.
    void onBezierPointDragging(QPointF anchorMm, QPointF currentMm);
    void onBezierPointCommitted(QPointF anchorMm, QPointF handleMm);
    void updateBezierPreview(QPointF cursorSceneMm);
    void finishBezier();
    void cancelBezierDraw();
    void removeLastBezierPoint();
    // Active/désactive les boutons génériques Terminer/Annuler (barre
    // d'outils) selon l'outil courant et l'état de son tracé en cours —
    // un seul point d'entrée partagé par polygone/bézier/satin plutôt que
    // trois logiques dupliquées.
    void updateDrawActionsState();
    // Suppression/duplication d'objets (menu contextuel canevas) : seules
    // actions destructives ou créatrices déclenchées depuis ce menu, toutes
    // passent par une commande annulable — jamais de mutation directe.
    void deleteVectorObject(ObjectId id);
    void deleteEmbroideryObjectOnly(ObjectId id);
    void duplicateVectorObject(ObjectId id);
    // Décalage (offset) façon Fusion 360 : crée une NOUVELLE forme, contour
    // aplati puis décalé via geometry::inset_path_set (Clipper2) -- ne
    // modifie jamais l'original, comme dupliquer. Les courbes (nœuds Lisses)
    // sont aplaties d'abord (mêmes raisons qu'à l'export DXF : Clipper2
    // travaille sur des polygones, pas des tangentes).
    void offsetVectorObject(ObjectId id);
    // Cœur de offsetVectorObject, sans QInputDialog (comme applyLoadedProject
    // pour loadProject) : delta déjà en µm, convention geometry::inset_path_set
    // (positif = retrait intérieur). Seam de test uniquement -- offsetVectorObject
    // reste le seul point d'entrée en usage réel.
    void offsetVectorObjectCore(ObjectId id, Micrometers delta);

    // --- Auto-satin par squelette et traversées (main_window_satin_auto.cpp) ---
    // Aperçu avant création / résumé de l'inspecteur : nombre de colonnes,
    // couverture estimée, fil en double, guides ignorés, refus nommés. Calculé par
    // le moteur (libs/auto_satin), jamais par l'interface.
    struct AutoSatinPreview {
        std::size_t columns{0};
        bool measured{false};
        double coverage{0.0};
        double overlap{0.0};
        double uncoveredMm2{0.0};
        int orphanGuides{0};
        QStringList messages;
    };
    struct AutoSatinSummaryCache {
        ObjectId id{};
        std::uint64_t key{0};
        QString text;
        bool valid{false};
    };
    [[nodiscard]] AutoSatinPreview previewAutoSatin(const document::VectorObject& source,
                                                    const document::AutoSatinParams& params) const;
    [[nodiscard]] QString describeAutoSatinPreview(const AutoSatinPreview& preview) const;
    // Mesures de CHAQUE méthode d'auto-satin sur ces contours (tableau « Comparer les
    // méthodes ») : calculées hors du fil d'interface sur un instantané, sans toucher au
    // document. Les réglages de `base` (espacement, angle, guides) sont ceux de chaque essai.
    [[nodiscard]] std::vector<AutoSatinStrategyComparison>
    compareAutoSatinStrategies(const std::vector<geometry::PathSet>& paths,
                               const document::AutoSatinParams& base);
    // Bouton « Comparer les méthodes… » de l'inspecteur : tableau, puis, si l'utilisateur en
    // désigne une autre, UNE commande annulable (« Changer la méthode de satin »).
    void compareAutoSatinMethods(ObjectId id);
    // Méthode d'un auto-satin qui produit au moins une colonne pour cette forme, en essayant
    // d'abord celle des réglages puis les autres dans l'ordre du sélecteur ; rien si aucune.
    [[nodiscard]] std::optional<document::AutoSatinStrategy>
    firstWorkingAutoSatinStrategy(const document::VectorObject& source,
                                  const document::AutoSatinParams& params) const;
    // Contour brut de la région de segmentation d'un vecteur, si le contour actuel le dépasse
    // nettement (le recouvrement des tatamis voisins y a été intégré à l'auto-numérisation :
    // utile au tatami, mais un satin y déborderait de sa région). Aucun si pas de région.
    [[nodiscard]] std::optional<std::vector<geometry::PathSet>>
    pristineSatinContour(const document::VectorObject& vector) const;
    [[nodiscard]] QString autoSatinSummary(const document::EmbroideryObject& emb);
    // `askParameters` : dialogue de réglages ; sinon `confirm` : simple question Oui/Non ; ni l'un
    // ni l'autre : création immédiate au dernier réglage (HP-UX-002).
    void createAutoSatinForSelection(bool askParameters, bool confirm = true);
    void createAutoSatin(bool askParameters, bool confirm = true);
    void createRunningStitchObjectImpl(bool askSettings);
    void createTatamiObjectImpl(bool askSettings);
    void applyAutoSatinEdit(ObjectId id, document::AutoSatinParams params, const QString& label,
                            const QString& mergeTag = {});
    // Tracé de >= 2 points : le premier fixe l'ancre, le dernier la direction (guide droit). À
    // partir de 3 points le tracé est conservé comme guide en courbe (balayage parallèle :
    // tangente).
    void addAutoSatinGuideFromStroke(ObjectId id, const std::vector<Vec2um>& stroke);
    void addAutoSatinGuideFromStroke(ObjectId id, Vec2um from, Vec2um to) {
        addAutoSatinGuideFromStroke(id, std::vector<Vec2um>{from, to});
    }
    void changeAutoSatinGuide(ObjectId id, int index, double angleDeg, bool absolute);
    // Clic sur un guide de la liste de l'inspecteur : cercle temporaire sur son ancre.
    void highlightAutoSatinGuide(ObjectId id, int index);
    QGraphicsItem* guideHighlight_{nullptr}; // propriété de la scène (baseItems_)
    // Édition multi-objets, boîte de forme, alignement, historique (main_window_editing.cpp).
    void connectInspectorEditing();
    // Coutures des formes sélectionnées (toutes les sections) : `sameType` si elles ont toutes
    // le même type de points (formulaire commun de l'inspecteur).
    struct EmbroideryGroup {
        std::vector<ObjectId> ids;
        bool sameType{false};
    };
    [[nodiscard]] EmbroideryGroup selectedEmbroideryGroup() const;
    // Applique `apply` aux paramètres de chaque couture du groupe, en un seul pas d'annulation
    // nommé d'après `field` (les objets qui ne changent pas sont laissés tels quels).
    void applyGroupParams(const QString& field,
                          const std::function<void(document::StitchParams&)>& apply);
    // Actions de lot sur les guides (« autoDirectionGuides », « clearDirectionGuides »,
    // « clearSatinGuides ») et pose d'un guide d'angle donné sur chaque auto-satin.
    void applyGroupAction(const QString& action);
    void applyGroupGuideAngle(double angleDeg, bool absolute);
    void applyVectorBox(ObjectId id, QRectF boxMm);
    void applyToSelection(int stitchType, bool setSpacing, double spacingMm, bool setAngle,
                          double angleDeg);
    void buildAlignMenu(QMenu* editMenu);
    void updateAlignActions();
    void alignSelection(int mode);
    void buildHistoryPanel();
    void refreshHistoryPanel();
    void jumpToHistory(int row);
    [[nodiscard]] static std::optional<QRectF> vectorBoxMm(const document::VectorObject& object);
    QDockWidget* historyDock_{nullptr};
    QListWidget* historyList_{nullptr};
    QList<QAction*> alignActs_;
    // Organiser les objets (main_window_arrange.cpp) : référence d'alignement, répartition,
    // verrou d'édition, redimensionnement numérique, décalage de duplication, conversions.
    void buildArrangeMenu(QMenu* editMenu);
    void updateArrangeActions();
    void alignShapes(int mode);
    void distributeShapes(bool horizontal);
    void setShapesLocked(const std::vector<ObjectId>& ids, bool locked);
    void resizeSelectionByFactors(double sx, double sy, bool fromCenter);
    void resizeSelectionDialog();
    void duplicateWithOffsetDialog();
    void convertSelectionType(int kind);
    int alignReference_{0}; // 0 sélection, 1 premier objet, 2 cadre
    Vec2um duplicateOffset_{Micrometers{2'000}, Micrometers{-2'000}};
    QList<QAction*> arrangeActs_;
    QList<QAction*> convertActs_;
    // Aperçus de glisser (poignées) : repère d'accroche, cadre de redimensionnement.
    void showNodeDragFeedback(QPointF sceneMm, ObjectId objectId);
    void hideNodeDragFeedback();
    void showResizePreview(QPointF anchorSceneMm, QPointF cornerSceneMm);
    void hideResizePreview();
    QGraphicsItem* snapIndicator_{nullptr};
    QGraphicsRectItem* resizePreview_{nullptr};
    // Remodelage direct d'un segment du contour (HP-VEC-016, main_window_reshape.cpp). Le geste
    // vit dans `reshape_` jusqu'au relâchement (une commande) ou à Échap (aucune commande) ;
    // l'aperçu est une copie du tracé recalculée par `geometry::reshape_path_set_segment`.
    struct ReshapeGesture {
        ObjectId object;
        std::size_t set{0};
        std::size_t path{0};
        std::size_t segment{0};
        double t{0.0};
        QPointF startSceneMm;
        geometry::PathSet base;
    };
    [[nodiscard]] bool canBeginSegmentReshape(ObjectId object, QPointF sceneMm) const;
    bool beginSegmentReshape(ObjectId object, QPointF sceneMm);
    void updateSegmentReshape(QPointF sceneMm, Qt::KeyboardModifiers mods);
    void commitSegmentReshape(QPointF sceneMm, Qt::KeyboardModifiers mods);
    void cancelSegmentReshape();
    [[nodiscard]] QPointF reshapeDragPoint(QPointF sceneMm, Qt::KeyboardModifiers mods) const;
    std::optional<ReshapeGesture> reshape_;
    // Édition vectorielle (HP-VEC-001/005/006, main_window_vector_edit.cpp).
    void buildVectorEditActions();
    void updateVectorEditActions();
    bool insertNodeAt(QPointF sceneMm);
    bool removeHoleAt(QPointF sceneMm);
    void outlineSelection(int kind, double gapMm, double satinWidthMm, bool aroundHoles);
    void outlineSelectionDialog();
    // Magnétisme à la grille (HP-VEC-009) : point de grille le plus proche si activé et à moins
    // de `radiusMm` du curseur.
    [[nodiscard]] std::optional<QPointF> gridSnapPointMm(QPointF cursorSceneMm,
                                                         double radiusMm) const;
    QAction* gridSnapAct_{nullptr};
    QAction* gridStepAct_{nullptr};
    QAction* insertDesignAct_{nullptr};
    double gridStepMm_{1.0};
    // Affichage par passe, zoom rectangle / 1:1 étalonné, grille et guides, aperçu en direct
    // (HP-VIEW-003/007/010/012, main_window_view_display.cpp).
    void buildViewDisplayMenu(QMenu* viewMenu);
    void applyViewDisplay();
    void addFunctionMarkers();
    ViewDisplayState viewDisplay_;
    QAction* zoomRectAct_{nullptr};
    QAction* outlineAct_{nullptr};
    // Sélection de nœuds et déplacement groupé (HP-VEC-017, main_window_node_select.cpp). Tous
    // les nœuds sélectionnés appartiennent au même chemin (set, path) de `nodeSelObject_`.
    void buildNodeSelectionActions();
    [[nodiscard]] bool isNodeSelected(const document::NodeRef& ref) const;
    void selectNodes(ObjectId object, const std::vector<document::NodeRef>& nodes, SelectMode mode);
    void clearNodeSelection();
    void onNodeRectangle(QRectF rectMm, SelectMode mode);
    void previewGroupMove(ObjectId object, Vec2um delta);
    void hideGroupPreview();
    void restyleNodeHandles();
    void commitGroupMove(ObjectId object, Vec2um delta);
    std::optional<ObjectId> nodeSelObject_;
    std::vector<document::NodeRef> selectedNodes_;
    QAction* softNodesAct_{nullptr};
    std::vector<std::pair<document::NodeRef, QAbstractGraphicsShapeItem*>> nodeHandles_;
    QGraphicsPathItem* reshapePreview_{nullptr};
    void removeAutoSatinGuide(ObjectId id, int index);
    void renderAutoSatinOverlay(const document::EmbroideryObject& obj,
                                const document::AutoSatinParams& params,
                                const document::VectorObject& source);

    // --- Remplissage directionnel (main_window_directional.cpp) ---
    // Paramètres directionnels de départ pour `emb` (réglages du tatami
    // repris le cas échéant, guide initial à son angle) ; nullopt sans forme
    // source. Pur calcul du cœur (`directional_from_tatami`).
    [[nodiscard]] std::optional<document::DirectionalFillParams>
    directionalParamsFor(const document::EmbroideryObject& emb) const;
    void buildDirectionalActions(QMenu* embMenu);
    [[nodiscard]] bool drawingDirectionGuide() const {
        return currentTool_ == Tool::DrawDirectionGuide || currentTool_ == Tool::DrawBreakLine;
    }
    void addDirectionGuidePoint(QPointF posMm);
    void updateDirectionGuidePreview(QPointF cursorSceneMm);
    // Surcouche du mode guides, appelée par renderBase : traits de direction
    // du champ, guides, ruptures et leurs poignées.
    void renderDirectionGuides();
    // Cohérence du mode guides avec la sélection (appelée par updateActions).
    void updateDirectionGuideActions();
    // Toute édition des guides passe par EditDirectionalFillCommand.
    void applyDirectionalEdit(ObjectId id, document::DirectionalFillParams params,
                              const QString& label);

    void executeOp(image::ImageOp op);
    void positionEmptyState(); // centre l'accueil dans la vue
    void updateEmptyState();   // affiche l'accueil quand aucun document
    // Applique la taille du cadre du document à la vue (si elle a changé).
    void applyCanvasToView();
    void refreshImage();
    // Une définition de contenu partagée par l'accueil, la sauvegarde,
    // l'autosave et les remplacements de document (y compris import machine).
    [[nodiscard]] bool hasDocumentContent() const;
    void displayImage(const image::Image& img);
    // Rendu en deux couches persistantes : la couche « base » (image, vecteurs,
    // poignées, régions) n'est reconstruite qu'à l'édition ; la couche « points »
    // est la seule reconstruite pendant la simulation.
    void renderBase(const image::Image& img);
    void renderStitches();
    void updateActions();

    // ---- Sélection (L5-T4a, specs/plans/ui-interaction-model.md §2.7) ----------
    // État de sélection complet. `objects` : objets vectoriels dans l'ordre de
    // sélection, le DERNIER est le principal. Un seul élément = cas legacy
    // (multiSelection_ reste vide, selectedObject_ porte l'objet).
    struct Selection {
        std::optional<RegionId> region; // région ACTIVE (la dernière cliquée)
        std::optional<ObjectId> embroidery;
        std::vector<ObjectId> objects;
        // Autres régions sélectionnées avec la région active (sélection multiple de régions).
        // Vide sans région active ; l'active n'y figure jamais.
        std::vector<RegionId> extraRegions;
    };
    // SEUL écrivain de selectedObject_/selectedRegion_/selectedEmbroidery_/
    // multiSelection_ (garde CTest check_selection_single_mutator). Normalise :
    // doublons retirés (dernière occurrence gardée), région/broderie
    // sélectionnée => au plus l'objet principal conservé, un seul objet =>
    // multiSelection_ vide. Ne rafraîchit rien (l'appelant appelle
    // displayImage/refreshImage + updateActions).
    void setSelection(Selection selection);
    // Modification partielle : copie l'état courant, applique `edit`, puis passe
    // par setSelection (jamais d'écriture directe des membres).
    void editSelection(const std::function<void(Selection&)>& edit);
    // Translate des objets vectoriels en UN pas d'annulation (CompositeCommand si
    // > 1) puis rafraîchit : partagé par les flèches et le glisser de corps.
    void translateObjects(const std::vector<ObjectId>& ids, Vec2um delta, bool coalesce = false);
    [[nodiscard]] bool isObjectSelected(ObjectId id) const;
    // Lecture : état courant sous forme de Selection (objets = multiSelection_
    // ou {selectedObject_}), pour les modifications partielles.
    [[nodiscard]] Selection currentSelection() const;
    // Objets vectoriels sélectionnés (ordre de sélection ; principal en dernier).
    [[nodiscard]] std::vector<ObjectId> selectedObjectIds() const;
    // Régions sélectionnées : les autres d'abord, l'active en dernier. Vide sans sélection.
    [[nodiscard]] std::vector<RegionId> selectedRegionIds() const;

    // --- Segmentation : sélection multiple et édition par groupes (main_window_regions.cpp) ---
    // Applique `mode` (Replace/Add/Toggle) à la sélection de régions ; la dernière région
    // de `ids` devient l'active. Rafraîchit l'affichage, les actions et le message d'état.
    void selectRegions(const std::vector<RegionId>& ids, SelectMode mode);
    void selectAllRegions();
    // Ctrl+A : tous les objets visibles si la couche Vecteurs est affichée, sinon les régions.
    void selectAllInActiveLayer();
    // Objets sélectionnables sous le point (mêmes règles pour clic, Maj/Ctrl+clic et « dessous »).
    [[nodiscard]] std::vector<ObjectId> selectableObjectsAt(QPointF posMm) const;
    // Cyclage par re-clic au même point (HP-UX-020) : renvoie l'objet à sélectionner.
    [[nodiscard]] ObjectId cycleTarget(const std::vector<ObjectId>& under, QPointF posMm);
    void zoomToSelection();
    // Copier / coller le style (HP-UX-035) : les paramètres de couture de l'objet actif sont
    // appliqués, en UNE commande annulable, aux objets sélectionnés de même type de point.
    void copyStyle();
    void pasteStyle();
    // Mode « Isoler » (HP-UX-034) : protection de SESSION, jamais écrite dans le projet. Les
    // formes non isolées sont masquées et exclues du clic, du rectangle et des flèches.
    void toggleIsolation(bool on);
    [[nodiscard]] bool isolating() const { return !isolatedIds_.empty(); }
    void selectRegionsWithSameColor();
    void selectNeighbourRegions();
    // Fusionne toutes les régions sélectionnées dans l'active (elle garde sa couleur) : un seul
    // pas d'annulation. Nécessite au moins deux régions.
    void mergeSelectedRegions();
    // Fusionne la région sélectionnée dans sa voisine à la plus longue frontière.
    void absorbSelectedRegionIntoNeighbour();
    // Donne cette couleur à toutes les régions sélectionnées (un pas d'annulation).
    void recolorRegions(const std::vector<RegionId>& ids, std::array<std::uint8_t, 3> rgb);
    // Rend à chaque région sélectionnée sa couleur moyenne dans l'image segmentée.
    void restoreSelectedRegionColors();
    // Supprime toutes les régions sélectionnées (un pas d'annulation).
    void deleteSelectedRegions();
    // Menu contextuel d'une région (clic droit sur la carte des régions).
    void showRegionContextMenu(RegionId clicked, QPoint globalPos);
    // Message d'état décrivant la sélection de régions courante.
    void announceRegionSelection();
    [[nodiscard]] bool isRegionSelected(RegionId id) const;

    // --- Segmentation : flux de travail (main_window_regions.cpp / main_window_workflow.cpp) ---
    // Fusionne `sources` dans `keep` (couleur de `keep` gardée) en UN pas d'annulation. Si des
    // objets vectoriels ont été créés depuis ces régions, demande quoi en faire (jamais d'objet
    // orphelin silencieux). `false` si l'utilisateur annule.
    bool mergeRegions(const std::vector<RegionId>& sources, RegionId keep);
    // Objets vectoriels issus de `regions` : si il y en a, propose de les conserver, de les
    // supprimer (ajoutés à `toRemove`) ou d'annuler (`false`). `verb` : « fusionnées », etc.
    [[nodiscard]] bool resolveLinkedVectorObjects(const std::vector<RegionId>& regions,
                                                  const QString& verb,
                                                  std::vector<ObjectId>& toRemove);
    // Nettoyage des îlots : demande l'aire maximale (mm²) et l'écart de couleur, puis fusionne
    // chaque petit îlot dans la voisine de couleur la plus proche, en UN pas d'annulation.
    void cleanIslands();
    // Mode « Fusionner avec… » : message d'état explicite + curseur « main » sur la vue.
    void setMergeMode(bool on);
    // Surbrillance de la région sous le curseur (carte des régions affichée, outil Sélection).
    void updateRegionHover(std::optional<QPointF> sceneMm);
    void hideRegionHover();
    // Opacité de la carte des régions (0,2 – 1,0) : réglable pour voir la photo dessous.
    void setRegionMapOpacity(double opacity);
    void setImageOpacity(double opacity);
    void buildImageOpacityControls(QMenu* viewMenu);
    void buildRegionViewControls(QMenu* segMenu);
    // Fusion de COULEURS (panneau « Couleurs », main_window_colors.cpp) : le panneau liste les
    // teintes de la segmentation ; les plans (`segmentation::plan_*`) sont prévisualisés sur une
    // COPIE de la segmentation (le document n'est pas touché) puis appliqués en une commande.
    void showColorsPanel();
    void refreshColorsPanel();
    void previewColorMerge(const segmentation::ColorMergePlan& plan);
    // Retire l'aperçu ; `redisplay` redessine la carte (faux quand l'appelant va la redessiner).
    void clearSegmentationPreview(bool redisplay);
    void applyColorMergePlan(const segmentation::ColorMergePlan& plan);
    void selectRegionsOfColors(const std::vector<std::array<std::uint8_t, 3>>& colors);
    // Parcours des régions au clavier : `step` = +1 (suivante) ou -1 (précédente), dans l'ordre des
    // identifiants avec retour au début ; sans sélection, la première (+1) ou la dernière (-1).
    void selectAdjacentRegion(int step);
    // « Segmenter l'image… » non modal avec aperçu (main_window_segment_preview.cpp) : le calcul
    // tourne dans un fil, un seul à la fois (les changements de réglage pendant un calcul sont
    // coalescés en un seul calcul suivant) ; seul le résultat de la dernière demande est affiché.
    void openSegmentationDialog();
    void requestSegmentationPreview(const segmentation::SegmentationOptions& options);
    void startSegmentationPreview(const segmentation::SegmentationOptions& options);
    void onSegmentationPreviewReady();
    // Affiche l'aperçu sur le canevas et met à jour les chiffres du dialogue.
    void showSegmentationPreview(const segmentation::Segmentation& seg, double seconds);
    void hideSegmentationPreview();
    void applySegmentationFromDialog(const segmentation::SegmentationOptions& options);
    void onSegmentationDialogClosed();
    // Actions grisées avec raison (Numérisation automatique, IA), nombre de régions affiché en
    // permanence : appelé par updateActions().
    void updateSegmentationWorkflowActions();
    // Clic sur une étape du panneau Workflow : lance l'action si elle est disponible, sinon dit
    // pourquoi elle ne l'est pas.
    void onWorkflowStepClicked(int step);
    // Tous les pixels de la région sont-ils dans le rectangle (en pixels) ?
    [[nodiscard]] bool regionFullyInside(RegionId id, int x0, int y0, int x1, int y1) const;
    [[nodiscard]] bool hasMultiSelection() const { return !multiSelection_.empty(); }
    // Retire de la sélection les ids disparus (suppression/undo/redo/chargement).
    void pruneSelection();
    // Invariants : pas de doublon, multi => >= 2 éléments, principal = dernier,
    // région/broderie => multi vide, ids vectoriels existants.
    [[nodiscard]] bool checkSelectionInvariants() const;
    // Entrées pour le câblage canevas (T4b) : sémantique Replace/Add (Maj)/
    // Toggle (Ctrl). `hit` vide = clic dans le vide (Replace : désélectionne les
    // objets et la broderie ; Add/Toggle : sans effet). La détection
    // géométrique reste à l'appelant.
    void applySelectionClick(std::optional<ObjectId> hit, SelectMode mode);
    void applySelectionRectangle(const std::vector<ObjectId>& hits, SelectMode mode);
    // Réaffiche après un changement de sélection (rendu, actions, inspecteur).
    void selectionChanged();

    document::Project project_;
    // Fichier `.osp` auquel le document est rattaché (vide tant qu'il n'a
    // jamais été enregistré) : cible de Ctrl+S et nom affiché dans le titre.
    QString currentProjectPath_;
    // Version du format du fichier ouvert quand il a été migré depuis un ancien
    // schéma (0 = pas de migration) et son chemin : au premier enregistrement
    // par-dessus, une copie `.vN.osp.bak` est faite.
    int migratedFromVersion_{0};
    QString migratedFromPath_;
    // Créneau autosave récupéré, conservé tant qu'aucun enregistrement ni
    // sauvegarde automatique n'a réussi depuis la récupération (vide sinon).
    QString pendingRecoveryOsp_;
    QString pendingRecoverySidecar_;
    // Fichiers récents (le plus récent en tête), persistés via QSettings
    // (recent_files.hpp) -- HP-FILE-003. Menu Fichier ▸ Récents et écran
    // d'accueil reconstruits à partir de cette liste par refreshRecentFilesUi().
    QStringList recentFiles_;
    QMenu* recentMenu_{nullptr};
    commands::UndoStack undoStack_;
    image::Image processed_; // dernier résultat du pipeline (pour l'affichage)
    // Entrées exactes ayant produit `processed_` (clé de contenu du cache,
    // cf. refreshImage) : `processed_` n'est recalculée que si l'image source
    // ou la pile d'opérations diffère réellement -- ce qui couvre d'office
    // undo/redo, changement de projet et nouvel import, sans liste de sites
    // d'invalidation à maintenir.
    image::Image processedSource_;
    std::vector<image::ImageOp> processedOps_;

    QGraphicsScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    EmptyStateWidget* emptyState_{nullptr};
    QLabel* cursorLabel_{nullptr};
    QToolButton* hoopStatusButton_{nullptr}; // taille du cadre cliquable (HP-UX-033)
    QList<QGraphicsItem*> baseItems_;        // couche image/vecteurs/régions
    QList<QGraphicsItem*> stitchItems_;      // couche points (reconstruite seule en simu)
    QAction* undoAct_{nullptr};
    QAction* redoAct_{nullptr};
    QAction* cropAct_{nullptr};
    QAction* showSegAct_{nullptr};
    QAction* mergeAct_{nullptr};
    QAction* mergeSelectionAct_{nullptr};
    QAction* absorbAct_{nullptr};
    QAction* cleanIslandsAct_{nullptr};
    QAction* mergeColorsAct_{nullptr};
    QAction* nextRegionAct_{nullptr};
    QAction* previousRegionAct_{nullptr};
    QDockWidget* colorsDock_{nullptr};
    SegmentationColorsPanel* colorsPanel_{nullptr};
    // Segmentation d'aperçu (fusion de couleurs) affichée à la place de celle du document.
    std::optional<segmentation::Segmentation> previewSegmentation_;
    // Dialogue « Segmenter » et son calcul d'aperçu (voir requestSegmentationPreview).
    struct SegmentationJob;
    QPointer<SegmentationDialog> segmentDialog_;
    QPointer<QThread> segmentThread_;
    std::shared_ptr<SegmentationJob> segmentJob_;
    std::optional<segmentation::SegmentationOptions> segmentPending_;
    std::shared_ptr<const image::Image> segmentSnapshot_; // image de travail à l'ouverture
    std::optional<segmentation::SegmentationOptions> segmentCachedOptions_;
    std::optional<segmentation::Segmentation> segmentCached_; // dernier aperçu calculé
    double segmentCachedSeconds_{0.0};                        // durée de ce calcul
    std::uint64_t segmentGeneration_{0};
    bool segmentAutoShownMap_{false}; // l'aperçu a affiché la carte (à masquer à la fermeture)
    QAction* selectSameColorAct_{nullptr};
    QAction* selectNeighboursAct_{nullptr};
    QAction* selectAllRegionsAct_{nullptr};
    QAction* restoreColorAct_{nullptr};
    QAction* showVectorsAct_{nullptr};
    QAction* showImageAct_{nullptr};
    QAction* showStitchesAct_{nullptr};
    std::vector<QDockWidget*> panelsToRestore_; // docks masqués par « Masquer les panneaux »
    QHash<QDockWidget*, bool>
        dockHadContent_;         // dernier état « a du contenu » (cf. setDockAutoVisible)
    bool hidePanelsMode_{false}; // mode « canevas seul » actif
    QMenu* panelsMenu_{nullptr}; // Affichage > Panneaux (toggleViewAction des docks)
    QAction* createStitchAct_{nullptr};
    QAction* createTatamiAct_{nullptr};
    QAction* createSatinAct_{nullptr};
    QAction* createStitchWithSettingsAct_{nullptr};
    QAction* createTatamiWithSettingsAct_{nullptr};
    QAction* createSatinWithSettingsAct_{nullptr};
    QAction* autoSatinAct_{nullptr};
    QAction* fillAngleAct_{nullptr};
    QAction* convertSatinAct_{nullptr};
    QAction* statsAct_{nullptr};

    // Barres d'outils et modes d'interaction.
    QToolBar* mainToolbar_{nullptr};
    QToolBar* contextToolbar_{nullptr};
    QString contextSig_; // signature de l'état affiché (évite les reconstructions)
    QToolBar* toolPalette_{nullptr};
    QAction* toolSelectAct_{nullptr};
    QAction* toolPanAct_{nullptr};
    QAction* toolRectAct_{nullptr};
    QAction* toolDrawRectAct_{nullptr};
    QAction* toolDrawEllipseAct_{nullptr};
    QAction* toolDrawPolygonAct_{nullptr};
    QAction* toolDrawPolygonRegularAct_{nullptr};
    // Nombre de côtés du polygone régulier (3-12) : lu au moment du glisser
    // (onBoxDrawn), pas seulement à la création de l'outil -- modifiable
    // sans changer d'outil entre deux formes.
    QSpinBox* polygonSidesSpin_{nullptr};
    QAction* toolDrawBezierAct_{nullptr};
    QAction* toolDrawFreeformAct_{nullptr};
    QAction* toolDrawSatinColumnAct_{nullptr};
    QAction* toolCutAct_{nullptr};
    QAction* toolTextAct_{nullptr};
    QAction* newTextAct_{nullptr};
    QAction* editTextAct_{nullptr};
    QAction* removeTextAct_{nullptr};
    QMenu* textMenu_{nullptr};
    QMenu* shapeMenu_{nullptr};
    QAction* unionAct_{nullptr};
    QAction* subtractAct_{nullptr};
    QAction* intersectAct_{nullptr};
    QAction* breakApartAct_{nullptr};
    QAction* blendZonesAct_{nullptr};
    // Option de la palette (HP-VEC-002) : le polygone et la courbe de Bézier se terminent
    // SANS être fermés (ligne ou courbe ouverte, pour un contour ou un satin de bordure).
    QAction* openPathAct_{nullptr};
    QAction* excludeAct_{nullptr};
    QAction* removeHolesAct_{nullptr};
    QAction* fillSmallHolesAct_{nullptr};
    QAction* fillBlockHolesAct_{nullptr};
    QAction* cutAct_{nullptr};
    QAction* copyAct_{nullptr};
    QAction* pasteAct_{nullptr};
    QAction* pasteInPlaceAct_{nullptr};
    QAction* duplicateWithStitchesAct_{nullptr};
    QAction* selectAllObjectsAct_{nullptr};
    QAction* selectSameColorObjectsAct_{nullptr};
    QAction* embroideryColorAct_{nullptr};
    QAction* rotateLeftAct_{nullptr};
    QAction* rotateRightAct_{nullptr};
    QAction* rotateAngleAct_{nullptr};
    QAction* mirrorHorizontalAct_{nullptr};
    QAction* mirrorVerticalAct_{nullptr};
    QAction* designCenterAct_{nullptr};
    QAction* designFitAct_{nullptr};
    QAction* designScaleAct_{nullptr};
    QAction* designRotateAct_{nullptr};
    QAction* designMirrorHorizontalAct_{nullptr};
    QAction* designMirrorVerticalAct_{nullptr};
    int pasteRepeat_{0};           // collages successifs depuis la dernière copie (décalage)
    double lastRotationDeg_{45.0}; // dernier angle saisi dans « Pivoter d'un angle… »
    // Boutons génériques partagés par tout outil de tracé multi-clics
    // (polygone/bézier/satin) : Terminer (Entrée) et Annuler (Échap),
    // toujours visibles dans la palette d'outils, actifs seulement pendant
    // un tracé en cours — le double-clic reste disponible mais n'est plus
    // le SEUL moyen de valider une forme.
    QAction* finishDrawAct_{nullptr};
    QAction* cancelDrawAct_{nullptr};
    QLabel* toolLabel_{nullptr};
    Tool currentTool_{Tool::Select};

    // Polygone en cours de tracé (outil DrawPolygon) : sommets déjà posés
    // (repère modèle, µm) + aperçu élastique (détruit à la fermeture, à
    // l'annulation, ou reconstruit à chaque `renderBase`/changement d'outil).
    std::vector<Vec2um> pendingPolygonVertices_;
    QGraphicsPathItem* polygonPreviewItem_{nullptr};
    QGraphicsEllipseItem* snapIndicatorItem_{
        nullptr}; // repère d'accroche (indépendant de baseItems_)

    // Tracé à main levée en cours (outil DrawFreeform) : points bruts captés
    // pendant le glisser (repère modèle, µm) + aperçu (même cycle de vie que
    // le polygone). Simplifié (Douglas-Peucker) seulement à la fermeture.
    std::vector<Vec2um> pendingFreeformPoints_;
    QGraphicsPathItem* freeformPreviewItem_{nullptr};

    // Colonne satin manuelle en cours de tracé (outil DrawSatinColumn) :
    // points alternés rail A / rail B (index pair = A, impair = B), repère
    // modèle µm. Une paire n'est complète qu'à l'index impair suivant —
    // `finishSatinColumn` gère un dernier point orphelin (cf. son commentaire).
    std::vector<Vec2um> pendingSatinPoints_;
    QGraphicsPathItem* satinPreviewItem_{nullptr};          // rails A/B provisoires
    QGraphicsPathItem* satinConnectorPreviewItem_{nullptr}; // paires déjà posées

    // Courbe de Bézier en cours de tracé (outil DrawBezier) : nœuds déjà
    // posés (Coin ou Lisse à poignées symétriques, repère modèle) + aperçu
    // (tracé confirmé + poignée en cours de glisser, détruits comme les
    // aperçus polygone/satin ci-dessus).
    std::vector<geometry::PathNode> pendingBezierNodes_;
    QGraphicsPathItem* bezierPreviewItem_{nullptr};       // tracé confirmé + segment élastique
    QGraphicsPathItem* bezierHandlePreviewItem_{nullptr}; // poignée en cours de glisser

    QList<QAction*> imageActions_;
    QList<QAction*> regionActions_; // nécessitent une région sélectionnée
    QAction* deleteSelectionAct_{
        nullptr}; // « Supprimer la sélection » (objectName action_deleteRegion)

    // Cache des points générés — recalculé à chaque modification du document
    // (jamais une vérité stockée, ADR-014). Un design importé (AD-04,
    // `document::Project::imported_design`) est une donnée SOURCE du
    // document, restituée par `generate_sequence` (inscription S2a) comme
    // n'importe quel autre contenu : `sequence_` reste la copie
    // d'affichage de la séquence effective, remplie au site de recalcul,
    // qu'elle vienne d'objets régénérés ou d'un design importé -- plus
    // d'exception ni de membre séparé (remplace l'ancien
    // `sequenceImported_`, §17).
    std::optional<stitch::StitchSequence> sequence_;
    QAction* exportDstAct_{nullptr};
    QAction* exportDstQuickAct_{nullptr};

    // Actions partagées entre menus et barre principale (une seule action : état,
    // raccourci et info-bulle identiques aux deux endroits).
    QAction* newProjectAct_{nullptr};
    QAction* loadProjectAct_{nullptr};
    QAction* zoomInAct_{nullptr};
    QAction* zoomOutAct_{nullptr};
    QAction* fitCanvasAct_{nullptr};
    QAction* duplicateSelectionAct_{nullptr};
    // Disposition des panneaux d'origine (capturée avant la restauration des
    // préférences) : « Réinitialiser la disposition ».
    QByteArray defaultWindowState_;
    QAction* offsetSelectionAct_{nullptr};

    QAction* saveProjectAct_{nullptr};
    QAction* saveProjectAsAct_{nullptr};
    QAction* exportDxfAct_{nullptr};
    QAction* clearRecentAct_{nullptr};

    // ---- Modèle d'interaction L5-T4b ------------------------------------------
    // Pousse le contexte de base au canevas (outil actif + modes d'édition) et
    // active le rectangle de sélection pour l'outil Sélection ; rafraîchit les
    // indications. Appelée par setTool et les bascules des modes d'édition.
    void updateInteractionContext();
    // Contexte d'interaction « logique » (table de gestes) de l'état courant.
    [[nodiscard]] Context interactionContext() const;
    // Recalcule la ligne d'indications (widget permanent de la barre d'état).
    void refreshHints();
    // Texte complet des indications (non élidé) : lu par les tests et l'infobulle.
    [[nodiscard]] QString hintsText() const { return hintsFullText_; }
    void showGesturesDialog();
    void showQuickStartDialog();
    // Manuel en ligne (help_manual.hpp, main_window_help.cpp) : section selon le contexte.
    void showManual(const QString& heading = {});
    void showContextualHelp();
    [[nodiscard]] QString contextualHelpHeading() const;
    void startGuidedTour();
    void buildNavigationMenu(QMenu* viewMenu);
    // Rendu réaliste des points (main_window_realistic.cpp) : sous-menu
    // Affichage > Rendu réaliste, fenêtre de réglages et peinture du cache.
    void buildRealisticMenu(QMenu* viewMenu);
    // Vrai si la couche points a été peinte en rendu réaliste (sinon l'appelant
    // dessine les lignes : désactivé, simulation, dézoom fort, rien à peindre).
    bool renderRealisticStitches();
    void applyRealisticPreferences(const RealisticPreferences& prefs);
    void showRealisticDialog();
    QAction* realisticAct_{nullptr};
    RealisticViewState realistic_;
    // Fiche de production imprimable / PDF (main_window_production.cpp).
    void buildProductionMenu(QMenu* fileMenu);
    void showProductionSheet();
    QAction* productionAct_{nullptr};
    // Coloris, tissu, fusion de fils (HP-THR-006/008/011, main_window_colorways.cpp).
    void buildColorwayMenu(QMenu* viewMenu);
    void showColorwayDialog();
    void showThreadMergeDialog();
    void syncColorwayUi();
    ColorwayDialog* colorwayDialog_{nullptr};
    QAction* colorwayAct_{nullptr};
    QAction* threadMergeAct_{nullptr};
    // Zones réservées au câblage des entrées livrées en cœur (une zone par lot, ne pas mélanger).
    // [cablage-A:debut]
    // Exporter l'image (HP-FMT-011, main_window_export_image.cpp).
    void buildExportImageAction(QMenu* fileMenu);
    void exportImage();
    QAction* exportImageAct_{nullptr};
    // [cablage-A:fin]
    // [cablage-B:debut]
    // Outils de tracé : Lisser, Convertir en courbes, formes prédéfinies
    // (main_window_vector_tools.cpp, HP-VEC-007 / 008 / 014).
    enum class PresetShape { Star, Heart, Arrow, RoundedRectangle, Arc, Spiral };
    void buildVectorToolsMenu();
    void updateVectorToolActions();
    void smoothSelection(Micrometers tolerance);
    void convertSelectionToCurves();
    // `sizeMm` : dimension principale de la forme ; `starPoints` : branches de l'étoile.
    void insertPresetShape(PresetShape shape, double sizeMm, int starPoints = 5);
    void promptPresetShape(PresetShape shape);
    QAction* smoothAct_{nullptr};
    QAction* convertToCurvesAct_{nullptr};
    QMenu* presetShapesMenu_{nullptr};
    // [cablage-B:fin]
    // [cablage-C:debut]
    // Outil Règle (HP-VEC-010, main_window_measure.cpp) : mesure purement visuelle, aucune
    // commande. Outil Pipette (HP-THR-012, main_window_eyedropper.cpp) : l'assignation passe par
    // SetObjectThreadCommand sur undoStack_.
    void buildMeasureTool();
    void buildEyedropperTool();
    // Ajoute Règle / Copier la mesure / Pipette à la palette d'outils (appelé par
    // buildToolPalette).
    void attachMeasureEyedropperTools();
    // Appelé par setTool() : coche, curseur, libellé et message d'état des outils Règle/Pipette.
    void syncMeasureEyedropperTool(Tool tool);
    void showMeasureRuler(QPointF startMm, QPointF endMm, bool finished);
    void clearMeasureRuler();
    void copyLastMeasure();
    // Échantillonne l'image affichée sous `posMm` (scène, mm) et ouvre le menu des fils proches.
    void eyedropperAt(QPointF posMm);
    void assignEyedropperChoice(const std::optional<thread_palette::ThreadKey>& key,
                                std::array<std::uint8_t, 3> rgb, const QString& label);
    // Pinceau de déformation (HP-VEC-018, main_window_brush.cpp).
    struct BrushStroke {
        ObjectId object;
        std::vector<geometry::PathSet> working; // géométrie courante du coup de pinceau
        QPointF lastSceneMm;
    };
    void buildBrushTool();
    void attachBrushTool();
    void syncBrushTool(Tool tool);
    void setBrushSettings(double radiusMm, double strength);
    void updateBrushRing(QPointF sceneMm);
    void hideBrushRing();
    void beginBrushStroke(QPointF sceneMm);
    void moveBrushStroke(QPointF sceneMm, Qt::KeyboardModifiers mods);
    void endBrushStroke(QPointF sceneMm);
    void cancelBrushStroke();
    // Pot de peinture (main_window_bucket.cpp) : le calcul est dans
    // commands::make_fill_gap_command.
    void buildBucketTool();
    void attachBucketTool();
    void syncBucketTool(Tool tool);
    void bucketFillAt(QPointF posMm);
    QAction* toolBucketAct_{nullptr};
    // Clic-pour-broder (HP-AUTO-003, main_window_click_to_stitch.cpp) : le calcul est dans
    // segmentation::grow_region et commands::make_click_to_stitch_command ; ici le câblage
    // (outil, barre de réglages, aperçu, clic -> commande annulable).
    void buildClickToStitchTool();
    void attachClickToStitchTool();
    void syncClickToStitchTool(Tool tool);
    void clickToStitchAt(QPointF sceneMm);
    void updateClickToStitchPreview(QPointF sceneMm);
    void hideClickToStitchPreview();
    void setClickToStitchTolerance(int tolerance);
    void setClickToStitchKind(int kindIndex);
    QAction* toolClickToStitchAct_{nullptr};
    QToolBar* clickToStitchBar_{nullptr};
    QSlider* clickToStitchToleranceSlider_{nullptr};
    QSpinBox* clickToStitchToleranceSpin_{nullptr};
    QComboBox* clickToStitchKindCombo_{nullptr};
    QGraphicsPixmapItem* clickToStitchPreviewItem_{nullptr};
    QTimer* clickToStitchPreviewTimer_{nullptr};
    std::optional<QPointF> clickToStitchHoverMm_;
    int clickToStitchTolerance_{12}; // ΔE76 maximal à la couleur cliquée
    int clickToStitchKind_{0};       // index de commands::ClickToStitchKind
    std::size_t clickToStitchPreviewPixels_{0};
    QAction* toolBrushAct_{nullptr};
    QAction* brushSettingsAct_{nullptr};
    QGraphicsEllipseItem* brushRing_{nullptr};
    std::optional<BrushStroke> brushStroke_;
    double brushRadiusMm_{5.0};
    double brushStrength_{0.6};
    QAction* toolMeasureAct_{nullptr};
    QAction* copyMeasureAct_{nullptr};
    QAction* toolEyedropperAct_{nullptr};
    QGraphicsItem* measureRuler_{nullptr}; // règle temporaire (jamais dans le document)
    QString lastMeasureText_;
    QPointer<QMenu> eyedropperMenu_; // menu des fils proches (non bloquant : popup)
    std::array<std::uint8_t, 3> eyedropperRgb_{};
    // [cablage-C:fin]
    // [cablage-D:debut]
    // [cablage-D:fin]
    // [cablage-E:debut]
    // HP-VIEW-005 : carte de densité (main_window_density.cpp). Image mise en cache sous une
    // clé (séquence effective + seuil), recalculée hors du fil UI quand la clé change.
    void buildDensityFeature();
    void refreshDensityOverlay(); // appelée par renderStitches() (pose le cache ou planifie)
    void recomputeDensity();
    void setDensityMapEnabled(bool on);
    [[nodiscard]] double densityThresholdValue() const;
    QAction* densityMapAct_{nullptr};
    // HP-VIEW-014 : carte d'épaisseur estimée, même surimpression que la densité (exclusives).
    QAction* thicknessMapAct_{nullptr};
    int densityCellUm_{0}; // côté de case du dernier calcul d'épaisseur (suit le zoom)
    [[nodiscard]] bool thicknessMode() const {
        return thicknessMapAct_ != nullptr && thicknessMapAct_->isChecked();
    }
    // Côté de case adapté au zoom courant de la vue (stitch_analysis::adaptive_cell).
    [[nodiscard]] Micrometers thicknessCellForView() const;
    void setThicknessMapEnabled(bool on);
    void recomputeThickness();
    // Pose le raster calculé comme image de surimpression (partagé densité / épaisseur).
    void adoptOverlayRaster(const DensityRaster& raster, Vec2um origin, Micrometers cell);
    QTimer* densityTimer_{nullptr};
    QPixmap densityPixmap_;
    QRectF densityRect_; // zone couverte par l'image, mm (repère scène)
    std::uint64_t densityKey_{0};
    bool densityValid_{false};
    double densityScaleMax_{1.0};
    int densityComputeCount_{0}; // calculs réellement faits (tests)
    // [cablage-E:fin]
    // [cablage-HOOP:debut]
    // Machine du projet (HP-HOOP-002, main_window_machine.cpp).
    void chooseMachine();
    void applyMachineChoice(const QString& machineId, const QString& hoopId);
    QAction* machineAct_{nullptr};
    // [cablage-HOOP:fin]
    void applyNavigationPreset(Preset preset);
    // Duplique `ids` (copies exactes, même position) puis translate les COPIES de
    // `delta`, en un seul pas d'annulation (CompositeCommand) ; les copies deviennent
    // la sélection. Alt + glisser (ligne M4).
    void duplicateAndTranslate(const std::vector<ObjectId>& ids, Vec2um delta);
    // Ctrl tenu (évènements du canevas) : l'accroche du tracé est suspendue (ligne D5).
    [[nodiscard]] bool snapSuspended() const { return (heldModifiers_ & Qt::ControlModifier) != 0; }
    QLabel* hintsLabel_{nullptr};
    QString hintsFullText_;
    Qt::KeyboardModifiers heldModifiers_{};
    // Surbrillance de pré-sélection (S11) : un seul item, masqué hors outil Sélection.
    QGraphicsPathItem* hoverItem_{nullptr};
    // Calcul de la surbrillance (cache de contours, voir hoverCache_) et sa planification :
    // premier mouvement traité aussitôt, les suivants coalescés par pas de 16 ms.
    void updateHoverHighlight(std::optional<QPointF> sceneMm);
    void scheduleHoverHighlight(QPointF sceneMm);
    void hideHoverHighlight();
    struct HoverShape {
        ObjectId id;
        QPainterPath path;
        QRectF bounds;
    };
    std::vector<HoverShape> hoverCache_;
    bool hoverCacheValid_{false};

    // Pixmap de l'image de base mémorisé : la conversion QImage -> QPixmap (copie complète) était
    // refaite à chaque rafraîchissement, même quand seule la broderie changeait.
    QPixmap basePixmapCache_;
    std::uint64_t basePixmapKey_{0};
    bool basePixmapKeyValid_{false};

    QTimer* hoverTimer_{nullptr};
    std::optional<QPointF> hoverPending_;
    int hoverComputations_{0}; // compteurs (tests) : calculs de survol, contours construits
    int hoverPathBuilds_{0};
    QAction* snapNodesAct_{nullptr};
    // Sommets des AUTRES objets (réglage edit/snapNodesOnDrag), rayon <= 1 mm et <= 10 px.
    [[nodiscard]] std::optional<QPointF> findNodeSnapMm(QPointF cursorSceneMm,
                                                        ObjectId exclude) const;
    QPointer<GesturesDialog> gesturesDialog_;
    QPointer<QuickStartDialog> quickStartDialog_;
    QPointer<ManualDialog> manualDialog_;
    QPointer<QMenu> selectBelowMenu_;
    std::vector<ObjectId> isolatedIds_;
    std::optional<document::StitchParams> styleClipboard_;
    QAction* copyStyleAct_{nullptr};
    QAction* pasteStyleAct_{nullptr};
    QAction* isolateAct_{nullptr};
    QPointF cycleAnchorMm_;
    std::vector<ObjectId> cycleIds_;
    int cycleIndex_{0};
    QAction* zoomSelectionAct_{nullptr};
    QAction* zoomPreviousAct_{nullptr};
    QAction* zoomActualAct_{nullptr};
    QActionGroup* navigationGroup_{nullptr};
    QAction* navOpenStitchAct_{nullptr};
    QAction* navTouchpadAct_{nullptr};
    QAction* helpQuickStartAct_{nullptr};
    QAction* helpGesturesAct_{nullptr};
    QAction* helpManualAct_{nullptr};
    QAction* helpContextAct_{nullptr};
    QAction* helpTourAct_{nullptr};
    QAction* aboutAct_{nullptr};
    // Actions du guide de prise en main (étapes construites à partir d'elles).
    QAction* openImageAct_{nullptr};
    QAction* openAnyAct_{nullptr};
    QAction* segmentAct_{nullptr};
    QAction* vectorizeRegionAct_{nullptr};
    QAction* autoDigitizeAct_{nullptr};
    QAction* aiSegmentAct_{nullptr};
    // Segmentation : nombre de régions (barre d'état permanente), surbrillance de survol d'une
    // région (un seul item, réutilisé) et opacité de la carte des régions.
    QLabel* regionCountLabel_{nullptr};
    QGraphicsPixmapItem* regionHoverItem_{nullptr};
    std::optional<RegionId> regionHoverId_;
    double regionMapOpacity_{0.9};
    double imageOpacity_{1.0}; // opacité de l'image originale, réglage purement visuel

    std::optional<RegionId> selectedRegion_;
    // Régions sélectionnées EN PLUS de selectedRegion_ (sélection multiple). Écrit uniquement
    // par setSelection().
    std::vector<RegionId> extraRegions_;
    std::optional<ObjectId> selectedObject_;
    std::optional<ObjectId> selectedEmbroidery_; // objet de broderie choisi dans l'ordre de couture
    // Multi-sélection d'objets vectoriels : VIDE dans le cas legacy (0 ou 1
    // objet, porté par selectedObject_) ; sinon >= 2 ids, principal =
    // back() == selectedObject_. Écrit uniquement par setSelection().
    std::vector<ObjectId> multiSelection_;
    bool mergeMode_{false};

    // Mode d'édition des points générés (Lot 8.2, cf. docs/lot8-manual-editing-design.md §6) :
    // mode exclusif, lié à UN SEUL objet capturé à l'activation
    // (`stitchEditTarget_`), jamais suivi automatiquement d'un changement de
    // sélection (sortie propre exigée -- cf. updateActions). `stitchEditView_`
    // est la vue brute/fingerprint/compteur de cet objet au moment du dernier
    // rafraîchissement : source unique pour placer les poignées ET construire
    // MoveStitchPointCommand, jamais recalculée à la main ailleurs.
    QAction* stitchEditModeAct_{nullptr};
    std::optional<ObjectId> stitchEditTarget_;
    std::optional<stitch_generation::ObjectEditView> stitchEditView_;
    // --- Édition de points : sélection, suppression, insertion (HP-EDIT-001/002/003) ---------
    // Indices bruts (`base_index`) des points sélectionnés ; remise à zéro dès que la vue brute
    // de l'objet change (empreinte) ou que le mode se ferme.
    std::set<std::size_t> stitchSelection_;
    std::uint64_t stitchSelectionFingerprint_{0};
    QAction* selectAllStitchPointsAct_{nullptr};
    std::array<QAction*, 6> stitchPointActs_{};
    [[nodiscard]] bool stitchPointEditActive() const;
    Vec2um stitchPointDisplayPos(std::size_t index);
    std::unordered_set<std::size_t> deletedStitchPoints();
    void addStitchPointHandle(std::size_t baseIndex, Vec2um pos, bool selected, bool deleted);
    void clickStitchPoint(std::size_t index, Qt::KeyboardModifiers mods);
    void restyleStitchPointHandles();
    void addInsertedPointHandle(std::size_t baseIndex, std::size_t slot, Vec2um pos);
    void moveInsertedStitchPoint(ObjectId id, std::size_t baseIndex, std::size_t slot, Vec2um to,
                                 std::uint64_t fingerprint, std::uint32_t pointCount);
    void selectAllStitchPoints();
    void selectStitchPointsInRect(QRectF rectMm, SelectMode mode);
    void moveStitchPoints(ObjectId targetId, std::size_t anchor, Vec2um from, Vec2um to,
                          std::uint64_t fingerprint, std::uint32_t pointCount);
    bool editSelectedStitchPoints(int action);
    bool insertStitchPointAt(QPointF sceneMm);
    void showStitchPointMenu(std::size_t index, QPoint screenPos);
    void buildStitchPointEditActions(QMenu* menu);
    void updateStitchPointActions();
    // Mode unifié (rails + guides ensemble) — voir buildMenus() pour le
    // détail ; simple agrégateur au-dessus des deux modes ci-dessous.
    QAction* satinEditModeAct_{nullptr};
    // Édition paramétrique des barreaux satin : contrairement au mode 8.2,
    // ces poignées régénèrent la colonne et restent donc dans le modèle.
    QAction* satinGuideModeAct_{nullptr};
    QAction* addSatinGuideAct_{nullptr};
    QAction* removeSatinGuideAct_{nullptr};
    std::optional<ObjectId> satinGuideTarget_;
    std::optional<std::size_t> selectedSatinGuide_;
    // Édition des NŒUDS de rail (mode remodelage) : distinct du mode guides
    // ci-dessus (qui édite les barreaux transversaux) — ici, les nœuds de
    // rail_a/rail_b eux-mêmes, déplaçables et scindables (double-clic sur un
    // segment insère un nœud par subdivision De Casteljau exacte).
    QAction* railEditModeAct_{nullptr};
    std::optional<ObjectId> railEditTarget_;
    // Mode « Guides de direction » (remplissage directionnel) : objet capturé
    // à l'activation, jamais suivi automatiquement (même règle que les modes
    // satin). Tracé en cours : points posés (repère modèle) + aperçu.
    QAction* directionGuideModeAct_{nullptr};
    QAction* autoDirectionGuideAct_{nullptr};
    QAction* twoColorBlendAct_{nullptr};
    QAction* addLayerAct_{nullptr};
    QAction* drawDirectionGuideAct_{nullptr};
    QAction* drawBreakLineAct_{nullptr};
    std::optional<ObjectId> directionGuideTarget_;
    AutoSatinSummaryCache autoSatinSummaryCache_;
    std::vector<Vec2um> pendingGuidePoints_;
    QGraphicsPathItem* guidePreviewItem_{nullptr};
    // État Clean/ManuallyEdited/Dirty des objets retouchés (absents = Clean),
    // recalculé à chaque `refreshImage()` (cf. `classify_all_edit_states`) —
    // jamais recalculé ailleurs (panneau Document, inspecteur, barre
    // contextuelle et gating du mode d'édition en lisent tous la même copie).
    std::vector<std::pair<ObjectId, stitch_generation::ObjectEditState>> editStates_;
    // Incrémenté à chaque remplacement intégral de `project_` (nouveau
    // document, projet chargé, import DST -- jamais sur une mutation en place
    // via l'undo stack). Capturé par valeur par les commandes différées
    // (QTimer::singleShot, cf. renderBase/MoveStitchPointCommand) : si la
    // génération a changé au moment où le timer se déclenche, le document a
    // été remplacé entre-temps et la commande différée est abandonnée plutôt
    // que d'exécuter sur -- ou pire, muter -- un projet qui n'est plus celui
    // pour lequel elle a été construite.
    std::uint64_t documentGeneration_{0};

    // --- HP-PERF-009 : régénération différée à l'édition de paramètres -----------------------
    // La commande d'annulation (fusionnée) est appliquée tout de suite ; la régénération des
    // points attend 150 ms d'inactivité puis se calcule hors du fil d'interface, sur un instantané
    // du projet (`stitch_generation::refresh_context`, la même fonction que la voie synchrone :
    // sortie octet pour octet identique). L'aperçu précédent reste affiché pendant le calcul ;
    // un résultat périmé (nouvelle édition, undo, autre mutation : tous passent par
    // `refreshImage`) est rejeté.
    static constexpr int kParamsRefreshDelayMs = 150;
    struct AsyncRefresh;
    // Après une édition appliquée au document : met à jour les indicateurs immédiats puis
    // arme (ou réarme) le débounce. Remplace `refreshImage()` dans les chemins d'édition de
    // paramètres.
    void scheduleParamsRefresh();
    // Exécute TOUT DE SUITE et de façon synchrone la régénération en attente (export, analyse,
    // statistiques, simulation, tests...) ; sans effet si rien n'attend.
    void flushPendingRefresh();
    void startAsyncRefresh();
    void pollAsyncRefresh();
    void commitCreationMemory();
    QTimer* paramsRefreshTimer_{nullptr};
    QTimer* asyncPollTimer_{nullptr};
    bool paramsRefreshPending_{false};
    std::uint64_t refreshSerial_{0};
    int refreshCount_{0}; // nombre de passages dans refreshImage (compteur de tests)
    std::shared_ptr<AsyncRefresh> asyncRefresh_;
    std::optional<stitch_generation::RefreshContext> precomputedContext_;
    std::optional<document::StitchParams> pendingCreationMemory_;
    std::function<void()> refreshWorkerHook_; // test seulement : exécuté dans le fil de calcul

    // HP-UX-029 : retouches manuelles qu'une édition de paramètres vient de désactiver.
    struct OverrideWarning {
        ObjectId id;
        int count{0};
        document::StitchParams before; // paramètres d'avant l'édition (undo => bandeau retiré)
    };
    std::optional<OverrideWarning> overrideWarning_;
    void noteOverridesInvalidated(ObjectId id, const document::StitchParams& before);
    void refreshOverrideWarning();

    // Analyse.
    QDockWidget* analysisDock_{nullptr};
    QListWidget* analysisList_{nullptr};
    QAction* analyzeAct_{nullptr};
    QLabel* analysisSummary_{nullptr};
    QLabel* analysisStale_{nullptr};
    QLabel* analysisHint_{nullptr};
    QComboBox* analysisFilter_{nullptr};
    QTimer* analysisTimer_{nullptr}; // ré-analyse différée (300 ms) quand le document change
    bool analysisIsStale_{false};
    bool analysisHasResult_{false};

    // Fils (main_window_threads.cpp) : bibliothèque active (intégrés + nuanciers de
    // l'utilisateur) et panneau.
    QDockWidget* threadDock_{nullptr};
    ThreadPanel* threadPanel_{nullptr};
    ThreadStrip* threadStrip_{nullptr};
    QToolBar* threadStripBar_{nullptr};
    thread_palette::ThreadLibrary threadLibrary_{thread_palette::ThreadLibrary::with_builtin()};
    QHash<QString, QString> userChartFiles_; // identifiant de nuancier -> fichier copié

    // Raccourcis fenêtre dont l'activation dépend du contexte (cf. updateShortcutsState).
    QShortcut* escapeShortcut_{nullptr};
    QShortcut* drawReturnShortcut_{nullptr};
    QShortcut* drawEnterShortcut_{nullptr};
    QShortcut* drawBackspaceShortcut_{nullptr};
    QAction* hidePanelsAct_{nullptr};
    QLabel* savedLabel_{nullptr};
    // Instant de la dernière sauvegarde (ou ouverture) : « Enregistré à HH:mm » ne doit pas
    // changer quand un undo ramène le document à l'état enregistré (UX-32).
    QTime savedAt_;
    QTimer* statusClearTimer_{nullptr};

    // Structure du document (Objets / Régions).
    QDockWidget* documentDock_{nullptr};
    DocumentPanel* documentPanel_{nullptr};

    // Indicateur de workflow.
    QDockWidget* workflowDock_{nullptr};
    WorkflowPanel* workflowPanel_{nullptr};

    // Inspecteur de propriétés.
    QDockWidget* propertiesDock_{nullptr};
    PropertiesPanel* propertiesPanel_{nullptr};
    int inspectedKind_{-1}; // -1 rien, 0 broderie, 1 vecteur, 2 région
    std::uint64_t inspectedId_{0};

    // Ordre de couture.
    QDockWidget* orderDock_{nullptr};
    QListWidget* orderList_{nullptr};
    QPushButton* orderUpBtn_{nullptr};
    QPushButton* orderDownBtn_{nullptr};
    QPushButton* orderLockBtn_{nullptr};
    // Ligne du panneau Ordre -> indice dans `embroidery_objects` (ordre de couture réel, calques
    // inclus : cf. `Project::sewingOrder`). Reconstruit par refreshOrderPanel.
    std::vector<std::size_t> orderRows_;
    [[nodiscard]] const document::EmbroideryObject* orderRowObject(int row) const;
    void updateOrderButtons();
    QLabel* orderCostLabel_{nullptr};
    QComboBox* orderStrategyCombo_{nullptr};

    // Filtres d'affichage de la broderie.
    QDockWidget* filterDock_{nullptr};
    std::array<QCheckBox*, 3> typeChecks_{nullptr, nullptr, nullptr}; // contour/tatami/satin
    QVBoxLayout* colorFilterLayout_{nullptr};
    QDoubleSpinBox* minAreaSpin_{nullptr};
    std::array<bool, 3> showType_{true, true, true};
    std::set<std::uint32_t> hiddenColors_; // 0xRRGGBB masqués
    double minAreaMm2_{0.0};

    // Simulation de couture. Quand active (simStep_ >= 0), l'affichage ne
    // montre les points que jusqu'à cet index.
    QToolBar* simToolbar_{nullptr};
    QSlider* simSlider_{nullptr};
    // HP-SIM-003 : objet en cours de couture, surligné dans les listes et le film couleur.
    std::optional<ObjectId> simHighlightId_;
    void applySimulationHighlight();
    SimulationNavBar* simNavBar_{nullptr}; // HP-SIM-002/004 : pas à pas, objet, temps
    QLabel* simLabel_{nullptr};
    QLabel* simSwatch_{nullptr};      // pastille de la couleur du fil courant
    QLabel* simObjectLabel_{nullptr}; // objet courant
    QComboBox* simSpeedCombo_{nullptr};
    double simAccum_{0.0}; // reste fractionnaire d'avance (vitesses < x1)
    SimWalkState simWalk_;
    bool simWalkValid_{false}; // simWalk_ décrit exactement ce qui est dessiné
    QGraphicsItem* simMarker_{nullptr};
    QAction* simPlayAct_{nullptr};
    QAction* simExitAct_{nullptr};
    QAction* simPrevColorAct_{nullptr};
    QAction* simNextColorAct_{nullptr};
    QTimer* simTimer_{nullptr};
    int simStep_{-1}; // -1 = simulation inactive (tout affiché)

    // HP-FILE-004 : tick périodique (120 s, non configurable en P0) qui
    // déclenche onAutosaveTick() -- construit dans le constructeur, démarré
    // immédiatement, arrêté par closeEvent() sur une fermeture acceptée.
    QTimer* autosaveTimer_{nullptr};

    [[nodiscard]] bool simulating() const { return simStep_ >= 0; }

    // Conversion coordonnées scène (mm) -> pixel de l'image de travail.
    [[nodiscard]] std::optional<QPoint> mmToImagePixel(QPointF mm) const;
    // Chemin Qt (scène, mm, Y vers le bas) d'un objet vectoriel.
    [[nodiscard]] static QPainterPath objectPainterPath(const document::VectorObject& object);
};

} // namespace openstitch::desktop
