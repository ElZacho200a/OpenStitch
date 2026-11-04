// SPDX-License-Identifier: Apache-2.0
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "inspector_model.hpp"
#include "param_spec.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
constexpr const char* kMixedType = "__mixed__";
} // namespace

InspectorPanel::InspectorPanel(DocumentSession& session, QWidget* parent)
    : QWidget(parent), session_(session), title_(new QLabel(this)), type_(new QComboBox(this)),
      colour_(new QPushButton(this)), visible_(new QCheckBox(tr("Visible"), this)),
      locked_(new QCheckBox(tr("Position verrouillée"), this)) {
    setObjectName(QStringLiteral("inspectorPanel"));
    title_->setObjectName(QStringLiteral("inspectorTitle"));
    type_->setObjectName(QStringLiteral("inspectorType"));
    colour_->setObjectName(QStringLiteral("inspectorColour"));
    visible_->setObjectName(QStringLiteral("inspectorVisible"));
    locked_->setObjectName(QStringLiteral("inspectorLocked"));
    type_->setAccessibleName(tr("Type de points"));
    colour_->setAccessibleName(tr("Couleur du fil"));
    visible_->setTristate(true);
    locked_->setTristate(true);

    // Entrée « mixte » placée en tête, sélectionnée quand les objets n'ont pas le même type.
    type_->addItem(QStringLiteral("—"), QString::fromLatin1(kMixedType));
    for (const auto& t : settableStitchTypes()) {
        type_->addItem(QString::fromUtf8(t.label.data(), static_cast<qsizetype>(t.label.size())),
                       QString::fromUtf8(t.id.data(), static_cast<qsizetype>(t.id.size())));
    }

    auto* form = new QFormLayout(this);
    form->setContentsMargins(4, 4, 4, 4);
    form->addRow(title_);
    form->addRow(tr("Type de points"), type_);
    form->addRow(tr("Couleur du fil"), colour_);
    form->addRow(visible_);
    form->addRow(locked_);
    paramsBox_ = new QGroupBox(tr("Réglages de couture"), this);
    paramsBox_->setObjectName(QStringLiteral("inspectorParams"));
    paramsForm_ = new QFormLayout(paramsBox_);
    paramsBox_->hide();
    form->addRow(paramsBox_);

    connect(type_, &QComboBox::activated, this, [this](int index) {
        if (!refreshing_ && index > 0) {
            applyStitchType(type_->itemData(index).toString());
        }
    });
    connect(colour_, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(Qt::white, this, tr("Couleur du fil"));
        if (c.isValid()) {
            applyColor(c);
        }
    });
    // `clicked` plutôt que `stateChanged` : une case tri-état passe de « mixte » à cochée au clic.
    connect(visible_, &QCheckBox::clicked, this, [this](bool checked) {
        if (!refreshing_) {
            applyVisible(checked);
        }
    });
    connect(locked_, &QCheckBox::clicked, this, [this](bool checked) {
        if (!refreshing_) {
            applyLocked(checked);
        }
    });

    sessionToken_ = session_.subscribe([this](ChangeKind) { refresh(); });
    selectionToken_ = session_.selection().subscribe([this](const SelectionModel&) { refresh(); });
    refresh();
}

InspectorPanel::~InspectorPanel() {
    session_.unsubscribe(sessionToken_);
    session_.selection().unsubscribe(selectionToken_);
}

