// SPDX-License-Identifier: Apache-2.0
#include "document_panel.hpp"

#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QDropEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>

#include "app_theme.hpp"
#include "calque_panel.hpp"
#include "ui_icons.hpp"

namespace openstitch::desktop {

namespace {

QIcon swatch(const std::array<std::uint8_t, 3>& rgb) {
    return icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2]));
}

QString type_label(const document::EmbroideryObject& e) {
    return e.is_tatami()                       ? QObject::tr("Tatami")
           : e.is_directional()                ? QObject::tr("Directionnel")
           : e.is_contour_fill()               ? QObject::tr("Concentrique")
           : e.is_motif_run()                  ? QObject::tr("Motif")
           : e.is_motif_fill()                 ? QObject::tr("Remplissage à motif")
           : e.is_radial_fill()                ? QObject::tr("Radial")
           : e.is_spiral_fill()                ? QObject::tr("Spirale")
           : e.is_crosshatch_fill()            ? QObject::tr("Croisé")
           : e.is_satin() || e.is_auto_satin() ? QObject::tr("Satin")
                                               : QObject::tr("Contour");
}

using icons::StitchGlyph;

// Famille de point d'un objet de broderie (glyphe, pastille et filtre par type).
StitchGlyph glyph_of(const document::EmbroideryObject& e) {
    return e.is_tatami()                       ? StitchGlyph::Tatami
           : e.is_directional()                ? StitchGlyph::Directional
           : e.is_satin() || e.is_auto_satin() ? StitchGlyph::Satin
                                               : StitchGlyph::Contour;
}

