// SPDX-License-Identifier: Apache-2.0
#include "properties_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>
#include <variant>

namespace openstitch::desktop {

namespace {

Micrometers to_um(double mm) {
    return to_micrometers(Millimeters{mm});
}

}  // namespace

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
    if (auto* lay = body_->layout()) {
        while (QLayoutItem* item = lay->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }
}

QDoubleSpinBox* PropertiesPanel::mmSpin(double valueMm, double maxMm) {
    auto* spin = new QDoubleSpinBox(body_);
    spin->setRange(0.0, maxMm);
    spin->setDecimals(2);
    spin->setSingleStep(0.1);
    spin->setSuffix(tr(" mm"));
    spin->setValue(valueMm);
    return spin;
}

void PropertiesPanel::showInfo(const QString& title, const QString& details) {
    clearBody();
    header_->setText(title);
    auto* label = new QLabel(details, body_);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->layout()->addWidget(label);
}

void PropertiesPanel::showEmbroidery(const document::EmbroideryObject& object) {
    clearBody();
    currentId_ = object.id;
    const ObjectId id = object.id;
    header_->setText(QString::fromStdString(object.name));

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    const QString typeName = object.is_tatami()  ? tr("Remplissage tatami")
                             : object.is_satin() ? tr("Colonne satin (expérimental)")
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
                rep->setRange(1, 3);
                rep->setValue(p.repeats);
                rep->setToolTip(tr("1 = simple, 2 = aller-retour, 3 = point triple"));
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Longueur minimale :"), minl);
                form->addRow(tr("Passages :"), rep);
                const auto emitEdit = [this, id, len, minl, rep] {
                    if (building_) return;
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
                angle->setRange(0, 179);
                angle->setSuffix(tr(" °"));
                angle->setValue(static_cast<int>(std::lround(
                    p.angle.radians * 180.0 / std::numbers::pi)) % 180);
                auto* inset = mmSpin(to_millimeters(p.inset).value, 5.0);
                auto* stagger = new QSpinBox(body_);
                stagger->setRange(1, 8);
                stagger->setValue(p.stagger);
                form->addRow(tr("Espacement des rangées :"), spacing);
                form->addRow(tr("Longueur de point :"), len);
                form->addRow(tr("Angle (orientation) :"), angle);
                form->addRow(tr("Retrait de bord :"), inset);
                form->addRow(tr("Décalage (stagger) :"), stagger);
                const auto emitEdit = [this, id, spacing, len, angle, inset, stagger] {
                    if (building_) return;
                    document::TatamiParams t;
                    t.row_spacing = to_um(spacing->value());
                    t.stitch_length = to_um(len->value());
                    t.angle = Angle{angle->value() * std::numbers::pi / 180.0};
                    t.inset = to_um(inset->value());
                    t.stagger = stagger->value();
                    emit paramsEdited(id, t);
                };
                connect(spacing, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(len, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(angle, &QSpinBox::valueChanged, this, emitEdit);
                connect(inset, &QDoubleSpinBox::valueChanged, this, emitEdit);
                connect(stagger, &QSpinBox::valueChanged, this, emitEdit);
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
                form->addRow(tr("Terminaison (fin) :"), capCombo);
                form->addRow(tr("Fixation (début) :"), lockStart);
                form->addRow(tr("Fixation (fin) :"), lockEnd);
                const auto emitEdit = [this, id, base, density, comp, underlay, shortCombo,
                                       splitCombo, capCombo, maxLen, edgeU, zigU, pullL, pullR,
                                       lockStart, lockEnd] {
                    if (building_) return;
                    document::SatinParams s = base;  // conserve rails + barreaux
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
                connect(lockStart, &QComboBox::currentIndexChanged, this, emitEdit);
                connect(lockEnd, &QComboBox::currentIndexChanged, this, emitEdit);
            }
        },
        object.params);
    building_ = false;

    auto* holder = new QWidget(body_);
    holder->setLayout(form);
    body_->layout()->addWidget(holder);
}

}  // namespace openstitch::desktop
