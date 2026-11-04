// SPDX-License-Identifier: Apache-2.0
// Export de débogage d'un objet de broderie.
// Membres de MainWindow extraits de main_window.cpp (HP-QA-001) : déplacement pur.
#include "main_window.hpp"
#include "simulation_nav_bar.hpp"
#include "stitch_type_catalog.hpp"
#include "thread_strip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_language.hpp"
#include "app_settings.hpp"
#include "app_theme.hpp"
#include "autodigitize_presets.hpp"
#include "autosave.hpp"
#include "brightness_dialog.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "command_palette_dialog.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "general_preferences_dialog.hpp"
#include "generation_options_dialog.hpp"
#include "help_dialogs.hpp"
#include "help_manual.hpp"
#include "import_dialog.hpp"
#include "length_units.hpp"
#include "machine_export_dialog.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "order_list_widget.hpp"
#include "responsive.hpp"
#include "vector_tooltip.hpp"

#include "calque_panel.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_settings_dialog.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/hoop_presets.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "properties_panel.hpp"
#include "recent_files.hpp"
#include "ruler.hpp"
#include "satin_guide_item.hpp"
#include "selection_hit_test.hpp"
#include "settings_dialog.hpp"
#include "ui_icons.hpp"
#include "ui_memory.hpp"
#include "ui_swatch.hpp"
#include "workflow_panel.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "main_window_internal.hpp"