const QIcon& glyph_icon(StitchGlyph g) {
    // Une icône par famille, construite une fois : le moteur d'icône relit le thème à chaque
    // peinture (teinte claire/sombre, état du widget) et dessine à la densité réelle de l'écran.
    static const std::array<QIcon, 6> cache = [] {
        std::array<QIcon, 6> a;
        for (int i = 0; i < 6; ++i) {
            a[static_cast<std::size_t>(i)] = icons::stitchGlyph(static_cast<StitchGlyph>(i));
        }
        return a;
    }();
    return cache[static_cast<std::size_t>(g)];
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

constexpr int kRawNameRole = Qt::UserRole + 1;      // nom brut (édition)
constexpr int kShownTextRole = Qt::UserRole + 2;    // libellé formaté (restauré après édition)
constexpr int kGroupSourceRole = Qt::UserRole + 3;  // objet vectoriel source d'un groupe
constexpr int kSwatchRole = Qt::UserRole + 4;       // QColor du fil (pastille)
constexpr int kTypeRole = Qt::UserRole + 5;         // libellé de type (pastille à droite)
constexpr int kGlyphRole = Qt::UserRole + 6;        // StitchGlyph (filtre par type)
constexpr int kTextFlagRole = Qt::UserRole + 7;     // objet issu du lettrage
constexpr int kSourceVectorRole = Qt::UserRole + 8; // forme source d'une ligne de broderie
constexpr int kVectorIdRole = Qt::UserRole + 9;     // ligne de forme vectorielle non cousue
constexpr int kVisibleCol = 1;
constexpr int kLockCol = 2;

constexpr int kSwatchSize = 14;
constexpr int kSwatchGap = 6;
constexpr int kGlyphSize = 16;

// Teinte d'accent douce (fond de la ligne sélectionnée) : jeton du thème.
QColor soft_accent(const Tokens& t) {
    return t.accentSoft;
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
        const QIcon glyph = opt.icon; // pictogramme de type, peint ici à côté du libellé
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
        int labelLeft = cell.left() + t.space2;
        if (swatchData.isValid()) {
            labelLeft += kSwatchSize + kSwatchGap;
        }
        const int glyphLeft = labelLeft;
        if (!glyph.isNull()) {
            labelLeft += kGlyphSize + kSwatchGap;
        }
        textRect.setLeft(labelLeft);
        // Texte dessiné ici (et non par la peinture de base, qui redessinerait aussi l'icône
        // d'origine) : le type est retiré du libellé, la pastille de droite le porte déjà.
        QString text = opt.text;
        if (!type.isEmpty()) {
            text.remove(type + QStringLiteral(" — "));
        }
        const QRect labelRect = textRect.adjusted(t.space2, 0, -t.space2, 0);
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(t.text);
        painter->drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft,
                          opt.fontMetrics.elidedText(text, Qt::ElideRight, labelRect.width()));
        painter->restore();

        if (!glyph.isNull()) {
            glyph.paint(
                painter,
                QRect(glyphLeft, cell.center().y() - kGlyphSize / 2 + 1, kGlyphSize, kGlyphSize),
                Qt::AlignCenter, QIcon::Normal, QIcon::Off);
        }

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

// Liste des objets : le glisser-déposer ne déplace JAMAIS les lignes lui-même (le document fait
// foi) -- le dépôt est remis au panneau, qui demande la commande de réordonnancement.
class ObjectTree final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(const QPoint&)> onDrop;

protected:
    bool viewportEvent(QEvent* event) override {
        // Les dépôts arrivent par le viewport : on les prend ici, avant le traitement de base.
        if (event->type() == QEvent::Drop) {
            dropEvent(static_cast<QDropEvent*>(event));
            return true;
        }
        return QTreeWidget::viewportEvent(event);
    }

    void dropEvent(QDropEvent* event) override {
        if (onDrop) {
            onDrop(event->position().toPoint());
        }
        // Action « copie » : la source ne supprime pas ses lignes en fin de glissement.
        event->setDropAction(Qt::CopyAction);
        event->accept();
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
    // Filtre par type de point (HP-UX-042) à côté de la recherche texte ; les deux se cumulent.
    typeFilter_ = new QComboBox(this);
    typeFilter_->setObjectName(QStringLiteral("combo_documentTypeFilter"));
    typeFilter_->setAccessibleName(tr("Filtrer par type de point"));
    typeFilter_->setToolTip(tr("N'afficher que les objets d'un type de point."));
    // Compact : ne doit pas élargir le dock (le canevas garde sa place sur petit écran).
    typeFilter_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    typeFilter_->setMinimumContentsLength(5);
    typeFilter_->addItem(tr("Tous les types"), -1);
    const std::pair<StitchGlyph, QString> typeEntries[] = {
        {StitchGlyph::Tatami, tr("Tatami")},   {StitchGlyph::Satin, tr("Satin")},
        {StitchGlyph::Contour, tr("Contour")}, {StitchGlyph::Directional, tr("Directionnel")},
        {StitchGlyph::Text, tr("Texte")},      {StitchGlyph::Vector, tr("Vecteur")}};
    for (const auto& [glyph, label] : typeEntries) {
        typeFilter_->addItem(glyph_icon(glyph), label, static_cast<int>(glyph));
    }
    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(filterEdit_, 1);
    filterRow->addWidget(typeFilter_);
    layout->addLayout(filterRow);
    connect(typeFilter_, &QComboBox::currentIndexChanged, this, [this] { applyFilter(); });
    tabs_ = new QTabWidget(this);
    layout->addWidget(tabs_);
    // Créé APRÈS les onglets (ordre des enfants : les recherches `findChild<QTreeWidget*>()` des
    // tests et de MainWindow trouvent l'arbre des objets d'abord) mais affiché au-dessus d'eux.
    calquePanel_ = new CalquePanel(this);
    layout->insertWidget(1, calquePanel_);
    // « + Couche » : empile une couche de couture sur la ligne choisie (HP-OBJ-023), comme le
    // bouton « nouveau calque » d'un éditeur d'images.
    addLayerBtn_ = new QPushButton(tr("+ Couche"), this);
    addLayerBtn_->setObjectName(QStringLiteral("button_documentAddLayer"));
    addLayerBtn_->setToolTip(tr("Ajouter une couche de couture sur la forme de la ligne choisie "
                                "(Maj + clic : choisir le type et la couleur)."));
    addLayerBtn_->setEnabled(false);
    layout->addWidget(addLayerBtn_);
    connect(addLayerBtn_, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem* item = layerTargetItem()) {
            const bool interactive =
                QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
            emit addLayerRequested(ObjectId{item->data(0, Qt::UserRole).toULongLong()},
                                   interactive);
        }
    });
    connect(filterEdit_, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    auto* tree = new ObjectTree(tabs_);
    tree->onDrop = [this](const QPoint& pos) { handleDrop(pos); };
    objectsList_ = tree;
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
    // Ctrl/Maj + clic : sélection étendue, synchronisée avec celle de MainWindow dans les deux
    // sens.
    objectsList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Ordre de couture par glisser-déposer (via ReorderEmbroideryCommand côté MainWindow).
    objectsList_->setDragDropMode(QAbstractItemView::DragDrop);
    objectsList_->setDefaultDropAction(Qt::MoveAction);
    objectsList_->setDropIndicatorShown(true);
    objectsList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objectsList_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (QMenu* menu = createContextMenu(pos)) {
            menu->exec(objectsList_->viewport()->mapToGlobal(pos));
            delete menu;
        }
    });
    regionsList_ = new QListWidget(tabs_);
    regionsList_->setAccessibleName(tr("Régions de segmentation"));
    tabs_->addTab(objectsList_, tr("Objets"));
    tabs_->addTab(regionsList_, tr("Régions"));

    applyTheme();
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { applyTheme(); });

    connect(objectsList_, &QTreeWidget::itemSelectionChanged, this, [this] {
        updateAddLayerButton(); // aussi pour une sélection venue du canevas (syncing_)
        onObjectSelectionChanged();
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
            // Ligne de groupe (couches d'une même forme) : ses cases visent toutes les couches.
            if (item->data(0, kGroupSourceRole).isValid() &&
                (col == kVisibleCol || col == kLockCol)) {
                std::vector<ObjectId> ids;
                for (int c = 0; c < item->childCount(); ++c) {
                    ids.push_back(ObjectId{item->child(c)->data(0, Qt::UserRole).toULongLong()});
                }
                const bool on = item->checkState(col) == Qt::Checked;
                if (col == kVisibleCol) {
                    QTimer::singleShot(
                        0, this, [this, ids, on] { emit visibilityChangeRequested(ids, on); });
                } else {
                    QTimer::singleShot(0, this,
                                       [this, ids, on] { emit orderLockChangeRequested(ids, on); });
                }
            }
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

    // État de l'utilisateur à restaurer par IDENTITÉ (jamais par position) : repli des groupes,
    // lignes sélectionnée / courante, défilement (HP-UX-037).
    using RowKey = std::pair<char, std::uint64_t>; // 'e' broderie, 'v' forme non cousue, 'g' groupe
    const auto keyOf = [](const QTreeWidgetItem* item) -> RowKey {
        if (item->data(0, Qt::UserRole).isValid()) {
            return {'e', item->data(0, Qt::UserRole).toULongLong()};
        }
        if (item->data(0, kVectorIdRole).isValid()) {
            return {'v', item->data(0, kVectorIdRole).toULongLong()};
        }
        if (item->data(0, kGroupSourceRole).isValid()) {
            return {'g', item->data(0, kGroupSourceRole).toULongLong()};
        }
        return {0, 0};
    };
    std::set<std::uint64_t> collapsedGroups;
    std::set<RowKey> selectedKeys;
    RowKey currentKey{0, 0};
    {
        const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
            const RowKey key = keyOf(item);
            if (key.first == 'g' && !item->isExpanded()) {
                collapsedGroups.insert(key.second);
            }
            if (item->isSelected()) {
                selectedKeys.insert(key);
            }
            if (item == objectsList_->currentItem()) {
                currentKey = key;
            }
            for (int c = 0; c < item->childCount(); ++c) {
                visit(item->child(c));
            }
        };
        for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
            visit(objectsList_->topLevelItem(i));
        }
    }
    const int scrollValue = objectsList_->verticalScrollBar()->value();
    objectsList_->clear();
    order_.clear();

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
    std::map<std::uint64_t, const document::VectorObject*> vectorById;
    for (const auto& v : project.vector_objects) {
        vectorById[v.id.value] = &v;
    }

    const auto find_vector_name = [&project](ObjectId id) -> QString {
        for (const auto& v : project.vector_objects) {
            if (v.id == id)
                return QString::fromStdString(v.name);
        }
        return QObject::tr("(vecteur inconnu)");
    };

    int order = 0;
    // Ordre de couture réel (calque d'abord, puis ordre du document) : le rang affiché est celui
    // où l'objet est vraiment cousu.
    for (const std::size_t sewnIndex : project.sewingOrder()) {
        const auto& e = project.embroidery_objects[sewnIndex];
        ++order;
        order_.push_back(e.id);
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
                group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled);
                // Pas de Qt::UserRole ici (isValid() == false) : un clic sur
                // le groupe lui-même ne sélectionne rien côté document.
            }
            item = new QTreeWidgetItem(group, {itemText(e, order, suffix)});
        } else {
            item = new QTreeWidgetItem(objectsList_, {itemText(e, order, suffix)});
        }
        const auto sourceIt = vectorById.find(e.source_vector.value);
        const bool fromText = sourceIt != vectorById.end() && sourceIt->second->text_owner;
        const StitchGlyph glyph = fromText ? StitchGlyph::Text : glyph_of(e);
        item->setIcon(0, glyph_icon(glyph)); // pictogramme de type ; la pastille de fil est peinte
        item->setData(0, kGlyphRole, static_cast<int>(glyph_of(e)));
        item->setData(0, kTextFlagRole, fromText);
        item->setData(0, kSourceVectorRole, static_cast<qulonglong>(e.source_vector.value));
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

    // Ligne de groupe : libellé « N couches » (ou « N sections » pour un plan satin), cases
    // Visible / Figé agrégées (partiellement cochée si les couches divergent) et pastille du fil
    // de la couche du dessus -- comme la ligne d'un dossier de calques.
    for (const auto& [source, group] : groupBySourceVector) {
        int visibleCount = 0;
        int lockedCount = 0;
        bool allSatin = true;
        const int n = group->childCount();
        for (int c = 0; c < n; ++c) {
            const QTreeWidgetItem* child = group->child(c);
            visibleCount += child->checkState(kVisibleCol) == Qt::Checked ? 1 : 0;
            lockedCount += child->checkState(kLockCol) == Qt::Checked ? 1 : 0;
            allSatin = allSatin &&
                       child->data(0, kGlyphRole).toInt() == static_cast<int>(StitchGlyph::Satin);
        }
        const auto stateOf = [n](int count) {
            return count == 0 ? Qt::Unchecked : count == n ? Qt::Checked : Qt::PartiallyChecked;
        };
        group->setText(0, (allSatin ? tr("%1 (%2 sections)") : tr("%1 (%2 couches)"))
                              .arg(find_vector_name(ObjectId{source}))
                              .arg(n));
        group->setToolTip(0, allSatin
                                 ? tr("Cliquer sélectionne toutes les sections de cette forme.")
                                 : tr("Couches de couture de cette forme, de bas en haut "
                                      "dans l'ordre de couture. Cliquer les sélectionne "
                                      "toutes."));
        group->setCheckState(kVisibleCol, stateOf(visibleCount));
        group->setCheckState(kLockCol, stateOf(lockedCount));
        group->setToolTip(kVisibleCol, tr("Afficher ou masquer toutes les couches de la forme."));
        group->setToolTip(kLockCol, tr("Figer ou libérer l'ordre de toutes les couches."));
        if (n > 0) {
            group->setData(0, kSwatchRole, group->child(n - 1)->data(0, kSwatchRole));
        }
    }

    // Formes vectorielles non cousues : lignes indépendantes (navigables avant toute couture).
    std::set<std::uint64_t> sewnSources;
    for (const auto& e : project.embroidery_objects) {
        sewnSources.insert(e.source_vector.value);
    }
    for (const auto& v : project.vector_objects) {
        if (sewnSources.count(v.id.value) != 0) {
            continue;
        }
        const QString name = QString::fromStdString(v.name);
        const QString vis = v.visible ? QString() : tr("  (masqué)");
        const QString label = tr("Vecteur — %1%2").arg(name, vis);
        auto* item = new QTreeWidgetItem(objectsList_, {label});
        item->setFlags(item->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
        item->setIcon(0, glyph_icon(StitchGlyph::Vector));
        item->setData(0, kSwatchRole, QColor(v.rgb[0], v.rgb[1], v.rgb[2]));
        item->setData(0, kTypeRole, tr("Vecteur"));
        item->setData(0, kGlyphRole, static_cast<int>(StitchGlyph::Vector));
        item->setData(0, kVectorIdRole, static_cast<qulonglong>(v.id.value));
        item->setData(0, kShownTextRole, label);
        item->setToolTip(0, tr("Forme non cousue : aucun objet de broderie ne la remplit encore."));
    }

    // Restauration par identité (repli, sélection, ligne courante) puis du défilement.
    for (const auto& [source, group] : groupBySourceVector) {
        group->setExpanded(collapsedGroups.count(source) == 0);
    }
    {
        const std::function<void(QTreeWidgetItem*)> restore = [&](QTreeWidgetItem* item) {
            const RowKey key = keyOf(item);
            if (selectedKeys.count(key) != 0) {
                item->setSelected(true);
            }
            if (key == currentKey && key.first != 0) {
                objectsList_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
            }
            for (int c = 0; c < item->childCount(); ++c) {
                restore(item->child(c));
            }
        };
        for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
            restore(objectsList_->topLevelItem(i));
        }
    }
    objectsList_->doItemsLayout();
    objectsList_->verticalScrollBar()->setValue(scrollValue);

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
    calquePanel_->refresh(project);
    updateAddLayerButton();
    applyFilter();
}

