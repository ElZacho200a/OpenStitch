// SPDX-License-Identifier: Apache-2.0
#include "document_panel.hpp"

#include <QIcon>
#include <QListWidget>
#include <QPixmap>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

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

// Suffixe + infobulle d'état (Lot 8.2) : "" / tooltip vide pour Clean, l'état
// implicite le plus fréquent (aucun bruit visuel sur la majorité des objets).
std::pair<QString, QString> edit_state_suffix(stitch_generation::ObjectEditState state) {
    using stitch_generation::ObjectEditState;
    switch (state) {
    case ObjectEditState::ManuallyEdited:
        return {QObject::tr("  ✎"), QObject::tr("Retouché manuellement")};
    case ObjectEditState::Dirty:
        return {QObject::tr("  ⚠"),
                QObject::tr("Retouches obsolètes : la géométrie source a changé, elles "
                            "ne sont plus appliquées.")};
    case ObjectEditState::Clean:
    default:
        return {QString(), QString()};
    }
}

// §21/§24 du plan de refonte satin (2026-08-14) : infobulle indiquant si CET
// objet vient d'un choix explicite de l'utilisateur ou d'une classification
// automatique -- jamais dans le libellé visible (pas de bruit visuel sur la
// majorité des objets, où la distinction importe rarement), seulement à la
// demande (survol).
QString intent_tooltip(document::EmbroideryIntent intent) {
    return intent == document::EmbroideryIntent::ForcedUserChoice
              ? QObject::tr("Créé par une action explicite de l'utilisateur")
              : QObject::tr("Classifié automatiquement (auto-numérisation)");
}

QString embroidery_item_text(const document::EmbroideryObject& e, const QString& suffix) {
    const QString vis = e.visible ? QString() : QObject::tr("  (masqué)");
    const QString lock = e.locked ? QObject::tr("  [verrouillé]") : QString();
    return QObject::tr("%1 — %2%3%4%5")
        .arg(type_label(e), QString::fromStdString(e.name), vis, lock, suffix);
}

}  // namespace

DocumentPanel::DocumentPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);

    objectsList_ = new QTreeWidget(tabs_);
    objectsList_->setHeaderHidden(true);
    objectsList_->setIndentation(12);
    objectsList_->setRootIsDecorated(true);
    regionsList_ = new QListWidget(tabs_);
    tabs_->addTab(objectsList_, tr("Objets"));
    tabs_->addTab(regionsList_, tr("Régions"));

    connect(objectsList_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (syncing_ || item == nullptr) return;
                // Un nœud de groupe (plan satin multi-sections) ne porte pas
                // d'ObjectId propre -- rien à sélectionner côté document.
                const QVariant data = item->data(0, Qt::UserRole);
                if (!data.isValid()) return;
                emit embroiderySelected(ObjectId{data.toULongLong()});
            });
    connect(regionsList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (syncing_ || item == nullptr) return;
                emit regionSelected(
                    RegionId{static_cast<std::uint32_t>(item->data(Qt::UserRole).toUInt())});
            });
}

void DocumentPanel::refresh(
    const document::Project& project,
    const std::vector<std::pair<ObjectId, stitch_generation::ObjectEditState>>& editStates) {
    syncing_ = true;

    objectsList_->clear();

    // §21 : regroupe les sections d'un même plan satin (même `source_vector`,
    // cf. `satin_planning::create_satin_plan`) sous un nœud parent -- compte
    // d'abord les occurrences par vecteur source pour ne créer un groupe QUE
    // lorsqu'il y a réellement plusieurs sections (un objet seul reste un
    // simple item de premier niveau, comportement visuel inchangé).
    // `source_vector.value == 0` = invalide (cf. `Id::valid()`, ids.hpp) --
    // ne JAMAIS regrouper sur cette valeur : plusieurs objets sans vecteur
    // source assigné ne partagent rien entre eux, un groupe les mêlerait à
    // tort (défaut trouvé en écrivant les tests -- test_document_panel.cpp
    // utilise justement deux objets sans `source_vector`).
    std::map<std::uint64_t, int> countBySourceVector;
    for (const auto& e : project.embroidery_objects) {
        if (e.source_vector.valid()) {
            ++countBySourceVector[e.source_vector.value];
        }
    }
    std::map<std::uint64_t, QTreeWidgetItem*> groupBySourceVector;

    const auto find_vector_name = [&project](ObjectId id) -> QString {
        for (const auto& v : project.vector_objects) {
            if (v.id == id) return QString::fromStdString(v.name);
        }
        return QObject::tr("(vecteur inconnu)");
    };

    for (const auto& e : project.embroidery_objects) {
        stitch_generation::ObjectEditState state = stitch_generation::ObjectEditState::Clean;
        for (const auto& [id, s] : editStates) {
            if (id == e.id) {
                state = s;
                break;
            }
        }
        const auto [suffix, tooltip] = edit_state_suffix(state);
        const QString fullTooltip =
            tooltip.isEmpty() ? intent_tooltip(e.intent) : tooltip + "\n" + intent_tooltip(e.intent);

        const bool grouped = countBySourceVector[e.source_vector.value] > 1;
        QTreeWidgetItem* item = nullptr;
        if (grouped) {
            QTreeWidgetItem*& group = groupBySourceVector[e.source_vector.value];
            if (group == nullptr) {
                group = new QTreeWidgetItem(objectsList_,
                                            {tr("%1 (%2 sections)")
                                                 .arg(find_vector_name(e.source_vector))
                                                 .arg(countBySourceVector[e.source_vector.value])});
                group->setExpanded(true);
                // Pas de Qt::UserRole ici (isValid() == false) : un clic sur
                // le groupe lui-même ne sélectionne rien côté document.
            }
            item = new QTreeWidgetItem(group, {embroidery_item_text(e, suffix)});
        } else {
            item = new QTreeWidgetItem(objectsList_, {embroidery_item_text(e, suffix)});
        }
        item->setIcon(0, swatch(e.rgb));
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(e.id.value));
        item->setToolTip(0, fullTooltip);
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
    if (kind == Kind::Embroidery) {
        objectsList_->clearSelection();
        objectsList_->setCurrentItem(nullptr);
        // Parcourt les items de premier niveau ET leurs enfants (sections
        // groupées, §21) -- la sélection peut cibler n'importe quel niveau.
        for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = objectsList_->topLevelItem(i);
            if (top->data(0, Qt::UserRole).isValid() && top->data(0, Qt::UserRole).toULongLong() == id) {
                objectsList_->setCurrentItem(top);
                break;
            }
            bool found = false;
            for (int c = 0; c < top->childCount(); ++c) {
                QTreeWidgetItem* child = top->child(c);
                if (child->data(0, Qt::UserRole).toULongLong() == id) {
                    objectsList_->setCurrentItem(child);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        regionsList_->setCurrentItem(nullptr);
    } else if (kind == Kind::Region) {
        regionsList_->clearSelection();
        regionsList_->setCurrentItem(nullptr);
        for (int i = 0; i < regionsList_->count(); ++i) {
            if (regionsList_->item(i)->data(Qt::UserRole).toULongLong() == id) {
                regionsList_->setCurrentRow(i);
                break;
            }
        }
        objectsList_->setCurrentItem(nullptr);
    } else {
        objectsList_->setCurrentItem(nullptr);
        regionsList_->setCurrentItem(nullptr);
    }
    syncing_ = false;
}

}  // namespace openstitch::desktop
