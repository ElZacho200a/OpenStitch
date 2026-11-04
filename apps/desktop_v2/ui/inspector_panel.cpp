// SPDX-License-Identifier: Apache-2.0
#include "inspector_panel.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

#include "inspector_model.hpp"

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
