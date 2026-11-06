// SPDX-License-Identifier: Apache-2.0
#include "region_panel.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace openstitch::desktop_v2 {

namespace {
QIcon swatch(const std::array<std::uint8_t, 3>& rgb) {
    QPixmap pixmap(16, 16);
    pixmap.fill(QColor(rgb[0], rgb[1], rgb[2]));
    return QIcon(pixmap);
}
QString describe(const RegionInfo& r) {
    return QObject::tr("Région %1 — %2 px").arg(r.id).arg(r.pixels);
}
} // namespace

RegionPanel::RegionPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("regionPanel"));
    auto* column = new QVBoxLayout(this);
    column->addWidget(new QLabel(tr("<b>Corriger la segmentation</b>"), this));

    status_ = new QLabel(tr("Choisissez l'outil « Régions » (X), puis cliquez une région sur le "
                            "motif ou dans la liste."),
                         this);
    status_->setObjectName(QStringLiteral("regionStatus"));
    status_->setWordWrap(true);
    column->addWidget(status_);

    // Sélection : mêmes gestes que les listes usuelles (Ctrl+clic, Maj+clic) et boutons de choix
    // rapide. Le canevas et cette liste partagent le MÊME ensemble (modèle de sélection commun).
    auto* selRow = new QHBoxLayout;
    all_ = new QPushButton(tr("Tout"), this);
    all_->setObjectName(QStringLiteral("regionSelectAll"));
    all_->setToolTip(tr("Sélectionne toutes les régions (Ctrl+A avec l'outil Régions)."));
    sameColor_ = new QPushButton(tr("Même couleur"), this);
    sameColor_->setObjectName(QStringLiteral("regionSelectSameColor"));
    sameColor_->setToolTip(
        tr("Sélectionne toutes les régions de la couleur de la région choisie."));
    neighbourSel_ = new QPushButton(tr("Voisines"), this);
    neighbourSel_->setObjectName(QStringLiteral("regionSelectNeighbours"));
    neighbourSel_->setToolTip(tr("Sélectionne les régions qui touchent la sélection."));
    invert_ = new QPushButton(tr("Inverser"), this);
    invert_->setObjectName(QStringLiteral("regionInvert"));
    invert_->setToolTip(tr("Sélectionne tout ce qui ne l'est pas (Ctrl+I)."));
    clear_ = new QPushButton(tr("Aucune"), this);
    clear_->setObjectName(QStringLiteral("regionSelectNone"));
    clear_->setToolTip(tr("Désélectionne tout (Échap)."));
    for (auto* b : {all_, sameColor_, neighbourSel_, invert_, clear_}) {
        selRow->addWidget(b);
    }
    column->addLayout(selRow);
    objects_ = new QPushButton(tr("Sélectionner les objets de ces régions"), this);
    objects_->setObjectName(QStringLiteral("regionSelectObjects"));
    objects_->setToolTip(tr("Passe à la sélection des objets de broderie issus de ces régions."));
    column->addWidget(objects_);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("regionList"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setAccessibleName(tr("Régions de la segmentation"));
    list_->setMinimumHeight(150); // la liste ne doit jamais être écrasée par les boutons
    column->addWidget(list_, 1);

    delete_ = new QPushButton(tr("Supprimer la région"), this);
    delete_->setObjectName(QStringLiteral("regionDelete"));
    delete_->setToolTip(tr("La région est absorbée par sa voisine principale."));
    column->addWidget(delete_);

    auto* mergeRow = new QHBoxLayout;
    neighbours_ = new QComboBox(this);
    neighbours_->setObjectName(QStringLiteral("regionNeighbours"));
    merge_ = new QPushButton(tr("Fusionner dans"), this);
    merge_->setObjectName(QStringLiteral("regionMerge"));
    merge_->setToolTip(tr("Fusionne la région choisie dans la voisine sélectionnée à droite."));
    mergeRow->addWidget(merge_);
    mergeRow->addWidget(neighbours_, 1);
    column->addLayout(mergeRow);

    recolor_ = new QPushButton(tr("Recolorer cette région…"), this);
    recolor_->setObjectName(QStringLiteral("regionRecolor"));
    recolor_->setToolTip(tr("Change uniquement la couleur de la région choisie ; annulable."));
    column->addWidget(recolor_);

    auto* colorsRow = new QHBoxLayout;
    colorTargets_ = new QComboBox(this);
    colorTargets_->setObjectName(QStringLiteral("regionColorTargets"));
    mergeColors_ = new QPushButton(tr("Fusionner les couleurs"), this);
    mergeColors_->setObjectName(QStringLiteral("regionMergeColors"));
    mergeColors_->setToolTip(tr("Toutes les régions de cette couleur prennent la teinte choisie. "
                                "Une seule étape d'annulation."));
    colorsRow->addWidget(mergeColors_);
    colorsRow->addWidget(colorTargets_, 1);
    column->addLayout(colorsRow);

    auto* cleanRow = new QHBoxLayout;
    minPixels_ = new QSpinBox(this);
    minPixels_->setObjectName(QStringLiteral("regionMinPixels"));
    minPixels_->setRange(1, 100000);
    minPixels_->setValue(40);
    minPixels_->setSuffix(tr(" px"));
    clean_ = new QPushButton(tr("Nettoyer les îlots plus petits que"), this);
    clean_->setObjectName(QStringLiteral("regionClean"));
    cleanRow->addWidget(clean_, 1);
    cleanRow->addWidget(minPixels_);
    column->addLayout(cleanRow);

    auto* holesRow = new QHBoxLayout;
    fillHoles_ = new QPushButton(tr("Boucher les petits trous"), this);
    fillHoles_->setObjectName(QStringLiteral("regionFillHoles"));
    fillHoles_->setToolTip(tr("Remplit les trous fermés sans toucher au fond extérieur."));
    maxHolePixels_ = new QSpinBox(this);
    maxHolePixels_->setObjectName(QStringLiteral("regionMaxHolePixels"));
    maxHolePixels_->setRange(1, 100000);
    maxHolePixels_->setValue(30);
    maxHolePixels_->setSuffix(tr(" px"));
    holesRow->addWidget(fillHoles_, 1);
    holesRow->addWidget(maxHolePixels_);
    column->addLayout(holesRow);

    // Réduction de la palette : moins de teintes, régions voisines de même teinte réunies.
    auto* paletteRow = new QHBoxLayout;
    reduceColors_ = new QSpinBox(this);
    reduceColors_->setObjectName(QStringLiteral("regionReduceColors"));
    reduceColors_->setRange(0, 64);
    reduceColors_->setSpecialValueText(tr("sans limite"));
    reduceColors_->setValue(0);
    reduceColors_->setToolTip(tr("Nombre maximum de couleurs conservées (0 = pas de limite)."));
    reduceDistance_ = new QDoubleSpinBox(this);
    reduceDistance_->setObjectName(QStringLiteral("regionReduceDistance"));
    reduceDistance_->setRange(0.0, 60.0);
    reduceDistance_->setSingleStep(1.0);
    reduceDistance_->setDecimals(1);
    reduceDistance_->setSpecialValueText(tr("aucune"));
    reduceDistance_->setValue(0.0);
    reduceDistance_->setToolTip(tr("Fusionne les teintes distantes d'au plus ce seuil (CIEDE2000, "
                                   "0 = aucun seuil). Le plus gros aplat garde sa teinte exacte."));
    paletteRow->addWidget(new QLabel(tr("Palette : couleurs max"), this));
    paletteRow->addWidget(reduceColors_);
    paletteRow->addWidget(new QLabel(tr("écart"), this));
    paletteRow->addWidget(reduceDistance_);
    column->addLayout(paletteRow);
    reduceMerge_ = new QCheckBox(tr("Réunir aussi les régions voisines de même couleur"), this);
    reduceMerge_->setObjectName(QStringLiteral("regionReduceMerge"));
    reduceMerge_->setChecked(true);
    column->addWidget(reduceMerge_);
    reduceSummary_ = new QLabel(this);
    reduceSummary_->setObjectName(QStringLiteral("regionReduceSummary"));
    column->addWidget(reduceSummary_);
    reduce_ = new QPushButton(tr("Réduire la palette"), this);
    reduce_->setObjectName(QStringLiteral("regionReduce"));
    reduce_->setToolTip(tr("Une étape d'annulation ; régénérez ensuite les objets."));
    column->addWidget(reduce_);

    auto* brushRow = new QHBoxLayout;
    auto* brushLabel = new QLabel(tr("Pinceau de frontière (outil U) : rayon"), this);
    brushLabel->setToolTip(tr("Choisissez une région avec l'outil Régions (X), puis peignez avec "
                              "l'outil Pinceau de frontière (U) ; Ctrl efface la région."));
    brushRadius_ = new QSpinBox(this);
    brushRadius_->setObjectName(QStringLiteral("regionBrushRadius"));
    brushRadius_->setRange(1, 200);
    brushRadius_->setValue(6);
    brushRadius_->setSuffix(tr(" px"));
    brushRow->addWidget(brushLabel, 1);
    brushRow->addWidget(brushRadius_);
    column->addLayout(brushRow);

    banner_ = new QLabel(this);
    banner_->setObjectName(QStringLiteral("regionBanner"));
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::RichText);
    column->addWidget(banner_);
    regenerate_ = new QPushButton(tr("Régénérer les objets depuis les régions"), this);
    regenerate_->setObjectName(QStringLiteral("regionRegenerate"));
    regenerate_->setToolTip(tr("Remplace les objets de broderie par ceux de la segmentation "
                               "corrigée. Vos retouches d'objets sont perdues, mais l'action est "
                               "annulable (Ctrl+Z)."));
    column->addWidget(regenerate_);

    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        std::vector<std::uint64_t> ids;
        for (auto* item : list_->selectedItems()) {
            ids.push_back(item->data(Qt::UserRole).toULongLong());
        }
        // La région « principale » est la dernière touchée : la liste la donne par `currentItem`.
        if (const auto* current = list_->currentItem();
            current != nullptr && current->isSelected()) {
            const auto id = current->data(Qt::UserRole).toULongLong();
            std::erase(ids, id);
            ids.push_back(id);
        }
        emit regionsChosen(ids);
        if (ids.size() == 1) {
            emit regionChosen(ids.front());
        }
    });
    connect(all_, &QPushButton::clicked, this, &RegionPanel::selectAllRequested);
    connect(sameColor_, &QPushButton::clicked, this, &RegionPanel::selectSameColorRequested);
    connect(neighbourSel_, &QPushButton::clicked, this, &RegionPanel::selectNeighboursRequested);
    connect(invert_, &QPushButton::clicked, this, &RegionPanel::invertRequested);
    connect(clear_, &QPushButton::clicked, this, &RegionPanel::clearRequested);
    connect(objects_, &QPushButton::clicked, this, &RegionPanel::selectObjectsRequested);
    connect(delete_, &QPushButton::clicked, this, &RegionPanel::deleteRequested);
    connect(merge_, &QPushButton::clicked, this, [this] {
        if (neighbours_->currentIndex() >= 0) {
            emit mergeRequested(neighbours_->currentData().toULongLong());
        }
    });
    connect(recolor_, &QPushButton::clicked, this, &RegionPanel::recolorRequested);
    connect(mergeColors_, &QPushButton::clicked, this, [this] {
        if (colorTargets_->currentIndex() >= 0) {
            emit mergeColorsRequested(colorTargets_->currentData().toULongLong());
        }
    });
    connect(clean_, &QPushButton::clicked, this,
            [this] { emit cleanRequested(minPixels_->value()); });
    connect(brushRadius_, &QSpinBox::valueChanged, this,
            [this](int px) { emit brushRadiusChanged(px); });
    connect(fillHoles_, &QPushButton::clicked, this,
            [this] { emit fillHolesRequested(maxHolePixels_->value()); });
    connect(regenerate_, &QPushButton::clicked, this, &RegionPanel::regenerateRequested);
    const auto reduceChanged = [this] {
        emit reduceParamsChanged(reduceColors_->value(), reduceDistance_->value(),
                                 reduceMerge_->isChecked());
    };
    connect(reduceColors_, &QSpinBox::valueChanged, this, reduceChanged);
    connect(reduceDistance_, &QDoubleSpinBox::valueChanged, this, reduceChanged);
    connect(reduceMerge_, &QCheckBox::toggled, this, reduceChanged);
    connect(reduce_, &QPushButton::clicked, this, [this] {
        emit reduceRequested(reduceColors_->value(), reduceDistance_->value(),
                             reduceMerge_->isChecked());
    });
    setDirty(false);
    updateButtons();
}

