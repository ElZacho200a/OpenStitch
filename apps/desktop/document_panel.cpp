// SPDX-License-Identifier: Apache-2.0
#include "document_panel.hpp"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAction>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

#include "app_theme.hpp"
#include "ui_icons.hpp"

namespace openstitch::desktop {

namespace {

QIcon swatch(const std::array<std::uint8_t, 3>& rgb) {
    return icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2]));
}

QString type_label(const document::EmbroideryObject& e) {
    return e.is_tatami()                       ? QObject::tr("Tatami")
           : e.is_directional()                ? QObject::tr("Directionnel")
           : e.is_satin() || e.is_auto_satin() ? QObject::tr("Satin")
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

// Nom affiché sans le préfixe de type (« Remplissage Région 12 » -> « Région 12 »).
QString display_name(const document::EmbroideryObject& e) {
    QString name = QString::fromStdString(e.name);
    for (const QString& prefix : {QObject::tr("Remplissage "), QObject::tr("Contour ")}) {
        if (name.startsWith(prefix) && name.size() > prefix.size()) {
            name = name.mid(prefix.size());
            break;
        }
    }
    return name;
}

constexpr int kRawNameRole = Qt::UserRole + 1;     // nom brut (édition)
constexpr int kShownTextRole = Qt::UserRole + 2;   // libellé formaté (restauré après édition)
constexpr int kGroupSourceRole = Qt::UserRole + 3; // objet vectoriel source d'un groupe
constexpr int kSwatchRole = Qt::UserRole + 4;      // QColor du fil (pastille)
constexpr int kTypeRole = Qt::UserRole + 5;        // libellé de type (pastille à droite)
constexpr int kVisibleCol = 1;
constexpr int kLockCol = 2;

constexpr int kSwatchSize = 14;
constexpr int kSwatchGap = 6;

// Mélange `over` sur `under` (ratio dans [0,1]) : dérive une teinte douce d'un jeton.
QColor blend(const QColor& under, const QColor& over, double ratio) {
    const auto mix = [ratio](int a, int b) { return static_cast<int>(a + (b - a) * ratio + 0.5); };
    return QColor(mix(under.red(), over.red()), mix(under.green(), over.green()),
                  mix(under.blue(), over.blue()));
}

// Teinte d'accent douce (fond de la ligne sélectionnée), dérivée des jetons.
QColor soft_accent(const Tokens& t) {
    return blend(t.surface, t.accent, 0.18);
}

QFont pill_font(const QFont& base) {
    QFont f = base;
    if (f.pointSizeF() > 1.0) {
        f.setPointSizeF(f.pointSizeF() - 1.0);
    }
    return f;
}

// Rendu de la ligne : pastille de couleur du fil à gauche, libellé, pastille de type
// discrète à droite, sélection en teinte d'accent douce. Les colonnes à cases
// (Vis./Figé) ne reçoivent que le fond de sélection.
class ObjectRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const Tokens& t = AppTheme::instance().tokens();
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const bool selected = (opt.state & QStyle::State_Selected) != 0;
        if (selected) {
            painter->fillRect(opt.rect, soft_accent(t));
        }
        opt.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        opt.palette.setColor(QPalette::Text, t.text);
        opt.palette.setColor(QPalette::HighlightedText, t.text);
        if (index.column() != 0) {
            QStyledItemDelegate::paint(painter, opt, index);
            return;
        }
        const QVariant swatchData = index.data(kSwatchRole);
        const QString type = index.data(kTypeRole).toString();
        opt.features &= ~QStyleOptionViewItem::HasDecoration;
        opt.icon = QIcon();
        const QRect cell = opt.rect;
        QRect textRect = cell;
        int pillWidth = 0;
        const QFont pf = pill_font(opt.font);
        const QFontMetrics pfm(pf);
        if (!type.isEmpty()) {
            pillWidth = pfm.horizontalAdvance(type) + 2 * t.space3;
            textRect.setRight(cell.right() - pillWidth - t.space2);
        }
        if (swatchData.isValid()) {
            textRect.setLeft(cell.left() + t.space2 + kSwatchSize + kSwatchGap);
        }
        opt.rect = textRect;
        QStyledItemDelegate::paint(painter, opt, index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (swatchData.isValid()) {
            const QRectF sw(cell.left() + t.space2, cell.center().y() - kSwatchSize / 2.0 + 0.5,
                            kSwatchSize, kSwatchSize);
            painter->setPen(QPen(t.border, 1));
            painter->setBrush(swatchData.value<QColor>());
            painter->drawRoundedRect(sw.adjusted(0.5, 0.5, -0.5, -0.5), t.radiusSm + 1,
                                     t.radiusSm + 1);
        }
        if (!type.isEmpty()) {
            const int h = pfm.height() + t.space1 * 2;
            const QRectF pill(cell.right() - pillWidth - t.space2 + 1, cell.center().y() - h / 2.0,
                              pillWidth, h);
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? t.surface : t.surfaceRaised);
            painter->drawRoundedRect(pill, h / 2.0, h / 2.0);
            painter->setFont(pf);
            painter->setPen(t.textSecondary);
            painter->drawText(pill, Qt::AlignCenter, type);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(std::max(s.height(), AppTheme::instance().tokens().controlHeight));
        return s;
    }
};

