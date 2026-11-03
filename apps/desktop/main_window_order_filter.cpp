// SPDX-License-Identifier: Apache-2.0
// Panneaux ordre de couture et filtres d'affichage.
// Membres de MainWindow extraits de main_window.cpp (HP-QA-001) : déplacement pur.
#include "main_window.hpp"
#include "simulation_nav_bar.hpp"
#include "stitch_type_catalog.hpp"
#include "thread_strip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_language.hpp"
#include "app_settings.hpp"
#include "app_theme.hpp"
#include "autodigitize_presets.hpp"
#include "autosave.hpp"
#include "brightness_dialog.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "command_palette_dialog.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "general_preferences_dialog.hpp"
#include "generation_options_dialog.hpp"
#include "help_dialogs.hpp"
#include "help_manual.hpp"
#include "import_dialog.hpp"
#include "length_units.hpp"
#include "machine_export_dialog.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "order_list_widget.hpp"
#include "responsive.hpp"
#include "vector_tooltip.hpp"

#include "calque_panel.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_settings_dialog.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/hoop_presets.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "properties_panel.hpp"
#include "recent_files.hpp"
#include "ruler.hpp"
#include "satin_guide_item.hpp"
#include "selection_hit_test.hpp"
#include "settings_dialog.hpp"
#include "ui_icons.hpp"
#include "ui_memory.hpp"
#include "ui_swatch.hpp"
#include "workflow_panel.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "main_window_internal.hpp"

namespace openstitch::desktop {

void MainWindow::buildOrderPanel() {
    orderDock_ = new QDockWidget(tr("Ordre de couture"), this);
    orderDock_->setObjectName(QStringLiteral("orderDock"));
    orderDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(orderDock_);
    auto* layout = new QVBoxLayout(panel);

    auto* orderList = new OrderListWidget(panel);
    orderList->setObjectName(QStringLiteral("list_sewingOrder"));
    orderList_ = orderList;
    layout->addWidget(orderList_, 1);
    // HP-SEQ-002 : glisser-déposer d'une ligne = une commande (un pas d'annulation).
    connect(orderList, &OrderListWidget::moveRequested, this, [this](int from, int to) {
        const auto* moved = orderRowObject(from);
        if (moved == nullptr || to < 0) {
            return;
        }
        const ObjectId movedId = moved->id;
        if (!commands::moved_sewing_order(project_, movedId, static_cast<std::size_t>(to))) {
            statusBar()->showMessage(
                tr("Déplacement impossible : un objet ne change pas de calque en se déplaçant."));
            return;
        }
        undoStack_.execute(std::make_unique<commands::MoveInSewingOrderCommand>(
                               movedId, static_cast<std::size_t>(to)),
                           project_);
        refreshImage();
        refreshOrderPanel();
        orderList_->setCurrentRow(to);
        updateActions();
    });
    connect(orderList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (const auto* rowObject = orderRowObject(row)) {
            // sélection d'objet vectoriel distincte
            setSelection({.region = selectedRegion_, .embroidery = rowObject->id, .objects = {}});
            updateActions();
            displayImage(processed_);
        } else {
            editSelection([](Selection& sel) { sel.embroidery.reset(); });
            updateActions();
        }
    });

    orderList_->setAccessibleName(tr("Ordre de couture des objets"));
    auto* buttons = new QHBoxLayout();
    orderUpBtn_ = new QPushButton(tr("↑ Monter"), panel);
    orderUpBtn_->setObjectName(QStringLiteral("button_orderUp"));
    orderUpBtn_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    orderUpBtn_->setToolTip(tr("Coudre cet objet plus tôt (Alt+Haut)."));
    orderDownBtn_ = new QPushButton(tr("↓ Descendre"), panel);
    orderDownBtn_->setObjectName(QStringLiteral("button_orderDown"));
    orderDownBtn_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    orderDownBtn_->setToolTip(tr("Coudre cet objet plus tard (Alt+Bas)."));
    // « Figer l'ordre » : l'optimisation de l'ordre ne déplace pas l'objet (ce n'est PAS un
    // verrou d'édition : il reste déplaçable et modifiable).
    orderLockBtn_ = new QPushButton(tr("Figer l'ordre"), panel);
    orderLockBtn_->setObjectName(QStringLiteral("button_orderLock"));
    orderLockBtn_->setCheckable(true);
    orderLockBtn_->setToolTip(tr("Figer la position de cet objet dans l'ordre de couture : "
                                 "« Optimiser l'ordre » ne le déplace pas. L'objet reste "
                                 "modifiable."));
    connect(orderUpBtn_, &QPushButton::clicked, this, &MainWindow::moveObjectUp);
    connect(orderDownBtn_, &QPushButton::clicked, this, &MainWindow::moveObjectDown);
    connect(orderLockBtn_, &QPushButton::clicked, this, &MainWindow::toggleObjectLock);
    buttons->addWidget(orderUpBtn_);
    buttons->addWidget(orderDownBtn_);
    buttons->addWidget(orderLockBtn_);
    layout->addLayout(buttons);
    connect(orderList_, &QListWidget::currentRowChanged, this, [this] { updateOrderButtons(); });