void RegionPanel::setRegions(const std::vector<RegionInfo>& regions, std::uint64_t selected) {
    setRegions(regions,
               selected == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{selected});
}

void RegionPanel::setRegions(const std::vector<RegionInfo>& regions,
                             const std::vector<std::uint64_t>& selectedIds) {
    const std::uint64_t selected = selectedIds.empty() ? 0 : selectedIds.back();
    selectedCount_ = static_cast<int>(selectedIds.size());
    const auto previousSelection = selected_;
    const int scrollPosition = list_->verticalScrollBar()->value();
    selected_ = selected;
    const QSignalBlocker block(list_); // remplir la liste ne doit pas « choisir » une région
    list_->clear();
    int row = -1;
    for (const auto& r : regions) {
        auto* item = new QListWidgetItem(swatch(r.rgb), describe(r), list_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(r.id));
        if (std::find(selectedIds.begin(), selectedIds.end(), r.id) != selectedIds.end()) {
            item->setSelected(true);
        }
        if (r.id == selected) {
            row = list_->count() - 1;
        }
    }
    if (row >= 0) {
        list_->setCurrentRow(row, QItemSelectionModel::NoUpdate); // sans toucher à la sélection
    }
    // Recolorer ou régénérer rafraîchit la liste : ne pas perdre sa position de lecture.
    // Une nouvelle sélection volontaire peut, elle, défiler jusqu'à l'objet choisi.
    if (selected == previousSelection || selected == 0) {
        list_->verticalScrollBar()->setValue(scrollPosition);
    }
    updateButtons();
}

