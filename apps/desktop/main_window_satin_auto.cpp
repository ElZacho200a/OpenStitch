// SPDX-License-Identifier: Apache-2.0
// Interface de l'auto-satin par squelette et traversées orientées (spec
// specs/plans/satin-squelette-traversees.md) : création, aperçu de couverture,
// guides d'orientation, visualisation du squelette et des traversées. Membres de
// MainWindow séparés de main_window.cpp pour la lisibilité. Aucune logique métier
// ici : le squelette, l'orientation et les traversées viennent de libs/auto_satin,
// et toute mutation du document passe par une commande annulable.
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

#include "app_theme.hpp"
#include "auto_satin_strategy_ui.hpp"
#include "busy_indicator.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/stitch_generation/auto_satin_engine.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

namespace {

// Scène en mm, Y vers le bas ; modèle en µm, Y vers le haut (ADR-003).
QPointF toScene(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF s) {
    return Vec2um{to_micrometers(Millimeters{s.x()}), to_micrometers(Millimeters{-s.y()})};
}

// Une seule correspondance réglages -> moteur pour tout le programme (génération, aperçu,
// inspecteur, API) : voir stitch_generation::auto_satin_engine.
auto_satin::SkeletonSatinParameters engine_parameters(const document::AutoSatinParams& p,
                                                      bool measureCoverage) {
    auto e = stitch_generation::auto_satin_engine(p);
    e.measure_coverage = measureCoverage;
    return e;
}

// Empreinte (FNV-1a) de ce qui détermine le résultat du moteur : forme source,
// guides et espacement. Sert de clé au cache du résumé de diagnostic.
std::uint64_t summary_key(const document::VectorObject& source,
                          const document::AutoSatinParams& p) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v);
        h *= 1099511628211ull;
    };
    const auto path = [&](const geometry::Path& pa) {
        mix(static_cast<std::int64_t>(pa.nodes.size()));
        for (const auto& n : pa.nodes) {
            mix(n.pos.x.value);
            mix(n.pos.y.value);
        }
    };
    for (const auto& set : source.paths) {
        path(set.outer);
        for (const auto& hole : set.holes) {
            path(hole);
        }
    }
    mix(p.spacing.value);
    // Méthode et réglages propres à une méthode : sans eux, changer de méthode ne rafraîchirait
    // pas le résumé de l'inspecteur (et `compact_axis` n'y figurait pas non plus).
    mix(static_cast<std::int64_t>(p.strategy));
    mix(p.sweep_angle ? static_cast<std::int64_t>(std::llround(p.sweep_angle->radians * 1e6))
                      : std::numeric_limits<std::int64_t>::min());
    mix(p.compact_axis ? 1 : 0);
    for (const auto& g : p.guides) {
        mix(g.anchor.x.value);
        mix(g.anchor.y.value);
        mix(static_cast<std::int64_t>(std::llround(g.angle.radians * 1e6)));
        mix(g.absolute ? 1 : 0);
        for (const Vec2um& point : g.path) {
            mix(point.x.value);
            mix(point.y.value);
        }
    }
    return h;
}

// Angle d'une droite, ramené dans (-π/2, π/2].
double wrap_half_pi(double a) {
    a = std::fmod(a, std::numbers::pi);
    if (a > std::numbers::pi / 2.0) {
        a -= std::numbers::pi;
    } else if (a <= -std::numbers::pi / 2.0) {
        a += std::numbers::pi;
    }
    return a;
}

using DescribeFn = std::function<QString(const document::AutoSatinParams&)>;
using CompareFn =
    std::function<std::vector<AutoSatinStrategyComparison>(const document::AutoSatinParams&)>;

