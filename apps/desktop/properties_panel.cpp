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
#include <functional>
#include <numbers>
#include <type_traits>
#include <utility>
#include <variant>

namespace openstitch::desktop {

namespace {

Micrometers to_um(double mm) {
    return to_micrometers(Millimeters{mm});
}

double to_deg(Angle a) {
    return a.radians * 180.0 / std::numbers::pi;
}

Angle from_deg(double deg) {
    return Angle{deg * std::numbers::pi / 180.0};
}

// Libellés et bornes partagés (satin manuel et auto-satin parlent la même langue).
QStringList shortStitchItems() {
    return {QObject::tr("Désactivés"), QObject::tr("Retirer/redistribuer"),
            QObject::tr("Inset simple"), QObject::tr("Inset multi-niveaux")};
}
QStringList splitItems() {
    return {QObject::tr("Désactivé"), QObject::tr("Simple"), QObject::tr("Décalé"),
            QObject::tr("Jitter")};
}
QStringList capItems() {
    return {QObject::tr("Plat"), QObject::tr("Arrondi"), QObject::tr("Effilé"),
            QObject::tr("Auto")};
}
QStringList lockItems() {
    return {QObject::tr("Aucun"), QObject::tr("Aller-retour"), QObject::tr("Triangle"),
            QObject::tr("Micro-zigzag")};
}

QStringList borderSideItems() {
    return {QObject::tr("Centré sur le contour"), QObject::tr("Intérieur"),
            QObject::tr("Extérieur")};
}
QStringList borderCornerItems() {
    return {QObject::tr("Vifs (onglet)"), QObject::tr("Arrondis")};
}

// Choix « Manuelle / Automatique » de la sous-couche (HP-ENG-002).
QComboBox* makeUnderlayModeCombo(QWidget* parent, document::UnderlayMode mode) {
    auto* combo = new QComboBox(parent);
    combo->setObjectName(QStringLiteral("combo_underlayMode"));
    combo->addItems({QObject::tr("Manuelle"), QObject::tr("Automatique")});
    combo->setCurrentIndex(static_cast<int>(mode));
    combo->setToolTip(
        QObject::tr("Automatique : le moteur choisit la sous-couche selon le type, la taille et "
                    "la largeur de la forme (rien pour une petite forme, centre pour un satin "
                    "étroit, contour et rangées perpendiculaires pour une grande surface). Les "
                    "réglages manuels sont alors ignorés."));
    return combo;
}

// Active ou grise un champ et son libellé de ligne.
void setRowEnabled(QFormLayout* form, QWidget* field, bool on) {
    field->setEnabled(on);
    if (QWidget* label = form->labelForField(field)) {
        label->setEnabled(on);
    }
}

template <class T>
using EditFn = std::function<void(const QString&, const std::function<void(T&)>&)>;

// Liaisons widget -> champ : chaque widget ne modifie QUE son champ (cf. showEmbroidery).
template <class T>
void bindMmField(QObject* ctx, const EditFn<T>& edit, QDoubleSpinBox* spin, const QString& label,
                 Micrometers T::*member) {
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, ctx, [edit, label, member](double v) {
        edit(label, [v, member](T& t) { t.*member = to_um(v); });
    });
}
template <class T>
void bindIntField(QObject* ctx, const EditFn<T>& edit, QSpinBox* spin, const QString& label,
                  int T::*member) {
    QObject::connect(spin, &QSpinBox::valueChanged, ctx, [edit, label, member](int v) {
        edit(label, [v, member](T& t) { t.*member = v; });
    });
}
template <class T>
void bindBoolField(QObject* ctx, const EditFn<T>& edit, QCheckBox* box, const QString& label,
                   bool T::*member) {
    QObject::connect(box, &QCheckBox::toggled, ctx, [edit, label, member](bool v) {
        edit(label, [v, member](T& t) { t.*member = v; });
    });
}
template <class T, class E>
void bindEnumField(QObject* ctx, const EditFn<T>& edit, QComboBox* combo, const QString& label,
                   E T::*member) {
    QObject::connect(combo, &QComboBox::currentIndexChanged, ctx, [edit, label, member](int index) {
        edit(label, [index, member](T& t) { t.*member = static_cast<E>(index); });
    });
}

// Grise `field` (et son libellé de ligne) tant que `box` est décochée.
void dependOnField(QFormLayout* form, QCheckBox* box, QWidget* field) {
    const auto apply = [form, field](bool on) {
        field->setEnabled(on);
        if (QWidget* label = form->labelForField(field)) {
            label->setEnabled(on);
        }
    };
    apply(box->isChecked());
    QObject::connect(box, &QCheckBox::toggled, field, apply);
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    wheelGuard_ = new WheelGuard(this);
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
    vectorId_.reset();
    hasShown_ = false;
    satinGuideList_.clear();
    satinSummary_.clear();
    satinGuideAngle_.clear();
    satinGuideAngleLabel_.clear();
    satinGuideAbsolute_.clear();
    satinGuideRemove_.clear();
    if (auto* lay = body_->layout()) {
        while (QLayoutItem* item = lay->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }
}

QDoubleSpinBox* PropertiesPanel::mmSpin(double valueMm, double maxMm, double minMm,
                                        const QString& tip) {
    auto* spin = new QDoubleSpinBox(body_);
    // Frappe au clavier : une seule modification à la validation, pas une par chiffre
    // (chaque modification régénère les points).
    spin->setKeyboardTracking(false);
    spin->setRange(minMm, maxMm);
    spin->setDecimals(2);
    spin->setSingleStep(0.1);
    spin->setSuffix(tr(" mm"));
    spin->setValue(valueMm);
    const QString range = tr("Plage : %1 – %2 mm").arg(minMm, 0, 'f', 2).arg(maxMm, 0, 'f', 2);
    spin->setToolTip(tip.isEmpty() ? range : tip + QLatin1Char('\n') + range);
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

void PropertiesPanel::showVectorObject(ObjectId id, const QString& title, const QString& details,
                                       QRectF boxMm) {
    clearBody();
    vectorId_ = id;
    vectorBox_ = boxMm;
    header_->setText(title);
    setEditState(std::nullopt, stitch_generation::ObjectEditState::Clean);
    auto* label = new QLabel(details, body_);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->layout()->addWidget(label);
    if (boxMm.width() <= 0.0 || boxMm.height() <= 0.0) {
        return; // forme dégénérée : pas de redimensionnement possible
    }
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    const auto makeSpin = [this](const char* name, double value, double minMm, double maxMm,
                                 const QString& tip) {
        auto* spin = new QDoubleSpinBox(body_);
        spin->setObjectName(QString::fromLatin1(name));
        spin->setKeyboardTracking(false);
        spin->setRange(minMm, maxMm);
        spin->setDecimals(2);
        spin->setSingleStep(0.5);
        spin->setSuffix(tr(" mm"));
        spin->setValue(value);
        spin->setToolTip(tip + QLatin1Char('\n') +
                         tr("Plage : %1 – %2 mm").arg(minMm, 0, 'f', 2).arg(maxMm, 0, 'f', 2));
        return spin;
    };
    auto* x = makeSpin("spin_vectorX", boxMm.x(), -5000.0, 5000.0,
                       tr("Position du bord gauche de la forme."));
    auto* y = makeSpin("spin_vectorY", boxMm.y(), -5000.0, 5000.0,
                       tr("Position du bord bas de la forme (Y vers le haut)."));
    auto* w = makeSpin("spin_vectorW", boxMm.width(), 0.1, 5000.0, tr("Largeur de la forme."));
    auto* h = makeSpin("spin_vectorH", boxMm.height(), 0.1, 5000.0, tr("Hauteur de la forme."));
    auto* keep = new QCheckBox(tr("Conserver les proportions"), body_);
    keep->setObjectName(QStringLiteral("check_vectorProportions"));
    keep->setChecked(true);
    form->addRow(tr("X :"), x);
    form->addRow(tr("Y :"), y);
    form->addRow(tr("Largeur :"), w);
    form->addRow(tr("Hauteur :"), h);
    form->addRow(QString(), keep);
    const double ratio = boxMm.width() / boxMm.height();
    const auto emitBox = [this, id, x, y, w, h] {
        emit vectorBoxEdited(id, QRectF(x->value(), y->value(), w->value(), h->value()));
    };
    connect(x, &QDoubleSpinBox::valueChanged, this, emitBox);
    connect(y, &QDoubleSpinBox::valueChanged, this, emitBox);
    connect(w, &QDoubleSpinBox::valueChanged, this, [h, keep, ratio, emitBox](double value) {
        if (keep->isChecked()) {
            const QSignalBlocker block(h);
            h->setValue(value / ratio);
        }
        emitBox();
    });
    connect(h, &QDoubleSpinBox::valueChanged, this, [w, keep, ratio, emitBox](double value) {
        if (keep->isChecked()) {
            const QSignalBlocker block(w);
            w->setValue(value * ratio);
        }
        emitBox();
    });
    auto* holder = new QWidget(body_);
    holder->setLayout(form);
    body_->layout()->addWidget(holder);

    // HP-STI-004 : création d'un satin de bordure à largeur fixe le long du contour.
    auto* borderForm = new QFormLayout();
    borderForm->setLabelAlignment(Qt::AlignRight);
    auto* heading = new QLabel(tr("Satin de bordure"), body_);
    QFont hf = heading->font();
    hf.setBold(true);
    heading->setFont(hf);
    borderForm->addRow(heading);
    auto* bWidth = makeSpin("spin_borderWidth", 3.0, 0.5, 20.0,
                            tr("Largeur constante de la colonne le long du contour."));
    bWidth->setSingleStep(0.1);
    auto* bSide = new QComboBox(body_);
    bSide->setObjectName(QStringLiteral("combo_borderSide"));
    bSide->addItems(borderSideItems());
    bSide->setToolTip(tr("Côté du contour où s'étend la colonne."));
    auto* bCorner = new QComboBox(body_);
    bCorner->setObjectName(QStringLiteral("combo_borderCorner"));
    bCorner->addItems(borderCornerItems());
    bCorner->setToolTip(tr("Vifs : onglet. Arrondis : arc dans les coins extérieurs."));
    auto* bCreate = new QPushButton(tr("Créer le satin de bordure"), body_);
    bCreate->setObjectName(QStringLiteral("button_createBorderSatin"));
    bCreate->setToolTip(tr("Un satin par contour (extérieur et trous), annulable (Ctrl+Z)."));
    borderForm->addRow(tr("Largeur :"), bWidth);
    borderForm->addRow(tr("Côté :"), bSide);
    borderForm->addRow(tr("Coins :"), bCorner);
    borderForm->addRow(QString(), bCreate);
    connect(bCreate, &QPushButton::clicked, this, [this, id, bWidth, bSide, bCorner] {
        emit createBorderSatinRequested(id, bWidth->value(), bSide->currentIndex(),
                                        bCorner->currentIndex());
    });
    auto* borderHolder = new QWidget(body_);
    borderHolder->setLayout(borderForm);
    body_->layout()->addWidget(borderHolder);
    wheelGuard_->guardAll(body_);
}

void PropertiesPanel::setJoinMode(std::optional<ObjectId> id, document::JoinMode mode) {
    if (!id || !currentId_ || *id != *currentId_ || joinCombo_ == nullptr) {
        return;
    }
    const QSignalBlocker block(joinCombo_);
    joinCombo_->setCurrentIndex(static_cast<int>(mode));
}

bool PropertiesPanel::showsVectorBox(ObjectId id, QRectF boxMm) const {
    if (!vectorId_ || *vectorId_ != id) {
        return false;
    }
    constexpr double tol = 0.02;
    return std::abs(vectorBox_.x() - boxMm.x()) <= tol &&
           std::abs(vectorBox_.y() - boxMm.y()) <= tol &&
           std::abs(vectorBox_.width() - boxMm.width()) <= tol &&
           std::abs(vectorBox_.height() - boxMm.height()) <= tol;
}

void PropertiesPanel::showMultiSelection(int objectCount, int embroideryCount) {
    clearBody();
    header_->setText(tr("%1 objets").arg(objectCount));
    setEditState(std::nullopt, stitch_generation::ObjectEditState::Clean);
    auto* label = new QLabel(
        tr("%1 objets vectoriels sélectionnés (%2 avec une couture).\nLe type de points ci-dessous "
           "s'applique à tous ; supprimer ou déplacer (flèches) aussi, et Édition > Aligner les "
           "range sur la sélection.")
            .arg(objectCount)
            .arg(embroideryCount),
        body_);
    label->setWordWrap(true);
    body_->layout()->addWidget(label);
    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    auto* type = new QComboBox(body_);
    type->setObjectName(QStringLiteral("combo_multiType"));
    type->addItems({tr("(inchangé)"), tr("Contour cousu"), tr("Tatami"), tr("Satin automatique"),
                    tr("Remplissage directionnel")});
    type->setToolTip(
        tr("Donne ce type de points à toutes les formes sélectionnées, comme le choix "
           "de type d'une forme seule. Les formes sans couture en reçoivent une ; "
           "celles qui ne peuvent pas être cousues en satin sont ignorées et listées."));
    auto* useSpacing = new QCheckBox(tr("Espacement des rangées"), body_);
    useSpacing->setObjectName(QStringLiteral("check_multiSpacing"));
    auto* spacing = mmSpin(0.4, 5.0, 0.1, tr("Écart entre rangées (tatami, directionnel, satin)."));
    spacing->setObjectName(QStringLiteral("spin_multiSpacing"));
    spacing->setEnabled(false);
    auto* useAngle = new QCheckBox(tr("Angle (tatami)"), body_);
    useAngle->setObjectName(QStringLiteral("check_multiAngle"));
    auto* angle = new QDoubleSpinBox(body_);
    angle->setObjectName(QStringLiteral("spin_multiAngle"));
    angle->setKeyboardTracking(false);
    angle->setRange(0.0, 179.9);
    angle->setDecimals(1);
    angle->setWrapping(true);
    angle->setSuffix(tr(" °"));
    angle->setEnabled(false);
    angle->setToolTip(tr("0° = horizontal, sens trigonométrique. Plage : 0 – 179,9°."));
    connect(useSpacing, &QCheckBox::toggled, spacing, &QWidget::setEnabled);
    connect(useAngle, &QCheckBox::toggled, angle, &QWidget::setEnabled);
    auto* apply = new QPushButton(tr("Appliquer à %1 objets").arg(objectCount), body_);
    apply->setObjectName(QStringLiteral("button_multiApply"));
    apply->setToolTip(tr("Applique les réglages cochés aux coutures des objets sélectionnés, "
                         "en une seule étape annulable."));
    form->addRow(tr("Type de points :"), type);
    form->addRow(useSpacing, spacing);
    form->addRow(useAngle, angle);
    form->addRow(QString(), apply);
    connect(apply, &QPushButton::clicked, this, [this, type, useSpacing, spacing, useAngle, angle] {
        emit applyToSelectionRequested(type->currentIndex() - 1, useSpacing->isChecked(),
                                       spacing->value(), useAngle->isChecked(), angle->value());
    });
    auto* holder = new QWidget(body_);
    holder->setLayout(form);
    body_->layout()->addWidget(holder);
    wheelGuard_->guardAll(body_);
}

bool PropertiesPanel::showsParams(const document::StitchParams& params) const {
    return hasShown_ && shown_ == params;
}

void PropertiesPanel::adoptParams(ObjectId id, const document::StitchParams& params) {
    if (hasShown_ && currentId_ && *currentId_ == id) {
        shown_ = params;
    }
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

void PropertiesPanel::updateGuideAngleLabel(bool absolute) {
    if (satinGuideAngleLabel_ == nullptr) {
        return;
    }
    satinGuideAngleLabel_->setText(
        absolute ? tr("Angle absolu (° depuis l'horizontale, sens trigonométrique) :")
                 : tr("Écart à la perpendiculaire de l'axe (°, 0° = perpendiculaire) :"));
}

void PropertiesPanel::setAutoSatinState(std::optional<ObjectId> id,
                                        const document::AutoSatinParams* params,
                                        const QString& summary) {
    if (!id || params == nullptr || !currentId_ || *currentId_ != *id) {
        return;
    }
    // Les guides se posent aussi depuis le canevas : la copie interne les suit pour que
    // `showsParams` ne voie pas d'écart (sinon le formulaire serait reconstruit).
    if (auto* shown = std::get_if<document::AutoSatinParams>(&shown_)) {
        shown->guides = params->guides;
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
            const double deg = to_deg(g.angle);
            auto* item = new QListWidgetItem(tr("Guide %1 — %2 mm, %3 mm — %4° (%5)")
                                                 .arg(++n)
                                                 .arg(to_millimeters(g.anchor.x).value, 0, 'f', 1)
                                                 .arg(to_millimeters(g.anchor.y).value, 0, 'f', 1)
                                                 .arg(deg, 0, 'f', 1)
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
            satinGuideAngle_->setValue(satinGuideList_->item(row)->data(Qt::UserRole).toDouble());
            const bool absolute = satinGuideList_->item(row)->data(Qt::UserRole + 1).toBool();
            satinGuideAbsolute_->setChecked(absolute);
            updateGuideAngleLabel(absolute);
        }
    }
    building_ = wasBuilding;
}

void PropertiesPanel::showEmbroidery(const document::EmbroideryObject& object) {
    clearBody();
    currentId_ = object.id;
    shown_ = object.params;
    hasShown_ = true;
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

    // HP-ENG-010 : sens de couture automatique, par objet (le réglage global est dans
    // « Options de génération »). Le mode n'appartient pas aux `StitchParams` : signal dédié.
    auto* joinCombo = new QComboBox(body_);
    joinCombo->setObjectName(QStringLiteral("combo_joinMode"));
    joinCombo->addItems({tr("Réglage du projet"), tr("Automatique"), tr("Désactivée")});
    joinCombo->setCurrentIndex(static_cast<int>(object.join));
    joinCombo->setToolTip(
        tr("Entrée/sortie automatiques : l'objet est cousu dans le sens qui rapproche son début "
           "de la fin de l'objet précédent (moins de sauts et de coupes). « Réglage du projet » "
           "suit Broderie > Options de génération ; « Désactivée » garde le sens naturel."));
    joinCombo_ = joinCombo;
    form->addRow(tr("Entrée/sortie :"), joinCombo);
    connect(joinCombo, &QComboBox::currentIndexChanged, this, [this, id](int index) {
        if (!building_) {
            emit joinModeEdited(id, index);
        }
    });

    building_ = true;
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;

            // --- Émission champ par champ -------------------------------------------
            // Chaque widget ne modifie QUE son champ de la copie `shown_` : les autres
            // valeurs (jamais relues dans les widgets, donc jamais arrondies) sont
            // conservées telles quelles. `label` nomme l'étape d'historique.
            const EditFn<T> edit = [this, id](const QString& label,
                                              const std::function<void(T&)>& fn) {
                if (building_) {
                    return;
                }
                if (auto* t = std::get_if<T>(&shown_)) {
                    fn(*t);
                    emit paramsEdited(id, shown_, label);
                }
            };
            [[maybe_unused]] const auto bindMm =
                [this, &edit](QDoubleSpinBox* spin, const QString& label, Micrometers T::*member) {
                    bindMmField<T>(this, edit, spin, label, member);
                };
            [[maybe_unused]] const auto bindInt =
                [this, &edit](QSpinBox* spin, const QString& label, int T::*member) {
                    bindIntField<T>(this, edit, spin, label, member);
                };
            [[maybe_unused]] const auto bindBool =
                [this, &edit](QCheckBox* box, const QString& label, bool T::*member) {
                    bindBoolField<T>(this, edit, box, label, member);
                };
            // Grise `field` (et son libellé) tant que `box` est décochée (Mo8).
            [[maybe_unused]] const auto dependOn = [form](QCheckBox* box, QWidget* field) {
                dependOnField(form, box, field);
            };

            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 20.0, 0.5,
                                   tr("Distance cible entre deux pénétrations."));
                auto* minl = mmSpin(to_millimeters(p.min_length).value, 20.0, 0.1,
                                    tr("En dessous de cette longueur, les points sont fusionnés."));
                auto* rep = new QSpinBox(body_);
                rep->setKeyboardTracking(false);
                rep->setRange(1, 3);
                rep->setValue(p.repeats);
                rep->setToolTip(tr("1 = simple, 2 = aller-retour, 3 = point triple"));
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Passages :"), rep);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                bindMm(minl, tr("Longueur minimale"), &T::min_length);
                bindInt(rep, tr("Passages"), &T::repeats);
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                auto* spacing = mmSpin(to_millimeters(p.row_spacing).value, 5.0, 0.1,
                                       tr("Écart entre deux rangées (densité)."));
                spacing->setObjectName(QStringLiteral("spin_rowSpacing"));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 10.0, 1.0,
                                   tr("Longueur de point le long d'une rangée."));
                auto* angle = new QDoubleSpinBox(body_);
                angle->setObjectName(QStringLiteral("spin_tatamiAngle"));
                angle->setKeyboardTracking(false);
                angle->setRange(0.0, 179.9);
                angle->setDecimals(1);
                angle->setWrapping(true);
                angle->setSuffix(tr(" °"));
                angle->setToolTip(tr("Orientation des rangées : 0° = horizontal, sens "
                                     "trigonométrique. Plage : 0 – 179,9°."));
                angle->setValue(std::fmod(std::fmod(to_deg(p.angle), 180.0) + 180.0, 180.0));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                     tr("Retrait des points par rapport au bord."));
                auto* stagger = new QSpinBox(body_);
                stagger->setKeyboardTracking(false);
                stagger->setRange(1, 8);
                stagger->setValue(p.stagger);
                stagger->setToolTip(tr("Nombre de rangées avant répétition de la phase des "
                                       "pénétrations. Plage : 1 – 8."));
                // Tatami avancé (Lot 7).
                auto* uEdge = new QCheckBox(tr("Sous-couche de contour"), body_);
                uEdge->setObjectName(QStringLiteral("check_underlayEdge"));
                uEdge->setChecked(p.underlay_edge);
                auto* uInset = mmSpin(to_millimeters(p.underlay_inset).value, 5.0, 0.0);
                uInset->setObjectName(QStringLiteral("spin_underlayInset"));
                auto* uPar = new QCheckBox(tr("Sous-couche parallèle"), body_);
                uPar->setObjectName(QStringLiteral("check_underlayParallel"));
                uPar->setChecked(p.underlay_parallel);
                auto* uSpacing = mmSpin(to_millimeters(p.underlay_spacing).value, 10.0, 0.1);
                uSpacing->setObjectName(QStringLiteral("spin_underlaySpacing"));
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setChecked(p.hidden_underpath);
                // HP-ENG-001 : compensation du tirage (allongement des rangées dans l'axe du fil).
                auto* pull =
                    mmSpin(to_millimeters(p.pull_compensation).value, 3.0, 0.0,
                           tr("Les rangées dépassent du contour de cette longueur, dans l'axe "
                              "du fil : le fil tire dans sa direction et la forme cousue "
                              "rétrécit. 0,2 à 0,4 mm est courant ; 0 = aucune."));
                pull->setObjectName(QStringLiteral("spin_tatamiPull"));
                auto* uMode = makeUnderlayModeCombo(body_, p.underlay_mode);
                form->addRow(tr("Espacement des rangées :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Angle (orientation) :"), angle);
                form->addRow(tr("Retrait de bord :"), inset);
                form->addRow(tr("Compensation du tirage :"), pull);
                form->addRow(tr("Décalage (stagger) :"), stagger);
                form->addRow(tr("Sous-couche :"), uMode);
                form->addRow(QString(), uEdge);
                form->addRow(tr("Retrait de la sous-couche :"), uInset);
                form->addRow(QString(), uPar);
                form->addRow(tr("Espacement de la sous-couche :"), uSpacing);
                form->addRow(QString(), underpath);
                dependOn(uEdge, uInset);
                dependOn(uPar, uSpacing);
                // Mode automatique : les réglages manuels sont ignorés, donc grisés.
                const auto syncUnderlay = [form, uMode, uEdge, uInset, uPar, uSpacing] {
                    const bool manual = uMode->currentIndex() == 0;
                    uEdge->setEnabled(manual);
                    uPar->setEnabled(manual);
                    setRowEnabled(form, uInset, manual && uEdge->isChecked());
                    setRowEnabled(form, uSpacing, manual && uPar->isChecked());
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode);
                bindMm(pull, tr("Compensation du tirage"), &T::pull_compensation);
                bindMm(spacing, tr("Espacement des rangées"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                connect(angle, &QDoubleSpinBox::valueChanged, this, [edit](double v) {
                    edit(tr("Angle"), [v](T& t) { t.angle = from_deg(v); });
                });
                bindMm(inset, tr("Retrait de bord"), &T::inset);
                bindInt(stagger, tr("Décalage"), &T::stagger);
                bindBool(uEdge, tr("Sous-couche de contour"), &T::underlay_edge);
                bindMm(uInset, tr("Retrait de la sous-couche"), &T::underlay_inset);
                bindBool(uPar, tr("Sous-couche parallèle"), &T::underlay_parallel);
                bindMm(uSpacing, tr("Espacement de la sous-couche"), &T::underlay_spacing);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
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
                // Guides et ruptures ne s'éditent pas ici (canevas) : jamais touchés.
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
                auto* spacing = mmSpin(to_millimeters(p.row_spacing).value, 5.0, 0.1,
                                       tr("Écart entre deux lignes de couture (densité)."));
                spacing->setObjectName(QStringLiteral("spin_rowSpacing"));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0, 1.0,
                                   tr("Longueur cible, bornée entre 1 et 7 mm."));
                auto* edge = new QSpinBox(body_);
                edge->setKeyboardTracking(false);
                edge->setRange(0, 100);
                edge->setSuffix(tr(" %"));
                edge->setValue(static_cast<int>(std::lround(p.edge_weight * 100.0)));
                edge->setToolTip(tr("Influence de la tangente du bord le plus proche sur la "
                                    "direction du fil (0 % = guides seuls). Plage : 0 – 100 %."));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                     tr("Retrait des points par rapport au bord."));
                auto* stagger = new QSpinBox(body_);
                stagger->setKeyboardTracking(false);
                stagger->setRange(1, 8);
                stagger->setValue(p.stagger);
                stagger->setToolTip(tr("Nombre de lignes avant répétition de la phase des "
                                       "pénétrations. Plage : 1 – 8."));
                auto* overlap = mmSpin(to_millimeters(p.sector_overlap).value, 1.0, 0.0,
                                       tr("Chevauchement des secteurs le long des lignes de "
                                          "rupture (0,2 à 0,3 mm évite les interstices)."));
                auto* uEdge = new QCheckBox(tr("Sous-couche de contour"), body_);
                uEdge->setObjectName(QStringLiteral("check_underlayEdge"));
                uEdge->setChecked(p.underlay_edge);
                auto* uInset = mmSpin(to_millimeters(p.underlay_inset).value, 5.0, 0.0);
                uInset->setObjectName(QStringLiteral("spin_underlayInset"));
                auto* uPar = new QCheckBox(tr("Sous-couche perpendiculaire"), body_);
                uPar->setObjectName(QStringLiteral("check_underlayParallel"));
                uPar->setChecked(p.underlay_parallel);
                auto* uSpacing = mmSpin(to_millimeters(p.underlay_spacing).value, 10.0, 0.1);
                uSpacing->setObjectName(QStringLiteral("spin_underlaySpacing"));
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
                auto* uMode = makeUnderlayModeCombo(body_, p.underlay_mode);
                form->addRow(tr("Sous-couche :"), uMode);
                form->addRow(QString(), uEdge);
                form->addRow(tr("Retrait de la sous-couche :"), uInset);
                form->addRow(QString(), uPar);
                form->addRow(tr("Espacement de la sous-couche :"), uSpacing);
                form->addRow(QString(), underpath);
                form->addRow(QString(), handmade);
                form->addRow(tr("Intensité :"), intensity);
                form->addRow(QString(), reseed);
                dependOn(uEdge, uInset);
                dependOn(uPar, uSpacing);
                const auto syncUnderlay = [form, uMode, uEdge, uInset, uPar, uSpacing] {
                    const bool manual = uMode->currentIndex() == 0;
                    uEdge->setEnabled(manual);
                    uPar->setEnabled(manual);
                    setRowEnabled(form, uInset, manual && uEdge->isChecked());
                    setRowEnabled(form, uSpacing, manual && uPar->isChecked());
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode);
                bindMm(spacing, tr("Espacement des lignes"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                connect(edge, &QSpinBox::valueChanged, this, [edit](int v) {
                    edit(tr("Influence des bords"), [v](T& t) { t.edge_weight = v / 100.0; });
                });
                bindMm(inset, tr("Retrait de bord"), &T::inset);
                bindInt(stagger, tr("Décalage"), &T::stagger);
                bindMm(overlap, tr("Chevauchement des secteurs"), &T::sector_overlap);
                bindBool(uEdge, tr("Sous-couche de contour"), &T::underlay_edge);
                bindMm(uInset, tr("Retrait de la sous-couche"), &T::underlay_inset);
                bindBool(uPar, tr("Sous-couche perpendiculaire"), &T::underlay_parallel);
                bindMm(uSpacing, tr("Espacement de la sous-couche"), &T::underlay_spacing);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
                bindInt(intensity, tr("Intensité"), &T::handmade_intensity);
                connect(handmade, &QCheckBox::toggled, this, [intensity, reseed](bool on) {
                    intensity->setEnabled(on);
                    reseed->setEnabled(on);
                });
                bindBool(handmade, tr("Aspect fait main"), &T::handmade);
                connect(reseed, &QPushButton::clicked, this, [this, edit] {
                    // Graine suivante d'un générateur congruentiel : déterministe,
                    // jamais tirée de l'horloge (projet reproductible). Pas de fusion
                    // d'annulation : chaque tirage est un pas distinct.
                    edit(tr("Autre tirage"),
                         [](T& t) { t.seed = t.seed * 1'664'525U + 1'013'904'223U; });
                });
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                // Les rails ne sont pas édités ici ; ils ne sont jamais touchés.
                auto* density = mmSpin(to_millimeters(p.density).value, 2.0, 0.1,
                                       tr("Écart entre deux traversées (densité)."));
                auto* comp = mmSpin(to_millimeters(p.pull_compensation).value, 2.0);
                auto* underlay = new QCheckBox(tr("Sous-couche centrale"), body_);
                underlay->setChecked(p.center_underlay);
                auto* shortCombo = new QComboBox(body_);
                shortCombo->addItems(shortStitchItems());
                shortCombo->setCurrentIndex(static_cast<int>(p.short_stitch));
                auto* splitCombo = new QComboBox(body_);
                splitCombo->addItems(splitItems());
                splitCombo->setCurrentIndex(static_cast<int>(p.split_stitch));
                auto* capStart = new QComboBox(body_);
                capStart->addItems(capItems());
                capStart->setCurrentIndex(static_cast<int>(p.cap_start));
                auto* capEnd = new QComboBox(body_);
                capEnd->addItems(capItems());
                capEnd->setCurrentIndex(static_cast<int>(p.cap_end));
                auto* maxLen =
                    mmSpin(to_millimeters(p.max_stitch_length).value, 15.0, 1.0,
                           tr("Longueur maximale d'un point satin avant fractionnement."));
                auto* maxHard = mmSpin(to_millimeters(p.max_width_hard).value, 60.0, 1.0);
                auto* edgeU = new QCheckBox(tr("Sous-couche de bord"), body_);
                edgeU->setChecked(p.underlay_edge);
                auto* zigU = new QCheckBox(tr("Sous-couche zigzag"), body_);
                zigU->setChecked(p.underlay_zigzag);
                auto* pullL = mmSpin(to_millimeters(p.pull_left).value, 3.0);
                auto* pullR = mmSpin(to_millimeters(p.pull_right).value, 3.0);
                auto* lockStart = new QComboBox(body_);
                lockStart->addItems(lockItems());
                lockStart->setCurrentIndex(static_cast<int>(p.lock_start));
                auto* lockEnd = new QComboBox(body_);
                lockEnd->addItems(lockItems());
                lockEnd->setCurrentIndex(static_cast<int>(p.lock_end));
                // HP-STI-004 : satin de bordure -- largeur, côté et coins régénèrent les rails
                // depuis le contour source (signal dédié, MainWindow exécute la commande).
                if (p.border) {
                    auto* heading = new QLabel(tr("Satin de bordure"), body_);
                    QFont hf = heading->font();
                    hf.setBold(true);
                    heading->setFont(hf);
                    form->addRow(heading);
                    auto* bWidth = mmSpin(to_millimeters(p.border->width).value, 20.0, 0.5,
                                          tr("Largeur constante de la colonne le long du contour."));
                    bWidth->setObjectName(QStringLiteral("spin_borderWidth"));
                    auto* bSide = new QComboBox(body_);
                    bSide->setObjectName(QStringLiteral("combo_borderSide"));
                    bSide->addItems(borderSideItems());
                    bSide->setCurrentIndex(static_cast<int>(p.border->side));
                    bSide->setToolTip(tr("Côté du contour où s'étend la colonne."));
                    auto* bCorner = new QComboBox(body_);
                    bCorner->setObjectName(QStringLiteral("combo_borderCorner"));
                    bCorner->addItems(borderCornerItems());
                    bCorner->setCurrentIndex(static_cast<int>(p.border->corner));
                    bCorner->setToolTip(tr("Vifs : jointure en onglet. Arrondis : le bord "
                                           "extérieur décrit un arc dans les coins."));
                    form->addRow(tr("Largeur de la bordure :"), bWidth);
                    form->addRow(tr("Côté :"), bSide);
                    form->addRow(tr("Coins :"), bCorner);
                    const auto emitBorder = [this, id, bWidth, bSide, bCorner] {
                        if (!building_) {
                            emit borderSatinEdited(id, bWidth->value(), bSide->currentIndex(),
                                                   bCorner->currentIndex());
                        }
                    };
                    connect(bWidth, &QDoubleSpinBox::valueChanged, this, emitBorder);
                    connect(bSide, &QComboBox::currentIndexChanged, this, emitBorder);
                    connect(bCorner, &QComboBox::currentIndexChanged, this, emitBorder);
                }
                auto* uMode = makeUnderlayModeCombo(body_, p.underlay_mode);
                form->addRow(tr("Espacement :"), density);
                form->addRow(tr("Compensation de tirage :"), comp);
                form->addRow(tr("Sous-couche :"), uMode);
                form->addRow(QString(), underlay);
                form->addRow(QString(), edgeU);
                form->addRow(QString(), zigU);
                const auto syncUnderlay = [uMode, underlay, edgeU, zigU] {
                    const bool manual = uMode->currentIndex() == 0;
                    underlay->setEnabled(manual);
                    edgeU->setEnabled(manual);
                    zigU->setEnabled(manual);
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode);
                form->addRow(tr("Compensation gauche :"), pullL);
                form->addRow(tr("Compensation droite :"), pullR);
                form->addRow(tr("Points courts dans les virages :"), shortCombo);
                form->addRow(tr("Fractionnement :"), splitCombo);
                form->addRow(tr("Longueur max de point :"), maxLen);
                form->addRow(tr("Largeur max dure :"), maxHard);
                form->addRow(tr("Forme du bout (début) :"), capStart);
                form->addRow(tr("Forme du bout (fin) :"), capEnd);
                form->addRow(tr("Point d'arrêt (début) :"), lockStart);
                form->addRow(tr("Point d'arrêt (fin) :"), lockEnd);
                bindMm(density, tr("Espacement"), &T::density);
                bindMm(comp, tr("Compensation de tirage"), &T::pull_compensation);
                bindBool(underlay, tr("Sous-couche centrale"), &T::center_underlay);
                bindBool(edgeU, tr("Sous-couche de bord"), &T::underlay_edge);
                bindBool(zigU, tr("Sous-couche zigzag"), &T::underlay_zigzag);
                bindMm(pullL, tr("Compensation gauche"), &T::pull_left);
                bindMm(pullR, tr("Compensation droite"), &T::pull_right);
                bindEnumField<T>(this, edit, shortCombo, tr("Points courts"), &T::short_stitch);
                bindEnumField<T>(this, edit, splitCombo, tr("Fractionnement"), &T::split_stitch);
                bindMm(maxLen, tr("Longueur max de point"), &T::max_stitch_length);
                bindMm(maxHard, tr("Largeur max dure"), &T::max_width_hard);
                bindEnumField<T>(this, edit, capStart, tr("Forme du bout (début)"), &T::cap_start);
                bindEnumField<T>(this, edit, capEnd, tr("Forme du bout (fin)"), &T::cap_end);
                bindEnumField<T>(this, edit, lockStart, tr("Point d'arrêt (début)"),
                                 &T::lock_start);
                bindEnumField<T>(this, edit, lockEnd, tr("Point d'arrêt (fin)"), &T::lock_end);
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                // Guides : édités par signaux dédiés (liste ci-dessous, rafraîchie par
                // setAutoSatinState). Les réglages scalaires passent par paramsEdited ;
                // MainWindow reprend alors les guides ACTUELS du document.
                auto* summary = new QLabel(body_);
                summary->setObjectName(QStringLiteral("label_autoSatinSummary"));
                summary->setWordWrap(true);
                satinSummary_ = summary;
                auto* spacing = mmSpin(to_millimeters(p.spacing).value, 2.0, 0.1,
                                       tr("Espacement cible entre deux traversées, mesuré au "
                                          "bord le plus écarté d'un virage."));
                spacing->setObjectName(QStringLiteral("spin_rowSpacing"));
                auto* threshold = mmSpin(to_millimeters(p.split_threshold).value, 20.0, 1.0,
                                         tr("Une traversée plus longue que cette valeur (Lmax) "
                                            "est fractionnée."));
                threshold->setObjectName(QStringLiteral("spin_autoSatinThreshold"));
                auto* splitLen = mmSpin(to_millimeters(p.split_length).value,
                                        to_millimeters(p.split_threshold).value, 0.5,
                                        tr("Longueur maximale des segments après fractionnement "
                                           "(y). Ne peut pas dépasser Lmax."));
                splitLen->setObjectName(QStringLiteral("spin_autoSatinSplitLength"));
                auto* splitCombo = new QComboBox(body_);
                splitCombo->setObjectName(QStringLiteral("combo_autoSatinSplit"));
                splitCombo->addItems(splitItems());
                splitCombo->setCurrentIndex(static_cast<int>(p.split_stitch));
                auto* shortCombo = new QComboBox(body_);
                shortCombo->addItems(shortStitchItems());
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
                auto* capStart = new QComboBox(body_);
                capStart->addItems(capItems());
                capStart->setCurrentIndex(static_cast<int>(p.cap_start));
                auto* capEnd = new QComboBox(body_);
                capEnd->addItems(capItems());
                capEnd->setCurrentIndex(static_cast<int>(p.cap_end));
                auto* lockStart = new QComboBox(body_);
                lockStart->addItems(lockItems());
                lockStart->setCurrentIndex(static_cast<int>(p.lock_start));
                auto* lockEnd = new QComboBox(body_);
                lockEnd->addItems(lockItems());
                lockEnd->setCurrentIndex(static_cast<int>(p.lock_end));

                // --- Guides d'orientation ---
                auto* guideList = new QListWidget(body_);
                guideList->setObjectName(QStringLiteral("list_satinGuides"));
                guideList->setAccessibleName(tr("Guides d'orientation"));
                guideList->setMaximumHeight(110);
                satinGuideList_ = guideList;
                auto* guideAngle = new QDoubleSpinBox(body_);
                guideAngle->setKeyboardTracking(false);
                guideAngle->setObjectName(QStringLiteral("spin_satinGuideAngle"));
                guideAngle->setRange(-179.9, 179.9);
                guideAngle->setDecimals(1);
                guideAngle->setSuffix(tr(" °"));
                guideAngle->setEnabled(false);
                satinGuideAngle_ = guideAngle;
                auto* guideAngleLabel = new QLabel(body_);
                satinGuideAngleLabel_ = guideAngleLabel;
                updateGuideAngleLabel(false);
                auto* guideAbs = new QCheckBox(tr("Angle absolu (repère du dessin)"), body_);
                guideAbs->setObjectName(QStringLiteral("check_satinGuideAbsolute"));
                guideAbs->setToolTip(tr("Décoché : écart à la perpendiculaire de l'axe (0° = "
                                        "perpendiculaire). Coché : angle fixe dans le dessin "
                                        "(0° = horizontal, sens trigonométrique)."));
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
                form->addRow(tr("Fractionnement :"), splitCombo);
                form->addRow(tr("Fractionner au-delà de (Lmax) :"), threshold);
                form->addRow(tr("Longueur max des segments (y ≤ Lmax) :"), splitLen);
                form->addRow(tr("Points courts dans les virages :"), shortCombo);
                section(tr("Compensation"));
                form->addRow(tr("Compensation de tirage :"), comp);
                form->addRow(tr("Compensation gauche :"), pullL);
                form->addRow(tr("Compensation droite :"), pullR);
                section(tr("Sous-couches"));
                auto* uMode = makeUnderlayModeCombo(body_, p.underlay_mode);
                form->addRow(tr("Sous-couche :"), uMode);
                form->addRow(QString(), underlay);
                form->addRow(QString(), edgeU);
                form->addRow(QString(), zigU);
                const auto syncUnderlay = [uMode, underlay, edgeU, zigU] {
                    const bool manual = uMode->currentIndex() == 0;
                    underlay->setEnabled(manual);
                    edgeU->setEnabled(manual);
                    zigU->setEnabled(manual);
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode);
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
                form->addRow(guideAngleLabel, guideAngle);
                form->addRow(QString(), guideAbs);
                form->addRow(QString(), guideRemove);
                form->addRow(QString(), guideAdd);

                // Fractionnement : Lmax et y n'ont de sens que si le mode est actif ; y est
                // borné dynamiquement par Lmax (Mo9).
                const auto syncSplitFields = [form, splitCombo, threshold, splitLen] {
                    const bool on = splitCombo->currentIndex() != 0;
                    for (QWidget* w :
                         {static_cast<QWidget*>(threshold), static_cast<QWidget*>(splitLen)}) {
                        w->setEnabled(on);
                        if (QWidget* label = form->labelForField(w)) {
                            label->setEnabled(on);
                        }
                    }
                };
                syncSplitFields();
                connect(splitCombo, &QComboBox::currentIndexChanged, this,
                        [syncSplitFields](int) { syncSplitFields(); });
                connect(threshold, &QDoubleSpinBox::valueChanged, this, [splitLen](double lmax) {
                    // Abaisser Lmax ramène y dessous : la modification de y qui en résulte
                    // est émise par son propre signal (un pas d'annulation fusionné).
                    splitLen->setMaximum(lmax);
                });

                bindMm(spacing, tr("Espacement"), &T::spacing);
                connect(threshold, &QDoubleSpinBox::valueChanged, this, [edit](double v) {
                    edit(tr("Seuil de fractionnement (Lmax)"), [v](T& t) {
                        t.split_threshold = to_um(v);
                        t.split_length =
                            Micrometers{std::min(t.split_length.value, t.split_threshold.value)};
                    });
                });
                bindMm(splitLen, tr("Longueur des segments (y)"), &T::split_length);
                bindEnumField<T>(this, edit, splitCombo, tr("Fractionnement"), &T::split_stitch);
                bindEnumField<T>(this, edit, shortCombo, tr("Points courts"), &T::short_stitch);
                bindMm(comp, tr("Compensation de tirage"), &T::pull_compensation);
                bindBool(underlay, tr("Sous-couche centrale"), &T::center_underlay);
                bindBool(edgeU, tr("Sous-couche de bord"), &T::underlay_edge);
                bindBool(zigU, tr("Sous-couche zigzag"), &T::underlay_zigzag);
                bindMm(pullL, tr("Compensation gauche"), &T::pull_left);
                bindMm(pullR, tr("Compensation droite"), &T::pull_right);
                bindEnumField<T>(this, edit, capStart, tr("Forme du bout (début)"), &T::cap_start);
                bindEnumField<T>(this, edit, capEnd, tr("Forme du bout (fin)"), &T::cap_end);
                bindEnumField<T>(this, edit, lockStart, tr("Point d'arrêt (début)"),
                                 &T::lock_start);
                bindEnumField<T>(this, edit, lockEnd, tr("Point d'arrêt (fin)"), &T::lock_end);

                connect(guideAdd, &QPushButton::clicked, this,
                        [this, id] { emit editSatinGuidesRequested(id); });
                connect(guideList, &QListWidget::currentRowChanged, this,
                        [this, id, guideList, guideAngle, guideAbs, guideRemove](int row) {
                            const bool has = row >= 0;
                            guideAngle->setEnabled(has);
                            guideAbs->setEnabled(has);
                            guideRemove->setEnabled(has);
                            if (!has) {
                                return;
                            }
                            // Valeurs portées par l'item (cf. setAutoSatinState).
                            {
                                const QSignalBlocker b1(guideAngle);
                                const QSignalBlocker b2(guideAbs);
                                guideAngle->setValue(
                                    guideList->item(row)->data(Qt::UserRole).toDouble());
                                guideAbs->setChecked(
                                    guideList->item(row)->data(Qt::UserRole + 1).toBool());
                            }
                            updateGuideAngleLabel(guideAbs->isChecked());
                            if (!building_) {
                                emit satinGuideSelected(id, row);
                            }
                        });
                const auto emitGuide = [this, id, guideList, guideAngle, guideAbs] {
                    if (building_ || guideList->currentRow() < 0) {
                        return;
                    }
                    updateGuideAngleLabel(guideAbs->isChecked());
                    emit satinGuideChangeRequested(id, guideList->currentRow(), guideAngle->value(),
                                                   guideAbs->isChecked());
                };
                connect(guideAngle, &QDoubleSpinBox::valueChanged, this, emitGuide);
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
    // Molette : ne change une valeur que sur un champ ayant le focus.
    wheelGuard_->guardAll(body_);
}

} // namespace openstitch::desktop