QTreeWidgetItem* DocumentPanel::layerTargetItem() const {
    const QList<QTreeWidgetItem*> items = objectsList_->selectedItems();
    if (items.size() != 1) {
        return nullptr;
    }
    QTreeWidgetItem* item = items.front();
    if (item->data(0, kGroupSourceRole).isValid() && item->childCount() > 0) {
        item = item->child(item->childCount() - 1); // la couche du dessus
    }
    return item->data(0, Qt::UserRole).isValid() ? item : nullptr;
}

void DocumentPanel::updateAddLayerButton() {
    if (addLayerBtn_ != nullptr) {
        addLayerBtn_->setEnabled(layerTargetItem() != nullptr);
    }
}

void DocumentPanel::applyTheme() {
    const Tokens& t = AppTheme::instance().tokens();
    const auto css = [](const QColor& c) { return c.name(QColor::HexRgb); };
    const QString sheet =
        QStringLiteral(
            "QLineEdit, QComboBox { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: %4px;"
            " padding: %5px %6px; }"
            "QLineEdit:focus, QComboBox:focus { border-color: %7; }"
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
    const int typeWanted = typeFilter_ != nullptr ? typeFilter_->currentData().toInt() : -1;
    const auto matches = [&needle](const QString& text) {
        return needle.isEmpty() || text.contains(needle, Qt::CaseInsensitive);
    };
    // Type : la famille de point, « Texte » (objets issus du lettrage) ou « Vecteur » (formes non
    // cousues). Une ligne de groupe n'a pas de type propre : elle suit ses sections.
    const auto typeMatches = [typeWanted](const QTreeWidgetItem* item) {
        if (typeWanted < 0) {
            return true;
        }
        if (typeWanted == static_cast<int>(StitchGlyph::Text)) {
            return item->data(0, kTextFlagRole).toBool();
        }
        return item->data(0, kGlyphRole).isValid() &&
               item->data(0, kGlyphRole).toInt() == typeWanted;
    };
    for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = objectsList_->topLevelItem(i);
        if (top->childCount() == 0) {
            top->setHidden(!(matches(top->text(0)) && typeMatches(top)));
            continue;
        }
        bool any = typeWanted < 0 && matches(top->text(0));
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* child = top->child(c);
            const bool childShown =
                (matches(child->text(0)) || matches(top->text(0))) && typeMatches(child);
            child->setHidden(!childShown);
            any = any || childShown;
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
    // Sélection EXPLICITE de la ligne (et non setCurrentItem seul : son effet sur la sélection
    // dépend du mode), toujours sans signal.
    const auto selectOnly = [this](QTreeWidgetItem* item) {
        objectsList_->clearSelection();
        if (item != nullptr) {
            item->setSelected(true);
            objectsList_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
        } else {
            objectsList_->setCurrentItem(nullptr);
        }
    };
    // Cherche une ligne (premier niveau ET sections groupées, §21) par rôle d'identité.
    const auto find = [this](int role, std::uint64_t wanted) -> QTreeWidgetItem* {
        for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
            QTreeWidgetItem* top = objectsList_->topLevelItem(i);
            if (top->data(0, role).isValid() && top->data(0, role).toULongLong() == wanted) {
                return top;
            }
            for (int c = 0; c < top->childCount(); ++c) {
                QTreeWidgetItem* child = top->child(c);
                if (child->data(0, role).isValid() &&
                    child->data(0, role).toULongLong() == wanted) {
                    return child;
                }
            }
        }
        return nullptr;
    };
    if (kind == Kind::Embroidery) {
        selectOnly(find(Qt::UserRole, id));
        regionsList_->setCurrentItem(nullptr);
    } else if (kind == Kind::Vector) {
        selectOnly(find(kVectorIdRole, id));
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
        selectOnly(nullptr);
    } else {
        selectOnly(nullptr);
        regionsList_->setCurrentItem(nullptr);
    }
    syncing_ = false;
}