// Loupe du champ de recherche, dessinée à la teinte secondaire du thème.
QIcon search_icon(const QColor& color) {
    constexpr int kLogical = 16;
    constexpr int kScale = 2;
    QPixmap pm(kLogical * kScale, kLogical * kScale);
    pm.setDevicePixelRatio(kScale);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawEllipse(QRectF(2.5, 2.5, 7.5, 7.5));
    p.drawLine(QPointF(9.8, 9.8), QPointF(13.2, 13.2));
    p.end();
    return QIcon(pm);
}

} // namespace

QString DocumentPanel::itemText(const document::EmbroideryObject& e, int order,
                                const QString& suffix) {
    const QString vis = e.visible ? QString() : QObject::tr("  (masqué)");
    const QString lock = e.locked ? QObject::tr("  [ordre figé]") : QString();
    // Le type est déjà dit par le préfixe : « Satin — Région 1156 » plutôt que
    // « Satin — Remplissage Région 1156 ». Le rang de couture ouvre la ligne.
    return QObject::tr("%1. %2 — %3%4%5")
        .arg(QString::number(order), type_label(e), display_name(e), vis, lock + suffix);
}

DocumentPanel::DocumentPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    filterEdit_ = new QLineEdit(this);
    filterEdit_->setObjectName(QStringLiteral("edit_documentFilter"));
    filterEdit_->setPlaceholderText(tr("Rechercher un objet ou une région…"));
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setAccessibleName(tr("Rechercher dans le document"));
    searchAction_ = filterEdit_->addAction(QIcon(), QLineEdit::LeadingPosition);
    layout->addWidget(filterEdit_);
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);
    connect(filterEdit_, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    objectsList_ = new QTreeWidget(tabs_);
    objectsList_->setObjectName(QStringLiteral("tree_documentObjects"));
    objectsList_->setAccessibleName(tr("Objets de broderie"));
    objectsList_->setColumnCount(3);
    objectsList_->setHeaderLabels({tr("Objet"), tr("Vis."), tr("Figé")});
    objectsList_->headerItem()->setToolTip(
        kVisibleCol, tr("Afficher ou masquer l'objet (un objet masqué n'est pas cousu)."));
    objectsList_->headerItem()->setToolTip(
        kLockCol, tr("Figer l'ordre de couture : l'optimisation de l'ordre ne déplace pas "
                     "cet objet."));
    objectsList_->header()->setStretchLastSection(false);
    objectsList_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    objectsList_->header()->setSectionResizeMode(kVisibleCol, QHeaderView::ResizeToContents);
    objectsList_->header()->setSectionResizeMode(kLockCol, QHeaderView::ResizeToContents);
    objectsList_->setIndentation(12);
    objectsList_->setRootIsDecorated(true);
    objectsList_->setItemDelegate(new ObjectRowDelegate(objectsList_));
    regionsList_ = new QListWidget(tabs_);
    regionsList_->setAccessibleName(tr("Régions de segmentation"));
    tabs_->addTab(objectsList_, tr("Objets"));
    tabs_->addTab(regionsList_, tr("Régions"));

    applyTheme();
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { applyTheme(); });

    connect(objectsList_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (syncing_ || item == nullptr)
                    return;
                // Un nœud de groupe (plan satin multi-sections) ne porte pas
                // d'ObjectId propre -- rien à sélectionner côté document.
                const QVariant data = item->data(0, Qt::UserRole);
                if (!data.isValid()) {
                    // Nœud de groupe : sélectionne la forme source, donc toutes ses sections.
                    const QVariant source = item->data(0, kGroupSourceRole);
                    if (source.isValid()) {
                        emit groupSelected(ObjectId{source.toULongLong()}, item->childCount());
                    }
                    return;
                }
                emit embroiderySelected(ObjectId{data.toULongLong()});
            });
    // Ctrl/Maj + clic : sélection multiple de régions (comme sur le canevas).
    regionsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(regionsList_, &QListWidget::itemSelectionChanged, this, [this] {
        if (syncing_) {
            return;
        }
        std::vector<std::uint64_t> ids;
        for (int i = 0; i < regionsList_->count(); ++i) {
            if (regionsList_->item(i)->isSelected()) {
                ids.push_back(regionsList_->item(i)->data(Qt::UserRole).toULongLong());
            }
        }
        const QListWidgetItem* current = regionsList_->currentItem();
        const std::uint64_t active = current != nullptr && current->isSelected()
                                         ? current->data(Qt::UserRole).toULongLong()
                                         : (ids.empty() ? 0 : ids.back());
        emit regionsSelected(ids, active);
    });
    // Cases Visible / Ordre figé (colonnes 1 et 2) et renommage (colonne 0).
    connect(objectsList_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int col) {
        if (syncing_) {
            return;
        }
        const QVariant data = item->data(0, Qt::UserRole);
        if (!data.isValid()) {
            return;
        }
        const ObjectId id{data.toULongLong()};
        // Émission DIFFÉRÉE : la commande qui s'ensuit reconstruit toute la liste (refresh) et
        // détruirait la ligne en plein traitement de son propre signal.
        if (col == kVisibleCol) {
            const bool on = item->checkState(kVisibleCol) == Qt::Checked;
            QTimer::singleShot(0, this, [this, id, on] { emit visibilityToggled(id, on); });
        } else if (col == kLockCol) {
            const bool on = item->checkState(kLockCol) == Qt::Checked;
            QTimer::singleShot(0, this, [this, id, on] { emit orderLockToggled(id, on); });
        } else if (col == 0 && item == renamingItem_) {
            const QString name = item->text(0).trimmed();
            if (!name.isEmpty() && name != item->data(0, kRawNameRole).toString()) {
                QTimer::singleShot(0, this, [this, id, name] { emit renameRequested(id, name); });
            }
            restoreRenamedText();
        }
    });
    // Double-clic sur le nom : édition en place du nom brut (annulable ensuite, Ctrl+Z).
    connect(objectsList_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int col) {
                if (col != 0 || !item->data(0, Qt::UserRole).isValid()) {
                    return;
                }
                renamingItem_ = item;
                syncing_ = true;
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setText(0, item->data(0, kRawNameRole).toString());
                syncing_ = false;
                objectsList_->editItem(item, 0);
            });
    // Fin d'édition (validée ou annulée par Échap) : le libellé formaté revient.
    connect(objectsList_->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this] { restoreRenamedText(); });
    connect(regionsList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (syncing_ || item == nullptr)
                    return;
                emit regionSelected(
                    RegionId{static_cast<std::uint32_t>(item->data(Qt::UserRole).toUInt())});
            });
}