namespace openstitch::desktop {

QString fmtUm(Micrometers v) {
    return QStringLiteral("%1 µm (%2 mm)").arg(v.value).arg(to_millimeters(v).value, 0, 'f', 3);
}
QString fmtVec(Vec2um v) {
    return QStringLiteral("(%1, %2) µm").arg(v.x.value).arg(v.y.value);
}
QString fmtOptVec(const std::optional<Vec2um>& v) {
    return v ? fmtVec(*v) : QStringLiteral("(absent)");
}
QString fmtBool(bool b) {
    return b ? QStringLiteral("oui") : QStringLiteral("non");
}
QString fmtNodeType(geometry::NodeType t) {
    return t == geometry::NodeType::Smooth ? QStringLiteral("Lisse") : QStringLiteral("Coin");
}
QString fmtShortStitch(document::SatinShortStitch v) {
    switch (v) {
    case document::SatinShortStitch::Disabled:
        return QStringLiteral("Désactivés");
    case document::SatinShortStitch::RemoveAndRedistribute:
        return QStringLiteral("Retirer/redistribuer");
    case document::SatinShortStitch::SingleInset:
        return QStringLiteral("Inset simple");
    case document::SatinShortStitch::MultiLevelInset:
        return QStringLiteral("Inset multi-niveaux");
    }
    return QStringLiteral("?");
}
QString fmtSplit(document::SatinSplit v) {
    switch (v) {
    case document::SatinSplit::Disabled:
        return QStringLiteral("Désactivé");
    case document::SatinSplit::Simple:
        return QStringLiteral("Simple");
    case document::SatinSplit::Staggered:
        return QStringLiteral("Décalé");
    case document::SatinSplit::DeterministicJitter:
        return QStringLiteral("Jitter déterministe");
    }
    return QStringLiteral("?");
}
QString fmtCap(document::SatinCap v) {
    switch (v) {
    case document::SatinCap::Flat:
        return QStringLiteral("Plat");
    case document::SatinCap::Rounded:
        return QStringLiteral("Arrondi");
    case document::SatinCap::Tapered:
        return QStringLiteral("Effilé");
    case document::SatinCap::Automatic:
        return QStringLiteral("Auto");
    }
    return QStringLiteral("?");
}
QString fmtLock(document::SatinLock v) {
    switch (v) {
    case document::SatinLock::None:
        return QStringLiteral("Aucun");
    case document::SatinLock::BackAndForth:
        return QStringLiteral("Aller-retour");
    case document::SatinLock::Triangle:
        return QStringLiteral("Triangle");
    case document::SatinLock::MicroZigzag:
        return QStringLiteral("Micro-zigzag");
    }
    return QStringLiteral("?");
}
QString fmtStitchPointType(document::StitchPointType v) {
    return v == document::StitchPointType::Stitch ? QStringLiteral("Stitch")
                                                  : QStringLiteral("Jump");
}
QString fmtCommandType(stitch::CommandType v) {
    switch (v) {
    case stitch::CommandType::Stitch:
        return QStringLiteral("Stitch");
    case stitch::CommandType::Jump:
        return QStringLiteral("Jump");
    case stitch::CommandType::Trim:
        return QStringLiteral("Trim");
    case stitch::CommandType::ColorChange:
        return QStringLiteral("ColorChange");
    case stitch::CommandType::Stop:
        return QStringLiteral("Stop");
    case stitch::CommandType::End:
        return QStringLiteral("End");
    }
    return QStringLiteral("?");
}
QString fmtPass(stitch::StitchPass v) {
    switch (v) {
    case stitch::StitchPass::Underlay:
        return QStringLiteral("Underlay");
    case stitch::StitchPass::TopStitch:
        return QStringLiteral("TopStitch");
    case stitch::StitchPass::Travel:
        return QStringLiteral("Travel");
    case stitch::StitchPass::Lock:
        return QStringLiteral("Lock");
    case stitch::StitchPass::Manual:
        return QStringLiteral("Manual");
    }
    return QStringLiteral("?");
}
QString fmtEditState(stitch_generation::ObjectEditState v) {
    switch (v) {
    case stitch_generation::ObjectEditState::Clean:
        return QStringLiteral("Clean");
    case stitch_generation::ObjectEditState::ManuallyEdited:
        return QStringLiteral("ManuallyEdited");
    case stitch_generation::ObjectEditState::Dirty:
        return QStringLiteral("Dirty");
    }
    return QStringLiteral("?");
}
QString fmtSeverity(stitch_analysis::Severity v) {
    switch (v) {
    case stitch_analysis::Severity::Info:
        return QStringLiteral("Info");
    case stitch_analysis::Severity::Warning:
        return QStringLiteral("Warning");
    case stitch_analysis::Severity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("?");
}

void dumpPath(QStringList& out, const geometry::Path& path, const QString& label) {
    out << QStringLiteral("  %1 : %2, %3 noeud(s)")
               .arg(label, path.closed ? QStringLiteral("fermé") : QStringLiteral("ouvert"))
               .arg(path.nodes.size());
    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
        const auto& n = path.nodes[i];
        out << QStringLiteral("    [%1] pos=%2  type=%3  tan_in=%4  tan_out=%5")
                   .arg(i)
                   .arg(fmtVec(n.pos), fmtNodeType(n.type), fmtOptVec(n.tan_in),
                        fmtOptVec(n.tan_out));
    }
}

QString MainWindow::buildDebugDump(ObjectId embroideryId) const {
    QStringList out;
    const auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr) {
        return tr("Objet de broderie introuvable (id %1).").arg(embroideryId.value);
    }

    out << QStringLiteral("=== OpenStitch — export debug ===")
        << QStringLiteral("Généré : %1")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        << QStringLiteral("Application : %1 %2")
               .arg(QString::fromUtf8(kAppName), QString::fromUtf8(kAppVersion))
        << QString();

