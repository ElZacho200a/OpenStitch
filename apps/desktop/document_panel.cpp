// SPDX-License-Identifier: Apache-2.0
#include "document_panel.hpp"

#include <QIcon>
#include <QListWidget>
#include <QPixmap>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openstitch::desktop {

namespace {

QIcon swatch(const std::array<std::uint8_t, 3>& rgb) {
    QPixmap pm(12, 12);
    pm.fill(QColor(rgb[0], rgb[1], rgb[2]));
    return QIcon(pm);
}

QString type_label(const document::EmbroideryObject& e) {
    return e.is_tatami()  ? QObject::tr("Tatami")
           : e.is_satin() ? QObject::tr("Satin")
                          : QObject::tr("Contour");
}

}  // namespace

DocumentPanel::DocumentPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);

    objectsList_ = new QListWidget(tabs_);
    regionsList_ = new QListWidget(tabs_);
    tabs_->addTab(objectsList_, tr("Objets"));
    tabs_->addTab(regionsList_, tr("Régions"));

    connect(objectsList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (syncing_ || item == nullptr) return;
                emit embroiderySelected(ObjectId{item->data(Qt::UserRole).toULongLong()});
            });
    connect(regionsList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (syncing_ || item == nullptr) return;
                emit regionSelected(
                    RegionId{static_cast<std::uint32_t>(item->data(Qt::UserRole).toUInt())});
            });
}

void DocumentPanel::refresh(const document::Project& project) {
    syncing_ = true;

    objectsList_->clear();
    for (const auto& e : project.embroidery_objects) {
        const QString vis = e.visible ? QString() : tr("  (masqué)");
        const QString lock = e.locked ? tr("  [verrouillé]") : QString();
        auto* item = new QListWidgetItem(
            swatch(e.rgb),
            tr("%1 — %2%3%4")
                .arg(type_label(e), QString::fromStdString(e.name), vis, lock));
        item->setData(Qt::UserRole, static_cast<qulonglong>(e.id.value));
        objectsList_->addItem(item);
    }

    regionsList_->clear();
    if (project.segmentation) {
        const double mmPerPx = project.mm_per_px.value;
        for (const auto& slot : project.segmentation->region_slots) {
            if (!slot) continue;
            const double areaMm2 = slot->pixel_count * mmPerPx * mmPerPx;
            auto* item = new QListWidgetItem(
                swatch(slot->rgb),
                tr("Région %1 — %2 mm²").arg(slot->id.value).arg(areaMm2, 0, 'f', 1));
            item->setData(Qt::UserRole, slot->id.value);
            regionsList_->addItem(item);
        }
    }

    syncing_ = false;
}

void DocumentPanel::syncSelection(Kind kind, std::uint64_t id) {
    syncing_ = true;
    const auto selectIn = [id](QListWidget* list) {
        list->clearSelection();
        list->setCurrentItem(nullptr);
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toULongLong() == id) {
                list->setCurrentRow(i);
                break;
            }
        }
    };
    if (kind == Kind::Embroidery) {
        selectIn(objectsList_);
        regionsList_->setCurrentItem(nullptr);
    } else if (kind == Kind::Region) {
        selectIn(regionsList_);
        objectsList_->setCurrentItem(nullptr);
    } else {
        objectsList_->setCurrentItem(nullptr);
        regionsList_->setCurrentItem(nullptr);
    }
    syncing_ = false;
}

}  // namespace openstitch::desktop