void DocumentPanel::refresh(
    const document::Project& project,
    const std::vector<std::pair<ObjectId, stitch_generation::ObjectEditState>>& editStates) {
    syncing_ = true;

    renamingItem_ = nullptr; // les lignes sont reconstruites
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
            if (v.id == id)
                return QString::fromStdString(v.name);
        }
        return QObject::tr("(vecteur inconnu)");
    };

    int order = 0;
    for (const auto& e : project.embroidery_objects) {
        ++order;
        stitch_generation::ObjectEditState state = stitch_generation::ObjectEditState::Clean;
        for (const auto& [id, s] : editStates) {
            if (id == e.id) {
                state = s;
                break;
            }
        }
        const auto [suffix, tooltip] = edit_state_suffix(state);
        const QString fullTooltip = tooltip.isEmpty() ? intent_tooltip(e.intent)
                                                      : tooltip + "\n" + intent_tooltip(e.intent);

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
                group->setData(0, kGroupSourceRole, static_cast<qulonglong>(e.source_vector.value));
                group->setToolTip(0, tr("Cliquer sélectionne toutes les sections de cette forme."));
                // Pas de Qt::UserRole ici (isValid() == false) : un clic sur
                // le groupe lui-même ne sélectionne rien côté document.
            }
            item = new QTreeWidgetItem(group, {itemText(e, order, suffix)});
        } else {
            item = new QTreeWidgetItem(objectsList_, {itemText(e, order, suffix)});
        }
        item->setIcon(0, swatch(e.rgb));
        item->setData(0, kSwatchRole, QColor(e.rgb[0], e.rgb[1], e.rgb[2]));
        item->setData(0, kTypeRole, type_label(e));
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(e.id.value));
        item->setData(0, kRawNameRole, QString::fromStdString(e.name));
        item->setData(0, kShownTextRole, itemText(e, order, suffix));
        item->setToolTip(0, fullTooltip + tr("\nDouble-clic : renommer."));
        item->setCheckState(kVisibleCol, e.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(kLockCol, e.locked ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(kVisibleCol, tr("Visible : décocher masque l'objet (annulable)."));
        item->setToolTip(kLockCol, tr("Ordre figé : l'optimisation ne déplace pas cet objet."));
    }

    regionsList_->clear();
    if (project.segmentation) {
        const double mmPerPx = project.mm_per_px.value;
        for (const auto& slot : project.segmentation->region_slots) {
            if (!slot)
                continue;
            const double areaMm2 = slot->pixel_count * mmPerPx * mmPerPx;
            auto* item = new QListWidgetItem(
                swatch(slot->rgb),
                tr("Région %1 — %2 mm²").arg(slot->id.value).arg(areaMm2, 0, 'f', 1));
            item->setData(Qt::UserRole, static_cast<qulonglong>(slot->id.value));
            regionsList_->addItem(item);
        }
    }

    syncing_ = false;
    applyFilter();
}