    out << QStringLiteral("--- Identité (objet de broderie) ---")
        << QStringLiteral("ObjectId : %1").arg(emb->id.value)
        << QStringLiteral("Nom : \"%1\"").arg(QString::fromStdString(emb->name))
        << QStringLiteral("Type de points : %1")
               .arg(emb->is_auto_satin()    ? QStringLiteral("Satin (squelette)")
                    : emb->is_satin()       ? QStringLiteral("Satin (rails)")
                    : emb->is_tatami()      ? QStringLiteral("Tatami")
                    : emb->is_directional() ? QStringLiteral("Remplissage directionnel")
                                            : QStringLiteral("Contour (running stitch)"))
        << QStringLiteral("Visible : %1").arg(fmtBool(emb->visible))
        << QStringLiteral("Ordre figé : %1").arg(fmtBool(emb->locked))
        << QStringLiteral("Couleur RGB : (%1, %2, %3)  #%4")
               .arg(emb->rgb[0])
               .arg(emb->rgb[1])
               .arg(emb->rgb[2])
               .arg(QStringLiteral("%1%2%3")
                        .arg(emb->rgb[0], 2, 16, QChar('0'))
                        .arg(emb->rgb[1], 2, 16, QChar('0'))
                        .arg(emb->rgb[2], 2, 16, QChar('0'))
                        .toUpper())
        << QStringLiteral("source_vector (ObjectId) : %1").arg(emb->source_vector.value)
        << QString();

    out << QStringLiteral("--- État d'édition (retouches manuelles, Lot 8) ---")
        << QStringLiteral("edited_fingerprint : %1").arg(emb->edited_fingerprint)
        << QStringLiteral("edited_point_count : %1").arg(emb->edited_point_count)
        << QStringLiteral("État dérivé : %1").arg(fmtEditState(editStateOf(emb->id)))
        << QStringLiteral("Retouches (overrides) : %1").arg(emb->overrides.size());
    if (emb->overrides.empty()) {
        out << QStringLiteral("  (aucune)");
    } else {
        for (const auto& ov : emb->overrides) {
            out << QStringLiteral("  base_index=%1  moved_to=%2  forced_type=%3  trim_after=%4")
                       .arg(ov.base_index)
                       .arg(fmtOptVec(ov.moved_to))
                       .arg(ov.forced_type ? fmtStitchPointType(*ov.forced_type)
                                           : QStringLiteral("(absent)"))
                       .arg(fmtBool(ov.trim_after));
        }
    }
    out << QString();