    orderStrategyCombo_ = new QComboBox(panel);
    orderStrategyCombo_->addItem(tr("Par couleur"),
                                 static_cast<int>(optimization::OrderStrategy::ByColor));
    orderStrategyCombo_->addItem(tr("Par proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ByProximity));
    orderStrategyCombo_->addItem(tr("Couleur puis proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ColorThenProximity));
    layout->addWidget(orderStrategyCombo_);
    // HP-SEQ-008 / HP-SEQ-001 : départ, arrivée du design et amélioration locale.
    auto* orderOptions = new OrderOptionsWidget(panel);
    orderOptions->setObjectName(QStringLiteral("orderOptions"));
    layout->addWidget(orderOptions);
    connect(orderOptions, &OrderOptionsWidget::endpointsChanged, this, [this, orderOptions] {
        document::SequenceFinishing next = project_.finishing;
        next.design_start = orderOptions->start();
        next.design_end = orderOptions->end();
        if (next == project_.finishing) {
            return;
        }
        undoStack_.execute(std::make_unique<commands::SetFinishingCommand>(next), project_);
        updateActions();
    });
    auto* applyBtn = new QPushButton(tr("Optimiser l'ordre"), panel);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyOrderStrategy);
    layout->addWidget(applyBtn);

    orderCostLabel_ = new QLabel(panel);
    orderCostLabel_->setWordWrap(true);
    layout->addWidget(orderCostLabel_);
    updateOrderButtons();

    orderDock_->setWidget(scrollableDockContent(panel, orderDock_));
    addDockWidget(Qt::LeftDockWidgetArea, orderDock_);
    orderDock_->hide();
}

// HP-ENG-010 : objet à ordonner, avec ses extrémités réelles de couture (repli : le centre).
static optimization::OrderItem
makeOrderItem(const document::EmbroideryObject& obj, Vec2um centroid,
              const std::vector<stitch::ObjectEndpoints>& endpoints) {
    optimization::OrderItem item{obj.id, obj.rgb, centroid, obj.locked};
    for (const auto& e : endpoints) {
        if (e.id == obj.id) {
            item.entry = e.first;
            item.exit = e.last;
            break;
        }
    }
    return item;
}

void MainWindow::refreshOrderPanel() {
    if (orderList_ == nullptr) {
        return;
    }
    const int previousRow = orderList_->currentRow();
    // Même coût que « Appliquer » : extrémités de la séquence affichée.
    const auto endpoints =
        sequence_ ? stitch::object_endpoints(*sequence_) : std::vector<stitch::ObjectEndpoints>{};
    std::vector<optimization::OrderItem> items;
    {
        // Signaux bloqués : clear() émettait currentRowChanged(-1), qui vidait la sélection de
        // broderie avant de la rétablir (clignotement de l'inspecteur et du canevas).
        const QSignalBlocker block(orderList_);
        orderList_->clear();
        int rank = 0;
        orderRows_ = project_.sewingOrder(); // calque d'abord, puis ordre du document
        for (const std::size_t index : orderRows_) {
            const auto& obj = project_.embroidery_objects[index];
            // Même libellé que le panneau Document (rang, type, nom, masqué, ordre figé).
            auto* item = new QListWidgetItem(DocumentPanel::itemText(obj, ++rank));
            item->setIcon(icons::colorSwatch(QColor(obj.rgb[0], obj.rgb[1], obj.rgb[2])));
            orderList_->addItem(item);
            items.push_back(makeOrderItem(obj, embroideryCentroid(obj), endpoints));
        }
        // La ligne courante suit la sélection de broderie si elle existe, sinon l'ancienne.
        int row = previousRow;
        if (selectedEmbroidery_) {
            for (std::size_t i = 0; i < orderRows_.size(); ++i) {
                if (project_.embroidery_objects[orderRows_[i]].id == *selectedEmbroidery_) {
                    row = static_cast<int>(i);
                    break;
                }
            }
        }
        if (row >= 0 && row < orderList_->count()) {
            orderList_->setCurrentRow(row);
        }
    }
    updateOrderButtons();
    if (auto* options =
            orderDock_->findChild<OrderOptionsWidget*>(QStringLiteral("orderOptions"))) {
        options->setEndpoints(project_.finishing.design_start, project_.finishing.design_end);
    }
    applySimulationHighlight();

    const auto cost = optimization::compute_cost(items, project_.finishing.design_start,
                                                 project_.finishing.design_end);
    orderCostLabel_->setText(tr("Trajet estimé : %1 mm — %2 changement(s) de fil")
                                 .arg(cost.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(cost.color_changes));
    orderCostLabel_->setToolTip(tr("Distance à vide entre objets successifs (de la fin de l'un "
                                   "au début du suivant), hors points de couture."));
    setDockAutoVisible(orderDock_, !project_.embroidery_objects.empty());
}

void MainWindow::updateOrderButtons() {
    if (orderList_ == nullptr || orderUpBtn_ == nullptr) {
        return;
    }
    // Monter/Descendre grisés aux bornes ; le bouton Figer reflète l'objet de la ligne.
    const int row = orderList_->currentRow();
    const auto* current = orderRowObject(row);
    // Un objet ne change pas de calque en se déplaçant : Monter/Descendre s'arrêtent à la
    // frontière de son calque (HP-OBJ-024).
    const auto* above = orderRowObject(row - 1);
    const auto* below = orderRowObject(row + 1);
    orderUpBtn_->setEnabled(current != nullptr && above != nullptr &&
                            project_.calqueRankOf(*above) == project_.calqueRankOf(*current));
    orderDownBtn_->setEnabled(current != nullptr && below != nullptr &&
                              project_.calqueRankOf(*below) == project_.calqueRankOf(*current));
    orderLockBtn_->setEnabled(current != nullptr);
    const QSignalBlocker block(orderLockBtn_);
    orderLockBtn_->setChecked(current != nullptr && current->locked);
}

const document::EmbroideryObject* MainWindow::orderRowObject(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= orderRows_.size() ||
        orderRows_[static_cast<std::size_t>(row)] >= project_.embroidery_objects.size()) {
        return nullptr;
    }
    return &project_.embroidery_objects[orderRows_[static_cast<std::size_t>(row)]];
}

int MainWindow::stitchTypeIndex(const document::EmbroideryObject& object) {
    return stitchTypeIndexOf(object);
}

double MainWindow::regionAreaMm2(const document::EmbroideryObject& object) const {
    const auto* vec = project_.findObject(object.source_vector);
    return vec == nullptr ? 0.0 : vectorAreaMm2(*vec);
}

double MainWindow::vectorAreaMm2(const document::VectorObject& vec) {
    const auto shoelace_um2 = [](const geometry::Path& path) {
        double a = 0.0;
        const auto& n = path.nodes;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const auto& p0 = n[i].pos;
            const auto& p1 = n[(i + 1) % n.size()].pos;
            a += static_cast<double>(p0.x.value) * p1.y.value -
                 static_cast<double>(p1.x.value) * p0.y.value;
        }
        return std::abs(a) / 2.0;
    };
    double area_um2 = 0.0;
    for (const auto& set : vec.paths) {
        area_um2 += shoelace_um2(set.outer);
        for (const auto& hole : set.holes) {
            area_um2 -= shoelace_um2(hole);
        }
    }
    return std::max(0.0, area_um2) / 1e6;
}

bool MainWindow::calqueShowsShape(const document::VectorObject& object) const {
    const document::Calque* calque = project_.calqueOfShape(object.id);
    return calque == nullptr || calque->visible;
}

bool MainWindow::vectorPassesFilter(const document::VectorObject& object) const {
    if (!calqueShowsShape(object)) {
        return false; // calque masqué (HP-OBJ-024)
    }
    if (!isolatedIds_.empty() &&
        std::find(isolatedIds_.begin(), isolatedIds_.end(), object.id) == isolatedIds_.end()) {
        return false; // mode Isoler : seules les formes isolées sont visibles et saisissables
    }
    if (minAreaMm2_ > 0.0 && vectorAreaMm2(object) < minAreaMm2_) {
        return false;
    }
    bool linked = false;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.source_vector != object.id) {
            continue;
        }
        linked = true;
        // Type et couleur seulement : la taille vient de la forme elle-même (ci-dessus).
        const int typeIndex = stitchTypeIndex(emb);
        const std::uint32_t key = (static_cast<std::uint32_t>(emb.rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(emb.rgb[1]) << 8) |
                                  static_cast<std::uint32_t>(emb.rgb[2]);
        if (showType_[static_cast<std::size_t>(stitchFilterFamily(typeIndex))] &&
            hiddenColors_.count(key) == 0) {
            return true;
        }
    }
    return !linked;
}

bool MainWindow::objectPassesFilter(const document::EmbroideryObject& object) const {
    if (!object.visible || !project_.calqueVisible(object)) {
        return false;
    }
    // Filtre « remplissage » : tatami et directionnel ensemble (index 1).
    const int typeIndex = stitchTypeIndex(object);
    if (!showType_[static_cast<std::size_t>(stitchFilterFamily(typeIndex))]) {
        return false;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(object.rgb[0]) << 16) |
                              (static_cast<std::uint32_t>(object.rgb[1]) << 8) |
                              static_cast<std::uint32_t>(object.rgb[2]);
    if (hiddenColors_.count(key) != 0) {
        return false;
    }
    if (minAreaMm2_ > 0.0 && regionAreaMm2(object) < minAreaMm2_) {
        return false;
    }
    return true;
}

void MainWindow::buildFilterPanel() {
    filterDock_ = new QDockWidget(tr("Filtres d'affichage"), this);
    filterDock_->setObjectName(QStringLiteral("filterDock"));
    filterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(filterDock_);
    auto* layout = new QVBoxLayout(panel);

    auto* showAllBtn = new QPushButton(tr("Tout afficher"), panel);
    showAllBtn->setObjectName(QStringLiteral("filterShowAll"));
    showAllBtn->setToolTip(
        tr("Réaffiche tous les types, toutes les couleurs et toutes les tailles. "
           "Clic droit sur un type ou une couleur : n'afficher que celui-ci."));
    connect(showAllBtn, &QPushButton::clicked, this, &MainWindow::showAllFilters);
    layout->addWidget(showAllBtn);

    // Types et taille minimale reviennent d'une session à l'autre ; les couleurs masquées non
    // (elles dépendent du projet ouvert).
    if (!qEnvironmentVariableIsSet("OPENSTITCH_NO_PERSISTED_FILTERS")) {
        QSettings settings;
        for (std::size_t t = 0; t < showType_.size(); ++t) {
            showType_[t] =
                settings.value(QStringLiteral("filters/showType%1").arg(t), true).toBool();
        }
        minAreaMm2_ =
            std::max(0.0, settings.value(QStringLiteral("filters/minAreaMm2"), 0.0).toDouble());
    }
    filterIndicator_ = new QPushButton(this);
    filterIndicator_->setObjectName(QStringLiteral("button_filterIndicator"));
    filterIndicator_->setFlat(true);
    filterIndicator_->setCursor(Qt::PointingHandCursor);
    filterIndicator_->setToolTip(tr("Des filtres d'affichage masquent des objets. Cliquer pour "
                                    "tout réafficher."));
    filterIndicator_->hide();
    connect(filterIndicator_, &QPushButton::clicked, this, &MainWindow::showAllFilters);
    statusBar()->addPermanentWidget(filterIndicator_);

    layout->addWidget(new QLabel(tr("Types de points :"), panel));
    const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
    for (int t = 0; t < 3; ++t) {
        auto* check = new QCheckBox(names[t], panel);
        check->setChecked(showType_[static_cast<std::size_t>(t)]);
        check->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(check, &QCheckBox::customContextMenuRequested, this, [this, t, check](QPoint pos) {
            QMenu menu(this);
            connect(menu.addAction(tr("Afficher uniquement ce type")), &QAction::triggered, this,
                    [this, t] { isolateFilterType(t); });
            connect(menu.addAction(tr("Tout afficher")), &QAction::triggered, this,
                    &MainWindow::showAllFilters);
            menu.exec(check->mapToGlobal(pos));
        });
        connect(check, &QCheckBox::toggled, this, [this, t](bool on) {
            showType_[static_cast<std::size_t>(t)] = on;
            saveFilterSettings();
            updateFilterIndicator();
            displayImage(processed_);
        });
        typeChecks_[static_cast<std::size_t>(t)] = check;
        layout->addWidget(check);
    }

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Taille min. des zones :"), panel));
    minAreaSpin_ = new QDoubleSpinBox(panel);
    minAreaSpin_->setRange(0.0, 10'000.0);
    minAreaSpin_->setDecimals(1);
    minAreaSpin_->setSuffix(tr(" mm²"));
    minAreaSpin_->setValue(minAreaMm2_);
    connect(minAreaSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        minAreaMm2_ = v;
        saveFilterSettings();
        updateFilterIndicator();
        displayImage(processed_);
    });
    layout->addWidget(minAreaSpin_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Couleurs de fil :"), panel));
    auto* colorBulk = new QHBoxLayout;
    const std::pair<QString, int> bulk[] = {
        {tr("Toutes"), 0}, {tr("Aucune"), 1}, {tr("Inverser"), 2}};
    for (const auto& [label, mode] : bulk) {
        auto* button = new QPushButton(label, panel);
        button->setObjectName(QStringLiteral("button_filterColors%1").arg(mode));
        button->setToolTip(tr("Affiche, masque ou inverse toutes les couleurs de fil."));
        connect(button, &QPushButton::clicked, this, [this, mode] { setAllColorFilters(mode); });
        colorBulk->addWidget(button);
    }
    layout->addLayout(colorBulk);
    auto* colorContainer = new QWidget(panel);
    colorFilterLayout_ = new QVBoxLayout(colorContainer);
    colorFilterLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(colorContainer);

    layout->addStretch(1);
    auto* filterScroll = new QScrollArea(filterDock_);
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setWidget(panel);
    filterDock_->setWidget(filterScroll);
    addDockWidget(Qt::RightDockWidgetArea, filterDock_);
    filterDock_->hide();
}

void MainWindow::refreshFilterPanel() {
    if (colorFilterLayout_ == nullptr) {
        return;
    }
    // Vide la liste de couleurs.
    while (QLayoutItem* item = colorFilterLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Couleurs distinctes (ordre stable : première apparition).
    std::vector<std::array<std::uint8_t, 3>> colors;
    for (const auto& emb : project_.embroidery_objects) {
        if (std::find(colors.begin(), colors.end(), emb.rgb) == colors.end()) {
            colors.push_back(emb.rgb);
        }
    }
    for (const auto& rgb : colors) {
        const std::uint32_t key = (static_cast<std::uint32_t>(rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(rgb[1]) << 8) |
                                  static_cast<std::uint32_t>(rgb[2]);
        auto* check = new QCheckBox();
        check->setChecked(hiddenColors_.count(key) == 0);
        check->setIcon(icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2])));
        // Nom du fil de nuancier s'il existe, sinon la teinte ; suivi du nombre d'objets.
        int count = 0;
        QString threadName;
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.rgb == rgb) {
                ++count;
                if (threadName.isEmpty() && emb.thread) {
                    threadName = QString::fromStdString(emb.thread->code);
                }
            }
        }
        check->setText(
            tr("%1%2 — %n objet(s)", "", count)
                .arg(QStringLiteral("#%1%2%3")
                         .arg(rgb[0], 2, 16, QLatin1Char('0'))
                         .arg(rgb[1], 2, 16, QLatin1Char('0'))
                         .arg(rgb[2], 2, 16, QLatin1Char('0')),
                     threadName.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(threadName)));
        check->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(check, &QCheckBox::customContextMenuRequested, this,
                [this, key, check](QPoint pos) {
                    QMenu menu(this);
                    connect(menu.addAction(tr("Afficher uniquement cette couleur")),
                            &QAction::triggered, this, [this, key] { isolateFilterColor(key); });
                    connect(menu.addAction(tr("Tout afficher")), &QAction::triggered, this,
                            &MainWindow::showAllFilters);
                    menu.exec(check->mapToGlobal(pos));
                });
        connect(check, &QCheckBox::toggled, this, [this, key](bool on) {
            if (on) {
                hiddenColors_.erase(key);
            } else {
                hiddenColors_.insert(key);
            }
            updateFilterIndicator();
            displayImage(processed_);
        });
        colorFilterLayout_->addWidget(check);
    }
    setDockAutoVisible(filterDock_, !project_.embroidery_objects.empty());
    updateFilterIndicator();
}