void DocumentPanel::applyTheme() {
    const Tokens& t = AppTheme::instance().tokens();
    const auto css = [](const QColor& c) { return c.name(QColor::HexRgb); };
    const QString sheet =
        QStringLiteral(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: %4px;"
            " padding: %5px %6px; }"
            "QLineEdit:focus { border-color: %7; }"
            "QTabWidget::pane { border: none; border-top: 1px solid %3; }"
            "QTabBar::tab { background: transparent; color: %8; border: none;"
            " border-bottom: 2px solid transparent; padding: %5px %9px; margin-right: %5px; }"
            "QTabBar::tab:selected { color: %2; border-bottom-color: %7; }"
            "QTabBar::tab:hover:!selected { color: %2; }"
            "QTreeWidget, QListWidget { background: %10; color: %2; border: none; outline: 0; }"
            "QListWidget::item { padding: %5px %5px; }"
            "QListWidget::item:selected { background: %11; color: %2; }"
            "QHeaderView::section { background: %10; color: %8; border: none;"
            " border-bottom: 1px solid %3; padding: %5px %6px; }")
            .arg(css(t.surfaceRaised), css(t.text), css(t.border))
            .arg(t.radiusMd * 2)
            .arg(t.space2)
            .arg(t.space3)
            .arg(css(t.focus), css(t.textSecondary))
            .arg(t.space4)
            .arg(css(t.surface), css(soft_accent(t)));
    setStyleSheet(sheet);
    if (searchAction_ != nullptr) {
        searchAction_->setIcon(search_icon(t.textSecondary));
    }
    objectsList_->viewport()->update();
}

void DocumentPanel::restoreRenamedText() {
    if (renamingItem_ == nullptr) {
        return;
    }
    QTreeWidgetItem* item = renamingItem_;
    renamingItem_ = nullptr;
    syncing_ = true;
    item->setText(0, item->data(0, kShownTextRole).toString());
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    syncing_ = false;
}

void DocumentPanel::applyFilter() {
    const QString needle = filterEdit_ != nullptr ? filterEdit_->text().trimmed() : QString();
    const auto matches = [&needle](const QString& text) {
        return needle.isEmpty() || text.contains(needle, Qt::CaseInsensitive);
    };
    for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = objectsList_->topLevelItem(i);
        bool any = matches(top->text(0));
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* child = top->child(c);
            const bool childMatches = matches(child->text(0)) || matches(top->text(0));
            child->setHidden(!childMatches);
            any = any || childMatches;
        }
        top->setHidden(!any);
    }
    for (int i = 0; i < regionsList_->count(); ++i) {
        regionsList_->item(i)->setHidden(!matches(regionsList_->item(i)->text()));
    }
}

void DocumentPanel::syncRegions(const std::vector<std::uint64_t>& ids, std::uint64_t active) {
    syncing_ = true;
    regionsList_->clearSelection();
    for (int i = 0; i < regionsList_->count(); ++i) {
        QListWidgetItem* item = regionsList_->item(i);
        const std::uint64_t id = item->data(Qt::UserRole).toULongLong();
        const bool picked = std::find(ids.begin(), ids.end(), id) != ids.end();
        item->setSelected(picked);
        if (picked && id == active) {
            regionsList_->setCurrentItem(item, QItemSelectionModel::NoUpdate);
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
            if (top->data(0, Qt::UserRole).isValid() &&
                top->data(0, Qt::UserRole).toULongLong() == id) {
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
            if (found)
                break;
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

} // namespace openstitch::desktop