    out << QStringLiteral("--- Paramètres de couture ---");
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                out << QStringLiteral("Variant : RunningStitchParams")
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("min_length : %1").arg(fmtUm(p.min_length))
                    << QStringLiteral("repeats : %1").arg(p.repeats);
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                out << QStringLiteral("Variant : TatamiParams")
                    << QStringLiteral("angle : %1 rad (%2°)")
                           .arg(p.angle.radians, 0, 'f', 4)
                           .arg(p.angle.radians * 180.0 / std::numbers::pi, 0, 'f', 2)
                    << QStringLiteral("row_spacing : %1").arg(fmtUm(p.row_spacing))
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("inset : %1").arg(fmtUm(p.inset))
                    << QStringLiteral("stagger : %1").arg(p.stagger)
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_parallel : %1").arg(fmtBool(p.underlay_parallel))
                    << QStringLiteral("underlay_inset : %1").arg(fmtUm(p.underlay_inset))
                    << QStringLiteral("underlay_spacing : %1").arg(fmtUm(p.underlay_spacing))
                    << QStringLiteral("hidden_underpath : %1").arg(fmtBool(p.hidden_underpath))
                    << QStringLiteral("entry_point : %1").arg(fmtOptVec(p.entry_point));
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                out << QStringLiteral("Variant : DirectionalFillParams")
                    << QStringLiteral("row_spacing : %1").arg(fmtUm(p.row_spacing))
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("edge_weight : %1").arg(p.edge_weight, 0, 'f', 2)
                    << QStringLiteral("inset : %1").arg(fmtUm(p.inset))
                    << QStringLiteral("stagger : %1").arg(p.stagger)
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_parallel : %1").arg(fmtBool(p.underlay_parallel))
                    << QStringLiteral("underlay_inset : %1").arg(fmtUm(p.underlay_inset))
                    << QStringLiteral("underlay_spacing : %1").arg(fmtUm(p.underlay_spacing))
                    << QStringLiteral("hidden_underpath : %1").arg(fmtBool(p.hidden_underpath))
                    << QStringLiteral("sector_overlap : %1").arg(fmtUm(p.sector_overlap))
                    << QStringLiteral("handmade : %1 (%2 %)")
                           .arg(fmtBool(p.handmade))
                           .arg(p.handmade_intensity)
                    << QStringLiteral("seed : %1").arg(p.seed) << QString();
                for (std::size_t i = 0; i < p.guides.size(); ++i) {
                    dumpPath(out, p.guides[i], QStringLiteral("guide[%1]").arg(i));
                }
                for (std::size_t i = 0; i < p.break_lines.size(); ++i) {
                    dumpPath(out, p.break_lines[i], QStringLiteral("break_line[%1]").arg(i));
                }
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                out << QStringLiteral("Variant : AutoSatinParams")
                    << QStringLiteral("spacing : %1").arg(fmtUm(p.spacing))
                    << QStringLiteral("split_stitch : %1").arg(fmtSplit(p.split_stitch))
                    << QStringLiteral("split_threshold (Lmax) : %1").arg(fmtUm(p.split_threshold))
                    << QStringLiteral("split_length (y) : %1").arg(fmtUm(p.split_length))
                    << QStringLiteral("short_stitch : %1").arg(fmtShortStitch(p.short_stitch))
                    << QStringLiteral("pull_compensation : %1").arg(fmtUm(p.pull_compensation))
                    << QStringLiteral("center_underlay : %1").arg(fmtBool(p.center_underlay))
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_zigzag : %1").arg(fmtBool(p.underlay_zigzag))
                    << QStringLiteral("pull_left : %1").arg(fmtUm(p.pull_left))
                    << QStringLiteral("pull_right : %1").arg(fmtUm(p.pull_right))
                    << QStringLiteral("push_start : %1").arg(fmtUm(p.push_start))
                    << QStringLiteral("push_end : %1").arg(fmtUm(p.push_end))
                    << QStringLiteral("cap_start : %1").arg(fmtCap(p.cap_start))
                    << QStringLiteral("cap_end : %1").arg(fmtCap(p.cap_end))
                    << QStringLiteral("lock_start : %1").arg(fmtLock(p.lock_start))
                    << QStringLiteral("lock_end : %1").arg(fmtLock(p.lock_end))
                    << QStringLiteral("guides : %1").arg(p.guides.size());
                for (std::size_t i = 0; i < p.guides.size(); ++i) {
                    const auto& g = p.guides[i];
                    out << QStringLiteral("guide[%1] : ancre (%2, %3) µm, angle %4 rad (%5)")
                               .arg(i)
                               .arg(g.anchor.x.value)
                               .arg(g.anchor.y.value)
                               .arg(g.angle.radians)
                               .arg(g.absolute ? QStringLiteral("absolu")
                                               : QStringLiteral("relatif"));
                }
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                out << QStringLiteral("Variant : SatinParams")
                    << QStringLiteral("density : %1").arg(fmtUm(p.density))
                    << QStringLiteral("pull_compensation : %1").arg(fmtUm(p.pull_compensation))
                    << QStringLiteral("center_underlay : %1").arg(fmtBool(p.center_underlay))
                    << QStringLiteral("max_width : %1").arg(fmtUm(p.max_width))
                    << QStringLiteral("max_width_hard : %1").arg(fmtUm(p.max_width_hard))
                    << QStringLiteral("short_stitch : %1").arg(fmtShortStitch(p.short_stitch))
                    << QStringLiteral("split_stitch : %1").arg(fmtSplit(p.split_stitch))
                    << QStringLiteral("cap_start : %1").arg(fmtCap(p.cap_start))
                    << QStringLiteral("cap_end : %1").arg(fmtCap(p.cap_end))
                    << QStringLiteral("max_stitch_length : %1").arg(fmtUm(p.max_stitch_length))
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_zigzag : %1").arg(fmtBool(p.underlay_zigzag))
                    << QStringLiteral("pull_left : %1").arg(fmtUm(p.pull_left))
                    << QStringLiteral("pull_right : %1").arg(fmtUm(p.pull_right))
                    << QStringLiteral("push_start : %1").arg(fmtUm(p.push_start))
                    << QStringLiteral("push_end : %1").arg(fmtUm(p.push_end))
                    << QStringLiteral("lock_start : %1").arg(fmtLock(p.lock_start))
                    << QStringLiteral("lock_end : %1").arg(fmtLock(p.lock_end))
                    << QStringLiteral("lock_length : %1").arg(fmtUm(p.lock_length))
                    << QStringLiteral("lock_passes : %1").arg(p.lock_passes)
                    << QStringLiteral("entry_point : %1").arg(fmtOptVec(p.entry_point))
                    << QStringLiteral("exit_point : %1").arg(fmtOptVec(p.exit_point)) << QString();