void RegionPanel::setNeighbours(const std::vector<RegionInfo>& neighbours) {
    const auto previousTarget = neighbours_->currentData();
    neighbours_->clear();
    for (const auto& n : neighbours) {
        neighbours_->addItem(swatch(n.rgb), describe(n), static_cast<qulonglong>(n.id));
    }
    const int previousIndex = neighbours_->findData(previousTarget);
    if (previousIndex >= 0) {
        neighbours_->setCurrentIndex(previousIndex);
    }
    updateButtons();
}

void RegionPanel::setColorTargets(const std::vector<RegionInfo>& targets) {
    const auto previousTarget = colorTargets_->currentData();
    colorTargets_->clear();
    for (const auto& t : targets) {
        colorTargets_->addItem(swatch(t.rgb), describe(t), static_cast<qulonglong>(t.id));
    }
    const int previousIndex = colorTargets_->findData(previousTarget);
    if (previousIndex >= 0) {
        colorTargets_->setCurrentIndex(previousIndex);
    }
    updateButtons();
}

void RegionPanel::setDirty(bool dirty) {
    dirty_ = dirty;
    banner_->setText(dirty ? tr("<b>Les objets ne suivent pas encore vos corrections.</b> "
                                "Régénérez-les pour qu'ils reflètent la segmentation corrigée.")
                           : tr("Les objets correspondent à la segmentation."));
    updateButtons();
}