void MainWindow::saveFilterSettings() const {
    if (qEnvironmentVariableIsSet("OPENSTITCH_NO_PERSISTED_FILTERS")) {
        return; // tests : aucun état ne fuit d'une suite à l'autre ni vers les réglages de
                // l'utilisateur
    }
    QSettings settings;
    for (std::size_t t = 0; t < showType_.size(); ++t) {
        settings.setValue(QStringLiteral("filters/showType%1").arg(t), showType_[t]);
    }
    settings.setValue(QStringLiteral("filters/minAreaMm2"), minAreaMm2_);
}

void MainWindow::updateFilterIndicator() {
    if (filterIndicator_ == nullptr) {
        return;
    }
    // Objets visibles que SEUL un filtre d'affichage cache : sans cet indicateur, ils semblent
    // avoir disparu.
    int hidden = 0;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.visible && project_.calqueVisible(emb) && !objectPassesFilter(emb)) {
            ++hidden;
        }
    }
    filterIndicator_->setVisible(hidden > 0);
    filterIndicator_->setText(tr("Filtres : %n objet(s) masqué(s)", "", hidden));
}

void MainWindow::setAllColorFilters(int mode) {
    std::set<std::uint32_t> present;
    for (const auto& emb : project_.embroidery_objects) {
        present.insert(colorKeyOf(emb.rgb));
    }
    if (mode == 0) {
        hiddenColors_.clear();
    } else {
        for (const std::uint32_t key : present) {
            const bool hiddenNow = hiddenColors_.count(key) != 0;
            const bool hide = mode == 1 ? true : !hiddenNow;
            if (hide) {
                hiddenColors_.insert(key);
            } else {
                hiddenColors_.erase(key);
            }
        }
    }
    refreshFilterPanel(); // recoche les cases
    displayImage(processed_);
}

