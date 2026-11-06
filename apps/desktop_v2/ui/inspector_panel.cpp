// SPDX-License-Identifier: Apache-2.0
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

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
    auto* boxLayout = new QVBoxLayout(paramsBox_);
    search_ = new QLineEdit(paramsBox_);
    search_->setObjectName(QStringLiteral("inspectorSearch"));
    search_->setPlaceholderText(tr("Rechercher un réglage…"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Rechercher un réglage"));
    advanced_ = new QCheckBox(tr("Réglages avancés"), paramsBox_);
    advanced_->setObjectName(QStringLiteral("inspectorAdvanced"));
    auto* inner = new QWidget(paramsBox_);
    paramsForm_ = new QFormLayout(inner);
    paramsForm_->setContentsMargins(0, 0, 0, 0);
    boxLayout->addWidget(search_);
    boxLayout->addWidget(advanced_);
    boxLayout->addWidget(inner);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyParamFilter(); });
    connect(advanced_, &QCheckBox::toggled, this, [this](bool) { applyParamFilter(); });
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
    // L'ancienne saisie ne doit jamais être appliquée à une nouvelle sélection. Un changement
    // de document ou de sélection invalide les commits différés avant de rafraîchir les champs.
    for (auto* timer : paramsBox_->findChildren<QTimer*>()) {
        timer->stop();
    }
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
        const int typeIndex = type_->findData(fromUtf8(settableIdOf(state.typeId.value)));
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

void InspectorPanel::setSearch(const QString& text) {
    search_->setText(text);
}

void InspectorPanel::setAdvancedShown(bool shown) {
    advanced_->setChecked(shown);
}

QStringList InspectorPanel::visibleParamKeys() const {
    QStringList out;
    for (int i = 0; i < paramKeys_.size() && i < paramsForm_->rowCount(); ++i) {
        const auto* field = paramsForm_->itemAt(i, QFormLayout::FieldRole);
        if (field != nullptr && field->widget() != nullptr && !field->widget()->isHidden()) {
            out << paramKeys_[i];
        }
    }
    return out;
}

void InspectorPanel::applyParamFilter() {
    const std::string query = search_->text().toStdString();
    const bool showAdvanced = advanced_->isChecked();
    int hidden = 0;
    int advancedTotal = 0;
    for (int row = 0; row < paramKeys_.size() && row < paramsForm_->rowCount(); ++row) {
        const bool advanced = paramAdvanced_[row];
        advancedTotal += advanced ? 1 : 0;
        bool matches = true;
        if (!query.empty()) {
            // La recherche porte sur le libellé ET sur la clé ; elle révèle aussi les avancés.
            const auto specs = paramSpecs(paramType_.toStdString());
            matches = false;
            for (const auto& spec : specs) {
                if (QString::fromUtf8(spec.key.data(), static_cast<qsizetype>(spec.key.size())) ==
                    paramKeys_[row]) {
                    matches = paramMatches(spec, query);
                    break;
                }
            }
        }
        const bool visible = matches && (!advanced || showAdvanced || !query.empty());
        paramsForm_->setRowVisible(row, visible);
        hidden += (advanced && !visible && matches) ? 1 : 0;
    }
    hiddenAdvanced_ = hidden;
    advanced_->setText(advancedTotal > 0 ? tr("Réglages avancés (%1)").arg(advancedTotal)
                                         : tr("Réglages avancés"));
    advanced_->setVisible(advancedTotal > 0);
}