void RegionPanel::setBusy(bool busy) {
    busy_ = busy;
    if (busy) {
        status_->setText(tr("Régénération en cours…"));
    }
    updateButtons();
}

void RegionPanel::setMessage(const QString& message) {
    status_->setText(message);
}

int RegionPanel::regionCount() const {
    return list_->count();
}

bool RegionPanel::regenerateEnabled() const {
    return regenerate_->isEnabled();
}

QString RegionPanel::message() const {
    return status_->text();
}

int RegionPanel::neighbourCount() const {
    return neighbours_->count();
}

void RegionPanel::updateButtons() {
    const bool has = selected_ != 0 && !busy_;
    all_->setEnabled(!busy_ && list_->count() > 0);
    invert_->setEnabled(!busy_ && list_->count() > 0);
    sameColor_->setEnabled(has);
    neighbourSel_->setEnabled(has);
    clear_->setEnabled(selectedCount_ > 0);
    objects_->setEnabled(has);
    delete_->setEnabled(has && list_->count() > selectedCount_);
    merge_->setEnabled(has && neighbours_->count() > 0);
    recolor_->setEnabled(has);
    mergeColors_->setEnabled(has && colorTargets_->count() > 0);
    clean_->setEnabled(!busy_ && list_->count() > 0);
    fillHoles_->setEnabled(!busy_ && list_->count() > 0);
    regenerate_->setEnabled(dirty_ && !busy_);
}

void RegionPanel::setReducePreview(int before, int after) {
    reduceSummary_->setText(after < before ? tr("%1 → %2 couleurs").arg(before).arg(after)
                                           : tr("%1 couleurs : rien à réduire").arg(before));
    reduce_->setEnabled(!busy_ && after < before);
}

void RegionPanel::requestReducePreview() {
    emit reduceParamsChanged(reduceColors_->value(), reduceDistance_->value(),
                             reduceMerge_->isChecked());
}

int RegionPanel::brushRadius() const {
    return brushRadius_->value();
}

void RegionPanel::setBrushRadius(int pixels) {
    brushRadius_->setValue(pixels);
}

} // namespace openstitch::desktop_v2
