// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

#include "openstitch/commands/undo_stack.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "tools.hpp"

class QGraphicsScene;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsEllipseItem;
class QLabel;
class QAction;
class QListWidget;
class QDockWidget;
class QSlider;
class QTimer;
class QToolBar;
class QComboBox;
class QCheckBox;
class QVBoxLayout;
class QDoubleSpinBox;
class QSpinBox;
class QMenu;

namespace openstitch::desktop {

class CanvasView;
class PropertiesPanel;
class DocumentPanel;
class WorkflowPanel;
class EmptyStateWidget;
// Seam de test unique (déclaré ici pour le friend ci-dessous) : donne à
// MainWindowTest (tests/unit/desktop/test_main_window.cpp) accès à
// applyLoadedProject() sans exposer de méthode de chargement dans l'API
// publique de production.
class MainWindowTest;

// Fenêtre principale. Règle du projet : aucune logique métier dans les
// widgets — chargement (libs/image), placement (libs/document), transformations
// (libs/image::ops) et undo/redo (libs/commands) viennent des bibliothèques
// cœur ; cette classe câble, affiche et recalcule l'aperçu.
class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class MainWindowTest;

public:
    MainWindow();
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // Réponse de l'utilisateur au choix multiple §23 du plan de refonte
    // satin (2026-08-14 : « Continuer avec satin partiel / Utiliser tatami
    // pour le reliquat / Annuler »). `Cancel` doit rester possible SANS
    // avoir encore rien committé au document -- c'est pourquoi ce choix est
    // demandé AVANT `undoStack_.execute(...)`, jamais après (contrairement à
    // l'ancienne information post-hoc qu'il remplace). Déclaré ici (avant
    // `private slots:`) : un type utilisé comme retour d'une declaration
    // doit être visible textuellement avant cette declaration -- moc peine
    // en plus a parser une declaration de type au milieu d'une liste de
    // slots.
    enum class SatinCoverageChoice { ContinuePartial, UseTatami, Cancel };

private slots:
    // Document vierge (Ctrl+N, HP-FILE-001) : garde des modifications non
    // enregistrées, puis remplacement complet du document par un `Project`
    // par défaut via `applyLoadedProject` — un seul chemin de remplacement de
    // document, donc jamais un mode d'édition ou un tracé en cours qui
    // survivrait au nouveau document.
    void newProject();
    void openImage();
    void undo();
    void redo();
    void adjustBrightnessContrast();
    void quantizeColors();
    void onCropSelected(QRectF rectMm);
    void segmentImage();
    void onCanvasClicked(QPointF posMm);
    void onCanvasContextMenu(QPointF posMm, QPoint globalPos);
    // Rectangle/ellipse dessiné (outils DrawRectangle/DrawEllipse) : interprété
    // selon `currentTool_`. Maj enfoncée + DrawEllipse = cercle contraint.
    void onBoxDrawn(QRectF rectMm, Qt::KeyboardModifiers modifiers);
    // Double-clic : clôt un polygone en cours de tracé (outil DrawPolygon).
    void onCanvasDoubleClicked(QPointF posMm);
    void deleteSelectedRegion();
    void recolorSelectedRegion();
    void vectorizeSelectedRegion();
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
    // Choix explicite quand une intention SATIN n'a pu être satisfaite qu'en
    // partie (§12/§23 du plan de refonte satin, 2026-08-14 : « aucun
    // fallback silencieux vers tatami », mais un VRAI choix actionnable
    // plutôt qu'une simple information). Retourne `ContinuePartial`
    // directement, sans dialogue, si le reliquat est négligeable (bruit de
    // pointe/jonction, même seuil que l'ancienne fonction). `sourceAreaMm2`
    // sert à exprimer le reliquat en pourcentage, pas seulement en mm² brut.
    SatinCoverageChoice
    askAboutIncompleteSatinCoverage(const std::vector<geometry::PathSet>& unresolvedResidual,
                                    double sourceAreaMm2);
    // Construit un remplissage tatami (VectorObject + EmbroideryObject,
    // même schéma que le repli automatique de `autodigitize.cpp`, jamais
    // réimplémenté différemment ici) pour chaque morceau de `residual` dont
    // l'aire dépasse un seuil de bruit géométrique -- réponse concrète au
    // choix "Utiliser tatami pour le reliquat" ci-dessus.
    void appendTatamiFallbackObjects(const std::vector<geometry::PathSet>& residual,
                                     const document::VectorObject& source,
                                     std::vector<document::VectorObject>& vectorsOut,
                                     std::vector<document::EmbroideryObject>& embroideriesOut);
    void openAiPreferences();
    void createRunningStitchObject();
    void createTatamiObject();
    void createSatinObject();
    // Ligne de coupe (outil DrawSatinCutLine, façon Ink/Stitch "cut line") :
    // découpe géométriquement `source->paths.front()` en morceaux
    // (`geometry::cut_path_set`) puis convertit chacun en colonne(s) satin
    // indépendamment (auto_satin::build_satin_columns par morceau) -- guide
    // manuel de décomposition aux jonctions difficiles, en complément de la
    // détection automatique de `createSatinObject()`. `cutA`/`cutB` : les
    // deux points du glisser, coordonnées modèle. Renvoie `true` si au moins
    // une colonne satin a été créée (l'appelant repasse alors en outil
    // Sélection), `false` si la coupe n'a rien produit (reste sur l'outil
    // pour laisser l'utilisateur réessayer).
    bool createSatinObjectWithCutLine(Vec2um cutA, Vec2um cutB);
    void onSatinCutLineDragging(QPointF anchorMm, QPointF currentMm);
    void onSatinCutLineCommitted(QPointF anchorMm, QPointF handleMm);
    void autoConvertToSatin();
    void changeFillAngle();
    void convertSatinsToTatami();
    // Change le type de points d'un objet de broderie (contour/tatami/satin).
    // Le type satin exige des rails, construits depuis le contour source.
    void setStitchType(ObjectId embroideryId, int type);
    void showStatistics();
    void setHoopSize();
    void exportDst();
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
    // Mode « Guides de direction » : affiche l'aperçu du champ, les guides et
    // les lignes de rupture de l'objet ciblé, avec poignées déplaçables.
    void onDirectionGuideModeToggled(bool on);
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
    void refreshRecentFilesUi();
    void updateWindowTitle();
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
    void buildFilterPanel();
    void refreshFilterPanel();
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
    // Objet de broderie rattaché à un objet vectoriel (nullptr si aucun).
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
    [[nodiscard]] std::optional<QPointF> findSnapPointMm(QPointF cursorSceneMm) const;
    // Affiche/masque le repère visuel d'accroche (cercle) au point donné.
    void updateSnapIndicator(std::optional<QPointF> snapSceneMm);
    // Miroir de ce qui précède pour le tracé à main levée (outil
    // DrawFreeform) : un point par évènement CanvasView::freeformPointMm
    // (pas de segment élastique jusqu'au curseur, contrairement au polygone —
    // chaque position déjà captée EST le tracé).
    void onFreeformPointAdded(QPointF posMm);
    void finishFreeform();
    void cancelFreeformDraw();
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
    void displayImage(const image::Image& img);
    // Rendu en deux couches persistantes : la couche « base » (image, vecteurs,
    // poignées, régions) n'est reconstruite qu'à l'édition ; la couche « points »
    // est la seule reconstruite pendant la simulation.
    void renderBase(const image::Image& img);
    void renderStitches();
    void updateActions();