                out << QStringLiteral("--- Rails et barreaux satin ---");
                dumpPath(out, p.rail_a, QStringLiteral("rail_a"));
                dumpPath(out, p.rail_b, QStringLiteral("rail_b"));
                out << QStringLiteral("rungs (%1) :").arg(p.rungs.size());
                if (p.rungs.empty()) {
                    out << QStringLiteral("  (aucun — fallback fill_satin sans barreaux)");
                } else {
                    for (std::size_t i = 0; i < p.rungs.size(); ++i) {
                        const auto& r = p.rungs[i];
                        out << QStringLiteral("  [%1] a=%2  b=%3  link_id=%4")
                                   .arg(i)
                                   .arg(fmtVec(r.a), fmtVec(r.b),
                                        r.link_id ? QString::number(*r.link_id)
                                                  : QStringLiteral("(absent)"));
                    }
                }
                if (p.topology) {
                    out << QStringLiteral("topology : section_index=%1  section_count=%2  "
                                          "start_junction=%3  end_junction=%4")
                               .arg(p.topology->section_index)
                               .arg(p.topology->section_count)
                               .arg(p.topology->start_junction
                                        ? QString::number(*p.topology->start_junction)
                                        : QStringLiteral("(absent)"))
                               .arg(p.topology->end_junction
                                        ? QString::number(*p.topology->end_junction)
                                        : QStringLiteral("(absent)"));
                } else {
                    out << QStringLiteral("topology : (absent — colonne indépendante/legacy)");
                }
            }
        },
        emb->params);
    out << QString();

    out << QStringLiteral("--- Objet vectoriel source ---");
    if (const auto* vec = project_.findObject(emb->source_vector)) {
        out << QStringLiteral("ObjectId : %1").arg(vec->id.value)
            << QStringLiteral("Nom : \"%1\"").arg(QString::fromStdString(vec->name))
            << QStringLiteral("source_region : %1")
                   .arg(vec->source_region ? QString::number(vec->source_region->value)
                                           : QStringLiteral("(absent)"))
            << QStringLiteral("Couleur RGB : (%1, %2, %3)")
                   .arg(vec->rgb[0])
                   .arg(vec->rgb[1])
                   .arg(vec->rgb[2])
            << QStringLiteral("Visible : %1").arg(fmtBool(vec->visible))
            << QStringLiteral("Nombre de morceaux (paths) : %1").arg(vec->paths.size());
        for (std::size_t s = 0; s < vec->paths.size(); ++s) {
            const auto& set = vec->paths[s];
            out << QStringLiteral(" Morceau %1 :").arg(s);
            dumpPath(out, set.outer, QStringLiteral("contour extérieur"));
            for (std::size_t h = 0; h < set.holes.size(); ++h) {
                dumpPath(out, set.holes[h], QStringLiteral("trou %1").arg(h));
            }
        }
    } else {
        out << QStringLiteral("(introuvable — ObjectId %1)").arg(emb->source_vector.value);
    }
    out << QString();

    out << QStringLiteral("--- Séquence générée (portion de cet objet) ---");
    if (!sequence_) {
        out << QStringLiteral("Aucune séquence générée pour l'instant (document sans image/objets, "
                              "ou génération en échec).");
    } else {
        const auto raw = stitch_generation::raw_slice(*sequence_, emb->id);
        out << QStringLiteral("Total commandes (raw_slice) : %1").arg(raw.size());
        if (raw.empty()) {
            out << QStringLiteral(
                "  (cet objet ne produit aucune commande — vérifier params/rails)");
        } else {
            const stitch::StitchSequence subSeq{raw};
            const auto stats = stitch::compute_stats(subSeq);
            out << QStringLiteral("  Stitch=%1  Jump=%2  Trim=%3  ColorChange=%4")
                       .arg(stats.stitches)
                       .arg(stats.jumps)
                       .arg(stats.trims)
                       .arg(stats.color_changes)
                << QStringLiteral("  Longueur de fil cousu : %1 mm")
                       .arg(stats.thread_length_um / 1000.0, 0, 'f', 2)
                << QStringLiteral("  Boîte englobante : min=%1  max=%2")
                       .arg(fmtVec(stats.bounds.min), fmtVec(stats.bounds.max));

            std::array<std::size_t, 5> perPass{}; // Underlay,TopStitch,Travel,Lock,Manual
            for (const auto& cmd : raw) {
                ++perPass[static_cast<std::size_t>(cmd.pass)];
            }
            out << QStringLiteral("  Par passe : Underlay=%1  TopStitch=%2  Travel=%3  Lock=%4  "
                                  "Manual=%5")
                       .arg(perPass[0])
                       .arg(perPass[1])
                       .arg(perPass[2])
                       .arg(perPass[3])
                       .arg(perPass[4]);

            out << QStringLiteral("Détail des commandes :");
            for (std::size_t i = 0; i < raw.size(); ++i) {
                const auto& cmd = raw[i];
                out << QStringLiteral("  [%1] pass=%2  type=%3  pos=%4")
                           .arg(i)
                           .arg(fmtPass(cmd.pass), fmtCommandType(cmd.type), fmtVec(cmd.pos));
            }
        }
    }
    out << QString();

    out << QStringLiteral("--- Analyse (stitch_analysis) ---");
    if (!sequence_) {
        out << QStringLiteral("(aucune séquence à analyser)");
    } else {
        const auto findings = stitch_analysis::analyze_project(project_, *sequence_).findings;
        std::size_t count = 0;
        for (const auto& f : findings) {
            if (f.object != emb->id) {
                continue;
            }
            ++count;
            out << QStringLiteral("  [%1] %2 : %3  (%4)")
                       .arg(fmtSeverity(f.severity), QString::fromStdString(f.category),
                            QString::fromStdString(f.message), fmtVec(f.location));
        }
        if (count == 0) {
            out << QStringLiteral("  Aucun problème détecté pour cet objet.");
        }
    }

    return out.join(QChar('\n'));
}