void InspectorPanel::refresh() {
    refreshing_ = true;
    const auto state = inspect(session_.project(), session_.selection().targets());
    rebuildParams(state);
    const bool any = state.count > 0;
    type_->setEnabled(any);
    colour_->setEnabled(any);
    visible_->setEnabled(any);
    locked_->setEnabled(any);
    if (!any) {
        title_->setText(tr("Aucun objet sélectionné"));
        type_->setCurrentIndex(0);
        colour_->setText(QString());
        colour_->setStyleSheet(QString());
        visible_->setCheckState(Qt::Unchecked);
        locked_->setCheckState(Qt::Unchecked);
    } else {
        title_->setText(state.count == 1 ? fromUtf8(state.name.value)
                                         : tr("%1 objets sélectionnés").arg(state.count));
        const int typeIndex = type_->findData(fromUtf8(state.typeId.value));
        type_->setCurrentIndex(state.typeId.mixed || typeIndex < 0 ? 0 : typeIndex);
        if (state.rgb.mixed) {
            colour_->setText(tr("Plusieurs couleurs"));
            colour_->setStyleSheet(QString());
        } else {
            const QColor c(state.rgb.value[0], state.rgb.value[1], state.rgb.value[2]);
            colour_->setText(c.name().toUpper());
            colour_->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                                       .arg(c.name(), c.lightness() < 128 ? "white" : "black"));
        }
        visible_->setCheckState(state.visible.mixed
                                    ? Qt::PartiallyChecked
                                    : (state.visible.value ? Qt::Checked : Qt::Unchecked));
        locked_->setCheckState(state.locked.mixed
                                   ? Qt::PartiallyChecked
                                   : (state.locked.value ? Qt::Checked : Qt::Unchecked));
    }
    refreshing_ = false;
}

void InspectorPanel::rebuildParams(const InspectorState& state) {
    const bool show = state.count > 0 && !state.typeId.mixed;
    const auto specs = show ? paramSpecs(state.typeId.value) : std::vector<ParamSpec>{};
    const QString type = show ? fromUtf8(state.typeId.value) : QString();
    if (specs.empty()) {
        paramsBox_->hide();
        paramKeys_.clear();
        paramType_.clear();
        while (paramsForm_->rowCount() > 0) {
            paramsForm_->removeRow(0);
        }
        return;
    }
    // Les champs sont reconstruits seulement quand le type commun change ; sinon on met à jour les
    // valeurs sur place (un widget en cours d'édition n'est jamais détruit sous le curseur).
    if (type != paramType_) {
        while (paramsForm_->rowCount() > 0) {
            paramsForm_->removeRow(0);
        }
        paramKeys_.clear();
        paramType_ = type;
        for (const auto& spec : specs) {
            const QString key =
                QString::fromUtf8(spec.key.data(), static_cast<qsizetype>(spec.key.size()));
            const QString label =
                QString::fromUtf8(spec.label.data(), static_cast<qsizetype>(spec.label.size()));
            const QString unit =
                QString::fromUtf8(spec.unit.data(), static_cast<qsizetype>(spec.unit.size()));
            QWidget* editor = nullptr;
            if (spec.kind == ParamKind::Bool) {
                auto* box = new QCheckBox(paramsBox_);
                box->setTristate(true);
                connect(box, &QCheckBox::clicked, this, [this, key](bool on) {
                    if (!refreshing_) {
                        applyParam(key, on ? 1.0 : 0.0);
                    }
                });
                editor = box;
            } else if (spec.kind == ParamKind::Integer) {
                auto* spin = new QSpinBox(paramsBox_);
                spin->setKeyboardTracking(false);
                spin->setRange(static_cast<int>(spec.min) - 1, static_cast<int>(spec.max));
                spin->setSpecialValueText(QStringLiteral("—")); // valeur « mixte » : minimum - 1
                connect(spin, &QSpinBox::valueChanged, this, [this, key, spin](int v) {
                    if (!refreshing_ && v >= spin->minimum() + 1) {
                        applyParam(key, v);
                    }
                });
                editor = spin;
            } else {
                auto* spin = new QDoubleSpinBox(paramsBox_);
                spin->setKeyboardTracking(false);
                spin->setDecimals(spec.step < 0.1 ? 2 : 1);
                spin->setSingleStep(spec.step);
                spin->setRange(spec.min - spec.step, spec.max);
                spin->setSpecialValueText(QStringLiteral("—"));
                if (!unit.isEmpty()) {
                    spin->setSuffix(QStringLiteral(" ") + unit);
                }
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key, spin](double v) {
                    if (!refreshing_ && v > spin->minimum() + 1e-9) {
                        applyParam(key, v);
                    }
                });
                editor = spin;
            }
            editor->setObjectName(QStringLiteral("param_") + key);
            editor->setAccessibleName(label);
            paramsForm_->addRow(label, editor);
            paramKeys_ << key;
        }
    }
    // Valeurs : commune, ou « mixte » (état intermédiaire / minimum - 1) si les objets diffèrent.
    for (const auto& spec : specs) {
        const QString key =
            QString::fromUtf8(spec.key.data(), static_cast<qsizetype>(spec.key.size()));
        auto* editor = paramsBox_->findChild<QWidget*>(QStringLiteral("param_") + key);
        if (editor == nullptr) {
            continue;
        }
        bool mixed = false;
        std::optional<double> first;
        for (const auto id : state.ids) {
            for (const auto& o : session_.project().embroidery_objects) {
                if (o.id.value != id) {
                    continue;
                }
                const auto v = readParam(o, spec.key);
                if (!v) {
                    continue;
                }
                if (!first) {
                    first = v;
                } else if (std::abs(*first - *v) > 1e-6) {
                    mixed = true;
                }
            }
        }
        if (auto* box = qobject_cast<QCheckBox*>(editor)) {
            box->setCheckState(mixed ? Qt::PartiallyChecked
                                     : (first && *first >= 0.5 ? Qt::Checked : Qt::Unchecked));
        } else if (auto* ispin = qobject_cast<QSpinBox*>(editor)) {
            ispin->setValue(mixed || !first ? ispin->minimum()
                                            : static_cast<int>(std::lround(*first)));
        } else if (auto* dspin = qobject_cast<QDoubleSpinBox*>(editor)) {
            dspin->setValue(mixed || !first ? dspin->minimum() : *first);
        }
    }
    paramsBox_->show();
}