    document::Project project_;
    // Fichier `.osp` auquel le document est rattaché (vide tant qu'il n'a
    // jamais été enregistré) : cible de Ctrl+S et nom affiché dans le titre.
    QString currentProjectPath_;
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
    QList<QGraphicsItem*> baseItems_;   // couche image/vecteurs/régions
    QList<QGraphicsItem*> stitchItems_; // couche points (reconstruite seule en simu)
    QAction* undoAct_{nullptr};
    QAction* redoAct_{nullptr};
    QAction* cropAct_{nullptr};
    QAction* showSegAct_{nullptr};
    QAction* mergeAct_{nullptr};
    QAction* showVectorsAct_{nullptr};
    QAction* showImageAct_{nullptr};
    QAction* showStitchesAct_{nullptr};
    std::vector<QDockWidget*> panelsToRestore_; // docks masqués par « Masquer les panneaux »
    QAction* createStitchAct_{nullptr};
    QAction* createTatamiAct_{nullptr};
    QAction* createSatinAct_{nullptr};
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
    QAction* toolDrawSatinCutLineAct_{nullptr};
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
    QGraphicsPathItem* cutLinePreviewItem_{nullptr};      // ligne de coupe en cours de glisser

    QList<QAction*> imageActions_;
    QList<QAction*> regionActions_; // nécessitent une région sélectionnée

    // Cache des points générés — recalculé à chaque modification du document
    // (jamais une vérité stockée, ADR-014). Exception : une séquence importée
    // d'un DST est la vérité (le DST ne contient pas d'objets, §17).
    std::optional<stitch::StitchSequence> sequence_;
    bool sequenceImported_{false};
    QAction* exportDstAct_{nullptr};

    std::optional<RegionId> selectedRegion_;
    std::optional<ObjectId> selectedObject_;
    std::optional<ObjectId> selectedEmbroidery_; // objet de broderie choisi dans l'ordre de couture
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
    QAction* drawDirectionGuideAct_{nullptr};
    QAction* drawBreakLineAct_{nullptr};
    std::optional<ObjectId> directionGuideTarget_;
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

    // Analyse.
    QDockWidget* analysisDock_{nullptr};
    QListWidget* analysisList_{nullptr};
    QAction* analyzeAct_{nullptr};

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
    QLabel* simLabel_{nullptr};
    QAction* simPlayAct_{nullptr};
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
