// SPDX-License-Identifier: Apache-2.0
#include "region_panel.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("regionList"));
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
    fillHoles_->setToolTip(tr("Ne remplit que les trous de fond fermés ; "
                             "les ouvertures vers le bord restent intactes."));
    maxHolePixels_ = new QSpinBox(this);
    maxHolePixels_->setObjectName(QStringLiteral("regionMaxHolePixels"));
    maxHolePixels_->setRange(1, 100000);
    maxHolePixels_->setValue(30);
    maxHolePixels_->setSuffix(tr(" px"));
    holesRow->addWidget(fillHoles_, 1);
    holesRow->addWidget(maxHolePixels_);
    column->addLayout(holesRow);

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

    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (item != nullptr) {
                    emit regionChosen(item->data(Qt::UserRole).toULongLong());
                }
            });
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
    connect(fillHoles_, &QPushButton::clicked, this,
            [this] { emit fillHolesRequested(maxHolePixels_->value()); });
    connect(regenerate_, &QPushButton::clicked, this, &RegionPanel::regenerateRequested);
    setDirty(false);
    updateButtons();
}

void RegionPanel::setRegions(const std::vector<RegionInfo>& regions, std::uint64_t selected) {
    selected_ = selected;
    const QSignalBlocker block(list_); // remplir la liste ne doit pas « choisir » une région
    list_->clear();
    int row = -1;
    for (const auto& r : regions) {
        auto* item = new QListWidgetItem(swatch(r.rgb), describe(r), list_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(r.id));
        if (r.id == selected) {
            row = list_->count() - 1;
        }
    }
    if (row >= 0) {
        list_->setCurrentRow(row);
    }
    updateButtons();
}

void RegionPanel::setNeighbours(const std::vector<RegionInfo>& neighbours) {
    neighbours_->clear();
    for (const auto& n : neighbours) {
        neighbours_->addItem(swatch(n.rgb), describe(n), static_cast<qulonglong>(n.id));
    }
    updateButtons();
}

void RegionPanel::setColorTargets(const std::vector<RegionInfo>& targets) {
    colorTargets_->clear();
    for (const auto& t : targets) {
        colorTargets_->addItem(swatch(t.rgb), describe(t), static_cast<qulonglong>(t.id));
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
    delete_->setEnabled(has && list_->count() > 1);
    merge_->setEnabled(has && neighbours_->count() > 0);
    recolor_->setEnabled(has);
    mergeColors_->setEnabled(has && colorTargets_->count() > 0);
    clean_->setEnabled(!busy_ && list_->count() > 0);
    fillHoles_->setEnabled(!busy_ && list_->count() > 0);
    regenerate_->setEnabled(dirty_ && !busy_);
}

} // namespace openstitch::desktop_v2