void DocumentPanel::syncObjects(const std::vector<std::uint64_t>& vectorIds) {
    // Forme représentée par une ligne : sa forme source (broderie, groupe) ou elle-même.
    const auto vectorOf = [](const QTreeWidgetItem* item) -> std::uint64_t {
        if (item->data(0, kVectorIdRole).isValid()) {
            return item->data(0, kVectorIdRole).toULongLong();
        }
        if (item->data(0, kGroupSourceRole).isValid()) {
            return item->data(0, kGroupSourceRole).toULongLong();
        }
        return item->data(0, kSourceVectorRole).toULongLong();
    };
    const std::set<std::uint64_t> wanted(vectorIds.begin(), vectorIds.end());
    std::set<std::uint64_t> shown;
    for (const QTreeWidgetItem* item : objectsList_->selectedItems()) {
        shown.insert(vectorOf(item));
    }
    if (shown == wanted) {
        return; // la liste reflète déjà cet ensemble : on garde l'ancre de l'utilisateur
    }
    syncing_ = true;
    objectsList_->clearSelection();
    QTreeWidgetItem* last = nullptr;
    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        const bool leaf =
            item->data(0, Qt::UserRole).isValid() || item->data(0, kVectorIdRole).isValid();
        if (leaf && wanted.count(vectorOf(item)) != 0) {
            item->setSelected(true);
            last = item;
        }
        for (int c = 0; c < item->childCount(); ++c) {
            visit(item->child(c));
        }
    };
    for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
        visit(objectsList_->topLevelItem(i));
    }
    objectsList_->setCurrentItem(last, 0, QItemSelectionModel::NoUpdate);
    regionsList_->setCurrentItem(nullptr);
    syncing_ = false;
}