void MainWindow::isolateFilterType(int filterType) {
    for (std::size_t t = 0; t < showType_.size(); ++t) {
        showType_[t] = static_cast<int>(t) == filterType;
        if (typeChecks_[t] != nullptr) {
            const QSignalBlocker blocker(typeChecks_[t]);
            typeChecks_[t]->setChecked(showType_[t]);
        }
    }
    saveFilterSettings();
    updateFilterIndicator();
    displayImage(processed_);
}

void MainWindow::isolateFilterColor(std::uint32_t colorKey) {
    hiddenColors_.clear();
    for (const auto& emb : project_.embroidery_objects) {
        if (colorKeyOf(emb.rgb) != colorKey) {
            hiddenColors_.insert(colorKeyOf(emb.rgb));
        }
    }
    refreshFilterPanel(); // recoche les cases de couleur
    displayImage(processed_);
}

void MainWindow::showAllFilters() {
    showType_ = {true, true, true};
    hiddenColors_.clear();
    minAreaMm2_ = 0.0;
    for (QCheckBox* check : typeChecks_) {
        if (check != nullptr) {
            const QSignalBlocker blocker(check);
            check->setChecked(true);
        }
    }
    if (minAreaSpin_ != nullptr) {
        const QSignalBlocker blocker(minAreaSpin_);
        minAreaSpin_->setValue(0.0);
    }
    saveFilterSettings();
    refreshFilterPanel();
    displayImage(processed_);
}

