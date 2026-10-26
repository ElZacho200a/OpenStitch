// SPDX-License-Identifier: Apache-2.0
#include "properties_panel.hpp"

#include "app_theme.hpp"
#include "auto_satin_strategy_ui.hpp"
#include "stitch_type_catalog.hpp"
#include "ui_icons.hpp"
#include "ui_memory.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
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

QStringList cornerItems() {
    return {QObject::tr("Aucun traitement"), QObject::tr("Onglet"), QObject::tr("Chevauchement"),
            QObject::tr("Coupe (saut)")};
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

// Adapte une modification typée (un champ de T) en modification de StitchParams : sans effet si
// les paramètres ne portent pas un T. (Fonction à part : une lambda imbriquée dans la lambda
// générique d'édition met MSVC en échec.)
template <class T>
std::function<void(document::StitchParams&)> toGroupApply(const std::function<void(T&)>& fn) {
    return [fn](document::StitchParams& params) {
        if (auto* target = std::get_if<T>(&params)) {
            fn(*target);
        }
    };
}

template <class T>
using EditFn = std::function<void(const QString&, const std::function<void(T&)>&)>;

// --- Valeurs mixtes (HP-UX-038) ---------------------------------------------------------------
// Un champ de groupe dont les valeurs diffèrent entre les objets s'affiche « — » :
//  - champ numérique : jeton sous le minimum (texte spécial « — ») ; la propriété `kMixedProp`
//    mémorise le vrai minimum et `takeMixed` rejette le jeton, borne les valeurs saisies entre le
//    jeton et le vrai minimum, puis rend le champ normal dès la première vraie valeur ;
//  - case à cocher : état intermédiaire (tristate), supprimé à la première vraie valeur ;
//  - liste : aucun choix courant, texte indicatif « — ».
// Éditer un champ ne touche que ce champ (l'application reste champ par champ).
constexpr const char* kMixedProp = "mixedRealMin";

QString mixedText() {
    return QObject::tr("—");
}

void markMixed(QDoubleSpinBox* spin) {
    const double realMin = spin->minimum();
    spin->setProperty(kMixedProp, realMin);
    spin->setMinimum(realMin - spin->singleStep());
    spin->setSpecialValueText(mixedText());
    spin->setValue(spin->minimum());
    spin->setToolTip(spin->toolTip() + QLatin1Char('\n') +
                     QObject::tr("Valeurs différentes selon les objets (« — »)."));
}
void markMixed(QSpinBox* spin) {
    const int realMin = spin->minimum();
    spin->setProperty(kMixedProp, realMin);
    spin->setMinimum(realMin - spin->singleStep());
    spin->setSpecialValueText(mixedText());
    spin->setValue(spin->minimum());
    spin->setToolTip(spin->toolTip() + QLatin1Char('\n') +
                     QObject::tr("Valeurs différentes selon les objets (« — »)."));
}
void markMixed(QCheckBox* box) {
    box->setTristate(true);
    box->setCheckState(Qt::PartiallyChecked);
    box->setToolTip(box->toolTip() + (box->toolTip().isEmpty() ? QString() : QStringLiteral("\n")) +
                    QObject::tr("Valeurs différentes selon les objets (« — »)."));
}
void markMixed(QComboBox* combo) {
    combo->setPlaceholderText(mixedText());
    combo->setCurrentIndex(-1);
    combo->setToolTip(combo->toolTip() +
                      (combo->toolTip().isEmpty() ? QString() : QStringLiteral("\n")) +
                      QObject::tr("Valeurs différentes selon les objets (« — »)."));
}

// Faux si `v` est le jeton « — » (rien à émettre). Sinon `v` est borné au vrai minimum et le champ
// redevient normal.
bool takeMixed(QDoubleSpinBox* spin, double& v) {
    const QVariant prop = spin->property(kMixedProp);
    if (!prop.isValid()) {
        return true;
    }
    const double realMin = prop.toDouble();
    if (v <= spin->minimum() + 1e-9) {
        return false;
    }
    v = std::max(v, realMin);
    const QSignalBlocker block(spin);
    spin->setProperty(kMixedProp, QVariant());
    spin->setSpecialValueText(QString());
    spin->setMinimum(realMin);
    spin->setValue(v);
    return true;
}
bool takeMixed(QSpinBox* spin, int& v) {
    const QVariant prop = spin->property(kMixedProp);
    if (!prop.isValid()) {
        return true;
    }
    const int realMin = prop.toInt();
    if (v <= spin->minimum()) {
        return false;
    }
    v = std::max(v, realMin);
    const QSignalBlocker block(spin);
    spin->setProperty(kMixedProp, QVariant());
    spin->setSpecialValueText(QString());
    spin->setMinimum(realMin);
    spin->setValue(v);
    return true;
}

// Vrai si `members` (représentant en tête) ne partagent pas la même valeur de `member`.
template <class T, class M> bool differs(const std::vector<T>& members, M T::*member) {
    return members.size() > 1 && std::any_of(members.begin() + 1, members.end(), [&](const T& t) {
               return !(t.*member == members.front().*member);
           });
}
// Variante pour un champ calculé (angle, optionnel, pourcentage…).
template <class T, class F> bool differsBy(const std::vector<T>& members, F&& key) {
    return members.size() > 1 && std::any_of(members.begin() + 1, members.end(), [&](const T& t) {
               return !(key(t) == key(members.front()));
           });
}

// Liaisons widget -> champ : chaque widget ne modifie QUE son champ (cf. showEmbroidery).
// `members` : paramètres de tout le groupe (vide hors groupe) pour afficher « — » si mixte.
template <class T>
void bindMmField(QObject* ctx, const EditFn<T>& edit, QDoubleSpinBox* spin, const QString& label,
                 Micrometers T::*member, const std::vector<T>& members = {}) {
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, ctx,
                     [edit, label, member, spin](double v) {
                         if (!takeMixed(spin, v)) {
                             return;
                         }
                         edit(label, [v, member](T& t) { t.*member = to_um(v); });
                     });
    if (differs(members, member)) {
        markMixed(spin);
    }
}
template <class T>
void bindIntField(QObject* ctx, const EditFn<T>& edit, QSpinBox* spin, const QString& label,
                  int T::*member, const std::vector<T>& members = {}) {
    QObject::connect(spin, &QSpinBox::valueChanged, ctx, [edit, label, member, spin](int v) {
        if (!takeMixed(spin, v)) {
            return;
        }
        edit(label, [v, member](T& t) { t.*member = v; });
    });
    if (differs(members, member)) {
        markMixed(spin);
    }
}
template <class T>
void bindBoolField(QObject* ctx, const EditFn<T>& edit, QCheckBox* box, const QString& label,
                   bool T::*member, const std::vector<T>& members = {}) {
    QObject::connect(box, &QCheckBox::toggled, ctx, [edit, label, member, box](bool v) {
        if (box->isTristate()) {
            return; // « — » en attente d'une première valeur : géré ci-dessous
        }
        edit(label, [v, member](T& t) { t.*member = v; });
    });
    if (differs(members, member)) {
        markMixed(box);
        // Depuis l'état intermédiaire, `toggled` ne part pas (la case reste « cochée » pour Qt) :
        // le premier vrai choix est lu sur l'état, puis la case redevient normale.
        QObject::connect(box, &QCheckBox::checkStateChanged, ctx,
                         [edit, label, member, box](Qt::CheckState state) {
                             if (state == Qt::PartiallyChecked || !box->isTristate()) {
                                 return;
                             }
                             {
                                 const QSignalBlocker block(box);
                                 box->setTristate(false);
                             }
                             const bool v = state == Qt::Checked;
                             edit(label, [v, member](T& t) { t.*member = v; });
                         });
    }
}
template <class T, class E>
void bindEnumField(QObject* ctx, const EditFn<T>& edit, QComboBox* combo, const QString& label,
                   E T::*member, const std::vector<T>& members = {}) {
    QObject::connect(combo, &QComboBox::currentIndexChanged, ctx, [edit, label, member](int index) {
        if (index < 0) {
            return; // « — » : aucune valeur choisie
        }
        edit(label, [index, member](T& t) { t.*member = static_cast<E>(index); });
    });
    if (differs(members, member)) {
        markMixed(combo);
    }
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

// Titre de section discret (petites capitales, couleur secondaire) : le style vient de la feuille
// de style locale du panneau (propriété `propsSection`).
QLabel* makeSectionLabel(const QString& title, QWidget* parent) {
    auto* heading = new QLabel(title.toUpper(), parent);
    heading->setProperty("propsSection", true);
    heading->setContentsMargins(0, 8, 0, 0);
    return heading;
}

// Grille libellé à gauche / contrôle à droite, espacement homogène de 8 px.
void styleForm(QFormLayout* form) {
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // Étiquette au-dessus du champ quand la colonne est étroite (HP-UX-027) : la largeur
    // minimale de l'inspecteur ne dépend plus de ses libellés.
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
}

// Feuille de style LOCALE au panneau, construite depuis les jetons (aucune couleur en dur).
QString panelStyleSheet(const Tokens& t) {
    const auto rgba = [](const QColor& c, int alpha) {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(alpha);
    };
    return QStringLiteral(
               "QLabel[propsSection=\"true\"] { color: %1; font-size: 10px; font-weight: 700; "
               "letter-spacing: 1px; }"
               "QLabel#props_header { color: %2; font-weight: 700; }"
               "QLabel[propsPill=\"true\"] { color: %3; background: %4; border: 1px solid %5; "
               "border-radius: %6px; padding: 1px 8px; font-size: 11px; font-weight: 600; }"
               "QGroupBox { border: 1px solid %7; border-radius: %8px; margin-top: 14px; "
               "padding: %9px; }"
               "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; "
               "color: %1; font-weight: 700; }"
               "QSpinBox, QDoubleSpinBox, QComboBox { min-height: %10px; }"
               "QPushButton { min-height: %10px; }")
        .arg(t.textSecondary.name(), t.text.name(), t.accent.name(), rgba(t.accent, 36),
             rgba(t.accent, 90))
        .arg(t.radiusMd * 3)
        .arg(t.border.name())
        .arg(t.radiusMd)
        .arg(t.space3)
        .arg(t.controlHeight);
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    wheelGuard_ = new WheelGuard(this);
    root_ = new QVBoxLayout(this);
    root_->setContentsMargins(12, 12, 12, 12);
    root_->setSpacing(8);

    // En-tête : pastille de couleur, nom de l'objet, pastille de type.
    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);
    colorDot_ = new QLabel(this);
    colorDot_->setObjectName(QStringLiteral("props_colorDot"));
    colorDot_->setFixedSize(16, 16);
    colorDot_->setVisible(false);
    headerRow->addWidget(colorDot_);
    header_ = new QLabel(tr("Aucune sélection"), this);
    header_->setObjectName(QStringLiteral("props_header"));
    header_->setWordWrap(true);
    headerRow->addWidget(header_, 1);
    typePill_ = new QLabel(this);
    typePill_->setObjectName(QStringLiteral("props_typePill"));
    typePill_->setProperty("propsPill", true);
    typePill_->setVisible(false);
    headerRow->addWidget(typePill_);
    root_->addLayout(headerRow);

    applyTheme();
    connect(&AppTheme::instance(), &AppTheme::changed, this, &PropertiesPanel::applyTheme);

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

    // HP-UX-029 : bandeau non modal, jamais une confirmation bloquante.
    overrideWarningLabel_ = new QLabel(this);
    overrideWarningLabel_->setObjectName(QStringLiteral("label_overridesWarning"));
    overrideWarningLabel_->setWordWrap(true);
    overrideWarningLabel_->setVisible(false);
    root_->addWidget(overrideWarningLabel_);
    reapplyButton_ = new QPushButton(tr("Réappliquer les retouches"), this);
    reapplyButton_->setObjectName(QStringLiteral("button_reapplyOverrides"));
    reapplyButton_->setToolTip(
        tr("Rétablit les paramètres d'avant la modification : les retouches manuelles "
           "redeviennent appliquées (annulable, Ctrl+Z)."));
    reapplyButton_->setVisible(false);
    connect(reapplyButton_, &QPushButton::clicked, this, [this] {
        if (overrideWarningId_) {
            emit reapplyOverridesRequested(*overrideWarningId_);
        }
    });
    root_->addWidget(reapplyButton_);

    // Lettrage : bandeau persistant, masqué hors d'une lettre de texte (cf. setTextInfo).
    textInfoLabel_ = new QLabel(this);
    textInfoLabel_->setObjectName(QStringLiteral("text_info_label"));
    textInfoLabel_->setWordWrap(true);
    textInfoLabel_->setVisible(false);
    root_->addWidget(textInfoLabel_);
    textEditButton_ = new QPushButton(tr("Modifier le texte…"), this);
    textEditButton_->setObjectName(QStringLiteral("text_edit_button"));
    textEditButton_->setToolTip(
        tr("Rouvre le texte : police, hauteur, espacement, alignement, type de point (F2)."));
    textEditButton_->setVisible(false);
    connect(textEditButton_, &QPushButton::clicked, this, &PropertiesPanel::editTextRequested);
    root_->addWidget(textEditButton_);

    layerRow_ = new QWidget(this);
    auto* layersLayout = new QVBoxLayout(layerRow_);
    layersLayout->setContentsMargins(0, 0, 0, 0);
    layersLayout->addWidget(new QLabel(tr("Couches de couture · ordre de couture"), layerRow_));
    fillLayers_ = new QComboBox(layerRow_);
    fillLayers_->setObjectName(QStringLiteral("combo_fillLayer"));
    fillLayers_->setAccessibleName(tr("Couche de remplissage à éditer"));
    fillLayers_->setToolTip(tr("Chaque couche garde sa couleur et ses réglages. "
                               "La liste suit l'ordre de couture, fond puis dessus."));
    layersLayout->addWidget(fillLayers_);
    // Barre de la couche choisie : monter / descendre dans la pile, masquer, renommer.
    auto* toolsRow = new QHBoxLayout;
    toolsRow->setContentsMargins(0, 0, 0, 0);
    const auto makeTool = [&](const QString& text, const QString& name, const QString& tip) {
        auto* b = new QPushButton(text, layerRow_);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        toolsRow->addWidget(b);
        return b;
    };
    layerUp_ = makeTool(tr("▲"), QStringLiteral("button_layerUp"),
                        tr("Monter la couche : cousue plus tard, donc par-dessus."));
    layerDown_ = makeTool(tr("▼"), QStringLiteral("button_layerDown"),
                          tr("Descendre la couche vers le fond : cousue plus tôt."));
    layerVisible_ =
        makeTool(tr("Masquer"), QStringLiteral("button_layerVisible"),
                 tr("Masquer ou afficher la couche (un objet masqué n'est pas cousu)."));
    layerRename_ =
        makeTool(tr("Renommer…"), QStringLiteral("button_layerRename"), tr("Renommer la couche."));
    layersLayout->addLayout(toolsRow);
    const auto currentLayerId = [this]() -> std::optional<ObjectId> {
        const int index = fillLayers_->currentIndex();
        if (index < 0) {
            return std::nullopt;
        }
        return ObjectId{fillLayers_->itemData(index).toULongLong()};
    };
    connect(layerUp_, &QPushButton::clicked, this, [this, currentLayerId] {
        if (const auto id = currentLayerId()) {
            emit layerMoveRequested(*id, +1);
        }
    });
    connect(layerDown_, &QPushButton::clicked, this, [this, currentLayerId] {
        if (const auto id = currentLayerId()) {
            emit layerMoveRequested(*id, -1);
        }
    });
    connect(layerVisible_, &QPushButton::clicked, this, [this, currentLayerId] {
        if (const auto id = currentLayerId()) {
            const bool nowVisible = fillLayers_->currentData(Qt::UserRole + 1).toBool();
            emit layerVisibilityRequested(*id, !nowVisible);
        }
    });
    connect(layerRename_, &QPushButton::clicked, this, [this, currentLayerId] {
        if (const auto id = currentLayerId()) {
            emit layerRenameRequested(*id);
        }
    });
    connect(fillLayers_, &QComboBox::currentIndexChanged, this, [this](int index) {
        updateLayerTools();
        if (index >= 0) {
            emit fillLayerSelected(ObjectId{fillLayers_->itemData(index).toULongLong()});
        }
    });
    root_->addWidget(layerRow_);
    layerRow_->hide();
    fillColor_ = new QPushButton(tr("Couleur de cette couche…"), this);
    fillColor_->setObjectName(QStringLiteral("button_fillColor"));
    connect(fillColor_, &QPushButton::clicked, this, [this] {
        if (currentId_) {
            emit fillColorRequested(*currentId_);
        }
    });
    root_->addWidget(fillColor_);
    fillColor_->hide();
    blendSingle_ = new QToolButton(this);
    blendSingle_->setObjectName(QStringLiteral("button_createColorBlend"));
    blendSingle_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    blendZones_ = new QToolButton(this);
    blendZones_->setObjectName(QStringLiteral("button_blendSelectedZones"));
    blendZones_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    root_->addWidget(blendSingle_);
    root_->addWidget(blendZones_);
    blendSingle_->hide();
    blendZones_->hide();

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

void PropertiesPanel::applyTheme() {
    setStyleSheet(panelStyleSheet(AppTheme::instance().tokens()));
}

void PropertiesPanel::setFillLayers(const QList<FillLayer>& layers,
                                    std::optional<ObjectId> selected) {
    const QSignalBlocker block(fillLayers_);
    bool same = fillLayers_->count() == layers.size();
    for (qsizetype i = 0; same && i < layers.size(); ++i) {
        same = fillLayers_->itemData(static_cast<int>(i)).toULongLong() == layers[i].id.value &&
               fillLayers_->itemText(static_cast<int>(i)) == layers[i].name &&
               fillLayers_->itemData(static_cast<int>(i), Qt::DecorationRole).value<QColor>() ==
                   layers[i].color &&
               fillLayers_->itemData(static_cast<int>(i), Qt::UserRole + 1).toBool() ==
                   layers[i].visible;
    }
    if (!same) {
        fillLayers_->clear();
        for (const auto& layer : layers) {
            fillLayers_->addItem(layer.name, QVariant::fromValue<qulonglong>(layer.id.value));
            fillLayers_->setItemData(fillLayers_->count() - 1, layer.color, Qt::DecorationRole);
            fillLayers_->setItemData(fillLayers_->count() - 1, layer.visible, Qt::UserRole + 1);
        }
    }
    if (selected) {
        fillLayers_->setCurrentIndex(
            fillLayers_->findData(QVariant::fromValue<qulonglong>(selected->value)));
    }
    layerRow_->setVisible(layers.size() > 1);
    fillColor_->setVisible(!layers.empty());
    updateLayerTools();
}

void PropertiesPanel::updateLayerTools() {
    const int index = fillLayers_->currentIndex();
    layerUp_->setEnabled(index >= 0 && index + 1 < fillLayers_->count());
    layerDown_->setEnabled(index > 0);
    layerVisible_->setEnabled(index >= 0);
    layerRename_->setEnabled(index >= 0);
    const bool visible = index < 0 || fillLayers_->currentData(Qt::UserRole + 1).toBool();
    layerVisible_->setText(visible ? tr("Masquer") : tr("Afficher"));
}

void PropertiesPanel::setBlendActions(QAction* single, QAction* zones, bool singleContext,
                                      bool zonesContext) {
    blendSingle_->setDefaultAction(single);
    blendZones_->setDefaultAction(zones);
    blendSingle_->setVisible(singleContext);
    blendZones_->setVisible(zonesContext);
}

void PropertiesPanel::setTextInfo(const QString& summary) {
    textInfoLabel_->setText(summary);
    textInfoLabel_->setVisible(!summary.isEmpty());
    textEditButton_->setVisible(!summary.isEmpty());
}

bool PropertiesPanel::textInfoVisible() const {
    return !textInfoLabel_->isHidden();
}

void PropertiesPanel::setOverrideWarning(std::optional<ObjectId> id, int count) {
    overrideWarningId_ = (id && count > 0) ? id : std::nullopt;
    const bool show = overrideWarningId_.has_value();
    overrideWarningLabel_->setVisible(show);
    reapplyButton_->setVisible(show);
    if (show) {
        overrideWarningLabel_->setText(
            count == 1 ? tr("Cette modification désactive 1 retouche (Ctrl+Z pour annuler).")
                       : tr("Cette modification désactive %1 retouches (Ctrl+Z pour annuler).")
                             .arg(count));
    }
}

bool PropertiesPanel::overrideWarningVisible() const {
    return !overrideWarningLabel_->isHidden();
}

void PropertiesPanel::addStitchTypeRow(int current) {
    // HP-UX-028 : les mêmes quatre boutons pour une forme neuve, une couture ou N formes.
    auto* row = new QWidget(body_);
    row->setObjectName(QStringLiteral("row_stitchType"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin"), tr("Directionnel"),
                             tr("Blanket")};
    for (int t = 0; t < 5; ++t) {
        auto* button = new QToolButton(row);
        button->setObjectName(QStringLiteral("button_stitchType%1").arg(t));
        button->setText(names[t]);
        button->setAccessibleName(tr("Type de points : %1").arg(names[t]));
        button->setToolTip(tr("Donne le type de points « %1 » à la sélection, au dernier "
                              "réglage (annulable en un pas).")
                               .arg(names[t]));
        button->setCheckable(true);
        button->setChecked(t == current);
        connect(button, &QToolButton::clicked, this, [this, t, button, current] {
            button->setChecked(t == current); // l'état suit le document, pas le clic
            emit stitchTypeRequested(t);
        });
        layout->addWidget(button);
    }
    // Types sans bouton direct (motifs, concentrique, radial, spirale, croisé) : menu « Plus ».
    {
        auto* more = new QToolButton(row);
        more->setObjectName(QStringLiteral("button_stitchTypeMore"));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setCheckable(true);
        more->setChecked(current >= 5);
        more->setText(current >= 5 ? stitchTypeLabel(current) + QStringLiteral(" ▾")
                                   : tr("Plus ▾"));
        more->setAccessibleName(tr("Autres types de points"));
        more->setToolTip(tr("Autres types de points : motifs, remplissages concentrique, radial, "
                            "spirale et croisé."));
        auto* moreMenu = new QMenu(more);
        for (int t = 5; t < kStitchTypeCount; ++t) {
            auto* act = moreMenu->addAction(stitchTypeLabel(t));
            act->setCheckable(true);
            act->setChecked(t == current);
            connect(act, &QAction::triggered, this, [this, t] { emit stitchTypeRequested(t); });
        }
        more->setMenu(moreMenu);
        layout->addWidget(more);
    }
    layout->addStretch(1);
    body_->layout()->addWidget(row);
}

void PropertiesPanel::clearBody() {
    currentId_.reset();
    vectorId_.reset();
    colorDot_->setVisible(false);
    typePill_->setVisible(false);
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
    // HP-VIEW-012 : par défaut l'aperçu suit la frappe (la commande se fusionne en un seul pas
    // d'annulation et la régénération est débouncée) ; décochable dans Affichage.
    spin->setKeyboardTracking(ui_memory::boolValue(QStringLiteral("view/liveTyping"), true));
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
    addStitchTypeRow(-1); // HP-UX-028 : forme sans couture, mêmes quatre types que partout
    if (boxMm.width() <= 0.0 || boxMm.height() <= 0.0) {
        return; // forme dégénérée : pas de redimensionnement possible
    }
    auto* form = new QFormLayout();
    styleForm(form);
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
    styleForm(borderForm);
    borderForm->addRow(makeSectionLabel(tr("Satin de bordure"), body_));
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

void PropertiesPanel::setConnectorMode(std::optional<ObjectId> id, document::ConnectorMode mode) {
    if (!id || !currentId_ || *id != *currentId_ || connectorCombo_ == nullptr) {
        return;
    }
    const QSignalBlocker block(connectorCombo_);
    connectorCombo_->setCurrentIndex(static_cast<int>(mode));
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

void PropertiesPanel::showMultiSelection(int objectCount, int embroideryCount,
                                         const document::EmbroideryObject* groupRep, int groupCount,
                                         const std::vector<document::StitchParams>& groupMembers) {
    clearBody();
    groupCount_ = 1;
    groupMembers_ = groupMembers;
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

    // HP-UX-028/038 : mêmes quatre boutons que pour une forme seule ; un seul modèle d'édition
    // (en direct, une commande composite par édition) -- plus de bouton « Appliquer à N ».
    int commonType = -1;
    if (groupRep != nullptr && groupCount >= 2) {
        commonType = stitchTypeIndexOf(*groupRep);
    }
    addStitchTypeRow(commonType);

    if (groupRep != nullptr && groupCount >= 2) {
        body_->layout()->addWidget(makeSectionLabel(
            tr("Réglages communs (%1 couture(s) de même type)").arg(groupCount), body_));
        groupCount_ = groupCount;
        buildEmbroideryForm(*groupRep);
    } else {
        // Types différents (ou sans couture) : seuls l'espacement et l'angle ont un sens commun.
        // Champs indéterminés (« — ») ; éditer l'un l'applique à tous, champ par champ.
        auto* form = new QFormLayout();
        styleForm(form);
        auto* spacing =
            mmSpin(0.4, 5.0, 0.1, tr("Écart entre rangées (tatami, directionnel, satin)."));
        spacing->setObjectName(QStringLiteral("spin_multiSpacing"));
        auto* angle = new QDoubleSpinBox(body_);
        angle->setObjectName(QStringLiteral("spin_multiAngle"));
        angle->setKeyboardTracking(false);
        angle->setRange(0.0, 179.9);
        angle->setDecimals(1);
        angle->setWrapping(true);
        angle->setSuffix(tr(" °"));
        angle->setToolTip(tr("0° = horizontal, sens trigonométrique. Plage : 0 – 179,9°."));
        markMixed(spacing);
        markMixed(angle);
        form->addRow(tr("Espacement des rangées :"), spacing);
        form->addRow(tr("Angle (tatami) :"), angle);
        connect(spacing, &QDoubleSpinBox::valueChanged, this, [this, spacing](double v) {
            if (takeMixed(spacing, v)) {
                emit applyToSelectionRequested(-1, true, v, false, 0.0);
            }
        });
        connect(angle, &QDoubleSpinBox::valueChanged, this, [this, angle](double v) {
            if (takeMixed(angle, v)) {
                emit applyToSelectionRequested(-1, false, 0.0, true, v);
            }
        });
        auto* holder = new QWidget(body_);
        holder->setLayout(form);
        body_->layout()->addWidget(holder);
    }
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
                                   stitch_generation::ObjectEditState state, int overrideCount) {
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
               "retouches pour ré-éditer sur la forme actuelle.") +
            (overrideCount > 0 ? tr("\n%1 retouche(s) désactivée(s).").arg(overrideCount)
                               : QString()));
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
    groupCount_ = 1;
    groupMembers_.clear();
    header_->setText(QString::fromStdString(object.name));
    buildEmbroideryForm(object);
}

void PropertiesPanel::buildEmbroideryForm(const document::EmbroideryObject& object) {
    currentId_ = object.id;
    shown_ = object.params;
    hasShown_ = true;
    const ObjectId id = object.id;
    const bool group = groupCount_ > 1;
    if (!group) {
        // HP-UX-028 : les quatre types en tête de l'inspecteur (en groupe : posés par
        // showMultiSelection).
        addStitchTypeRow(stitchTypeIndexOf(object));
    }

    auto* form = new QFormLayout();
    styleForm(form);
    // HP-UX-038 : en groupe, les contrôles à portée INDIVIDUELLE (ils n'agissent que sur l'objet
    // actif, jamais sur toute la sélection) sont isolés sous « Objet actif seulement ».
    QFormLayout* activeForm = nullptr;
    const auto scoped = [&]() -> QFormLayout* {
        if (!group) {
            return form;
        }
        if (activeForm == nullptr) {
            activeForm = new QFormLayout();
            styleForm(activeForm);
        }
        return activeForm;
    };

    const QString typeName = object.is_tatami()            ? tr("Remplissage tatami")
                             : object.is_directional()     ? tr("Remplissage directionnel")
                             : object.is_blanket()         ? tr("Blanket / zigzag")
                             : object.is_contour_fill()    ? tr("Remplissage concentrique")
                             : object.is_motif_run()       ? tr("Motif le long du contour")
                             : object.is_motif_fill()      ? tr("Remplissage à motif")
                             : object.is_radial_fill()     ? tr("Remplissage radial")
                             : object.is_spiral_fill()     ? tr("Remplissage en spirale")
                             : object.is_crosshatch_fill() ? tr("Remplissage croisé")
                             : object.is_auto_satin()
                                 ? tr("Satin automatique (traversées orientées)")
                             : object.is_satin() ? tr("Colonne satin à rails (manuelle)")
                                                 : tr("Contour cousu");
    form->addRow(tr("Type :"), new QLabel(typeName, body_));
    if (!group) {
        // En-tête : pastille de couleur de l'objet et pastille de type.
        colorDot_->setPixmap(
            icons::colorSwatch(QColor(object.rgb[0], object.rgb[1], object.rgb[2]), 16)
                .pixmap(16, 16));
        colorDot_->setVisible(true);
        typePill_->setText(typeName);
        typePill_->setVisible(true);
    }

    // HP-OBJ-018 : pastille de couleur. Un clic déclenche l'action « Couleur de la broderie… »
    // de MainWindow (une seule source d'état et de grisage), qui vise toute la sélection.
    auto* colorButton = new QPushButton(body_);
    colorButton->setObjectName(QStringLiteral("button_embroideryColor"));
    colorButton->setIcon(
        icons::colorSwatch(QColor(object.rgb[0], object.rgb[1], object.rgb[2]), 16));
    colorButton->setText(QStringLiteral("#%1%2%3")
                             .arg(object.rgb[0], 2, 16, QLatin1Char('0'))
                             .arg(object.rgb[1], 2, 16, QLatin1Char('0'))
                             .arg(object.rgb[2], 2, 16, QLatin1Char('0')));
    colorButton->setToolTip(
        tr("Couleur de la broderie : cliquez pour choisir une couleur libre (appliquée à toute la "
           "sélection). Pour un fil de nuancier, utilisez le panneau Fils."));
    form->addRow(tr("Couleur :"), colorButton);
    connect(colorButton, &QPushButton::clicked, this,
            [this] { emit regionActionRequested(QStringLiteral("action_embroideryColor")); });

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
    scoped()->addRow(tr("Entrée/sortie :"), joinCombo);
    connect(joinCombo, &QComboBox::currentIndexChanged, this, [this, id](int index) {
        if (!building_) {
            emit joinModeEdited(id, index);
        }
    });

    // HP-SEQ-005 : connecteur après l'objet (vers le suivant de même fil), par objet.
    auto* connectorCombo = new QComboBox(body_);
    connectorCombo->setObjectName(QStringLiteral("combo_connectorMode"));
    connectorCombo->addItems(
        {tr("Réglage du projet"), tr("Toujours couper"), tr("Ne jamais couper")});
    connectorCombo->setCurrentIndex(static_cast<int>(object.connector));
    connectorCombo->setToolTip(
        tr("Ce qui suit cet objet jusqu'au suivant de même fil. « Toujours couper » : point "
           "d'arrêt et coupe, jamais remplacés par un pont caché. « Ne jamais couper » : ni "
           "coupe ni point d'arrêt, le fil reste tendu jusqu'à l'objet suivant ; vérifiez qu'il "
           "sera caché. Sans effet avant un changement de fil."));
    connectorCombo_ = connectorCombo;
    scoped()->addRow(tr("Connecteur suivant :"), connectorCombo);
    connect(connectorCombo, &QComboBox::currentIndexChanged, this, [this, id](int index) {
        if (!building_) {
            emit connectorModeEdited(id, index);
        }
    });

    building_ = true;
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;

            // HP-UX-038 : paramètres de TOUT le groupe (représentant en tête) pour repérer les
            // champs dont les valeurs diffèrent (affichés « — »).
            std::vector<T> members;
            if (group) {
                for (const auto& sp : groupMembers_) {
                    if (const auto* t = std::get_if<T>(&sp)) {
                        members.push_back(*t);
                    }
                }
            }

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
                    if (groupCount_ > 1) {
                        // Groupe : seul ce champ est appliqué, à chaque objet du groupe (et à la
                        // copie des valeurs de chaque membre, pour l'affichage « — »).
                        for (auto& member : groupMembers_) {
                            if (auto* tm = std::get_if<T>(&member)) {
                                fn(*tm);
                            }
                        }
                        emit groupParamsEdited(label, toGroupApply<T>(fn));
                    } else {
                        emit paramsEdited(id, shown_, label);
                    }
                }
            };
            [[maybe_unused]] const auto bindMm = [this, &edit, &members](QDoubleSpinBox* spin,
                                                                         const QString& label,
                                                                         Micrometers T::*member) {
                bindMmField<T>(this, edit, spin, label, member, members);
            };
            [[maybe_unused]] const auto bindInt =
                [this, &edit, &members](QSpinBox* spin, const QString& label, int T::*member) {
                    bindIntField<T>(this, edit, spin, label, member, members);
                };
            [[maybe_unused]] const auto bindBool =
                [this, &edit, &members](QCheckBox* box, const QString& label, bool T::*member) {
                    bindBoolField<T>(this, edit, box, label, member, members);
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
                auto* runStyle = new QComboBox(body_);
                runStyle->setObjectName(QStringLiteral("combo_runningStyle"));
                runStyle->addItems(
                    {tr("Point droit"), tr("Point bean"), tr("Point arrière"), tr("Point tige")});
                runStyle->setCurrentIndex(static_cast<int>(p.style));
                runStyle->setToolTip(tr("Droit : passages ci-dessous. Bean : trois passages par "
                                        "intervalle. Arrière : chaque point revient en arrière. "
                                        "Tige : pénétrations décalées latéralement."));
                auto* stemOff = mmSpin(to_millimeters(p.stem_offset).value, 3.0, 0.1,
                                       tr("Décalage latéral du point tige."));
                stemOff->setObjectName(QStringLiteral("spin_stemOffset"));
                form->addRow(tr("Style :"), runStyle);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Passages :"), rep);
                form->addRow(tr("Décalage du point tige :"), stemOff);
                const auto syncRunStyle = [form, runStyle, rep, stemOff] {
                    const int s = runStyle->currentIndex();
                    setRowEnabled(form, rep, s == 0);
                    setRowEnabled(form, stemOff, s == 3);
                };
                syncRunStyle();
                connect(runStyle, &QComboBox::currentIndexChanged, this,
                        [syncRunStyle](int) { syncRunStyle(); });
                bindEnumField<T>(this, edit, runStyle, tr("Style de point"), &T::style, members);
                bindMm(stemOff, tr("Décalage du point tige"), &T::stem_offset);
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
                auto* uZig = new QCheckBox(tr("Sous-couche en zigzag"), body_);
                uZig->setObjectName(QStringLiteral("check_underlayZigzag"));
                uZig->setToolTip(tr("Rangées diagonales à 45° des rangées de la couche "
                                    "supérieure, croisées avec la sous-couche perpendiculaire : "
                                    "stabilise un grand aplat."));
                uZig->setChecked(p.underlay_zigzag);
                // HP-ENG-001 : compensation du tirage (allongement des rangées dans l'axe du fil).
                auto* pull =
                    mmSpin(to_millimeters(p.pull_compensation).value, 3.0, 0.0,
                           tr("Les rangées dépassent du contour de cette longueur, dans l'axe "
                              "du fil : le fil tire dans sa direction et la forme cousue "
                              "rétrécit. 0,2 à 0,4 mm est courant ; 0 = aucune."));
                pull->setObjectName(QStringLiteral("spin_tatamiPull"));
                // Bords effilochés : écrêtage des bouts de rangée et passe de contour.
                auto* rowClip =
                    mmSpin(to_millimeters(p.row_end_clip).value, 5.0, 0.0,
                           tr("Rabat les pointes : une fin de rangée qui dépasse de plus que "
                              "cette longueur les rangées voisines est raccourcie (bords "
                              "effilochés sur un bord presque parallèle aux rangées). 1 mm est "
                              "courant ; 0 = aucun."));
                rowClip->setObjectName(QStringLiteral("spin_tatamiRowEndClip"));
                auto* outline = new QCheckBox(tr("Contour cousu par-dessus"), body_);
                outline->setObjectName(QStringLiteral("check_tatamiEdgeOutline"));
                outline->setToolTip(tr("Coud un point droit le long du bord (et des trous) après "
                                       "le remplissage, pour masquer les bouts de rangées."));
                outline->setChecked(p.edge_outline);
                auto* uMode = makeUnderlayModeCombo(body_, p.underlay_mode);
                form->addRow(tr("Espacement des rangées :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Angle (orientation) :"), angle);
                form->addRow(tr("Retrait de bord :"), inset);
                form->addRow(tr("Compensation du tirage :"), pull);
                form->addRow(tr("Écrêtage des bouts :"), rowClip);
                form->addRow(QString(), outline);
                form->addRow(tr("Décalage (stagger) :"), stagger);
                form->addRow(tr("Sous-couche :"), uMode);
                form->addRow(QString(), uEdge);
                form->addRow(tr("Retrait de la sous-couche :"), uInset);
                form->addRow(QString(), uPar);
                form->addRow(tr("Espacement de la sous-couche :"), uSpacing);
                form->addRow(QString(), uZig);
                form->addRow(QString(), underpath);
                dependOn(uEdge, uInset);
                dependOn(uPar, uSpacing);
                // Mode automatique : les réglages manuels sont ignorés, donc grisés.
                const auto syncUnderlay = [form, uMode, uEdge, uInset, uPar, uSpacing, uZig] {
                    const bool manual = uMode->currentIndex() == 0;
                    uEdge->setEnabled(manual);
                    uPar->setEnabled(manual);
                    uZig->setEnabled(manual);
                    setRowEnabled(form, uInset, manual && uEdge->isChecked());
                    setRowEnabled(form, uSpacing, manual && uPar->isChecked());
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode, members);
                bindMm(pull, tr("Compensation du tirage"), &T::pull_compensation);
                bindBool(uZig, tr("Sous-couche en zigzag"), &T::underlay_zigzag);
                bindMm(rowClip, tr("Écrêtage des bouts de rangée"), &T::row_end_clip);
                bindBool(outline, tr("Contour cousu par-dessus"), &T::edge_outline);
                bindMm(spacing, tr("Espacement des rangées"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                connect(angle, &QDoubleSpinBox::valueChanged, this, [edit, angle](double v) {
                    if (!takeMixed(angle, v)) {
                        return;
                    }
                    edit(tr("Angle"), [v](T& t) { t.angle = from_deg(v); });
                });
                if (differsBy(members, [](const T& t) {
                        return std::lround(
                            std::fmod(std::fmod(to_deg(t.angle), 180.0) + 180.0, 180.0) * 10.0);
                    })) {
                    markMixed(angle);
                }
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
                if (group) {
                    delete toDirectional; // conversion de type : bloc « Type de points »
                } else {
                    form->addRow(QString(), toDirectional);
                    connect(toDirectional, &QPushButton::clicked, this,
                            [this, id] { emit convertToDirectionalRequested(id); });
                    auto* toContour =
                        new QPushButton(tr("Convertir en remplissage concentrique"), body_);
                    toContour->setObjectName(QStringLiteral("button_convertToContourFill"));
                    toContour->setToolTip(tr("Remplace les rangées par des anneaux concentriques "
                                             "qui suivent le bord (echo). Annulable (Ctrl+Z)."));
                    form->addRow(QString(), toContour);
                    connect(toContour, &QPushButton::clicked, this,
                            [this, id] { emit convertToContourFillRequested(id); });
                    // HP-STI-008 / 009 / 014 : radial, spirale, croisé (kind 0 / 1 / 2).
                    const struct {
                        const char* name;
                        QString label;
                        QString tip;
                    } extra[] = {
                        {"button_convertToRadialFill", tr("Convertir en remplissage radial"),
                         tr("Rayons depuis un centre, densité constante à la périphérie. "
                            "Annulable (Ctrl+Z).")},
                        {"button_convertToSpiralFill", tr("Convertir en remplissage en spirale"),
                         tr("Spirale continue depuis un centre. Annulable (Ctrl+Z).")},
                        {"button_convertToCrosshatchFill", tr("Convertir en remplissage croisé"),
                         tr("Deux ou trois tatamis espacés à angles différents. "
                            "Annulable (Ctrl+Z).")}};
                    for (int kind = 0; kind < 3; ++kind) {
                        auto* b = new QPushButton(extra[kind].label, body_);
                        b->setObjectName(QString::fromLatin1(extra[kind].name));
                        b->setToolTip(extra[kind].tip);
                        form->addRow(QString(), b);
                        connect(b, &QPushButton::clicked, this,
                                [this, id, kind] { emit convertToFillKindRequested(id, kind); });
                    }
                }
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
                auto* spacing =
                    mmSpin(to_millimeters(p.density_gradient ? p.density_gradient->spacing_from
                                                             : p.row_spacing)
                               .value,
                           5.0, 0.1, tr("Écart entre deux lignes de couture (densité)."));
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
                auto* outline = new QCheckBox(tr("Contour cousu par-dessus"), body_);
                outline->setObjectName(QStringLiteral("check_directionalEdgeOutline"));
                outline->setToolTip(tr("Coud un point droit le long du bord (et des trous) après "
                                       "le remplissage, pour masquer les bouts de lignes "
                                       "(bords effilochés)."));
                outline->setChecked(p.edge_outline);
                // HP-ENG-001 : prolonge chaque ligne de couture à ses deux bouts.
                auto* dirPull =
                    mmSpin(to_millimeters(p.pull_compensation).value, 3.0, 0.0,
                           tr("Chaque ligne de couture dépasse du contour de cette longueur, dans "
                              "sa direction de bout : le fil tire dans sa direction et la forme "
                              "cousue rétrécit. 0,2 à 0,4 mm est courant ; 0 = aucune."));
                dirPull->setObjectName(QStringLiteral("spin_directionalPull"));
                auto* dirZig = new QCheckBox(tr("Sous-couche en zigzag"), body_);
                dirZig->setObjectName(QStringLiteral("check_directionalUnderlayZigzag"));
                dirZig->setChecked(p.underlay_zigzag);
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
                // Dégradé de densité : l'axe dépend de la forme, donc c'est
                // MainWindow qui le calcule (demande `densityGradientRequested`).
                auto* gradOn = new QCheckBox(tr("Dégradé de densité"), body_);
                gradOn->setObjectName(QStringLiteral("check_densityGradient"));
                gradOn->setChecked(p.density_gradient.has_value());
                gradOn->setToolTip(
                    tr("L'écart entre lignes varie le long d'un axe, de « Espacement "
                       "des lignes » au début à « Écart de fin » à l'autre bout."));
                const double startMm =
                    to_millimeters(p.density_gradient ? p.density_gradient->spacing_from
                                                      : p.row_spacing)
                        .value;
                auto* gradEnd = mmSpin(
                    p.density_gradient ? to_millimeters(p.density_gradient->spacing_to).value
                                       : 2.0 * startMm,
                    4.0, 0.1, tr("Écart entre lignes à la fin de l'axe. Plage : 0,1 – 4 mm."));
                gradEnd->setObjectName(QStringLiteral("spin_gradientEndSpacing"));
                auto* gradAngle = new QDoubleSpinBox(body_);
                gradAngle->setObjectName(QStringLiteral("spin_gradientAngle"));
                gradAngle->setKeyboardTracking(false);
                gradAngle->setRange(0.0, 359.9);
                gradAngle->setDecimals(1);
                gradAngle->setWrapping(true);
                gradAngle->setSuffix(tr(" °"));
                gradAngle->setToolTip(
                    tr("Direction de l'axe, du côté dense vers le côté clairsemé : "
                       "0° = vers la droite, 90° = vers le haut."));
                double axisDeg = 90.0;
                if (p.density_gradient) {
                    const auto& g = *p.density_gradient;
                    axisDeg = std::atan2(static_cast<double>(g.to.y.value - g.from.y.value),
                                         static_cast<double>(g.to.x.value - g.from.x.value)) *
                              180.0 / std::numbers::pi;
                    axisDeg = std::fmod(axisDeg + 360.0, 360.0);
                }
                gradAngle->setValue(axisDeg);
                gradEnd->setEnabled(p.density_gradient.has_value());
                gradAngle->setEnabled(p.density_gradient.has_value());
                if (group) {
                    delete summary;
                    delete editGuides;
                    auto* autoAll = new QPushButton(tr("Guides automatiques pour tous"), body_);
                    autoAll->setObjectName(QStringLiteral("button_groupAutoDirectionGuides"));
                    autoAll->setToolTip(tr("Ajoute à chaque forme un guide calculé d'après son axe "
                                           "(les remplissages tatami du groupe ne sont pas "
                                           "concernés : convertissez-les d'abord)."));
                    connect(autoAll, &QPushButton::clicked, this, [this] {
                        emit groupActionRequested(QStringLiteral("autoDirectionGuides"));
                    });
                    auto* clearAll =
                        new QPushButton(tr("Retirer guides et ruptures de tous"), body_);
                    clearAll->setObjectName(QStringLiteral("button_groupClearDirectionGuides"));
                    connect(clearAll, &QPushButton::clicked, this, [this] {
                        emit groupActionRequested(QStringLiteral("clearDirectionGuides"));
                    });
                    form->addRow(QString(), autoAll);
                    form->addRow(QString(), clearAll);
                } else {
                    form->addRow(QString(), summary);
                    form->addRow(QString(), editGuides);
                }
                form->addRow(p.density_gradient ? tr("Écart au début du dégradé :")
                                                : tr("Espacement des lignes :"),
                             spacing);
                scoped()->addRow(QString(), gradOn);
                scoped()->addRow(tr("Écart de fin :"), gradEnd);
                scoped()->addRow(tr("Angle du dégradé :"), gradAngle);
                auto* regular = new QSpinBox(body_);
                regular->setObjectName(QStringLiteral("spin_spacingRegularity"));
                regular->setKeyboardTracking(false);
                regular->setRange(0, 100);
                regular->setSuffix(tr(" %"));
                regular->setValue(static_cast<int>(std::lround(p.spacing_regularity * 100.0)));
                regular->setToolTip(tr("Égalise l'écart entre lignes voisines (utile quand les "
                                       "directions convergent) en s'autorisant à dévier un peu "
                                       "de la direction. 0 % = tracé brut ; autour de 50 % : "
                                       "compromis. Calcul plus long."));
                form->addRow(tr("Régularité de l'espacement :"), regular);
                connect(regular, &QSpinBox::valueChanged, this, [edit, regular](int v) {
                    if (!takeMixed(regular, v)) {
                        return;
                    }
                    edit(tr("Régularité de l'espacement"),
                         [v](T& t) { t.spacing_regularity = v / 100.0; });
                });
                if (differsBy(members, [](const T& t) {
                        return std::lround(t.spacing_regularity * 100.0);
                    })) {
                    markMixed(regular);
                }
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
                form->addRow(QString(), dirZig);
                form->addRow(QString(), underpath);
                form->addRow(QString(), outline);
                form->addRow(tr("Compensation du tirage :"), dirPull);
                form->addRow(QString(), handmade);
                form->addRow(tr("Intensité :"), intensity);
                form->addRow(QString(), reseed);
                dependOn(uEdge, uInset);
                dependOn(uPar, uSpacing);
                const auto syncUnderlay = [form, uMode, uEdge, uInset, uPar, uSpacing, dirZig] {
                    const bool manual = uMode->currentIndex() == 0;
                    uEdge->setEnabled(manual);
                    uPar->setEnabled(manual);
                    dirZig->setEnabled(manual);
                    setRowEnabled(form, uInset, manual && uEdge->isChecked());
                    setRowEnabled(form, uSpacing, manual && uPar->isChecked());
                };
                syncUnderlay();
                connect(uMode, &QComboBox::currentIndexChanged, this,
                        [syncUnderlay](int) { syncUnderlay(); });
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode, members);
                // Le début du dégradé suit l'espacement de référence.
                connect(spacing, &QDoubleSpinBox::valueChanged, this, [edit, spacing](double v) {
                    if (!takeMixed(spacing, v)) {
                        return;
                    }
                    edit(tr("Espacement des lignes"), [v](T& t) {
                        t.row_spacing = to_um(v);
                        if (t.density_gradient) {
                            t.density_gradient->spacing_from = t.row_spacing;
                        }
                    });
                });
                if (differsBy(members, [](const T& t) {
                        return (t.density_gradient ? t.density_gradient->spacing_from
                                                   : t.row_spacing)
                            .value;
                    })) {
                    markMixed(spacing);
                }
                const auto requestGradient = [this, id, gradOn, gradEnd, gradAngle] {
                    if (!building_) {
                        emit densityGradientRequested(id, gradOn->isChecked(), gradAngle->value(),
                                                      gradEnd->value());
                    }
                };
                connect(gradOn, &QCheckBox::toggled, this,
                        [gradEnd, gradAngle, requestGradient](bool on) {
                            gradEnd->setEnabled(on);
                            gradAngle->setEnabled(on);
                            requestGradient();
                        });
                connect(gradEnd, &QDoubleSpinBox::valueChanged, this,
                        [requestGradient](double) { requestGradient(); });
                connect(gradAngle, &QDoubleSpinBox::valueChanged, this,
                        [requestGradient](double) { requestGradient(); });
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                connect(edge, &QSpinBox::valueChanged, this, [edit, edge](int v) {
                    if (!takeMixed(edge, v)) {
                        return;
                    }
                    edit(tr("Influence des bords"), [v](T& t) { t.edge_weight = v / 100.0; });
                });
                if (differsBy(members,
                              [](const T& t) { return std::lround(t.edge_weight * 100.0); })) {
                    markMixed(edge);
                }
                bindMm(inset, tr("Retrait de bord"), &T::inset);
                bindInt(stagger, tr("Décalage"), &T::stagger);
                bindMm(overlap, tr("Chevauchement des secteurs"), &T::sector_overlap);
                bindBool(uEdge, tr("Sous-couche de contour"), &T::underlay_edge);
                bindMm(uInset, tr("Retrait de la sous-couche"), &T::underlay_inset);
                bindBool(uPar, tr("Sous-couche perpendiculaire"), &T::underlay_parallel);
                bindMm(uSpacing, tr("Espacement de la sous-couche"), &T::underlay_spacing);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
                bindBool(outline, tr("Contour cousu par-dessus"), &T::edge_outline);
                bindMm(dirPull, tr("Compensation du tirage"), &T::pull_compensation);
                bindBool(dirZig, tr("Sous-couche en zigzag"), &T::underlay_zigzag);
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
                    scoped()->addRow(makeSectionLabel(tr("Satin de bordure"), body_));
                    auto* bWidth =
                        mmSpin(to_millimeters(p.border->width).value, 20.0, 0.5,
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
                    scoped()->addRow(tr("Largeur de la bordure :"), bWidth);
                    scoped()->addRow(tr("Côté :"), bSide);
                    scoped()->addRow(tr("Coins :"), bCorner);
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
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode, members);
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
                auto* autoSpacing =
                    new QCheckBox(tr("Espacement automatique selon la largeur"), body_);
                autoSpacing->setObjectName(QStringLiteral("chk_satinAutoSpacing"));
                autoSpacing->setChecked(p.auto_spacing);
                autoSpacing->setToolTip(tr("Colonnes étroites plus lâches, larges plus serrées."));
                auto* corner = new QComboBox(body_);
                corner->setObjectName(QStringLiteral("combo_satinCorner"));
                corner->addItems(cornerItems());
                corner->setCurrentIndex(static_cast<int>(p.corner_mode));
                corner->setToolTip(tr("Traitement des coins vifs de la colonne."));
                form->addRow(QString(), autoSpacing);
                form->addRow(tr("Coins vifs :"), corner);
                bindBool(autoSpacing, tr("Espacement automatique"), &T::auto_spacing);
                bindEnumField<T>(this, edit, corner, tr("Coins vifs"), &T::corner_mode, members);
                bindMm(density, tr("Espacement"), &T::density);
                bindMm(comp, tr("Compensation de tirage"), &T::pull_compensation);
                bindBool(underlay, tr("Sous-couche centrale"), &T::center_underlay);
                bindBool(edgeU, tr("Sous-couche de bord"), &T::underlay_edge);
                bindBool(zigU, tr("Sous-couche zigzag"), &T::underlay_zigzag);
                bindMm(pullL, tr("Compensation gauche"), &T::pull_left);
                bindMm(pullR, tr("Compensation droite"), &T::pull_right);
                bindEnumField<T>(this, edit, shortCombo, tr("Points courts"), &T::short_stitch,
                                 members);
                bindEnumField<T>(this, edit, splitCombo, tr("Fractionnement"), &T::split_stitch,
                                 members);
                bindMm(maxLen, tr("Longueur max de point"), &T::max_stitch_length);
                bindMm(maxHard, tr("Largeur max dure"), &T::max_width_hard);
                bindEnumField<T>(this, edit, capStart, tr("Forme du bout (début)"), &T::cap_start,
                                 members);
                bindEnumField<T>(this, edit, capEnd, tr("Forme du bout (fin)"), &T::cap_end,
                                 members);
                bindEnumField<T>(this, edit, lockStart, tr("Point d'arrêt (début)"), &T::lock_start,
                                 members);
                bindEnumField<T>(this, edit, lockEnd, tr("Point d'arrêt (fin)"), &T::lock_end,
                                 members);
            } else if constexpr (std::is_same_v<T, document::BlanketStitchParams>) {
                // HP-STI-003 : blanket stitch / zigzag le long du contour.
                auto* style = new QComboBox(body_);
                style->setObjectName(QStringLiteral("combo_blanketStyle"));
                style->addItems({tr("Blanket (dents perpendiculaires)"), tr("Zigzag")});
                style->setCurrentIndex(static_cast<int>(p.style));
                auto* side = new QComboBox(body_);
                side->setObjectName(QStringLiteral("combo_blanketSide"));
                side->addItems({tr("Gauche du tracé"), tr("Droite du tracé"), tr("Centré")});
                side->setCurrentIndex(static_cast<int>(p.side));
                side->setToolTip(tr("Côté de la bande par rapport au sens du chemin."));
                auto* width = mmSpin(to_millimeters(p.width).value, 20.0, 0.2,
                                     tr("Largeur de la bande (longueur des dents). 0,2 – 20 mm."));
                width->setObjectName(QStringLiteral("spin_blanketWidth"));
                auto* spacing = mmSpin(to_millimeters(p.spacing).value, 20.0, 0.3,
                                       tr("Écart entre deux dents/points le long du chemin. "
                                          "0,3 – 20 mm."));
                spacing->setObjectName(QStringLiteral("spin_blanketSpacing"));
                form->addRow(tr("Style :"), style);
                form->addRow(tr("Côté :"), side);
                form->addRow(tr("Largeur :"), width);
                form->addRow(tr("Espacement :"), spacing);
                bindEnumField<T>(this, edit, style, tr("Style blanket"), &T::style, members);
                bindEnumField<T>(this, edit, side, tr("Côté du blanket"), &T::side, members);
                bindMm(width, tr("Largeur du blanket"), &T::width);
                bindMm(spacing, tr("Espacement du blanket"), &T::spacing);
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                // Guides : édités par signaux dédiés (liste ci-dessous, rafraîchie par
                // setAutoSatinState). Les réglages scalaires passent par paramsEdited ;
                // MainWindow reprend alors les guides ACTUELS du document.
                auto* summary = new QLabel(body_);
                summary->setObjectName(QStringLiteral("label_autoSatinSummary"));
                summary->setWordWrap(true);
                if (!group) {
                    satinSummary_ = summary;
                }
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

                // --- Méthode de calcul des traversées (choix de l'utilisateur) ---
                auto* method = new QComboBox(body_);
                method->setObjectName(QStringLiteral("combo_autoSatinStrategy"));
                method->setAccessibleName(tr("Méthode de satin"));
                method->addItems(autoSatinStrategyItems());
                for (std::size_t i = 0; i < kAutoSatinStrategies.size(); ++i) {
                    method->setItemData(static_cast<int>(i),
                                        autoSatinStrategyTip(kAutoSatinStrategies[i]),
                                        Qt::ToolTipRole);
                }
                method->setCurrentIndex(static_cast<int>(p.strategy));
                method->setToolTip(autoSatinStrategyTip(p.strategy));
                auto* sweepAuto = new QCheckBox(tr("Angle des fils automatique"), body_);
                sweepAuto->setObjectName(QStringLiteral("check_autoSatinSweepAuto"));
                sweepAuto->setChecked(!p.sweep_angle.has_value());
                auto* sweepAngle = new QDoubleSpinBox(body_);
                sweepAngle->setObjectName(QStringLiteral("spin_autoSatinSweepAngle"));
                sweepAngle->setKeyboardTracking(false);
                sweepAngle->setRange(-179.9, 179.9);
                sweepAngle->setDecimals(1);
                sweepAngle->setSuffix(tr(" °"));
                sweepAngle->setValue(p.sweep_angle ? to_deg(*p.sweep_angle) : 0.0);
                sweepAngle->setToolTip(tr("Direction des fils (0° = horizontal, sens "
                                          "trigonométrique), valable pour toute la forme."));
                auto* methodNote = new QLabel(
                    tr("Sans guide, cet angle s'applique à toute la forme. Les guides ci-dessous "
                       "changent l'orientation des fils à leur endroit (angle interpolé entre "
                       "guides)."),
                    body_);
                methodNote->setObjectName(QStringLiteral("label_autoSatinMethodNote"));
                methodNote->setWordWrap(true);
                QPushButton* compareMethods = nullptr;
                if (!group) {
                    compareMethods = new QPushButton(tr("Comparer les méthodes…"), body_);
                    compareMethods->setObjectName(QStringLiteral("button_autoSatinCompare"));
                    compareMethods->setToolTip(
                        tr("Calcule toutes les méthodes sur cette forme et affiche leurs "
                           "mesures (couverture, fil en double, croisements) ; vous choisissez."));
                }

                // --- Guides d'orientation ---
                auto* guideList = new QListWidget(body_);
                guideList->setObjectName(QStringLiteral("list_satinGuides"));
                guideList->setAccessibleName(tr("Guides d'orientation"));
                guideList->setMaximumHeight(110);
                if (!group) {
                    satinGuideList_ = guideList;
                }
                auto* guideAngle = new QDoubleSpinBox(body_);
                guideAngle->setKeyboardTracking(false);
                guideAngle->setObjectName(QStringLiteral("spin_satinGuideAngle"));
                guideAngle->setRange(-179.9, 179.9);
                guideAngle->setDecimals(1);
                guideAngle->setSuffix(tr(" °"));
                guideAngle->setEnabled(false);
                auto* guideAngleLabel = new QLabel(body_);
                if (!group) {
                    satinGuideAngle_ = guideAngle;
                    satinGuideAngleLabel_ = guideAngleLabel;
                    updateGuideAngleLabel(false);
                }
                auto* guideAbs = new QCheckBox(tr("Angle absolu (repère du dessin)"), body_);
                guideAbs->setObjectName(QStringLiteral("check_satinGuideAbsolute"));
                guideAbs->setToolTip(tr("Décoché : écart à la perpendiculaire de l'axe (0° = "
                                        "perpendiculaire). Coché : angle fixe dans le dessin "
                                        "(0° = horizontal, sens trigonométrique)."));
                guideAbs->setEnabled(false);
                if (!group) {
                    satinGuideAbsolute_ = guideAbs;
                }
                auto* guideRemove = new QPushButton(tr("Supprimer le guide"), body_);
                guideRemove->setObjectName(QStringLiteral("button_satinGuideRemove"));
                guideRemove->setEnabled(false);
                if (!group) {
                    satinGuideRemove_ = guideRemove;
                }
                auto* guideAdd = new QPushButton(tr("Placer un guide sur le canevas…"), body_);
                guideAdd->setObjectName(QStringLiteral("button_editSatinGuides"));
                guideAdd->setToolTip(tr("Tracez un trait : sa position ancre le guide et sa "
                                        "direction fixe l'angle des fils (D)."));

                // Intertitres : 20 champs à plat sont illisibles ; on les regroupe par sujet.
                const auto section = [this, form](const QString& title) {
                    form->addRow(makeSectionLabel(title, body_));
                };
                if (!group) {
                    form->addRow(QString(), summary);
                } else {
                    delete summary;
                }
                section(tr("Méthode"));
                form->addRow(tr("Méthode :"), method);
                form->addRow(QString(), sweepAuto);
                form->addRow(tr("Angle des fils :"), sweepAngle);
                form->addRow(QString(), methodNote);
                if (compareMethods != nullptr) {
                    form->addRow(QString(), compareMethods);
                }
                const auto syncMethod = [form, method, sweepAuto, sweepAngle, methodNote] {
                    const bool sweep = method->currentIndex() ==
                                       static_cast<int>(document::AutoSatinStrategy::ParallelSweep);
                    sweepAuto->setEnabled(sweep);
                    setRowEnabled(form, sweepAngle, sweep && !sweepAuto->isChecked());
                    methodNote->setVisible(sweep);
                    method->setToolTip(
                        autoSatinStrategyTip(kAutoSatinStrategies[static_cast<std::size_t>(
                            std::clamp(method->currentIndex(), 0,
                                       static_cast<int>(kAutoSatinStrategies.size()) - 1))]));
                };
                syncMethod();
                connect(method, &QComboBox::currentIndexChanged, this,
                        [syncMethod](int) { syncMethod(); });
                connect(sweepAuto, &QCheckBox::toggled, this, [syncMethod](bool) { syncMethod(); });
                bindEnumField<T>(this, edit, method, tr("Méthode de satin"), &T::strategy, members);
                connect(sweepAuto, &QCheckBox::toggled, this, [edit, sweepAngle](bool on) {
                    const double deg = sweepAngle->value();
                    edit(tr("Angle des fils"), [on, deg](T& t) {
                        if (on) {
                            t.sweep_angle.reset();
                        } else {
                            t.sweep_angle = from_deg(deg);
                        }
                    });
                });
                connect(sweepAngle, &QDoubleSpinBox::valueChanged, this,
                        [edit, sweepAngle](double v) {
                            if (!takeMixed(sweepAngle, v)) {
                                return;
                            }
                            edit(tr("Angle des fils"), [v](T& t) { t.sweep_angle = from_deg(v); });
                        });
                if (differsBy(members, [](const T& t) { return t.split_threshold.value; })) {
                    markMixed(threshold);
                }
                if (differsBy(members, [](const T& t) { return t.sweep_angle.has_value(); })) {
                    markMixed(sweepAuto);
                }
                if (differsBy(members, [](const T& t) {
                        return t.sweep_angle ? std::lround(to_deg(*t.sweep_angle) * 10.0)
                                             : std::numeric_limits<long>::min();
                    })) {
                    markMixed(sweepAngle);
                }
                if (compareMethods != nullptr) {
                    connect(compareMethods, &QPushButton::clicked, this,
                            [this, id] { emit compareSatinMethodsRequested(id); });
                }
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
                bindEnumField<T>(this, edit, uMode, tr("Sous-couche"), &T::underlay_mode, members);
                section(tr("Extrémités"));
                form->addRow(tr("Forme du bout (début) :"), capStart);
                form->addRow(tr("Forme du bout (fin) :"), capEnd);
                form->addRow(tr("Point d'arrêt (début) :"), lockStart);
                form->addRow(tr("Point d'arrêt (fin) :"), lockEnd);
                auto* autoSpacing =
                    new QCheckBox(tr("Espacement automatique selon la largeur"), body_);
                autoSpacing->setObjectName(QStringLiteral("chk_autoSatinAutoSpacing"));
                autoSpacing->setChecked(p.auto_spacing);
                autoSpacing->setToolTip(tr("Colonnes étroites plus lâches, larges plus serrées."));
                auto* corner = new QComboBox(body_);
                corner->setObjectName(QStringLiteral("combo_autoSatinCorner"));
                corner->addItems(cornerItems());
                corner->setCurrentIndex(static_cast<int>(p.corner_mode));
                corner->setToolTip(tr("Traitement des coins vifs de la colonne."));
                form->addRow(QString(), autoSpacing);
                form->addRow(tr("Coins vifs :"), corner);
                bindBool(autoSpacing, tr("Espacement automatique"), &T::auto_spacing);
                bindEnumField<T>(this, edit, corner, tr("Coins vifs"), &T::corner_mode, members);
                section(tr("Orientation des fils"));
                if (group) {
                    // Guides en lot : un même angle pour toutes les formes du groupe.
                    delete guideList;
                    delete guideAbs;
                    delete guideRemove;
                    delete guideAdd;
                    delete guideAngleLabel;
                    guideAngle->setEnabled(true);
                    guideAngle->setValue(0.0);
                    auto* absolute = new QCheckBox(tr("Angle absolu (repère du dessin)"), body_);
                    absolute->setObjectName(QStringLiteral("check_groupGuideAbsolute"));
                    absolute->setToolTip(tr("Décoché : écart à la perpendiculaire de l'axe de "
                                            "chaque forme (0° = perpendiculaire). Coché : angle "
                                            "fixe dans le dessin (0° = horizontal)."));
                    guideAngle->setObjectName(QStringLiteral("spin_groupGuideAngle"));
                    auto* apply =
                        new QPushButton(tr("Poser ce guide sur toutes les formes"), body_);
                    apply->setObjectName(QStringLiteral("button_groupApplyGuide"));
                    apply->setToolTip(tr("Remplace les guides de chaque forme par un guide unique "
                                         "à cet angle, ancré au centre de la forme."));
                    auto* clear = new QPushButton(tr("Retirer les guides de toutes"), body_);
                    clear->setObjectName(QStringLiteral("button_groupClearSatinGuides"));
                    form->addRow(tr("Angle du guide (° ) :"), guideAngle);
                    form->addRow(QString(), absolute);
                    form->addRow(QString(), apply);
                    form->addRow(QString(), clear);
                    connect(apply, &QPushButton::clicked, this, [this, guideAngle, absolute] {
                        emit groupGuideAngleRequested(guideAngle->value(), absolute->isChecked());
                    });
                    connect(clear, &QPushButton::clicked, this, [this] {
                        emit groupActionRequested(QStringLiteral("clearSatinGuides"));
                    });
                } else if (p.guides.empty()) {
                    auto* none = new QLabel(tr("Aucun guide : les fils sont perpendiculaires à "
                                               "l'axe de la forme. Placez un guide pour les "
                                               "orienter autrement."),
                                            body_);
                    none->setWordWrap(true);
                    none->setEnabled(false);
                    form->addRow(none);
                }
                if (!group) {
                    form->addRow(tr("Guides d'orientation :"), guideList);
                    form->addRow(guideAngleLabel, guideAngle);
                    form->addRow(QString(), guideAbs);
                    form->addRow(QString(), guideRemove);
                    form->addRow(QString(), guideAdd);
                }

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
                connect(threshold, &QDoubleSpinBox::valueChanged, this,
                        [splitLen, threshold](double lmax) {
                            if (threshold->property(kMixedProp).isValid() &&
                                lmax <= threshold->minimum() + 1e-9) {
                                return; // « — » : ne borne pas y
                            }
                            // Abaisser Lmax ramène y dessous : la modification de y qui en résulte
                            // est émise par son propre signal (un pas d'annulation fusionné).
                            splitLen->setMaximum(lmax);
                        });

                bindMm(spacing, tr("Espacement"), &T::spacing);
                connect(threshold, &QDoubleSpinBox::valueChanged, this,
                        [edit, threshold](double v) {
                            if (!takeMixed(threshold, v)) {
                                return;
                            }
                            edit(tr("Seuil de fractionnement (Lmax)"), [v](T& t) {
                                t.split_threshold = to_um(v);
                                t.split_length = Micrometers{
                                    std::min(t.split_length.value, t.split_threshold.value)};
                            });
                        });
                bindMm(splitLen, tr("Longueur des segments (y)"), &T::split_length);
                bindEnumField<T>(this, edit, splitCombo, tr("Fractionnement"), &T::split_stitch,
                                 members);
                bindEnumField<T>(this, edit, shortCombo, tr("Points courts"), &T::short_stitch,
                                 members);
                bindMm(comp, tr("Compensation de tirage"), &T::pull_compensation);
                bindBool(underlay, tr("Sous-couche centrale"), &T::center_underlay);
                bindBool(edgeU, tr("Sous-couche de bord"), &T::underlay_edge);
                bindBool(zigU, tr("Sous-couche zigzag"), &T::underlay_zigzag);
                bindMm(pullL, tr("Compensation gauche"), &T::pull_left);
                bindMm(pullR, tr("Compensation droite"), &T::pull_right);
                bindEnumField<T>(this, edit, capStart, tr("Forme du bout (début)"), &T::cap_start,
                                 members);
                bindEnumField<T>(this, edit, capEnd, tr("Forme du bout (fin)"), &T::cap_end,
                                 members);
                bindEnumField<T>(this, edit, lockStart, tr("Point d'arrêt (début)"), &T::lock_start,
                                 members);
                bindEnumField<T>(this, edit, lockEnd, tr("Point d'arrêt (fin)"), &T::lock_end,
                                 members);

                if (!group) {
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
                        emit satinGuideChangeRequested(id, guideList->currentRow(),
                                                       guideAngle->value(), guideAbs->isChecked());
                    };
                    connect(guideAngle, &QDoubleSpinBox::valueChanged, this, emitGuide);
                    connect(guideAbs, &QCheckBox::toggled, this, emitGuide);
                    connect(guideRemove, &QPushButton::clicked, this, [this, id, guideList] {
                        if (guideList->currentRow() >= 0) {
                            emit satinGuideRemoveRequested(id, guideList->currentRow());
                        }
                    });
                }
                // Premier remplissage de la liste et du résumé (formulaire d'un seul objet).
                if (!group) {
                    setAutoSatinState(id, &p, QString());
                }
            } else if constexpr (std::is_same_v<T, document::ContourFillParams>) {
                // HP-STI-007 : remplissage concentrique ; tout passe par `paramsEdited`.
                auto* spacing =
                    mmSpin(to_millimeters(p.row_spacing).value, 10.0, 0.1,
                           tr("Écart entre deux anneaux (densité). Plage : 0,1 – 10 mm."));
                spacing->setObjectName(QStringLiteral("spin_contourSpacing"));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0, 1.0,
                                   tr("Longueur MAXIMALE d'un point, bornée entre 1 et 7 mm."));
                len->setObjectName(QStringLiteral("spin_contourStitchLength"));
                auto* minl = mmSpin(to_millimeters(p.min_length).value, 7.0, 0.0,
                                    tr("Longueur minimale d'un point (hors coins)."));
                minl->setObjectName(QStringLiteral("spin_contourMinLength"));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                     tr("Retrait du premier anneau par rapport au bord."));
                inset->setObjectName(QStringLiteral("spin_contourInset"));
                auto* spiral = new QCheckBox(tr("Liaison en spirale"), body_);
                spiral->setObjectName(QStringLiteral("check_contourSpiral"));
                spiral->setToolTip(tr("Chaque tour glisse vers l'anneau suivant, sans saut. "
                                      "Décochée : anneaux fermés reliés par un court pas."));
                spiral->setChecked(p.spiral);
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setObjectName(QStringLiteral("check_contourUnderpath"));
                underpath->setChecked(p.hidden_underpath);
                form->addRow(tr("Espacement des anneaux :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Retrait du bord :"), inset);
                form->addRow(QString(), spiral);
                form->addRow(QString(), underpath);
                bindMm(spacing, tr("Espacement des anneaux"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                bindMm(minl, tr("Longueur minimale"), &T::min_length);
                bindMm(inset, tr("Retrait du bord"), &T::inset);
                bindBool(spiral, tr("Liaison en spirale"), &T::spiral);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
            } else if constexpr (std::is_same_v<T, document::MotifRunParams> ||
                                 std::is_same_v<T, document::MotifFillParams>) {
                // HP-STI-002 / HP-STI-006 : motif répété le long du contour ou pavant la région.
                const bool isFill = std::is_same_v<T, document::MotifFillParams>;
                const QString pfx =
                    isFill ? QStringLiteral("motifFill") : QStringLiteral("motifRun");
                auto* motif = new QComboBox(body_);
                motif->setObjectName(QStringLiteral("combo_%1Motif").arg(pfx));
                motif->addItems({tr("Vague"), tr("Zigzag"), tr("Feuille"), tr("Cœur"),
                                 tr("Losange"), tr("Feston")});
                motif->setCurrentIndex(static_cast<int>(p.motif));
                auto* size = mmSpin(to_millimeters(p.size).value, 40.0, 0.5,
                                    tr("Hauteur du motif (carré en remplissage). 0,5 – 40 mm."));
                size->setObjectName(QStringLiteral("spin_%1Size").arg(pfx));
                auto* spacing = mmSpin(to_millimeters(p.spacing).value, 100.0, 1.0,
                                       isFill ? tr("Pas de la grille entre centres de motifs. "
                                                   "1 – 100 mm.")
                                              : tr("Longueur d'une répétition le long du tracé. "
                                                   "1 – 100 mm."));
                spacing->setObjectName(QStringLiteral("spin_%1Spacing").arg(pfx));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0, 1.0,
                                   tr("Longueur MAXIMALE d'un point, bornée entre 1 et 7 mm."));
                len->setObjectName(QStringLiteral("spin_%1StitchLength").arg(pfx));
                auto* minl = mmSpin(to_millimeters(p.min_length).value, 7.0, 0.0,
                                    tr("Longueur minimale d'un point."));
                minl->setObjectName(QStringLiteral("spin_%1MinLength").arg(pfx));
                form->addRow(tr("Motif :"), motif);
                form->addRow(tr("Taille :"), size);
                form->addRow(isFill ? tr("Pas de la grille :") : tr("Répétition :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                bindEnumField<T>(this, edit, motif, tr("Motif"), &T::motif, members);
                bindMm(size, tr("Taille du motif"), &T::size);
                bindMm(spacing, tr("Pas du motif"), &T::spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                bindMm(minl, tr("Longueur minimale"), &T::min_length);
                if constexpr (std::is_same_v<T, document::MotifRunParams>) {
                    auto* alt = new QCheckBox(tr("Alterner (miroir un sur deux)"), body_);
                    alt->setObjectName(QStringLiteral("check_motifRunAlternate"));
                    alt->setChecked(p.alternate);
                    form->addRow(QString(), alt);
                    bindBool(alt, tr("Alternance du motif"), &T::alternate);
                } else {
                    auto* angle = new QDoubleSpinBox(body_);
                    angle->setObjectName(QStringLiteral("spin_motifFillAngle"));
                    angle->setKeyboardTracking(false);
                    angle->setRange(0.0, 179.9);
                    angle->setDecimals(1);
                    angle->setWrapping(true);
                    angle->setSuffix(tr(" °"));
                    angle->setToolTip(tr("Orientation de la grille : 0° = horizontal."));
                    angle->setValue(std::fmod(std::fmod(to_deg(p.angle), 180.0) + 180.0, 180.0));
                    auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                         tr("Retrait des motifs par rapport au bord."));
                    inset->setObjectName(QStringLiteral("spin_motifFillInset"));
                    auto* stagger = new QCheckBox(tr("Rangées décalées"), body_);
                    stagger->setObjectName(QStringLiteral("check_motifFillStagger"));
                    stagger->setChecked(p.stagger);
                    auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                    underpath->setObjectName(QStringLiteral("check_motifFillUnderpath"));
                    underpath->setChecked(p.hidden_underpath);
                    form->addRow(tr("Angle :"), angle);
                    form->addRow(tr("Retrait du bord :"), inset);
                    form->addRow(QString(), stagger);
                    form->addRow(QString(), underpath);
                    connect(angle, &QDoubleSpinBox::valueChanged, this, [edit](double v) {
                        edit(tr("Angle du motif"), [v](T& t) { t.angle = from_deg(v); });
                    });
                    bindMm(inset, tr("Retrait du bord"), &T::inset);
                    bindBool(stagger, tr("Rangées décalées"), &T::stagger);
                    bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
                }
            } else if constexpr (std::is_same_v<T, document::RadialFillParams> ||
                                 std::is_same_v<T, document::SpiralFillParams>) {
                // HP-STI-008 / 009 : radial et spirale partagent densité, longueurs et centre.
                constexpr bool kSpiral = std::is_same_v<T, document::SpiralFillParams>;
                const QString pfx = kSpiral ? QStringLiteral("spiral") : QStringLiteral("radial");
                auto* spacing = mmSpin(to_millimeters(p.row_spacing).value, 10.0, 0.1,
                                       kSpiral ? tr("Écart entre deux tours. Plage : 0,1 – 10 mm.")
                                               : tr("Écart d'arc entre rayons à la périphérie. "
                                                    "Plage : 0,1 – 10 mm."));
                spacing->setObjectName(QStringLiteral("spin_%1Spacing").arg(pfx));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0, 1.0,
                                   tr("Longueur MAXIMALE d'un point, bornée entre 1 et 7 mm."));
                len->setObjectName(QStringLiteral("spin_%1StitchLength").arg(pfx));
                auto* minl = mmSpin(to_millimeters(p.min_length).value, 7.0, 0.0,
                                    tr("Longueur minimale d'un point."));
                minl->setObjectName(QStringLiteral("spin_%1MinLength").arg(pfx));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                     tr("Retrait par rapport au bord."));
                inset->setObjectName(QStringLiteral("spin_%1Inset").arg(pfx));
                auto* autoCenter = new QCheckBox(tr("Centre automatique"), body_);
                autoCenter->setObjectName(QStringLiteral("check_%1AutoCenter").arg(pfx));
                autoCenter->setToolTip(
                    tr("Décochée : le centre est fixé aux coordonnées ci-dessous "
                       "(repère du document, en mm)."));
                autoCenter->setChecked(!p.center.has_value());
                const Vec2um c0 = p.center.value_or(Vec2um{});
                auto* cx = mmSpin(to_millimeters(c0.x).value, 5000.0, -5000.0,
                                  tr("Abscisse du centre (mm)."));
                cx->setObjectName(QStringLiteral("spin_%1CenterX").arg(pfx));
                auto* cy = mmSpin(to_millimeters(c0.y).value, 5000.0, -5000.0,
                                  tr("Ordonnée du centre (mm), Y vers le haut."));
                cy->setObjectName(QStringLiteral("spin_%1CenterY").arg(pfx));
                cx->setEnabled(p.center.has_value());
                cy->setEnabled(p.center.has_value());
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setObjectName(QStringLiteral("check_%1Underpath").arg(pfx));
                underpath->setChecked(p.hidden_underpath);
                form->addRow(kSpiral ? tr("Écart des tours :") : tr("Écart des rayons :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Retrait du bord :"), inset);
                form->addRow(QString(), autoCenter);
                form->addRow(tr("Centre X :"), cx);
                form->addRow(tr("Centre Y :"), cy);
                bindMm(spacing, tr("Espacement"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                bindMm(minl, tr("Longueur minimale"), &T::min_length);
                bindMm(inset, tr("Retrait du bord"), &T::inset);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
                // Centre : un seul gestionnaire ; l'état des cases/valeurs est relu à chaque fois.
                const auto editCenter = [edit, autoCenter, cx, cy, this] {
                    const bool useAuto = autoCenter->isChecked();
                    const Micrometers x = to_um(cx->value());
                    const Micrometers y = to_um(cy->value());
                    cx->setEnabled(!useAuto);
                    cy->setEnabled(!useAuto);
                    edit(tr("Centre"), [useAuto, x, y](T& t) {
                        if (useAuto) {
                            t.center.reset();
                        } else {
                            t.center = Vec2um{x, y};
                        }
                    });
                };
                connect(autoCenter, &QCheckBox::toggled, this, editCenter);
                connect(cx, &QDoubleSpinBox::valueChanged, this, editCenter);
                connect(cy, &QDoubleSpinBox::valueChanged, this, editCenter);
                if constexpr (kSpiral) {
                    auto* cw = new QCheckBox(tr("Sens horaire"), body_);
                    cw->setObjectName(QStringLiteral("check_spiralClockwise"));
                    cw->setChecked(p.clockwise);
                    form->addRow(QString(), cw);
                    bindBool(cw, tr("Sens de la spirale"), &T::clockwise);
                }
                form->addRow(QString(), underpath);
            } else if constexpr (std::is_same_v<T, document::CrosshatchFillParams>) {
                // HP-STI-014 : 2 ou 3 tatamis espacés à angles répartis sur 180°.
                auto* spacing =
                    mmSpin(to_millimeters(p.row_spacing).value, 10.0, 0.3,
                           tr("Écart entre rangées d'UNE couche. Plage : 0,3 – 10 mm."));
                spacing->setObjectName(QStringLiteral("spin_crosshatchSpacing"));
                auto* len = mmSpin(to_millimeters(p.stitch_length).value, 7.0, 1.0,
                                   tr("Longueur de point le long d'une rangée (1 – 7 mm)."));
                len->setObjectName(QStringLiteral("spin_crosshatchStitchLength"));
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0, 0.0,
                                     tr("Retrait par rapport au bord."));
                inset->setObjectName(QStringLiteral("spin_crosshatchInset"));
                auto* layers = new QSpinBox(body_);
                layers->setObjectName(QStringLiteral("spin_crosshatchLayers"));
                layers->setKeyboardTracking(false);
                layers->setRange(2, 3);
                layers->setValue(p.layers);
                layers->setToolTip(tr("2 = rangées perpendiculaires, 3 = trois directions à 60°."));
                auto* angle = new QDoubleSpinBox(body_);
                angle->setObjectName(QStringLiteral("spin_crosshatchAngle"));
                angle->setKeyboardTracking(false);
                angle->setRange(0.0, 179.9);
                angle->setDecimals(1);
                angle->setWrapping(true);
                angle->setSuffix(tr(" °"));
                angle->setToolTip(tr("Orientation de la première couche : 0° = horizontal, sens "
                                     "trigonométrique."));
                angle->setValue(std::fmod(std::fmod(to_deg(p.angle), 180.0) + 180.0, 180.0));
                auto* underpath = new QCheckBox(tr("Liaisons cousues cachées"), body_);
                underpath->setObjectName(QStringLiteral("check_crosshatchUnderpath"));
                underpath->setChecked(p.hidden_underpath);
                form->addRow(tr("Écart des rangées :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Couches :"), layers);
                form->addRow(tr("Angle :"), angle);
                form->addRow(tr("Retrait du bord :"), inset);
                form->addRow(QString(), underpath);
                bindMm(spacing, tr("Écart des rangées"), &T::row_spacing);
                bindMm(len, tr("Longueur de point"), &T::stitch_length);
                bindMm(inset, tr("Retrait du bord"), &T::inset);
                bindInt(layers, tr("Couches"), &T::layers);
                bindBool(underpath, tr("Liaisons cousues cachées"), &T::hidden_underpath);
                connect(angle, &QDoubleSpinBox::valueChanged, this, [edit, angle](double v) {
                    if (!takeMixed(angle, v)) {
                        return;
                    }
                    edit(tr("Angle"), [v](T& t) { t.angle = from_deg(v); });
                });
            }
        },
        object.params);
    building_ = false;

    auto* holder = new QWidget(body_);
    holder->setLayout(form);
    body_->layout()->addWidget(holder);
    if (activeForm != nullptr) {
        auto* title = makeSectionLabel(tr("Objet actif seulement"), body_);
        title->setToolTip(tr("Ces réglages n'agissent que sur « %1 » (le premier objet du groupe), "
                             "pas sur toute la sélection.")
                              .arg(QString::fromStdString(object.name)));
        body_->layout()->addWidget(title);
        auto* activeHolder = new QWidget(body_);
        activeHolder->setObjectName(QStringLiteral("group_activeObjectOnly"));
        activeHolder->setLayout(activeForm);
        body_->layout()->addWidget(activeHolder);
    }
    // Molette : ne change une valeur que sur un champ ayant le focus.
    wheelGuard_->guardAll(body_);
}

} // namespace openstitch::desktop
