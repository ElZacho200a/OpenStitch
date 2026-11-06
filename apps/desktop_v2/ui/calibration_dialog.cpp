// SPDX-License-Identifier: Apache-2.0
#include "calibration_dialog.hpp"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
constexpr int kColId = 0;
constexpr int kColDescription = 1;
constexpr int kColNominal = 2;
constexpr int kColPredicted = 3;
constexpr int kColMeasured = 4;
constexpr int kColError = 5;
} // namespace

CalibrationDialog::CalibrationDialog(const document::Project* project, QWidget* parent)
    : QDialog(parent), project_(project) {
    setObjectName("calibrationDialog");
    setWindowTitle(tr("Étalonnage du tirage"));
    resize(900, 560);
    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Brodez la pièce-test sur votre tissu (X du dessin = sens de la chaîne), mesurez les "
           "distances au pied à coulisse puis saisissez-les. Le modèle reste « non calibré » : "
           "l'ajustement n'est qu'une proposition."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Tissu :"), this));
    fabric_ = new QComboBox(this);
    fabric_->setObjectName("calibrationFabric");
    for (const auto& name : PullCalibration::fabricNames()) {
        fabric_->addItem(fromUtf8(name));
    }
    top->addWidget(fabric_);
    top->addStretch(1);
    auto* piece = new QPushButton(tr("Enregistrer la pièce-test (DST)…"), this);
    auto* load = new QPushButton(tr("Charger des mesures (CSV)…"), this);
    auto* save = new QPushButton(tr("Enregistrer le tableau (CSV)…"), this);
    top->addWidget(piece);
    top->addWidget(load);
    top->addWidget(save);
    root->addLayout(top);

    table_ = new QTableWidget(this);
    table_->setObjectName("calibrationTable");
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({tr("Mesure"), tr("Description"), tr("Nominal (mm)"),
                                       tr("Prédit (mm)"), tr("Mesuré (mm)"), tr("Écart (mm)")});
    table_->horizontalHeader()->setSectionResizeMode(kColDescription, QHeaderView::Stretch);
    table_->setAlternatingRowColors(true);
    root->addWidget(table_, 1);

    comparison_ = new QLabel(this);
    comparison_->setObjectName("calibrationComparison");
    comparison_->setWordWrap(true);
    fit_ = new QLabel(this);
    fit_->setObjectName("calibrationFit");
    fit_->setWordWrap(true);
    root->addWidget(comparison_);
    root->addWidget(fit_);

    auto* close = new QPushButton(tr("Fermer"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(close, 0, Qt::AlignRight);

    connect(fabric_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (calibration_.setFabric(name.toStdString())) {
            rebuildTable();
        }
    });
    connect(table_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (building_ || column != kColMeasured) {
            return;
        }
        const auto* id = table_->item(row, kColId);
        const auto* cell = table_->item(row, kColMeasured);
        if (id == nullptr || cell == nullptr) {
            return;
        }
        const QString text = cell->text().trimmed().replace(',', '.');
        if (text.isEmpty()) {
            calibration_.clearMeasure(id->text().toStdString());
        } else {
            bool ok = false;
            const double value = text.toDouble(&ok);
            if (!ok || !calibration_.setMeasure(id->text().toStdString(), value)) {
                fit_->setText(tr("Valeur refusée : saisissez une distance positive en mm."));
                return;
            }
        }
        rebuildTable();
    });
    connect(piece, &QPushButton::clicked, this, [this] { saveTestPiece(); });
    connect(load, &QPushButton::clicked, this, [this] { chooseCsv(); });
    connect(save, &QPushButton::clicked, this, [this] { saveCsv(); });
    rebuildTable();
}

void CalibrationDialog::rebuildTable() {
    building_ = true;
    const auto rows = calibration_.rows();
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[static_cast<std::size_t>(i)];
        const auto readOnly = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(i, col, item);
        };
        readOnly(kColId, fromUtf8(r.id));
        readOnly(kColDescription, fromUtf8(r.description));
        readOnly(kColNominal, QString::number(r.nominalMm, 'f', 3));
        readOnly(kColPredicted, QString::number(r.predictedMm, 'f', 3));
        table_->setItem(
            i, kColMeasured,
            new QTableWidgetItem(r.hasMeasure ? QString::number(r.measuredMm, 'f', 3) : QString()));
        readOnly(kColError, r.hasMeasure ? QString::number(r.errorMm, 'f', 3) : QString());
    }
    building_ = false;
    updateSummary();
}

void CalibrationDialog::updateSummary() {
    if (project_ != nullptr) {
        comparison_->setText(
            fromUtf8(compareProjectPull(*project_, calibration_.fabric()).summary()));
    } else {
        comparison_->setText(tr("Aucun motif ouvert à comparer."));
    }
    QString text = fromUtf8(calibration_.fit().message);
    if (calibration_.measuredCount() > 0) {
        text =
            tr("Écart quadratique prédit / mesuré : %1 mm. ").arg(calibration_.rmsMm(), 0, 'f', 3) +
            text;
    }
    fit_->setText(text);
}

int CalibrationDialog::loadMeasurementsCsv(const QString& csv) {
    const auto kept = calibration_.applyCsv(csv.toStdString());
    rebuildTable();
    return static_cast<int>(kept);
}

bool CalibrationDialog::setMeasure(const QString& id, double millimeters) {
    const bool ok = calibration_.setMeasure(id.toStdString(), millimeters);
    if (ok) {
        rebuildTable();
    }
    return ok;
}

void CalibrationDialog::setFabric(const QString& name) {
    fabric_->setCurrentText(name);
}

int CalibrationDialog::rowCount() const {
    return table_->rowCount();
}

QString CalibrationDialog::comparisonText() const {
    return comparison_->text();
}

QString CalibrationDialog::fitText() const {
    return fit_->text();
}

void CalibrationDialog::chooseCsv() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Charger des mesures"), {}, tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Lecture impossible : %1").arg(path));
        return;
    }
    const int kept = loadMeasurementsCsv(QString::fromUtf8(file.readAll()));
    if (kept == 0) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Aucune mesure exploitable (colonne measured_mm)."));
    }
}

void CalibrationDialog::saveCsv() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer le tableau"),
                                                      "mesures-tirage.csv", tr("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(calibration_.csv().data(), static_cast<qint64>(calibration_.csv().size())) < 0) {
        QMessageBox::warning(this, windowTitle(), tr("Écriture impossible : %1").arg(path));
    }
}

void CalibrationDialog::saveTestPiece() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer la pièce-test"),
                                                      "piece-test-tirage.dst", tr("DST (*.dst)"));
    if (path.isEmpty()) {
        return;
    }
    const auto written = calibration_.writeTestPiece(path.toStdString());
    if (!written) {
        QMessageBox::warning(this, windowTitle(), fromUtf8(written.error().message));
    }
}

} // namespace openstitch::desktop_v2