void InspectorPanel::rebuildParams(const InspectorState& state) {
    const bool show = state.count > 0 && !state.typeId.mixed;
    const auto specs = show ? paramSpecs(state.typeId.value) : std::vector<ParamSpec>{};
    const QString type = show ? fromUtf8(state.typeId.value) : QString();
    if (specs.empty()) {
        paramsBox_->hide();
        paramKeys_.clear();
        paramAdvanced_.clear();
        paramLabels_.clear();
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
        paramAdvanced_.clear();
        paramLabels_.clear();
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
            } else if (spec.kind == ParamKind::Choice) {
                auto* combo = new QComboBox(paramsBox_);
                combo->addItem(QStringLiteral("—"), -1); // « mixte » : objets différents
                int n = 0;
                for (const auto& [value, optionLabel] : paramOptions(spec)) {
                    (void)value;
                    combo->addItem(fromUtf8(optionLabel), n++);
                }
                connect(combo, &QComboBox::activated, this, [this, key, combo](int row) {
                    if (!refreshing_ && row > 0) {
                        applyParam(key, combo->itemData(row).toInt());
                    }
                });
                editor = combo;
            } else if (spec.kind == ParamKind::Integer) {
                auto* spin = new QSpinBox(paramsBox_);
                spin->setKeyboardTracking(false);
                spin->setRange(static_cast<int>(spec.min) - 1, static_cast<int>(spec.max));
                spin->setSpecialValueText(QStringLiteral("—")); // valeur « mixte » : minimum - 1
                // Un glisser sur les flèches ou plusieurs pressions successives forment une
                // seule intention : on attend la fin de la rafale avant de créer l'annulation.
                auto* commitTimer = new QTimer(spin);
                commitTimer->setSingleShot(true);
                commitTimer->setInterval(220);
                const auto commit = [this, key, spin, commitTimer] {
                    if (!commitTimer->isActive() || refreshing_) {
                        return;
                    }
                    commitTimer->stop();
                    const int value = spin->value();
                    if (value > spin->minimum()) {
                        applyParam(key, value);
                    }
                };
                connect(spin, &QSpinBox::valueChanged, spin, [this, spin, commitTimer](int v) {
                    if (!refreshing_ && v > spin->minimum()) {
                        commitTimer->start();
                    }
                });
                connect(commitTimer, &QTimer::timeout, spin, [this, key, spin] {
                    if (!refreshing_ && spin->value() > spin->minimum()) {
                        applyParam(key, spin->value());
                    }
                });
                connect(spin, &QSpinBox::editingFinished, spin, commit);
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
                auto* commitTimer = new QTimer(spin);
                commitTimer->setSingleShot(true);
                commitTimer->setInterval(220);
                const auto commit = [this, key, spin, commitTimer] {
                    if (!commitTimer->isActive() || refreshing_) {
                        return;
                    }
                    commitTimer->stop();
                    const double value = spin->value();
                    if (value > spin->minimum() + 1e-9) {
                        applyParam(key, value);
                    }
                };
                connect(spin, &QDoubleSpinBox::valueChanged, spin,
                        [this, spin, commitTimer](double v) {
                            if (!refreshing_ && v > spin->minimum() + 1e-9) {
                                commitTimer->start();
                            }
                        });
                connect(commitTimer, &QTimer::timeout, spin, [this, key, spin] {
                    if (!refreshing_ && spin->value() > spin->minimum() + 1e-9) {
                        applyParam(key, spin->value());
                    }
                });
                connect(spin, &QDoubleSpinBox::editingFinished, spin, commit);
                editor = spin;
            }
            editor->setObjectName(QStringLiteral("param_") + key);
            editor->setAccessibleName(label);
            paramsForm_->addRow(label, editor);
            paramKeys_ << key;
            paramLabels_ << label;
            paramAdvanced_.push_back(paramLevel(spec) == ParamLevel::Advanced);
        }
        applyParamFilter();
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
        if (auto* combo = qobject_cast<QComboBox*>(editor)) {
            combo->setCurrentIndex(mixed || !first ? 0 : static_cast<int>(std::lround(*first)) + 1);
        } else if (auto* box = qobject_cast<QCheckBox*>(editor)) {
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
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        return combo->currentIndex() - 1.0;
    }
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
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        return combo->currentIndex() == 0;
    }
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
    if (typeId == QLatin1String("auto_satin") && !state.ids.empty()) {
        // Comme un remplissage : la première méthode qui construit des colonnes est retenue ; un
        // essai refusé laisse le document intact. Seul le dernier refus est annoncé.
        const auto last = satinAttemptCount() - 1;
        for (std::size_t attempt = 0; attempt < last; ++attempt) {
            if (session_.applyBatch(batchAutoSatin(state.ids, attempt), "Type : satin").committed) {
                return true;
            }
        }
        return run(batchAutoSatin(state.ids, last), tr("Type : satin"));
    }
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