// Tableau « Comparer les méthodes » : une ligne par méthode, mesures géométriques des traversées
// (avant finitions). Aucun choix automatique : l'utilisateur désigne la méthode ; fermer ne
// change rien. Renvoie la méthode choisie, ou rien si le dialogue est fermé.
std::optional<document::AutoSatinStrategy>
showStrategyComparison(QWidget* parent, const std::vector<AutoSatinStrategyComparison>& rows,
                       document::AutoSatinStrategy current) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Comparer les méthodes de satin"));
    dialog.resize(900, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(
        QObject::tr("Mesures géométriques des traversées, avant finitions (sous-couches, points "
                    "courts, fractionnement). Une bonne couverture ne remplace pas un essai sur "
                    "machine : le choix esthétique et textile vous revient."),
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* table = new QTableWidget(static_cast<int>(rows.size()), 8, &dialog);
    table->setObjectName(QStringLiteral("table_autoSatinCompare"));
    table->setAccessibleName(QObject::tr("Comparaison des méthodes de satin"));
    table->setHorizontalHeaderLabels({QObject::tr("Méthode"), QObject::tr("Colonnes"),
                                      QObject::tr("Couverture"), QObject::tr("Fil en double"),
                                      QObject::tr("Non couvert (mm²)"), QObject::tr("Croisements"),
                                      QObject::tr("Plus long fil (mm)"), QObject::tr("Remarques")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    int selectRow = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const int row = static_cast<int>(i);
        const auto text = [&](int col, const QString& value, bool right = true) {
            auto* item = new QTableWidgetItem(value);
            if (right) {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            item->setToolTip(autoSatinStrategyTip(r.strategy));
            table->setItem(row, col, item);
        };
        text(0,
             (r.strategy == current ? QObject::tr("✓ ") : QString()) +
                 autoSatinStrategyLabel(r.strategy),
             false);
        if (r.strategy == current) {
            selectRow = row;
        }
        if (!r.ok) {
            text(1, QObject::tr("refusée"));
            for (int col = 2; col <= 6; ++col) {
                text(col, QStringLiteral("—"));
            }
        } else {
            text(1, QString::number(r.columns));
            text(2, QStringLiteral("%1 %").arg(r.coverage * 100.0, 0, 'f', 1));
            text(3, QStringLiteral("×%1").arg(r.overlap, 0, 'f', 2));
            text(4, QString::number(r.uncoveredMm2, 'f', 1));
            text(5, QString::number(r.crossingPairs));
            text(6, QString::number(r.maxThreadMm, 'f', 1));
        }
        text(7, r.messages.join(QStringLiteral(" · ")), false);
    }
    table->resizeColumnsToContents();
    table->selectRow(selectRow);
    layout->addWidget(table);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* use =
        buttons->addButton(QObject::tr("Utiliser cette méthode"), QDialogButtonBox::AcceptRole);
    use->setObjectName(QStringLiteral("button_autoSatinCompareUse"));
    buttons->addButton(QObject::tr("Fermer"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    const auto syncUse = [&] {
        const int row = table->currentRow();
        use->setEnabled(row >= 0 && row < static_cast<int>(rows.size()) &&
                        rows[static_cast<std::size_t>(row)].ok);
    };
    syncUse();
    QObject::connect(table, &QTableWidget::currentCellChanged, &dialog, syncUse);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&](int row, int) {
        if (rows[static_cast<std::size_t>(row)].ok) {
            dialog.accept();
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const int row = table->currentRow();
    if (row < 0 || row >= static_cast<int>(rows.size()) ||
        !rows[static_cast<std::size_t>(row)].ok) {
        return std::nullopt;
    }
    return rows[static_cast<std::size_t>(row)].strategy;
}

// Réglages du satin automatique (une fenêtre pour une ou plusieurs formes) ; faux si annulé.
// `describe` recalcule le résumé (couverture, colonnes) quand la méthode change ; `compare`
// calcule le tableau de toutes les méthodes (hors du fil d'interface, sans toucher au document).
bool askAutoSatinParams(QWidget* parent, const QString& info, document::AutoSatinParams& params,
                        const DescribeFn& describe, const CompareFn& compare) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Satin"));
    auto* layout = new QFormLayout(&dialog);
    auto* infoLabel = new QLabel(info, &dialog);
    infoLabel->setObjectName(QStringLiteral("label_autoSatinInfo"));
    infoLabel->setWordWrap(true);
    layout->addRow(infoLabel);
    auto* warn = new QLabel(QObject::tr("⚠ Le satin automatique n'a pas été validé sur machine. "
                                        "Vérifiez le résultat (densité, virages, jonctions) avant "
                                        "broderie."),
                            &dialog);
    warn->setWordWrap(true);
    warn->setStyleSheet(
        QStringLiteral("color:%1;").arg(AppTheme::instance().tokens().warning.name()));
    layout->addRow(warn);

    // Méthode de calcul des traversées : le choix revient à l'utilisateur, jamais à l'outil.
    auto* method = new QComboBox(&dialog);
    method->setObjectName(QStringLiteral("combo_autoSatinStrategy"));
    method->addItems(autoSatinStrategyItems());
    for (std::size_t i = 0; i < kAutoSatinStrategies.size(); ++i) {
        method->setItemData(static_cast<int>(i), autoSatinStrategyTip(kAutoSatinStrategies[i]),
                            Qt::ToolTipRole);
    }
    method->setCurrentIndex(static_cast<int>(params.strategy));
    method->setToolTip(QObject::tr("Méthode de calcul des traversées de ce satin."));
    auto* sweepAuto = new QCheckBox(QObject::tr("Angle des fils automatique"), &dialog);
    sweepAuto->setObjectName(QStringLiteral("check_autoSatinSweepAuto"));
    sweepAuto->setChecked(!params.sweep_angle.has_value());
    auto* sweepAngle = new QDoubleSpinBox(&dialog);
    sweepAngle->setObjectName(QStringLiteral("spin_autoSatinSweepAngle"));
    sweepAngle->setRange(-179.9, 179.9);
    sweepAngle->setDecimals(1);
    sweepAngle->setSuffix(QObject::tr(" °"));
    sweepAngle->setValue(params.sweep_angle ? params.sweep_angle->radians * 180.0 / std::numbers::pi
                                            : 0.0);
    auto* compareButton = new QPushButton(QObject::tr("Comparer les méthodes…"), &dialog);
    compareButton->setObjectName(QStringLiteral("button_autoSatinCompare"));
    compareButton->setEnabled(static_cast<bool>(compare));

    auto* spacingSpin = new QDoubleSpinBox(&dialog);
    spacingSpin->setRange(0.1, 1.5);
    spacingSpin->setDecimals(2);
    spacingSpin->setSuffix(QObject::tr(" mm"));
    spacingSpin->setValue(to_millimeters(params.spacing).value);
    auto* compSpin = new QDoubleSpinBox(&dialog);
    compSpin->setRange(0.0, 1.0);
    compSpin->setDecimals(2);
    compSpin->setSuffix(QObject::tr(" mm"));
    compSpin->setValue(0.0);
    auto* underlayCheck = new QCheckBox(QObject::tr("Sous-couche centrale"), &dialog);
    underlayCheck->setChecked(params.center_underlay);
    auto* splitCheck =
        new QCheckBox(QObject::tr("Fractionner les traversées longues (> 7 mm)"), &dialog);
    splitCheck->setChecked(params.split_stitch != document::SatinSplit::Disabled);
    layout->addRow(QObject::tr("Méthode :"), method);
    layout->addRow(QString(), sweepAuto);
    layout->addRow(QObject::tr("Angle des fils :"), sweepAngle);
    layout->addRow(QString(), compareButton);
    layout->addRow(QObject::tr("Espacement :"), spacingSpin);
    layout->addRow(QObject::tr("Compensation de tirage :"), compSpin);
    layout->addRow(underlayCheck);
    layout->addRow(splitCheck);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    // Réglages courants du formulaire (méthode, angle, espacement) : résumé et comparaison.
    const auto current = [&] {
        document::AutoSatinParams p = params;
        const int index = std::clamp(method->currentIndex(), 0,
                                     static_cast<int>(kAutoSatinStrategies.size()) - 1);
        p.strategy = kAutoSatinStrategies[static_cast<std::size_t>(index)];
        p.spacing = to_micrometers(Millimeters{spacingSpin->value()});
        if (sweepAuto->isChecked()) {
            p.sweep_angle.reset();
        } else {
            p.sweep_angle = Angle{sweepAngle->value() * std::numbers::pi / 180.0};
        }
        return p;
    };
    const auto syncSweep = [&] {
        const bool sweep =
            method->currentIndex() == static_cast<int>(document::AutoSatinStrategy::ParallelSweep);
        sweepAuto->setEnabled(sweep);
        sweepAngle->setEnabled(sweep && !sweepAuto->isChecked());
    };
    syncSweep();
    // Résumé recalculé avec un petit délai : une rafale de changements (molette du spinbox)
    // ne lance qu'un calcul.
    auto* refresh = new QTimer(&dialog);
    refresh->setSingleShot(true);
    refresh->setInterval(180);
    QObject::connect(refresh, &QTimer::timeout, &dialog, [&] {
        if (describe) {
            infoLabel->setText(describe(current()));
        }
    });
    const auto changed = [&](auto&&...) {
        syncSweep();
        refresh->start();
    };
    QObject::connect(method, &QComboBox::currentIndexChanged, &dialog, changed);
    QObject::connect(sweepAuto, &QCheckBox::toggled, &dialog, changed);
    QObject::connect(sweepAngle, &QDoubleSpinBox::valueChanged, &dialog, changed);
    QObject::connect(spacingSpin, &QDoubleSpinBox::valueChanged, &dialog, changed);
    QObject::connect(compareButton, &QPushButton::clicked, &dialog, [&] {
        const auto rows = compare(current());
        if (const auto chosen = showStrategyComparison(&dialog, rows, current().strategy)) {
            method->setCurrentIndex(static_cast<int>(*chosen));
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const auto chosen = current();
    params.strategy = chosen.strategy;
    params.sweep_angle = chosen.sweep_angle;
    params.spacing = to_micrometers(Millimeters{spacingSpin->value()});
    params.pull_compensation = to_micrometers(Millimeters{compSpin->value()});
    params.center_underlay = underlayCheck->isChecked();
    params.split_stitch =
        splitCheck->isChecked() ? document::SatinSplit::Staggered : document::SatinSplit::Disabled;
    return true;
}

} // namespace

std::optional<std::vector<geometry::PathSet>>
MainWindow::pristineSatinContour(const document::VectorObject& vector) const {
    if (!vector.source_region || !project_.segmentation) {
        return std::nullopt;
    }
    vectorization::VectorizeOptions options;
    options.mm_per_px = project_.mm_per_px;
    const auto raw =
        vectorization::vectorize_region(*project_.segmentation, *vector.source_region, options);
    if (!raw || raw->empty()) {
        return std::nullopt;
    }
    double current = 0.0;
    for (const auto& set : vector.paths) {
        current += geometry::path_set_area_um2(set);
    }
    double pristine = 0.0;
    for (const auto& set : *raw) {
        pristine += geometry::path_set_area_um2(set);
    }
    // Moins de 3 % de plus : le contour est déjà celui de la région (ou retouché à la main).
    if (pristine <= 0.0 || current <= pristine * 1.03) {
        return std::nullopt;
    }
    return *raw;
}

MainWindow::AutoSatinPreview
MainWindow::previewAutoSatin(const document::VectorObject& source,
                             const document::AutoSatinParams& params) const {
    AutoSatinPreview out;
    double areaSum = 0.0;
    double coveredSum = 0.0;
    double overlapSum = 0.0;
    for (const auto& set : source.paths) {
        const auto res = auto_satin::generate_satin(set, engine_parameters(params, true));
        if (!res) {
            out.messages << QString::fromStdString(res.error().message);
            continue;
        }
        out.columns += res->columns.size();
        out.orphanGuides += res->diagnostics.orphan_guides;
        for (const auto& m : res->diagnostics.messages) {
            const QString q = QString::fromStdString(m);
            if (!out.messages.contains(q)) {
                out.messages << q;
            }
        }
        if (res->diagnostics.coverage_measured) {
            double area = std::abs(geometry::signed_area_um2(set.outer));
            for (const auto& hole : set.holes) {
                area -= std::abs(geometry::signed_area_um2(hole));
            }
            area = std::max(area, 0.0) / 1e6;
            areaSum += area;
            coveredSum += area * res->diagnostics.coverage_ratio;
            overlapSum += area * res->diagnostics.overlap_ratio;
            out.uncoveredMm2 += res->diagnostics.uncovered_area_mm2;
        }
    }
    if (areaSum > 0.0) {
        out.measured = true;
        out.coverage = coveredSum / areaSum;
        out.overlap = overlapSum / areaSum;
    }
    return out;
}

QString MainWindow::describeAutoSatinPreview(const AutoSatinPreview& preview) const {
    QString text = tr("%n colonne(s)", nullptr, static_cast<int>(preview.columns));
    if (preview.measured) {
        text += tr(" · couverture estimée %1 % · fil en double ×%2")
                    .arg(preview.coverage * 100.0, 0, 'f', 1)
                    .arg(preview.overlap, 0, 'f', 2);
        if (preview.uncoveredMm2 >= 1.0) {
            text += tr("\n⚠ %1 mm² resteraient sans point.").arg(preview.uncoveredMm2, 0, 'f', 1);
        }
    }
    if (preview.orphanGuides > 0) {
        text += tr("\n⚠ %1 guide(s) trop loin de l'axe, ignoré(s).").arg(preview.orphanGuides);
    }
    for (const auto& m : preview.messages) {
        text += tr("\nAttention : %1").arg(m);
    }
    return text;
}

QString MainWindow::autoSatinSummary(const document::EmbroideryObject& emb) {
    const auto* params = std::get_if<document::AutoSatinParams>(&emb.params);
    const auto* source = project_.findObject(emb.source_vector);
    if (params == nullptr || source == nullptr) {
        return {};
    }
    const std::uint64_t key = summary_key(*source, *params);
    if (autoSatinSummaryCache_.id == emb.id && autoSatinSummaryCache_.key == key &&
        autoSatinSummaryCache_.valid) {
        return autoSatinSummaryCache_.text;
    }
    const QString method = tr("Méthode : %1").arg(autoSatinStrategyLabel(params->strategy));
    autoSatinSummaryCache_ = {emb.id, key,
                              method + QLatin1Char('\n') +
                                  describeAutoSatinPreview(previewAutoSatin(*source, *params)),
                              true};
    return autoSatinSummaryCache_.text;
}

std::vector<AutoSatinStrategyComparison>
MainWindow::compareAutoSatinStrategies(const std::vector<geometry::PathSet>& paths,
                                       const document::AutoSatinParams& base) {
    BusyIndicator busy(this, tr("Comparaison des méthodes de satin…"));
    // Instantané : le calcul n'accède qu'à des copies, jamais au document ni à l'interface.
    return busy.run([paths, base] {
        std::vector<AutoSatinStrategyComparison> rows;
        for (const auto strategy : kAutoSatinStrategies) {
            AutoSatinStrategyComparison row;
            row.strategy = strategy;
            document::AutoSatinParams p = base;
            p.strategy = strategy;
            const auto engine = engine_parameters(p, true);
            double areaSum = 0.0;
            double coveredSum = 0.0;
            double overlapSum = 0.0;
            for (const auto& set : paths) {
                const auto res = auto_satin::generate_satin(set, engine);
                if (!res) {
                    const QString msg = QString::fromStdString(res.error().message);
                    if (!row.messages.contains(msg)) {
                        row.messages << msg;
                    }
                    continue;
                }
                const auto m = auto_satin::measure_result(*res);
                row.columns += m.columns;
                row.crossings += m.crossings;
                row.crossingPairs += m.crossing_pairs;
                row.maxThreadMm = std::max(row.maxThreadMm, m.max_crossing_mm);
                row.uncoveredMm2 += m.uncovered_area_mm2;
                for (const auto& message : res->diagnostics.messages) {
                    const QString msg = QString::fromStdString(message);
                    if (!row.messages.contains(msg)) {
                        row.messages << msg;
                    }
                }
                double area = std::abs(geometry::signed_area_um2(set.outer));
                for (const auto& hole : set.holes) {
                    area -= std::abs(geometry::signed_area_um2(hole));
                }
                area = std::max(area, 0.0) / 1e6;
                areaSum += area;
                coveredSum += area * m.coverage_ratio;
                overlapSum += area * m.overlap_ratio;
            }
            row.ok = row.columns > 0;
            if (areaSum > 0.0) {
                row.coverage = coveredSum / areaSum;
                row.overlap = overlapSum / areaSum;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

std::optional<document::AutoSatinStrategy>
MainWindow::firstWorkingAutoSatinStrategy(const document::VectorObject& source,
                                          const document::AutoSatinParams& params) const {
    std::vector<document::AutoSatinStrategy> order{params.strategy};
    for (const auto s : kAutoSatinStrategies) {
        if (s != params.strategy) {
            order.push_back(s);
        }
    }
    for (const auto s : order) {
        document::AutoSatinParams p = params;
        p.strategy = s;
        if (previewAutoSatin(source, p).columns > 0) {
            return s;
        }
    }
    return std::nullopt;
}

void MainWindow::compareAutoSatinMethods(ObjectId id) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
    if (sat == nullptr || source == nullptr) {
        return;
    }
    const auto rows = compareAutoSatinStrategies(source->paths, *sat);
    const auto chosen = showStrategyComparison(this, rows, sat->strategy);
    if (!chosen || *chosen == sat->strategy) {
        return; // fermer ou garder la méthode actuelle ne change rien
    }
    document::AutoSatinParams params = *sat;
    params.strategy = *chosen;
    applyAutoSatinEdit(id, std::move(params),
                       tr("Changer la méthode de satin : %1").arg(autoSatinStrategyLabel(*chosen)));
}

void MainWindow::createAutoSatin(bool askParameters, bool confirm) {
    if (hasMultiSelection()) {
        createAutoSatinForSelection(askParameters, confirm);
        return;
    }
    if (!selectedObject_) {
        return;
    }
    const auto* current = project_.findObject(*selectedObject_);
    if (current == nullptr || current->paths.empty()) {
        return;
    }
    // Le satin suit la région telle que segmentée, pas le contour agrandi par le recouvrement
    // des tatamis voisins (sinon il déborde de sa zone).
    const auto restored = pristineSatinContour(*current);
    document::VectorObject restoredCopy;
    const document::VectorObject* source = current;
    if (restored) {
        restoredCopy = *current;
        restoredCopy.paths = *restored;
        source = &restoredCopy;
    }

    document::AutoSatinParams params;
    params.strategy = document::kDefaultAutoSatinStrategy;
    ui_memory::creation::apply(params); // HP-UX-002 : dernier espacement mémorisé
    AutoSatinPreview preview = previewAutoSatin(*source, params);

    // Le squelette n'a produit aucune colonne : avant de refuser, on essaie les autres méthodes
    // (disque, pétale, bloc large). La méthode retenue est PROPOSÉE puis montrée à
    // l'utilisateur (dialogue ou question) ; jamais appliquée à son insu.
    QString suggestion;
    if (preview.columns == 0) {
        if (const auto alt = firstWorkingAutoSatinStrategy(*source, params);
            alt && *alt != params.strategy) {
            const QString skeletonRefusal = preview.messages.isEmpty()
                                                ? tr("forme non exploitable")
                                                : preview.messages.join(QLatin1Char('\n'));
            params.strategy = *alt;
            preview = previewAutoSatin(*source, params);
            suggestion = tr("La méthode « %1 » ne peut pas traiter cette forme (%2).\n"
                            "Méthode proposée : « %3 ».\n\n")
                             .arg(autoSatinStrategyLabel(document::kDefaultAutoSatinStrategy),
                                  skeletonRefusal, autoSatinStrategyLabel(*alt));
        }
    }

    if (preview.columns == 0) {
        // Aucune méthode n'a produit de colonne : refus nommé, jamais contourné par
        // une géométrie de moindre qualité. Seul choix actionnable : un tatami.
        const auto answer = QMessageBox::question(
            this, tr("Satin impossible"),
            tr("Aucune colonne satin n'a pu être construite pour cette région :\n%1\n\n"
               "Utiliser un remplissage tatami à la place ?")
                .arg(preview.messages.isEmpty() ? tr("forme non exploitable")
                                                : preview.messages.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        document::EmbroideryObject tatami;
        tatami.id = project_.object_ids.next();
        tatami.name = tr("Tatami de %1").arg(QString::fromStdString(source->name)).toStdString();
        tatami.source_vector = source->id;
        tatami.rgb = source->rgb;
        tatami.params = document::TatamiParams{};
        tatami.intent = document::EmbroideryIntent::ForcedUserChoice;
        undoStack_.execute(
            std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(tatami)), project_);
        showStitchesAct_->setChecked(true);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Remplissage tatami créé (satin impossible)."));
        return;
    }

    if (askParameters) {
        const auto describe = [this, source](const document::AutoSatinParams& p) {
            return describeAutoSatinPreview(previewAutoSatin(*source, p));
        };
        const auto compare = [this, source](const document::AutoSatinParams& p) {
            return compareAutoSatinStrategies(source->paths, p);
        };
        if (!askAutoSatinParams(this, suggestion + describeAutoSatinPreview(preview), params,
                                describe, compare)) {
            return;
        }
        ui_memory::creation::remember(document::StitchParams{params});
    } else if (confirm) {
        const auto answer =
            QMessageBox::question(this, tr("Convertir en satin"),
                                  tr("%1%2\n\nCréer le satin ? (annulable)")
                                      .arg(suggestion, describeAutoSatinPreview(preview)));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    // Les réglages du dialogue (méthode comprise) décident : on recalcule le résumé affiché
    // dans la barre d'état pour la méthode finalement retenue.
    preview = previewAutoSatin(*source, params);

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Satin de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;
    // L'utilisateur a explicitement demandé un satin : jamais une classification
    // automatique (§21/§24 du plan de refonte satin).
    object.intent = document::EmbroideryIntent::ForcedUserChoice;
    if (restored) {
        auto group = std::make_unique<commands::CompositeCommand>("Créer un satin");
        group->add(std::make_unique<commands::SetVectorPathsCommand>(source->id, *restored,
                                                                     "Contour brut de la région"));
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
        undoStack_.execute(std::move(group), project_);
    } else {
        undoStack_.execute(
            std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)), project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Satin créé : %1.").arg(describeAutoSatinPreview(preview).section('\n', 0, 0)) +
        (restored ? tr(" Contour ramené à celui de la région (recouvrement tatami retiré).")
                  : QString()));
}

void MainWindow::applyAutoSatinEdit(ObjectId id, document::AutoSatinParams params,
                                    const QString& label, const QString& mergeTag) {
    auto cmd = std::make_unique<commands::EditAutoSatinCommand>(id, std::move(params),
                                                                label.toStdString());
    cmd->setMergeTag(mergeTag.toStdString());
    undoStack_.execute(std::move(cmd), project_);
    refreshImage();
    updateActions();
}

void MainWindow::addAutoSatinGuideFromStroke(ObjectId id, const std::vector<Vec2um>& stroke) {
    if (stroke.size() < 2) {
        return;
    }
    const Vec2um from = stroke.front();
    const Vec2um to = stroke.back();
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr) {
        return;
    }
    const double dx = static_cast<double>(to.x.value - from.x.value);
    const double dy = static_cast<double>(to.y.value - from.y.value);
    if (std::hypot(dx, dy) < 300.0) {
        statusBar()->showMessage(tr("Trait trop court pour fixer une direction."));
        return;
    }
    document::AutoSatinParams params = *sat;
    // Trait tracé = direction des fils voulue, en absolu (repère du dessin).
    document::AutoSatinGuide guide{from, Angle{wrap_half_pi(std::atan2(dy, dx))}, true, {}};
    if (stroke.size() >= 3) {
        guide.path = stroke; // courbe : exploitée par le balayage parallèle
    }
    params.guides.push_back(std::move(guide));
    applyAutoSatinEdit(id, std::move(params), tr("Ajouter un guide d'orientation"));
}

void MainWindow::changeAutoSatinGuide(ObjectId id, int index, double angleDeg, bool absolute) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    document::AutoSatinParams params = *sat;
    auto& guide = params.guides[static_cast<std::size_t>(index)];
    const Angle next{wrap_half_pi(angleDeg * std::numbers::pi / 180.0)};
    if (guide.angle.radians == next.radians && guide.absolute == absolute) {
        return;
    }
    guide.angle = next;
    guide.absolute = absolute;
    applyAutoSatinEdit(id, std::move(params), tr("Modifier un guide d'orientation"),
                       QStringLiteral("guide-%1").arg(index));
}

void MainWindow::highlightAutoSatinGuide(ObjectId id, int index) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    if (guideHighlight_ != nullptr) {
        baseItems_.removeAll(guideHighlight_);
        delete guideHighlight_;
        guideHighlight_ = nullptr;
    }
    const auto& tokens = AppTheme::instance().tokens();
    const QPointF c = toScene(sat->guides[static_cast<std::size_t>(index)].anchor);
    auto* ring = new QGraphicsEllipseItem(-13.0, -13.0, 26.0, 26.0);
    ring->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    QPen pen(tokens.warning, 3.0);
    ring->setPen(pen);
    ring->setBrush(Qt::NoBrush);
    ring->setPos(c);
    ring->setZValue(102);
    ring->setToolTip(tr("Guide d'orientation #%1").arg(index + 1));
    scene_->addItem(ring);
    baseItems_.append(ring);
    guideHighlight_ = ring;
    statusBar()->showMessage(tr("Guide #%1 mis en évidence sur le canevas.").arg(index + 1), 3000);
}

void MainWindow::removeAutoSatinGuide(ObjectId id, int index) {
    const auto* emb = project_.findEmbroidery(id);
    const auto* sat =
        emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
    if (sat == nullptr || index < 0 || static_cast<std::size_t>(index) >= sat->guides.size()) {
        return;
    }
    document::AutoSatinParams params = *sat;
    params.guides.erase(params.guides.begin() + index);
    applyAutoSatinEdit(id, std::move(params), tr("Supprimer un guide d'orientation"));
}

void MainWindow::renderAutoSatinOverlay(const document::EmbroideryObject& obj,
                                        const document::AutoSatinParams& params,
                                        const document::VectorObject& source) {
    const auto& tokens = AppTheme::instance().tokens();

    QPainterPath chords;
    QPainterPath axes;
    std::vector<std::vector<Vec2um>> axisList;
    for (const auto& set : source.paths) {
        const auto res = auto_satin::generate_satin(set, engine_parameters(params, false));
        if (!res) {
            continue;
        }
        for (const auto& column : res->columns) {
            for (const auto& c : column.crossings) {
                chords.moveTo(toScene(c.a));
                chords.lineTo(toScene(c.b));
            }
        }
        for (const auto& axis : res->axes) {
            if (axis.size() < 2) {
                continue;
            }
            axes.moveTo(toScene(axis.front()));
            for (std::size_t i = 1; i < axis.size(); ++i) {
                axes.lineTo(toScene(axis[i]));
            }
            axisList.push_back(axis);
        }
    }
    // 1) Traversées calculées (aperçu fidèle de l'orientation des fils).
    QColor chordColor = tokens.canvasHandle;
    chordColor.setAlpha(110);
    QPen chordPen(chordColor);
    chordPen.setCosmetic(true);
    chordPen.setWidthF(1.0);
    auto* chordItem = scene_->addPath(chords, chordPen, Qt::NoBrush);
    chordItem->setZValue(96);
    baseItems_.append(chordItem);
    // 2) Squelette (axe de référence de chaque colonne).
    QPen axisPen(tokens.accent);
    axisPen.setCosmetic(true);
    axisPen.setWidthF(1.6);
    axisPen.setStyle(Qt::DashLine);
    auto* axisItem = scene_->addPath(axes, axisPen, Qt::NoBrush);
    axisItem->setZValue(97);
    axisItem->setToolTip(tr("Axe de référence des traversées"));
    baseItems_.append(axisItem);

    // 3) Guides : un trait indiquant la direction des fils et une poignée
    //    (glisser = déplacer l'ancre, clic droit = supprimer).
    const ObjectId targetId = obj.id;
    const std::uint64_t generation = documentGeneration_;
    for (std::size_t gi = 0; gi < params.guides.size(); ++gi) {
        const auto& guide = params.guides[gi];
        double direction = guide.angle.radians;
        if (!guide.absolute) {
            // Relatif : perpendiculaire à la tangente de l'axe le plus proche + écart.
            double best = std::numeric_limits<double>::max();
            double alpha = 0.0;
            for (const auto& axis : axisList) {
                for (std::size_t i = 0; i + 1 < axis.size(); ++i) {
                    const QPointF a = toScene(axis[i]);
                    const QPointF b = toScene(axis[i + 1]);
                    const QPointF p = toScene(guide.anchor);
                    const QPointF ab = b - a;
                    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
                    const double t =
                        len2 > 1e-12
                            ? std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) /
                                             len2,
                                         0.0, 1.0)
                            : 0.0;
                    const QPointF q = a + ab * t;
                    const double d = std::hypot(p.x() - q.x(), p.y() - q.y());
                    if (d < best) {
                        best = d;
                        alpha = std::atan2(-ab.y(), ab.x()); // scène Y bas -> repère modèle
                    }
                }
            }
            direction = alpha + std::numbers::pi / 2.0 + guide.angle.radians;
        }
        const QPointF c = toScene(guide.anchor);
        const double half = 5.0; // mm
        const QPointF d(std::cos(direction) * half, -std::sin(direction) * half);
        QPainterPath arrow;
        if (guide.path.size() >= 2) { // guide en courbe : le tracé lui-même
            arrow.moveTo(toScene(guide.path.front()));
            for (std::size_t i = 1; i < guide.path.size(); ++i) {
                arrow.lineTo(toScene(guide.path[i]));
            }
        } else {
            arrow.moveTo(c - d);
            arrow.lineTo(c + d);
        }
        QPen pen(tokens.warning);
        pen.setCosmetic(true);
        pen.setWidthF(2.4);
        auto* line = scene_->addPath(arrow, pen, Qt::NoBrush);
        line->setZValue(98);
        line->setToolTip(tr("Guide d'orientation #%1 (%2)")
                             .arg(gi + 1)
                             .arg(guide.absolute ? tr("absolu") : tr("relatif")));
        baseItems_.append(line);

        const auto onReleased = [this, targetId, generation, gi](QPointF released) {
            QTimer::singleShot(0, this, [this, targetId, generation, gi, released] {
                if (generation != documentGeneration_) {
                    return;
                }
                const auto* e = project_.findEmbroidery(targetId);
                const auto* s =
                    e != nullptr ? std::get_if<document::AutoSatinParams>(&e->params) : nullptr;
                if (s == nullptr || gi >= s->guides.size()) {
                    return;
                }
                const Vec2um to = toModel(released);
                if (s->guides[gi].anchor == to) {
                    return; // clic sans déplacement : aucun historique fantôme
                }
                auto next = *s;
                next.guides[gi].anchor = to;
                applyAutoSatinEdit(targetId, std::move(next),
                                   tr("Déplacer un guide d'orientation"));
            });
        };
        const auto onContextMenu = [this, targetId, generation, gi](QPoint globalPos) {
            QTimer::singleShot(0, this, [this, targetId, generation, gi, globalPos] {
                if (generation != documentGeneration_) {
                    return;
                }
                QMenu menu(this);
                QAction* remove = menu.addAction(tr("Supprimer le guide"));
                if (menu.exec(globalPos) == remove) {
                    removeAutoSatinGuide(targetId, static_cast<int>(gi));
                }
            });
        };
        auto* handle = new NodeHandleItem(c, onReleased, {}, onContextMenu);
        handle->setPen(QPen(tokens.warning, 1.5));
        handle->setBrush(QBrush(tokens.warning.lighter(160)));
        handle->setToolTip(tr("Glisser pour déplacer — clic droit pour supprimer"));
        scene_->addItem(handle);
        baseItems_.append(handle);
    }
}

// Satin automatique pour toutes les formes sélectionnées : un seul réglage, un satin par forme
// (contour brut de la région, comme pour une forme seule). Les formes sans colonne possible
// reçoivent un tatami si l'utilisateur l'accepte ; tout est un seul pas d'annulation.
void MainWindow::createAutoSatinForSelection(bool askParameters, bool confirm) {
    struct Candidate {
        const document::VectorObject* source{nullptr};
        std::optional<std::vector<geometry::PathSet>> restored;
        std::vector<geometry::PathSet> paths; // contours réellement utilisés (bruts si restaurés)
        bool ok{false};                       // au moins une méthode produit des colonnes
        std::optional<document::AutoSatinStrategy> strategy; // méthode retenue pour cette forme
    };
    std::vector<Candidate> candidates;
    document::AutoSatinParams params;
    params.strategy = document::kDefaultAutoSatinStrategy;
    ui_memory::creation::apply(params); // HP-UX-002 : dernier espacement mémorisé
    QString firstInfo;
    for (const ObjectId id : selectedObjectIds()) {
        const auto* current = project_.findObject(id);
        if (current == nullptr || current->paths.empty()) {
            continue;
        }
        Candidate c;
        c.source = current;
        c.restored = pristineSatinContour(*current);
        document::VectorObject copy = *current;
        if (c.restored) {
            copy.paths = *c.restored;
        }
        c.paths = copy.paths;
        AutoSatinPreview preview = previewAutoSatin(copy, params);
        c.strategy = params.strategy;
        if (preview.columns == 0) {
            // Le squelette refuse cette forme : on essaie les autres méthodes avant de la
            // déclarer impossible (la méthode retenue est montrée dans le dialogue).
            if (const auto alt = firstWorkingAutoSatinStrategy(copy, params)) {
                document::AutoSatinParams p = params;
                p.strategy = *alt;
                preview = previewAutoSatin(copy, p);
                c.strategy = *alt;
            }
        }
        c.ok = preview.columns > 0;
        if (c.ok && firstInfo.isEmpty()) {
            firstInfo = describeAutoSatinPreview(preview);
        }
        candidates.push_back(std::move(c));
    }
    if (candidates.empty()) {
        return;
    }
    const int okCount = static_cast<int>(std::count_if(candidates.begin(), candidates.end(),
                                                       [](const Candidate& c) { return c.ok; }));
    const int failCount = static_cast<int>(candidates.size()) - okCount;
    bool tatamiForFailures = false;
    if (failCount > 0) {
        const auto answer = QMessageBox::question(
            this, tr("Satin impossible pour certaines formes"),
            tr("%1 forme(s) sur %2 ne peuvent pas être cousues en satin (forme non "
               "exploitable).\n\n"
               "Leur donner un remplissage tatami à la place ? (Non : elles sont ignorées.)")
                .arg(failCount)
                .arg(candidates.size()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            return;
        }
        tatamiForFailures = answer == QMessageBox::Yes;
    }
    if (okCount > 0) {
        if (askParameters) {
            const QString info =
                tr("%1 forme(s) : mêmes réglages pour toutes.\n%2").arg(okCount).arg(firstInfo);
            const Candidate* first = nullptr;
            for (const Candidate& c : candidates) {
                if (c.ok) {
                    first = &c;
                    break;
                }
            }
            if (first != nullptr && first->strategy) {
                params.strategy = *first->strategy;
            }
            const auto describe = [this, first](const document::AutoSatinParams& p) {
                document::VectorObject v;
                v.paths = first->paths;
                return describeAutoSatinPreview(previewAutoSatin(v, p));
            };
            const auto compare = [this, first](const document::AutoSatinParams& p) {
                return compareAutoSatinStrategies(first->paths, p);
            };
            if (!askAutoSatinParams(this, info, params, describe, compare)) {
                return;
            }
            // La méthode choisie vaut pour toutes les formes où elle fonctionne ; sinon la
            // première qui fonctionne pour cette forme (jamais un satin sans colonne).
            for (Candidate& c : candidates) {
                if (!c.ok) {
                    continue;
                }
                document::VectorObject v;
                v.paths = c.paths;
                c.strategy = firstWorkingAutoSatinStrategy(v, params);
                c.ok = c.strategy.has_value();
            }
            ui_memory::creation::remember(document::StitchParams{params});
        } else if (confirm) {
            const auto answer = QMessageBox::question(
                this, tr("Convertir en satin"),
                tr("Créer un satin pour %1 forme(s) ? (annulable en une étape)").arg(okCount));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
    } else if (!tatamiForFailures) {
        return;
    }

    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Créer un satin pour %1 formes").arg(candidates.size()).toStdString());
    for (const Candidate& c : candidates) {
        if (!c.ok && !tatamiForFailures) {
            continue;
        }
        document::EmbroideryObject object;
        object.id = project_.object_ids.next();
        object.source_vector = c.source->id;
        object.rgb = c.source->rgb;
        object.intent = document::EmbroideryIntent::ForcedUserChoice;
        if (c.ok) {
            object.name =
                tr("Satin de %1").arg(QString::fromStdString(c.source->name)).toStdString();
            document::AutoSatinParams perShape = params;
            perShape.strategy = c.strategy.value_or(params.strategy);
            object.params = perShape;
            if (c.restored) {
                group->add(std::make_unique<commands::SetVectorPathsCommand>(
                    c.source->id, *c.restored, "Contour brut de la région"));
            }
        } else {
            object.name =
                tr("Tatami de %1").arg(QString::fromStdString(c.source->name)).toStdString();
            object.params = document::TatamiParams{};
        }
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
    }
    if (group->empty()) {
        return;
    }
    undoStack_.execute(std::move(group), project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Satin créé pour %1 forme(s)%2.")
                                 .arg(okCount)
                                 .arg(tatamiForFailures && failCount > 0
                                          ? tr(", tatami pour %1 autre(s)").arg(failCount)
                                          : QString()),
                             12000);
}

} // namespace openstitch::desktop