std::optional<double> InspectorPanel::paramValue(const QString& key) const {
    auto* editor = paramsBox_->findChild<QWidget*>(QStringLiteral("param_") + key);
    if (auto* box = qobject_cast<QCheckBox*>(editor)) {
        return box->checkState() == Qt::Checked ? 1.0 : 0.0;
    }
    if (auto* ispin = qobject_cast<QSpinBox*>(editor)) {
        return ispin->value();
    }
    if (auto* dspin = qobject_cast<QDoubleSpinBox*>(editor)) {
        return dspin->value();
    }
    return std::nullopt;
}

bool InspectorPanel::paramMixed(const QString& key) const {
    auto* editor = paramsBox_->findChild<QWidget*>(QStringLiteral("param_") + key);
    if (auto* box = qobject_cast<QCheckBox*>(editor)) {
        return box->checkState() == Qt::PartiallyChecked;
    }
    if (auto* ispin = qobject_cast<QSpinBox*>(editor)) {
        return ispin->value() == ispin->minimum();
    }
    if (auto* dspin = qobject_cast<QDoubleSpinBox*>(editor)) {
        return dspin->value() <= dspin->minimum() + 1e-9;
    }
    return false;
}

bool InspectorPanel::applyParam(const QString& key, double value) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.count == 0 || state.typeId.mixed) {
        return false;
    }
    return run(batchSetParam(state.ids, state.typeId.value, key.toStdString(), value),
               tr("Réglage de couture"));
}

bool InspectorPanel::run(const std::string& batch, const QString& label) {
    if (batch.empty()) {
        return false;
    }
    const auto result = session_.applyBatch(batch, label.toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        emit refused(outcomes.empty() ? tr("Modification refusée")
                                      : fromUtf8(outcomes.back().message));
        refresh(); // rétablit l'affichage du document
    }
    return result.committed;
}

bool InspectorPanel::applyColor(const QColor& color) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    return run(batchSetColor(state.ids, {static_cast<std::uint8_t>(color.red()),
                                         static_cast<std::uint8_t>(color.green()),
                                         static_cast<std::uint8_t>(color.blue())}),
               tr("Couleur du fil"));
}

bool InspectorPanel::applyStitchType(const QString& typeId) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    return run(batchSetStitchType(state.ids, typeId.toStdString()), tr("Type de points"));
}

bool InspectorPanel::applyVisible(bool visible) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    return run(batchSetVisible(state.ids, visible), visible ? tr("Afficher") : tr("Masquer"));
}

bool InspectorPanel::applyLocked(bool locked) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    return run(batchSetLocked(state.ids, locked), locked ? tr("Verrouiller") : tr("Déverrouiller"));
}

} // namespace openstitch::desktop_v2