std::vector<QColor> MainWindow::imageColors() const {
    std::vector<std::uint32_t> seen;
    std::vector<QColor> colors;
    for (const auto& vec : project_.vector_objects) {
        const std::uint32_t key = colorKeyOf(vec.rgb);
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            colors.emplace_back(vec.rgb[0], vec.rgb[1], vec.rgb[2]);
        }
    }
    return colors;
}

QColor MainWindow::pickThreadColor(const QColor& initial, const QString& title) {
    // Les « Custom colors » du sélecteur Qt sont globales à l'application : on les remplace par
    // les couleurs de l'image courante (16 emplacements).
    const std::vector<QColor> colors = imageColors();
    for (int i = 0; i < QColorDialog::customCount(); ++i) {
        QColorDialog::setCustomColor(i, static_cast<std::size_t>(i) < colors.size()
                                            ? colors[static_cast<std::size_t>(i)]
                                            : QColor(Qt::white));
    }
    return QColorDialog::getColor(initial, this, title);
}

void MainWindow::moveObjectUp() {
    const int row = orderList_->currentRow();
    if (row <= 0 || !orderUpBtn_->isEnabled()) {
        return;
    }
    std::vector<ObjectId> order;
    for (const std::size_t index : orderRows_) {
        order.push_back(project_.embroidery_objects[index].id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row - 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row - 1);
    updateActions();
}

void MainWindow::moveObjectDown() {
    const int row = orderList_->currentRow();
    if (row < 0 || !orderDownBtn_->isEnabled()) {
        return;
    }
    std::vector<ObjectId> order;
    for (const std::size_t index : orderRows_) {
        order.push_back(project_.embroidery_objects[index].id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row + 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row + 1);
    updateActions();
}

void MainWindow::toggleObjectLock() {
    const auto* rowObject = orderRowObject(orderList_->currentRow());
    if (rowObject == nullptr) {
        return;
    }
    const auto& obj = *rowObject;
    undoStack_.execute(std::make_unique<commands::SetEmbroideryLockCommand>(obj.id, !obj.locked),
                       project_);
    refreshOrderPanel();
    updateActions();
}

void MainWindow::applyOrderStrategy() {
    if (project_.embroidery_objects.size() < 2) {
        return;
    }
    // Calques (HP-OBJ-024) : on optimise l'ordre réel de couture, puis on regroupe par calque -- le
    // rang du calque reste la clé principale, l'optimisation ne joue qu'à l'intérieur de chacun.
    // HP-ENG-010 : extrémités réelles de la séquence affichée (repli : centre de l'objet).
    const auto endpoints =
        sequence_ ? stitch::object_endpoints(*sequence_) : std::vector<stitch::ObjectEndpoints>{};
    const auto makeItem = [&](const document::EmbroideryObject& obj) {
        return makeOrderItem(obj, embroideryCentroid(obj), endpoints);
    };
    std::vector<optimization::OrderItem> items;
    for (const std::size_t index : project_.sewingOrder()) {
        items.push_back(makeItem(project_.embroidery_objects[index]));
    }
    const auto strategy =
        static_cast<optimization::OrderStrategy>(orderStrategyCombo_->currentData().toInt());
    // HP-SEQ-001/008 : amélioration locale (borne déterministe) et points de départ / arrivée.
    optimization::OrderOptions options;
    options.start = project_.finishing.design_start;
    options.end = project_.finishing.design_end;
    if (const auto* extra =
            orderDock_->findChild<OrderOptionsWidget*>(QStringLiteral("orderOptions"));
        extra != nullptr && extra->improveEnabled()) {
        options.improve_budget = optimization::kDefaultImproveBudget;
    }
    const auto before = optimization::compute_cost(items, options.start, options.end);
    const auto order =
        project_.groupedByCalque(optimization::optimize_order(items, strategy, options));
    captureOrderJumpsBaseline(endpoints); // surimpression « Sauts à vide » : anciens sauts

    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    updateActions();

    // Coût après (recalculé sur le nouvel ordre).
    std::vector<optimization::OrderItem> reordered;
    for (const auto& obj : project_.embroidery_objects) {
        reordered.push_back(makeItem(obj));
    }
    const auto after = optimization::compute_cost(reordered, options.start, options.end);
    statusBar()->showMessage(tr("Ordre optimisé : trajet %1 → %2 mm, changements de fil %3 → %4")
                                 .arg(before.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(after.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(before.color_changes)
                                 .arg(after.color_changes));
}

} // namespace openstitch::desktop
