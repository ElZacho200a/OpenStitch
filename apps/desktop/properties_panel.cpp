// SPDX-License-Identifier: Apache-2.0
#include "properties_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <variant>

namespace openstitch::desktop {

namespace {

Micrometers to_um(double mm) {
    return to_micrometers(Millimeters{mm});
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(8, 8, 8, 8);
    root_->setSpacing(6);

    header_ = new QLabel(tr("Aucune sélection"), this);
    QFont hf = header_->font();
    hf.setBold(true);
    header_->setFont(hf);
    header_->setWordWrap(true);
    root_->addWidget(header_);

    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : toujours présent,
    // masqué quand non pertinent — mis à jour indépendamment du corps du
    // formulaire (cf. setEditState).
    editStateLabel_ = new QLabel(this);
    editStateLabel_->setWordWrap(true);
    editStateLabel_->setVisible(false);
    root_->addWidget(editStateLabel_);
    discardButton_ = new QPushButton(tr("Abandonner les retouches"), this);
    discardButton_->setToolTip(
        tr("Revient à la géométrie générée pour cet objet (annulable, Ctrl+Z)."));
    discardButton_->setVisible(false);
    connect(discardButton_, &QPushButton::clicked, this, [this] {
        if (editStateId_) {
            emit discardOverridesRequested(*editStateId_);
        }
    });
    root_->addWidget(discardButton_);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    root_->addWidget(line);

    body_ = new QWidget(this);
    new QVBoxLayout(body_);
    body_->layout()->setContentsMargins(0, 0, 0, 0);
    root_->addWidget(body_);
    root_->addStretch(1);

    showInfo(tr("Aucune sélection"),
             tr("Sélectionnez une région, un objet vectoriel ou un objet de broderie."));
}

void PropertiesPanel::clearBody() {
    currentId_.reset();
    satinGuideList_.clear();
    satinSummary_.clear();
    satinGuideAngle_.clear();
    satinGuideAbsolute_.clear();
    satinGuideRemove_.clear();
    if (auto* lay = body_->layout()) {
        while (QLayoutItem* item = lay->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }
}

QDoubleSpinBox* PropertiesPanel::mmSpin(double valueMm, double maxMm) {
    auto* spin = new QDoubleSpinBox(body_);
    // Frappe au clavier : une seule modification à la validation, pas une par chiffre
    // (chaque modification régénère les points).
    spin->setKeyboardTracking(false);
    spin->setRange(0.0, maxMm);
    spin->setDecimals(2);
    spin->setSingleStep(0.1);
    spin->setSuffix(tr(" mm"));
    spin->setValue(valueMm);
    return spin;
}

void PropertiesPanel::showRegions(const RegionSelectionInfo& info) {
    clearBody();
    header_->setText(info.title);
    auto* summary = new QLabel(info.summary, body_);
    summary->setWordWrap(true);
    body_->layout()->addWidget(summary);

    // Pastille de la couleur de la région active : un clic ouvre le sélecteur de couleur.
    auto* colorButton = new QPushButton(
        tr("Couleur : %1  (cliquer pour changer)").arg(info.activeColor.name().toUpper()), body_);
    colorButton->setObjectName(QStringLiteral("button_regionColor"));
    colorButton->setToolTip(tr("Change la couleur de toutes les régions sélectionnées."));
    QPixmap pm(18, 18);
    pm.fill(info.activeColor);
    colorButton->setIcon(QIcon(pm));
    connect(colorButton, &QPushButton::clicked, this,
            [this] { emit regionActionRequested(QStringLiteral("action_recolorRegions")); });
    body_->layout()->addWidget(colorButton);

    const auto addButton = [this](const QString& text, const QString& actionName, bool enabled,
                                  const QString& tip) {
        auto* button = new QPushButton(text, body_);
        button->setObjectName(QStringLiteral("button_") + actionName);
        button->setEnabled(enabled);
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this,
                [this, actionName] { emit regionActionRequested(actionName); });
        body_->layout()->addWidget(button);
    };
    addButton(tr("Fusionner la sélection"), QStringLiteral("action_mergeSelection"), info.canMerge,
              tr("Fusionne toutes les régions sélectionnées dans la dernière cliquée (Ctrl+M)."));
    addButton(tr("Fusionner dans la voisine principale"),
              QStringLiteral("action_absorbIntoNeighbour"), info.canAbsorb,
              tr("La région rejoint la voisine avec laquelle elle partage la plus longue "
                 "frontière (Ctrl+Maj+M)."));
    addButton(tr("Sélectionner la même couleur"), QStringLiteral("action_selectSameColor"), true,
              tr("Ajoute à la sélection toutes les régions de cette couleur."));
    addButton(tr("Sélectionner les voisines"), QStringLiteral("action_selectNeighbours"), true,
              tr("Ajoute à la sélection les régions qui touchent la sélection."));
    addButton(tr("Rétablir la couleur d'origine"), QStringLiteral("action_restoreRegionColors"),
              true, tr("Rend à chaque région la couleur moyenne de l'image segmentée."));
    addButton(tr("Supprimer"), QStringLiteral("action_deleteRegion"), true,
              tr("Retire les régions sélectionnées (elles redeviennent du fond)."));
    auto* hint = new QLabel(tr("Ctrl + clic : ajouter/retirer une région · Maj + clic : ajouter · "
                               "glisser un cadre : sélectionner plusieurs régions."),
                            body_);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    body_->layout()->addWidget(hint);
    setEditState(std::nullopt, stitch_generation::ObjectEditState::Clean);
}

void PropertiesPanel::showInfo(const QString& title, const QString& details) {
    clearBody();
    header_->setText(title);
    auto* label = new QLabel(details, body_);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->layout()->addWidget(label);
    setEditState(std::nullopt, stitch_generation::ObjectEditState::Clean);
}

void PropertiesPanel::setEditState(std::optional<ObjectId> id,
                                   stitch_generation::ObjectEditState state) {
    editStateId_ = id;
    if (!id) {
        editStateLabel_->setVisible(false);
        discardButton_->setVisible(false);
        return;
    }
    using stitch_generation::ObjectEditState;
    switch (state) {
    case ObjectEditState::Clean:
        editStateLabel_->setVisible(false);
        discardButton_->setVisible(false);
        break;
    case ObjectEditState::ManuallyEdited:
        editStateLabel_->setText(tr("✎ Retouché manuellement"));
        editStateLabel_->setToolTip(
            tr("Un ou plusieurs points ont été déplacés/modifiés à la main. Toute "
               "modification ultérieure de la forme ou des paramètres de cet objet "
               "devra être reconfirmée."));
        editStateLabel_->setVisible(true);
        discardButton_->setVisible(true);
        break;
    case ObjectEditState::Dirty:
        editStateLabel_->setText(tr("⚠ Retouches obsolètes"));
        editStateLabel_->setToolTip(
            tr("La géométrie ou l'ordre de couture de cet objet a changé depuis les "
               "dernières retouches manuelles : elles ne sont plus appliquées "
               "(la couture générée est utilisée telle quelle). Abandonnez les "
               "retouches pour ré-éditer sur la forme actuelle."));
        editStateLabel_->setVisible(true);
        discardButton_->setVisible(true);
        break;
    }
}

void PropertiesPanel::setAutoSatinState(std::optional<ObjectId> id,
                                        const document::AutoSatinParams* params,
                                        const QString& summary) {
    if (!id || params == nullptr || !currentId_ || *currentId_ != *id) {
        return;
    }
    if (satinSummary_ != nullptr && !summary.isEmpty()) {
        satinSummary_->setText(summary);
    }
    if (satinGuideList_ == nullptr) {
        return;
    }
    const int keep = satinGuideList_->currentRow();
    const bool wasBuilding = building_;
    building_ = true;
    {
        const QSignalBlocker block(satinGuideList_);
        satinGuideList_->clear();
        int n = 0;
        for (const auto& g : params->guides) {
            const int deg =
                static_cast<int>(std::lround(g.angle.radians * 180.0 / std::numbers::pi));
            auto* item = new QListWidgetItem(tr("Guide %1 — %2 mm, %3 mm — %4° (%5)")
                                                 .arg(++n)
                                                 .arg(to_millimeters(g.anchor.x).value, 0, 'f', 1)
                                                 .arg(to_millimeters(g.anchor.y).value, 0, 'f', 1)
                                                 .arg(deg)
                                                 .arg(g.absolute ? tr("absolu") : tr("relatif")));
            item->setData(Qt::UserRole, deg);
            item->setData(Qt::UserRole + 1, g.absolute);
            satinGuideList_->addItem(item);
        }
        if (keep >= 0 && keep < satinGuideList_->count()) {
            satinGuideList_->setCurrentRow(keep);
        }
    }
    const int row = satinGuideList_->currentRow();
    const bool has = row >= 0;
    if (satinGuideAngle_ != nullptr && satinGuideAbsolute_ != nullptr &&
        satinGuideRemove_ != nullptr) {
        satinGuideAngle_->setEnabled(has);
        satinGuideAbsolute_->setEnabled(has);
        satinGuideRemove_->setEnabled(has);
        if (has) {
            const QSignalBlocker b1(satinGuideAngle_);
            const QSignalBlocker b2(satinGuideAbsolute_);
            satinGuideAngle_->setValue(satinGuideList_->item(row)->data(Qt::UserRole).toInt());
            satinGuideAbsolute_->setChecked(
                satinGuideList_->item(row)->data(Qt::UserRole + 1).toBool());
        }
    }
    building_ = wasBuilding;
}

void PropertiesPanel::showEmbroidery(const document::EmbroideryObject& object) {
    clearBody();
    currentId_ = object.id;
    const ObjectId id = object.id;
    header_->setText(QString::fromStdString(object.name));

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    const QString typeName = object.is_tatami()        ? tr("Remplissage tatami")
                             : object.is_directional() ? tr("Remplissage directionnel")
                             : object.is_auto_satin()  ? tr("Satin (squelette et traversées)")
                             : object.is_satin()       ? tr("Colonne satin à rails (manuelle)")
                                                       : tr("Contour cousu");
    form->addRow(tr("Type :"), new QLabel(typeName, body_));

    building_ = true;
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 20.0);
                auto* minl = mmSpin(to_millimeters(p.min_length).value, 20.0);
                auto* rep = new QSpinBox(body_);
                rep->setKeyboardTracking(false);
                rep->setRange(1, 3);
                rep->setValue(p.repeats);
                rep->setToolTip(tr("1 = simple, 2 = aller-retour, 3 = point triple"));
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Passages :"), rep);
                const auto emitEdit = [this, id, len, minl, rep] {
                    if (building_)
                        return;
                    document::RunningStitchParams r;
                    r.stitch_length = to_um(len->value());
                    r.min_length = to_um(minl->value());
                    r.repeats = rep->value();
                    emit paramsEdited(id, r);
                };
                connect(len, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(minl, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(rep, &QSpinBox::valueChanged, this, emitEdit);
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                auto* spacing = mmSpin(to_millimeters(p.row_spacing).value, 5.0);
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 10.0);
                auto* angle = new QSpinBox(body_);
                angle->setKeyboardTracking(false);
                angle->setRange(0, 179);
                angle->setSuffix(tr(" °"));
                angle->setValue(
                    static_cast<int>(std::lround(p.angle.radians * 180.0 / std::numbers::pi)) %
                    180);
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0);
                auto* stagger = new QSpinBox(body_);
                stagger->setKeyboardTracking(false);
                stagger->setRange(1, 8);
                stagger->setValue(p.stagger);
                // Tatami avancé (Lot 7).
                const document::TatamiParams tbase = p;
                auto* uEdge = new QCheckBox(tr("Sous-couche de contour"), body_);
                uEdge->setChecked(p.underlay_edge);
                auto* uInset = mmSpin(to_millimeters(p.underlay_inset).value, 5.0);
                auto* uPar = new QCheckBox(tr("Sous-couche parallèle"), body_);
                uPar->setChecked(p.underlay_parallel);
                auto* uSpacing = mmSpin(to_millimeters(p.underlay_spacing).value, 10.0);
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setChecked(p.hidden_underpath);
                form->addRow(tr("Espacement des rangées :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Angle (orientation) :"), angle);
                form->addRow(tr("Retrait de bord :"), inset);
                form->addRow(tr("Décalage (stagger) :"), stagger);
                form->addRow(QString(), uEdge);
                form->addRow(tr("Retrait de la sous-couche :"), uInset);
                form->addRow(QString(), uPar);
                form->addRow(tr("Espacement de la sous-couche :"), uSpacing);
                form->addRow(QString(), underpath);
                const auto emitEdit = [this, id, tbase, spacing, len, angle, inset, stagger, uEdge,
                                       uInset, uPar, uSpacing, underpath] {
                    if (building_)
                        return;
                    document::TatamiParams t = tbase; // conserve sous-couche fine + entrée
                    t.row_spacing = to_um(spacing->value());
                    t.stitch_length = to_um(len->value());
                    t.angle = Angle{angle->value() * std::numbers::pi / 180.0};
                    t.inset = to_um(inset->value());
                    t.stagger = stagger->value();
                    t.underlay_edge = uEdge->isChecked();
                    t.underlay_inset = to_um(uInset->value());
                    t.underlay_parallel = uPar->isChecked();
                    t.underlay_spacing = to_um(uSpacing->value());
                    t.hidden_underpath = underpath->isChecked();
                    emit paramsEdited(id, t);
                };
                connect(spacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(len, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(angle, &QSpinBox::valueChanged, this, emitEdit);
                connect(inset, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(stagger, &QSpinBox::valueChanged, this, emitEdit);
                connect(uEdge, &QCheckBox::toggled, this, emitEdit);
                connect(uInset, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(uPar, &QCheckBox::toggled, this, emitEdit);
                connect(uSpacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(underpath, &QCheckBox::toggled, this, emitEdit);
                // Conversion vers le remplissage directionnel (points qui
                // suivent la forme) : un seul clic, annulable.
                auto* toDirectional =
                    new QPushButton(tr("Convertir en remplissage directionnel"), body_);
                toDirectional->setObjectName(QStringLiteral("button_convertToDirectional"));
                toDirectional->setToolTip(
                    tr("Remplace les rangées droites par des lignes qui suivent des courbes "
                       "guides (passé empiétant). Les réglages actuels sont conservés ; un "
                       "guide droit reproduit l'angle courant. Annulable (Ctrl+Z)."));
                form->addRow(QString(), toDirectional);
                connect(toDirectional, &QPushButton::clicked, this,
                        [this, id] { emit convertToDirectionalRequested(id); });
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                // Guides et ruptures ne s'éditent pas ici (canevas) : conservés.
                const document::DirectionalFillParams dbase = p;
                auto* summary = new QLabel(tr("%1 guide(s) · %2 ligne(s) de rupture")
                                               .arg(p.guides.size())
                                               .arg(p.break_lines.size()),
                                           body_);
                summary->setObjectName(QStringLiteral("label_directionalSummary"));
                auto* editGuides = new QPushButton(tr("Éditer les guides…"), body_);
                editGuides->setObjectName(QStringLiteral("button_editDirectionGuides"));
                editGuides->setToolTip(tr("Tracer, déplacer ou supprimer les courbes guides et "
                                          "les lignes de rupture sur le canevas (D)."));
                connect(editGuides, &QPushButton::clicked, this,
                        [this, id] { emit editDirectionGuidesRequested(id); });
                auto* spacing = mmSpin(to_millimeters(p.row_spacing).value, 5.0);
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0);
                len->setMinimum(1.0);
                len->setToolTip(tr("Longueur cible, bornée entre 1 et 7 mm."));
                auto* edge = new QSpinBox(body_);
                edge->setKeyboardTracking(false);
                edge->setRange(0, 100);
                edge->setSuffix(tr(" %"));
                edge->setValue(static_cast<int>(std::lround(p.edge_weight * 100.0)));
                edge->setToolTip(tr("Influence de la tangente du bord le plus proche sur la "
                                    "direction du fil (0 % = guides seuls)."));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0);
                auto* stagger = new QSpinBox(body_);
                stagger->setKeyboardTracking(false);
                stagger->setRange(1, 8);
                stagger->setValue(p.stagger);
                auto* overlap = mmSpin(to_millimeters(p.sector_overlap).value, 1.0);
                overlap->setToolTip(tr("Chevauchement des secteurs le long des lignes de "
                                       "rupture (0,2 à 0,3 mm évite les interstices)."));
                auto* uEdge = new QCheckBox(tr("Sous-couche de contour"), body_);
                uEdge->setChecked(p.underlay_edge);
                auto* uInset = mmSpin(to_millimeters(p.underlay_inset).value, 5.0);
                auto* uPar = new QCheckBox(tr("Sous-couche perpendiculaire"), body_);
                uPar->setChecked(p.underlay_parallel);
                auto* uSpacing = mmSpin(to_millimeters(p.underlay_spacing).value, 10.0);
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setChecked(p.hidden_underpath);
                auto* handmade = new QCheckBox(tr("Aspect fait main"), body_);
                handmade->setObjectName(QStringLiteral("check_handmade"));
                handmade->setChecked(p.handmade);
                handmade->setToolTip(tr("Longueurs irrégulières, pénétrations imbriquées et "
                                        "légère ondulation, comme un passé empiétant. "
                                        "Reproductible : même projet, même résultat."));
                auto* intensity = new QSpinBox(body_);
                intensity->setKeyboardTracking(false);
                intensity->setObjectName(QStringLiteral("spin_handmadeIntensity"));
                intensity->setRange(0, 100);
                intensity->setSuffix(tr(" %"));
                intensity->setValue(std::clamp(p.handmade_intensity, 0, 100));
                intensity->setEnabled(p.handmade);
                auto* reseed = new QPushButton(tr("Autre tirage"), body_);
                reseed->setObjectName(QStringLiteral("button_handmadeReseed"));
                reseed->setToolTip(tr("Change la graine de l'aspect fait main (variation "
                                      "différente, toujours reproductible)."));
                reseed->setEnabled(p.handmade);
                form->addRow(QString(), summary);
                form->addRow(QString(), editGuides);
                form->addRow(tr("Espacement des lignes :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Influence des bords :"), edge);
                form->addRow(tr("Retrait de bord :"), inset);
                form->addRow(tr("Décalage (stagger) :"), stagger);
                form->addRow(tr("Chevauchement des secteurs :"), overlap);
                form->addRow(QString(), uEdge);
                form->addRow(tr("Retrait de la sous-couche :"), uInset);
                form->addRow(QString(), uPar);
                form->addRow(tr("Espacement de la sous-couche :"), uSpacing);
                form->addRow(QString(), underpath);
                form->addRow(QString(), handmade);
                form->addRow(tr("Intensité :"), intensity);
                form->addRow(QString(), reseed);
                const auto build = [dbase, spacing, len, edge, inset, stagger, overlap, uEdge,
                                    uInset, uPar, uSpacing, underpath, handmade, intensity] {
                    document::DirectionalFillParams d = dbase; // conserve guides + graine
                    d.row_spacing = to_um(spacing->value());
                    d.stitch_length = to_um(len->value());
                    d.edge_weight = edge->value() / 100.0;
                    d.inset = to_um(inset->value());
                    d.stagger = stagger->value();
                    d.sector_overlap = to_um(overlap->value());
                    d.underlay_edge = uEdge->isChecked();
                    d.underlay_inset = to_um(uInset->value());
                    d.underlay_parallel = uPar->isChecked();
                    d.underlay_spacing = to_um(uSpacing->value());
                    d.hidden_underpath = underpath->isChecked();
                    d.handmade = handmade->isChecked();
                    d.handmade_intensity = intensity->value();
                    return d;
                };
                const auto emitEdit = [this, id, build] {
                    if (building_)
                        return;
                    emit paramsEdited(id, build());
                };
                connect(spacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(len, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(edge, &QSpinBox::valueChanged, this, emitEdit);
                connect(inset, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(stagger, &QSpinBox::valueChanged, this, emitEdit);
                connect(overlap, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(uEdge, &QCheckBox::toggled, this, emitEdit);
                connect(uInset, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(uPar, &QCheckBox::toggled, this, emitEdit);
                connect(uSpacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(underpath, &QCheckBox::toggled, this, emitEdit);
                connect(intensity, &QSpinBox::valueChanged, this, emitEdit);
                connect(handmade, &QCheckBox::toggled, this, [intensity, reseed](bool on) {
                    intensity->setEnabled(on);
                    reseed->setEnabled(on);
                });
                connect(handmade, &QCheckBox::toggled, this, emitEdit);
                connect(reseed, &QPushButton::clicked, this, [this, id, build] {
                    auto d = build();
                    // Graine suivante d'un générateur congruentiel : déterministe,
                    // jamais tirée de l'horloge (projet reproductible).
                    d.seed = d.seed * 1'664'525U + 1'013'904'223U;
                    emit paramsEdited(id, d);
                });
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                // Les rails ne sont pas édités ici ; on les conserve tels quels.
                const document::SatinParams base = p;
                auto* density = mmSpin(to_millimeters(p.density).value, 2.0);
                auto* comp = mmSpin(to_millimeters(p.pull_compensation).value, 2.0);
                auto* underlay = new QCheckBox(tr("Sous-couche centrale"), body_);
                underlay->setChecked(p.center_underlay);
                auto* shortCombo = new QComboBox(body_);
                shortCombo->addItems({tr("Désactivés"), tr("Retirer/redistribuer"),
                                      tr("Inset simple"), tr("Inset multi-niveaux")});
                shortCombo->setCurrentIndex(static_cast<int>(p.short_stitch));
                auto* splitCombo = new QComboBox(body_);
                splitCombo->addItems({tr("Désactivé"), tr("Simple"), tr("Décalé"), tr("Jitter")});
                splitCombo->setCurrentIndex(static_cast<int>(p.split_stitch));
                auto* capCombo = new QComboBox(body_);
                capCombo->addItems({tr("Plat"), tr("Arrondi"), tr("Effilé"), tr("Auto")});
                capCombo->setCurrentIndex(static_cast<int>(p.cap_end));
                auto* maxLen = mmSpin(to_millimeters(p.max_stitch_length).value, 15.0);
                auto* maxHard = mmSpin(to_millimeters(p.max_width_hard).value, 60.0);
                maxHard->setMinimum(1.0);
                auto* edgeU = new QCheckBox(tr("Sous-couche de bord"), body_);
                edgeU->setChecked(p.underlay_edge);
                auto* zigU = new QCheckBox(tr("Sous-couche zigzag"), body_);
                zigU->setChecked(p.underlay_zigzag);
                auto* pullL = mmSpin(to_millimeters(p.pull_left).value, 3.0);
                auto* pullR = mmSpin(to_millimeters(p.pull_right).value, 3.0);
                const QStringList lockItems{tr("Aucun"), tr("Aller-retour"), tr("Triangle"),
                                            tr("Micro-zigzag")};
                auto* lockStart = new QComboBox(body_);
                lockStart->addItems(lockItems);
                lockStart->setCurrentIndex(static_cast<int>(p.lock_start));
                auto* lockEnd = new QComboBox(body_);
                lockEnd->addItems(lockItems);
                lockEnd->setCurrentIndex(static_cast<int>(p.lock_end));
                form->addRow(tr("Densité :"), density);
                form->addRow(tr("Compensation de tirage :"), comp);
                form->addRow(QString(), underlay);
                form->addRow(QString(), edgeU);
                form->addRow(QString(), zigU);
                form->addRow(tr("Compensation gauche :"), pullL);
                form->addRow(tr("Compensation droite :"), pullR);
                form->addRow(tr("Points courts (virages) :"), shortCombo);
                form->addRow(tr("Fractionnement :"), splitCombo);
                form->addRow(tr("Longueur max de point :"), maxLen);
                form->addRow(tr("Largeur max dure :"), maxHard);
                form->addRow(tr("Terminaison (fin) :"), capCombo);
                form->addRow(tr("Fixation (début) :"), lockStart);
                form->addRow(tr("Fixation (fin) :"), lockEnd);
                const auto emitEdit = [this, id, base, density, comp, underlay, shortCombo,
                                       splitCombo, capCombo, maxLen, maxHard, edgeU, zigU, pullL,
                                       pullR, lockStart, lockEnd] {
                    if (building_)
                        return;
                    document::SatinParams s = base; // conserve rails + barreaux
                    s.density = to_um(density->value());
                    s.pull_compensation = to_um(comp->value());
                    s.center_underlay = underlay->isChecked();
                    s.underlay_edge = edgeU->isChecked();
                    s.underlay_zigzag = zigU->isChecked();
                    s.pull_left = to_um(pullL->value());
                    s.pull_right = to_um(pullR->value());
                    s.short_stitch =
                        static_cast<document::SatinShortStitch>(shortCombo->currentIndex());
                    s.split_stitch = static_cast<document::SatinSplit>(splitCombo->currentIndex());
                    s.cap_end = static_cast<document::SatinCap>(capCombo->currentIndex());
                    s.max_stitch_length = to_um(maxLen->value());
                    s.max_width_hard = to_um(maxHard->value());
                    s.lock_start = static_cast<document::SatinLock>(lockStart->currentIndex());
                    s.lock_end = static_cast<document::SatinLock>(lockEnd->currentIndex());
                    emit paramsEdited(id, s);
                };
                connect(density, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(comp, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(underlay, &QCheckBox::toggled, this, emitEdit);
                connect(edgeU, &QCheckBox::toggled, this, emitEdit);
                connect(zigU, &QCheckBox::toggled, this, emitEdit);
                connect(pullL, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(pullR, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(shortCombo, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(splitCombo, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(capCombo, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(maxLen, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(maxHard, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(lockStart, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(lockEnd, &QComboBox::currentIndexChanged, this, emitEdit);
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                // Guides : édités par signaux dédiés (liste ci-dessous, rafraîchie par
                // setAutoSatinState). Les réglages scalaires passent par paramsEdited ;
                // MainWindow reprend alors les guides ACTUELS du document.
                const document::AutoSatinParams base = p;
                auto* summary = new QLabel(body_);
                summary->setObjectName(QStringLiteral("label_autoSatinSummary"));
                summary->setWordWrap(true);
                satinSummary_ = summary;
                auto* spacing = mmSpin(to_millimeters(p.spacing).value, 2.0);
                spacing->setMinimum(0.1);
                spacing->setToolTip(tr("Espacement cible entre deux traversées, mesuré au bord le "
                                       "plus écarté d'un virage."));
                auto* threshold = mmSpin(to_millimeters(p.split_threshold).value, 20.0);
                threshold->setMinimum(1.0);
                threshold->setToolTip(tr("Longueur de traversée au-delà de laquelle elle est "
                                         "fractionnée (Lmax)."));
                auto* splitLen = mmSpin(to_millimeters(p.split_length).value, 20.0);
                splitLen->setMinimum(1.0);
                splitLen->setToolTip(tr("Longueur maximale des segments après fractionnement (y), "
                                        "bornée à Lmax."));
                auto* splitCombo = new QComboBox(body_);
                splitCombo->addItems({tr("Désactivé"), tr("Simple"), tr("Décalé"), tr("Jitter")});
                splitCombo->setCurrentIndex(static_cast<int>(p.split_stitch));
                auto* shortCombo = new QComboBox(body_);
                shortCombo->addItems({tr("Désactivés"), tr("Retirer/redistribuer"),
                                      tr("Inset simple"), tr("Inset multi-niveaux")});
                shortCombo->setCurrentIndex(static_cast<int>(p.short_stitch));
                auto* comp = mmSpin(to_millimeters(p.pull_compensation).value, 2.0);
                auto* underlay = new QCheckBox(tr("Sous-couche centrale"), body_);
                underlay->setChecked(p.center_underlay);
                auto* edgeU = new QCheckBox(tr("Sous-couche de bord"), body_);
                edgeU->setChecked(p.underlay_edge);
                auto* zigU = new QCheckBox(tr("Sous-couche zigzag"), body_);
                zigU->setChecked(p.underlay_zigzag);
                auto* pullL = mmSpin(to_millimeters(p.pull_left).value, 3.0);
                auto* pullR = mmSpin(to_millimeters(p.pull_right).value, 3.0);
                const QStringList capItems{tr("Plat"), tr("Arrondi"), tr("Effilé"), tr("Auto")};
                auto* capStart = new QComboBox(body_);
                capStart->addItems(capItems);
                capStart->setCurrentIndex(static_cast<int>(p.cap_start));
                auto* capEnd = new QComboBox(body_);
                capEnd->addItems(capItems);
                capEnd->setCurrentIndex(static_cast<int>(p.cap_end));
                const QStringList lockItems{tr("Aucun"), tr("Aller-retour"), tr("Triangle"),
                                            tr("Micro-zigzag")};
                auto* lockStart = new QComboBox(body_);
                lockStart->addItems(lockItems);
                lockStart->setCurrentIndex(static_cast<int>(p.lock_start));
                auto* lockEnd = new QComboBox(body_);
                lockEnd->addItems(lockItems);
                lockEnd->setCurrentIndex(static_cast<int>(p.lock_end));

                // --- Guides d'orientation ---
                auto* guideList = new QListWidget(body_);
                guideList->setObjectName(QStringLiteral("list_satinGuides"));
                guideList->setMaximumHeight(110);
                satinGuideList_ = guideList;
                auto* guideAngle = new QSpinBox(body_);
                guideAngle->setKeyboardTracking(false);
                guideAngle->setObjectName(QStringLiteral("spin_satinGuideAngle"));
                guideAngle->setRange(-179, 179);
                guideAngle->setSuffix(tr(" °"));
                guideAngle->setEnabled(false);
                satinGuideAngle_ = guideAngle;
                auto* guideAbs = new QCheckBox(tr("Angle absolu (repère du dessin)"), body_);
                guideAbs->setObjectName(QStringLiteral("check_satinGuideAbsolute"));
                guideAbs->setToolTip(tr("Décoché : écart à la perpendiculaire de l'axe (0° = "
                                        "perpendiculaire). Coché : angle fixe dans le dessin."));
                guideAbs->setEnabled(false);
                satinGuideAbsolute_ = guideAbs;
                auto* guideRemove = new QPushButton(tr("Supprimer le guide"), body_);
                guideRemove->setObjectName(QStringLiteral("button_satinGuideRemove"));
                guideRemove->setEnabled(false);
                satinGuideRemove_ = guideRemove;
                auto* guideAdd = new QPushButton(tr("Placer un guide sur le canevas…"), body_);
                guideAdd->setObjectName(QStringLiteral("button_editSatinGuides"));
                guideAdd->setToolTip(tr("Tracez un trait : sa position ancre le guide et sa "
                                        "direction fixe l'angle des fils (D)."));

                // Intertitres : 20 champs à plat sont illisibles ; on les regroupe par sujet.
                const auto section = [this, form](const QString& title) {
                    auto* heading = new QLabel(title, body_);
                    QFont f = heading->font();
                    f.setBold(true);
                    heading->setFont(f);
                    heading->setContentsMargins(0, 8, 0, 0);
                    form->addRow(heading);
                };
                form->addRow(QString(), summary);
                section(tr("Remplissage"));
                form->addRow(tr("Espacement :"), spacing);
                form->addRow(tr("Seuil de fractionnement (Lmax) :"), threshold);
                form->addRow(tr("Longueur des segments (y) :"), splitLen);
                form->addRow(tr("Fractionnement :"), splitCombo);
                form->addRow(tr("Points courts dans les virages :"), shortCombo);
                section(tr("Compensation"));
                form->addRow(tr("Compensation de tirage :"), comp);
                form->addRow(tr("Compensation gauche :"), pullL);
                form->addRow(tr("Compensation droite :"), pullR);
                section(tr("Sous-couches"));
                form->addRow(QString(), underlay);
                form->addRow(QString(), edgeU);
                form->addRow(QString(), zigU);
                section(tr("Extrémités"));
                form->addRow(tr("Forme du bout (début) :"), capStart);
                form->addRow(tr("Forme du bout (fin) :"), capEnd);
                form->addRow(tr("Point d'arrêt (début) :"), lockStart);
                form->addRow(tr("Point d'arrêt (fin) :"), lockEnd);
                section(tr("Orientation des fils"));
                if (p.guides.empty()) {
                    auto* none = new QLabel(tr("Aucun guide : les fils sont perpendiculaires à "
                                               "l'axe de la forme. Placez un guide pour les "
                                               "orienter autrement."),
                                            body_);
                    none->setWordWrap(true);
                    none->setEnabled(false);
                    form->addRow(none);
                }
                form->addRow(tr("Guides d'orientation :"), guideList);
                form->addRow(tr("Angle du guide :"), guideAngle);
                form->addRow(QString(), guideAbs);
                form->addRow(QString(), guideRemove);
                form->addRow(QString(), guideAdd);

                const auto emitEdit = [this, id, base, spacing, threshold, splitLen, splitCombo,
                                       shortCombo, comp, underlay, edgeU, zigU, pullL, pullR,
                                       capStart, capEnd, lockStart, lockEnd] {
                    if (building_)
                        return;
                    document::AutoSatinParams s =
                        base; // guides, entrée/sortie : repris du document
                    s.spacing = to_um(spacing->value());
                    s.split_threshold = to_um(threshold->value());
                    s.split_length = to_um(std::min(splitLen->value(), threshold->value()));
                    s.split_stitch = static_cast<document::SatinSplit>(splitCombo->currentIndex());
                    s.short_stitch =
                        static_cast<document::SatinShortStitch>(shortCombo->currentIndex());
                    s.pull_compensation = to_um(comp->value());
                    s.center_underlay = underlay->isChecked();
                    s.underlay_edge = edgeU->isChecked();
                    s.underlay_zigzag = zigU->isChecked();
                    s.pull_left = to_um(pullL->value());
                    s.pull_right = to_um(pullR->value());
                    s.cap_start = static_cast<document::SatinCap>(capStart->currentIndex());
                    s.cap_end = static_cast<document::SatinCap>(capEnd->currentIndex());
                    s.lock_start = static_cast<document::SatinLock>(lockStart->currentIndex());
                    s.lock_end = static_cast<document::SatinLock>(lockEnd->currentIndex());
                    emit paramsEdited(id, s);
                };
                connect(spacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(threshold, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(splitLen, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(splitCombo, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(shortCombo, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(comp, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(underlay, &QCheckBox::toggled, this, emitEdit);
                connect(edgeU, &QCheckBox::toggled, this, emitEdit);
                connect(zigU, &QCheckBox::toggled, this, emitEdit);
                connect(pullL, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(pullR, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(capStart, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(capEnd, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(lockStart, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(lockEnd, &QComboBox::currentIndexChanged, this, emitEdit);

                connect(guideAdd, &QPushButton::clicked, this,
                        [this, id] { emit editSatinGuidesRequested(id); });
                connect(guideList, &QListWidget::currentRowChanged, this,
                        [this, guideList, guideAngle, guideAbs, guideRemove](int row) {
                            const bool has = row >= 0;
                            guideAngle->setEnabled(has);
                            guideAbs->setEnabled(has);
                            guideRemove->setEnabled(has);
                            if (!has) {
                                return;
                            }
                            // Valeurs portées par l'item (cf. setAutoSatinState).
                            const QSignalBlocker b1(guideAngle);
                            const QSignalBlocker b2(guideAbs);
                            guideAngle->setValue(guideList->item(row)->data(Qt::UserRole).toInt());
                            guideAbs->setChecked(
                                guideList->item(row)->data(Qt::UserRole + 1).toBool());
                        });
                const auto emitGuide = [this, id, guideList, guideAngle, guideAbs] {
                    if (building_ || guideList->currentRow() < 0) {
                        return;
                    }
                    emit satinGuideChangeRequested(id, guideList->currentRow(),
                                                   static_cast<double>(guideAngle->value()),
                                                   guideAbs->isChecked());
                };
                connect(guideAngle, &QSpinBox::valueChanged, this, emitGuide);
                connect(guideAbs, &QCheckBox::toggled, this, emitGuide);
                connect(guideRemove, &QPushButton::clicked, this, [this, id, guideList] {
                    if (guideList->currentRow() >= 0) {
                        emit satinGuideRemoveRequested(id, guideList->currentRow());
                    }
                });
                // Premier remplissage de la liste et du résumé.
                setAutoSatinState(id, &p, QString());
            }
        },
        object.params);
    building_ = false;

    auto* holder = new QWidget(body_);
    holder->setLayout(form);
    body_->layout()->addWidget(holder);
}

} // namespace openstitch::desktop
