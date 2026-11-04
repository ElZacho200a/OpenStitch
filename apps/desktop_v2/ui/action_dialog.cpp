// SPDX-License-Identifier: Apache-2.0
#include "action_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

ActionDialog::ActionDialog(const ActionDef& action, const Values& initial, QWidget* parent)
    : QDialog(parent), action_(action) {
    setObjectName(QStringLiteral("actionDialog"));
    setWindowTitle(qs(action.label).remove(QStringLiteral("…")));
    auto* form = new QFormLayout;
    for (const auto& f : action.fields) {
        const auto it = initial.find(f.key);
        const QString value = qs(it != initial.end() ? it->second : f.def);
        QWidget* editor = nullptr;
        switch (f.kind) {
        case FieldKind::Number: {
            auto* spin = new QDoubleSpinBox(this);
            spin->setRange(f.min, f.max);
            spin->setDecimals(f.max - f.min <= 10.0 ? 2 : 1);
            spin->setSingleStep(f.max - f.min <= 10.0 ? 0.05 : 1.0);
            if (!f.unit.empty()) {
                spin->setSuffix(QStringLiteral(" ") + qs(f.unit));
            }
            spin->setValue(value.toDouble());
            editor = spin;
            break;
        }
        case FieldKind::Integer: {
            auto* spin = new QSpinBox(this);
            spin->setRange(static_cast<int>(f.min), static_cast<int>(f.max));
            spin->setValue(value.toInt());
            editor = spin;
            break;
        }
        case FieldKind::Bool: {
            auto* box = new QCheckBox(this);
            box->setChecked(value == QLatin1String("1") || value == QLatin1String("true"));
            editor = box;
            break;
        }
        case FieldKind::Choice: {
            auto* combo = new QComboBox(this);
            for (const auto& c : f.choices) {
                combo->addItem(qs(c.label), qs(c.value));
            }
            combo->setCurrentIndex(std::max(0, combo->findData(value)));
            editor = combo;
            break;
        }
        case FieldKind::Text:
            editor = new QLineEdit(value, this);
            break;
        case FieldKind::File: {
            auto* row = new QWidget(this);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* path = new QLineEdit(value, row);
            path->setObjectName(QStringLiteral("path"));
            auto* browse = new QPushButton(tr("Parcourir…"), row);
            h->addWidget(path, 1);
            h->addWidget(browse);
            connect(browse, &QPushButton::clicked, this, [this, path] {
                const QString chosen =
                    QFileDialog::getOpenFileName(this, windowTitle(), path->text());
                if (!chosen.isEmpty()) {
                    path->setText(chosen);
                }
            });
            editor = row;
            break;
        }
        }
        editor->setObjectName(QStringLiteral("field_") + qs(f.key));
        editor->setAccessibleName(qs(f.label));
        editors_[f.key] = editor;
        form->addRow(qs(f.label), editor);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

Values ActionDialog::values() const {
    Values out;
    for (const auto& f : action_.fields) {
        QWidget* e = editors_.at(f.key);
        QString text;
        if (auto* d = qobject_cast<QDoubleSpinBox*>(e)) {
            text = QString::number(d->value(), 'f', 4);
        } else if (auto* i = qobject_cast<QSpinBox*>(e)) {
            text = QString::number(i->value());
        } else if (auto* b = qobject_cast<QCheckBox*>(e)) {
            text = b->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
        } else if (auto* c = qobject_cast<QComboBox*>(e)) {
            text = c->currentData().toString();
        } else if (auto* l = qobject_cast<QLineEdit*>(e)) {
            text = l->text();
        } else if (auto* p = e->findChild<QLineEdit*>(QStringLiteral("path"))) {
            text = p->text();
        }
        out[f.key] = text.toStdString();
    }
    return out;
}

} // namespace openstitch::desktop_v2