void MainWindow::showDebugDump(ObjectId embroideryId) {
    const QString text = buildDebugDump(embroideryId);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Données de débogage — objet %1").arg(embroideryId.value));
    dialog.resize(760, 640);
    auto* layout = new QVBoxLayout(&dialog);

    auto* editor = new QPlainTextEdit(&dialog);
    editor->setReadOnly(true);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    editor->setFont(mono);
    editor->setPlainText(text);
    layout->addWidget(editor);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* copyBtn = buttons->addButton(tr("Copier"), QDialogButtonBox::ActionRole);
    auto* saveBtn = buttons->addButton(tr("Enregistrer sous…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(copyBtn, &QPushButton::clicked, &dialog,
            [text] { QGuiApplication::clipboard()->setText(text); });
    connect(saveBtn, &QPushButton::clicked, &dialog, [this, &dialog, text] {
        const QString path =
            QFileDialog::getSaveFileName(&dialog, tr("Enregistrer les données de débogage"),
                                         QStringLiteral("debug.txt"), tr("Texte (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(text.toUtf8());
        } else {
            QMessageBox::warning(this, tr("Écriture impossible"),
                                 tr("Le fichier n'a pas pu être écrit. Vérifiez le dossier "
                                    "choisi et vos droits d'écriture."));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

} // namespace openstitch::desktop