void DocumentPanel::collectSelection(std::vector<ObjectId>& embroidery,
                                     std::vector<ObjectId>& vectors) const {
    // Dans l'ordre d'affichage de la liste, pas dans l'ordre de clic ; un groupe sélectionné
    // compte toutes ses sections.
    const std::function<void(QTreeWidgetItem*, bool)> visit = [&](QTreeWidgetItem* item,
                                                                  bool inheritedSelection) {
        const bool selected = item->isSelected() || inheritedSelection;
        if (selected && item->data(0, Qt::UserRole).isValid()) {
            const ObjectId id{item->data(0, Qt::UserRole).toULongLong()};
            if (std::find(embroidery.begin(), embroidery.end(), id) == embroidery.end()) {
                embroidery.push_back(id);
            }
        } else if (selected && item->data(0, kVectorIdRole).isValid()) {
            vectors.push_back(ObjectId{item->data(0, kVectorIdRole).toULongLong()});
        }
        const bool isGroup = item->data(0, kGroupSourceRole).isValid();
        for (int c = 0; c < item->childCount(); ++c) {
            visit(item->child(c), selected && isGroup);
        }
    };
    for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
        visit(objectsList_->topLevelItem(i), false);
    }
}

void DocumentPanel::onObjectSelectionChanged() {
    if (syncing_) {
        return;
    }
    const QList<QTreeWidgetItem*> items = objectsList_->selectedItems();
    if (items.isEmpty()) {
        emit selectionCleared();
        return;
    }
    if (items.size() == 1) {
        const QTreeWidgetItem* item = items.front();
        const QVariant data = item->data(0, Qt::UserRole);
        if (data.isValid()) {
            emit embroiderySelected(ObjectId{data.toULongLong()});
        } else if (item->data(0, kVectorIdRole).isValid()) {
            emit vectorSelected(ObjectId{item->data(0, kVectorIdRole).toULongLong()});
        } else if (item->data(0, kGroupSourceRole).isValid()) {
            // Nœud de groupe (plan satin multi-sections) : sélectionne la forme source, donc
            // toutes ses sections.
            emit groupSelected(ObjectId{item->data(0, kGroupSourceRole).toULongLong()},
                               item->childCount());
        }
        return;
    }
    std::vector<ObjectId> embroidery;
    std::vector<ObjectId> vectors;
    collectSelection(embroidery, vectors);
    emit objectsSelected(embroidery, vectors);
}

std::vector<ObjectId> DocumentPanel::reorderedAfterDrop(const std::vector<ObjectId>& order,
                                                        const std::vector<ObjectId>& moved,
                                                        ObjectId target, bool after) {
    const auto isMoved = [&moved](ObjectId id) {
        return std::find(moved.begin(), moved.end(), id) != moved.end();
    };
    if (moved.empty() || (target.valid() && isMoved(target))) {
        return order;
    }
    std::vector<ObjectId> block; // ordre relatif d'origine conservé
    std::vector<ObjectId> rest;
    for (const ObjectId id : order) {
        (isMoved(id) ? block : rest).push_back(id);
    }
    auto at = rest.end();
    if (target.valid()) {
        const auto it = std::find(rest.begin(), rest.end(), target);
        if (it == rest.end()) {
            return order; // cible inconnue : ne rien faire plutôt que deviner
        }
        at = after ? it + 1 : it;
    }
    rest.insert(at, block.begin(), block.end());
    return rest;
}

void DocumentPanel::handleDrop(const QPoint& viewportPos) {
    std::vector<ObjectId> moved;
    std::vector<ObjectId> vectors;
    collectSelection(moved, vectors);
    if (moved.empty()) {
        return;
    }
    ObjectId target{};
    bool after = false;
    if (const QTreeWidgetItem* item = objectsList_->itemAt(viewportPos)) {
        if (item->data(0, Qt::UserRole).isValid()) {
            target = ObjectId{item->data(0, Qt::UserRole).toULongLong()};
            after = viewportPos.y() >= objectsList_->visualItemRect(item).center().y();
        } else if (item->data(0, kGroupSourceRole).isValid() && item->childCount() > 0) {
            // Déposer sur l'en-tête d'un groupe : juste avant sa première section.
            target = ObjectId{item->child(0)->data(0, Qt::UserRole).toULongLong()};
        } else {
            return; // forme non cousue : pas de rang de couture
        }
    }
    // Couches d'une même forme : le glisser-déposer réordonne la pile DANS la forme. Un dépôt
    // hors de celle-ci est ramené au bord le plus proche de sa pile (Maj le laisse sortir, pour
    // coudre une couche à un autre rang du dessin).
    if (!QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
        std::map<std::uint64_t, std::uint64_t> sourceOf; // id de broderie -> forme source
        const std::function<void(QTreeWidgetItem*)> scan = [&](QTreeWidgetItem* it) {
            if (it->data(0, Qt::UserRole).isValid()) {
                sourceOf[it->data(0, Qt::UserRole).toULongLong()] =
                    it->data(0, kSourceVectorRole).toULongLong();
            }
            for (int c = 0; c < it->childCount(); ++c) {
                scan(it->child(c));
            }
        };
        for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
            scan(objectsList_->topLevelItem(i));
        }
        const std::uint64_t shape = sourceOf[moved.front().value];
        const bool oneShape =
            shape != 0 && std::all_of(moved.begin(), moved.end(),
                                      [&](ObjectId id) { return sourceOf[id.value] == shape; });
        if (oneShape) {
            std::vector<ObjectId> stack; // la pile de la forme, hors couches déplacées
            for (const ObjectId id : order_) {
                if (sourceOf[id.value] == shape &&
                    std::find(moved.begin(), moved.end(), id) == moved.end()) {
                    stack.push_back(id);
                }
            }
            const bool targetInStack = target.valid() && sourceOf[target.value] == shape;
            // Une forme à couche unique (ou dont toutes les couches bougent) n'a pas de pile à
            // préserver : le dépôt reste libre.
            if (!stack.empty() && !targetInStack) {
                const auto firstOfShape =
                    std::find_if(order_.begin(), order_.end(),
                                 [&](ObjectId id) { return sourceOf[id.value] == shape; });
                const auto targetPos =
                    target.valid() ? std::find(order_.begin(), order_.end(), target) : order_.end();
                const bool beforeStack = target.valid() && targetPos < firstOfShape;
                target = beforeStack ? stack.front() : stack.back();
                after = !beforeStack;
            }
        }
    }
    const std::vector<ObjectId> next = reorderedAfterDrop(order_, moved, target, after);
    if (next == order_) {
        return;
    }
    // Émission différée : la commande reconstruit la liste (refresh) pendant le glissement.
    QTimer::singleShot(0, this, [this, next] { emit orderChangeRequested(next); });
}

QMenu* DocumentPanel::createContextMenu(const QPoint& viewportPos) {
    QTreeWidgetItem* item = objectsList_->itemAt(viewportPos);
    if (item == nullptr) {
        return nullptr;
    }
    if (!item->isSelected()) {
        // Clic droit sur une ligne hors sélection : elle devient la sélection (signaux émis).
        objectsList_->clearSelection();
        item->setSelected(true);
        objectsList_->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    }
    std::vector<ObjectId> embroidery;
    std::vector<ObjectId> vectors;
    collectSelection(embroidery, vectors);
    if (embroidery.empty() && vectors.empty()) {
        return nullptr;
    }
    bool allVisible = true;
    bool allLocked = true;
    const std::function<void(QTreeWidgetItem*)> scan = [&](QTreeWidgetItem* it) {
        if (it->data(0, Qt::UserRole).isValid()) {
            const ObjectId id{it->data(0, Qt::UserRole).toULongLong()};
            if (std::find(embroidery.begin(), embroidery.end(), id) != embroidery.end()) {
                allVisible = allVisible && it->checkState(kVisibleCol) == Qt::Checked;
                allLocked = allLocked && it->checkState(kLockCol) == Qt::Checked;
            }
        }
        for (int c = 0; c < it->childCount(); ++c) {
            scan(it->child(c));
        }
    };
    for (int i = 0; i < objectsList_->topLevelItemCount(); ++i) {
        scan(objectsList_->topLevelItem(i));
    }

    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("menu_documentRow"));
    if (!embroidery.empty()) {
        QAction* vis = menu->addAction(allVisible ? tr("&Masquer") : tr("&Afficher"));
        vis->setObjectName(QStringLiteral("action_docRowVisibility"));
        connect(vis, &QAction::triggered, this, [this, embroidery, allVisible] {
            emit visibilityChangeRequested(embroidery, !allVisible);
        });
        QAction* lock = menu->addAction(allLocked ? tr("&Libérer l'ordre") : tr("&Figer l'ordre"));
        lock->setObjectName(QStringLiteral("action_docRowLock"));
        lock->setToolTip(tr("Un ordre figé n'est pas déplacé par l'optimisation de l'ordre."));
        connect(lock, &QAction::triggered, this, [this, embroidery, allLocked] {
            emit orderLockChangeRequested(embroidery, !allLocked);
        });
        menu->addSeparator();
    }
    // Couches de couture : ajouter / dupliquer sur la ligne (ou sur la couche du dessus d'un
    // groupe), supprimer une seule couche sans toucher aux autres.
    bool oneLayerOfMany = false;
    if (const QTreeWidgetItem* target = layerTargetItem()) {
        const ObjectId targetId{target->data(0, Qt::UserRole).toULongLong()};
        QAction* add = menu->addAction(tr("Ajouter une &couche"));
        add->setObjectName(QStringLiteral("action_docRowAddLayer"));
        connect(add, &QAction::triggered, this,
                [this, targetId] { emit addLayerRequested(targetId, false); });
        QAction* addAsk = menu->addAction(tr("Ajouter une couche…"));
        addAsk->setObjectName(QStringLiteral("action_docRowAddLayerAsk"));
        addAsk->setToolTip(tr("Choisir le type de points et la couleur du fil."));
        connect(addAsk, &QAction::triggered, this,
                [this, targetId] { emit addLayerRequested(targetId, true); });
        if (objectsList_->selectedItems().front() == target) {
            QAction* dup = menu->addAction(tr("&Dupliquer la couche"));
            dup->setObjectName(QStringLiteral("action_docRowDuplicateLayer"));
            connect(dup, &QAction::triggered, this,
                    [this, targetId] { emit duplicateLayerRequested(targetId); });
            oneLayerOfMany = target->parent() != nullptr && target->parent()->childCount() > 1;
        }
        menu->addSeparator();
    }
    QAction* del =
        menu->addAction(oneLayerOfMany ? tr("&Supprimer cette couche") : tr("&Supprimer"));
    del->setObjectName(QStringLiteral("action_docRowDelete"));
    connect(del, &QAction::triggered, this,
            [this, embroidery, vectors] { emit deleteRequested(embroidery, vectors); });
    return menu;
}

} // namespace openstitch::desktop
