// SPDX-License-Identifier: Apache-2.0
#include "machine_export_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace openstitch::desktop {

namespace {

constexpr const char* kSettingsFormat = "export/format";
constexpr const char* kSettingsTrims = "export/trims";
constexpr const char* kSettingsStops = "export/stops";
constexpr const char* kSettingsColors = "export/colorChanges";
constexpr const char* kSettingsCompanion = "export/colorCompanion";
constexpr const char* kSettingsCompanionKind = "export/colorCompanionKind";
constexpr const char* kSettingsReduceTrims = "export/reduceTrims";
constexpr const char* kSettingsSameColour = "export/reduceTrimsSameColour";
constexpr const char* kSettingsMaxBridge = "export/reduceTrimsMaxMm";
constexpr const char* kSettingsBetween = "export/routesBetween";
constexpr const char* kSettingsWithin = "export/routesWithin";
constexpr const char* kSettingsUnderlay = "export/routesUnderlay";
constexpr const char* kSettingsRegions = "export/routesRegions";

} // namespace

MachineExportDialog::MachineExportDialog(SummaryFn summarize, const QString& initialFormatId,
                                         QWidget* parent)
    : QDialog(parent), summarize_(std::move(summarize)) {
    setWindowTitle(tr("Exporter une broderie machine"));
    setObjectName(QStringLiteral("dialog_machineExport"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    formatBox_ = new QComboBox(this);
    formatBox_->setObjectName(QStringLiteral("combo_exportFormat"));
    for (const auto& f : formats::registered_formats()) {
        if (!f.can_write || f.encode_ex == nullptr) {
            continue;
        }
        QString exts;
        for (const auto& e : f.extensions) {
            exts += (exts.isEmpty() ? QString() : QStringLiteral(", ")) + QStringLiteral(".") +
                    QString::fromStdString(e);
        }
        formatBox_->addItem(tr("%1 (%2)").arg(QString::fromStdString(f.display_name), exts),
                            QString::fromStdString(f.id));
    }
    form->addRow(tr("Format :"), formatBox_);

    trims_ = new QCheckBox(tr("Écrire les coupes de fil (réglage de ce fichier)"), this);
    trims_->setObjectName(QStringLiteral("check_exportTrims"));
    trims_->setToolTip(
        tr("Réglage de CE fichier d'export. Le seuil de coupe automatique est un réglage du "
           "projet (Broderie ▸ Options de génération) : il décide où une coupe existe, cette "
           "case décide si elle est écrite dans le fichier."));
    stops_ = new QComboBox(this);
    stops_->setObjectName(QStringLiteral("combo_exportStops"));
    stops_->addItem(tr("Natif du format"), static_cast<int>(formats::StopMode::Native));
    stops_->addItem(tr("Comme un changement de couleur"),
                    static_cast<int>(formats::StopMode::AsColorChange));
    stops_->addItem(tr("Ignorés"), static_cast<int>(formats::StopMode::Drop));
    colorChanges_ = new QCheckBox(tr("Écrire les changements de couleur"), this);
    colorChanges_->setObjectName(QStringLiteral("check_exportColorChanges"));
    form->addRow(QString(), trims_);
    form->addRow(tr("Arrêts machine :"), stops_);
    form->addRow(QString(), colorChanges_);

    // Le DST n'emporte que des arrêts couleur et l'EXP aucune couleur : un fichier compagnon voisin
    // (même nom, extension .inf ou .csv) les conserve pour la réimportation et l'atelier.
    companion_ = new QCheckBox(tr("Écrire aussi le fichier de couleurs à côté"), this);
    companion_->setObjectName(QStringLiteral("check_exportColorCompanion"));
    companion_->setToolTip(
        tr("Écrit la liste des couleurs de fil (un fil par bloc de couleur) dans un second "
           "fichier, même nom, à côté du motif. OpenStitch le relit à l'import du DST ou de "
           "l'EXP. Le .csv s'ouvre dans un tableur ; la structure du .inf n'est pas validée "
           "par un logiciel tiers."));
    companionKind_ = new QComboBox(this);
    companionKind_->setObjectName(QStringLiteral("combo_exportColorCompanionKind"));
    companionKind_->addItem(tr("Tableur lisible (.csv)"),
                            static_cast<int>(formats::CompanionKind::Csv));
    companionKind_->addItem(tr("Binaire compact (.inf)"),
                            static_cast<int>(formats::CompanionKind::Inf));
    form->addRow(QString(), companion_);
    form->addRow(tr("Format du fichier de couleurs :"), companionKind_);

    // Point de départ de la machine : ne change que le saut d'approche initial, jamais l'ordre
    // des régions (utile pour broder en plusieurs étapes, en repositionnant le cadre).
    startMode_ = new QComboBox(this);
    startMode_->setObjectName(QStringLiteral("combo_exportStartMode"));
    startMode_->addItem(tr("Premier point du motif"),
                        static_cast<int>(formats::StartMode::FirstPoint));
    startMode_->addItem(tr("Centre de la broderie"), static_cast<int>(formats::StartMode::Center));
    startMode_->addItem(tr("Point choisi (X / Y)"), static_cast<int>(formats::StartMode::Custom));
    startMode_->setToolTip(tr("Position de l'aiguille au démarrage de la machine. Seul le premier "
                              "déplacement change ; l'ordre et la position des points sont "
                              "conservés. X / Y en mm dans le repère du document (0 = centre du "
                              "canevas, Y vers le haut)."));
    const auto makeStartSpin = [this](const char* name, const QString& prefix) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setObjectName(QString::fromLatin1(name));
        spin->setRange(-1000.0, 1000.0);
        spin->setDecimals(1);
        spin->setSingleStep(1.0);
        spin->setPrefix(prefix);
        spin->setSuffix(tr(" mm"));
        return spin;
    };
    startX_ = makeStartSpin("spin_exportStartX", tr("X "));
    startY_ = makeStartSpin("spin_exportStartY", tr("Y "));
    auto* startPoint = new QWidget(this);
    auto* startRow = new QHBoxLayout(startPoint);
    startRow->setContentsMargins(0, 0, 0, 0);
    startRow->addWidget(startX_);
    startRow->addWidget(startY_);
    form->addRow(tr("Point de départ :"), startMode_);
    form->addRow(QString(), startPoint);

    // HP-SEQ-006 : optimisation des coupes demandée pour CET export, indépendante du format
    // (le DST en profite aussi). Appliquée à la toute fin, le projet n'est pas modifié.
    reduceTrims_ = new QCheckBox(
        tr("Optimiser les trajets (réglage de ce fichier : moins de coupes et de sauts)"), this);
    reduceTrims_->setObjectName(QStringLiteral("check_exportReduceTrims"));
    reduceTrims_->setToolTip(
        tr("Appliqué à l'export seulement, tout à la fin : le projet n'est pas modifié et le "
           "remplissage visible n'est jamais changé. Une coupe n'est jamais supprimée avant un "
           "changement de fil. Par défaut l'ordre des objets n'est pas modifié."));
    between_ = new QCheckBox(
        tr("Entre les objets : pont caché si le trajet est recouvert plus tard"), this);
    between_->setObjectName(QStringLiteral("check_exportRoutesBetween"));
    within_ = new QCheckBox(tr("Dans un objet : liens sous-couche ↔ remplissage cachés"), this);
    within_->setObjectName(QStringLiteral("check_exportRoutesWithin"));
    underlay_ = new QCheckBox(tr("Sous-couche : ordre et sens des passes (invisible)"), this);
    underlay_->setObjectName(QStringLiteral("check_exportRoutesUnderlay"));
    regions_ = new QCheckBox(
        tr("Mode avancé : réordonner les régions d'une même couleur (plus invasif)"), this);
    regions_->setObjectName(QStringLiteral("check_exportRoutesRegions"));
    regions_->setToolTip(
        tr("Change l'ordre de couture des objets d'une même couleur pour raccourcir les "
           "déplacements. L'ordre de deux objets qui se touchent ou se recouvrent est conservé "
           "(c'est lui qui décide de ce qui est cousu par-dessus), les objets verrouillés restent "
           "en place et aucun objet n'est modifié ni retourné. À vérifier à l'aperçu : l'ordre de "
           "couture influe aussi sur le tirage du tissu."));
    sameColour_ = new QCheckBox(tr("Accepter aussi une surface déjà cousue du même fil"), this);
    sameColour_->setObjectName(QStringLiteral("check_exportReduceTrimsSameColour"));
    sameColour_->setToolTip(tr("Le fil de liaison se confond avec le fil déjà cousu ; un léger "
                               "relief reste possible, surtout sur un satin."));
    maxBridge_ = new QDoubleSpinBox(this);
    maxBridge_->setObjectName(QStringLiteral("spin_exportReduceTrimsMax"));
    maxBridge_->setRange(1.0, 100.0);
    maxBridge_->setDecimals(1);
    maxBridge_->setSingleStep(1.0);
    maxBridge_->setSuffix(tr(" mm"));
    form->addRow(QString(), reduceTrims_);
    form->addRow(QString(), between_);
    form->addRow(QString(), within_);
    form->addRow(QString(), underlay_);
    form->addRow(QString(), regions_);
    form->addRow(QString(), sameColour_);
    form->addRow(tr("Longueur maximale d'un pont :"), maxBridge_);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("label_exportSummary"));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(summary_);

    // Problèmes détectés : montrés ici, sans fermer le dialogue (HP-UX-032).
    problemsView_ = new QPlainTextEdit(this);
    problemsView_->setObjectName(QStringLiteral("text_exportProblems"));
    problemsView_->setReadOnly(true);
    problemsView_->setAccessibleName(tr("Problèmes détectés avant l'export"));
    problemsView_->setMaximumHeight(140);
    problemsView_->hide();
    layout->addWidget(problemsView_);

    auto* buttons = new QDialogButtonBox(this);
    chooseBtn_ = buttons->addButton(tr("Choisir le fichier…"), QDialogButtonBox::AcceptRole);
    chooseBtn_->setObjectName(QStringLiteral("action_exportChoose"));
    saveBtn_ =
        buttons->addButton(tr("Enregistrer le projet d'abord"), QDialogButtonBox::ActionRole);
    saveBtn_->setObjectName(QStringLiteral("action_exportSaveProject"));
    saveBtn_->setToolTip(tr("Enregistre le projet (.osp) sans quitter cette fenêtre : le format "
                            "machine ne conserve pas les objets éditables."));
    problemsBtn_ = buttons->addButton(tr("Voir les problèmes"), QDialogButtonBox::ActionRole);
    problemsBtn_->setObjectName(QStringLiteral("action_exportViewProblems"));
    openAnalysisBtn_ = buttons->addButton(tr("Ouvrir l'analyse"), QDialogButtonBox::ActionRole);
    openAnalysisBtn_->setObjectName(QStringLiteral("action_exportOpenAnalysis"));
    openAnalysisBtn_->setToolTip(
        tr("Ferme cette fenêtre et ouvre le panneau Analyse pour corriger les problèmes."));
    cancelBtn_ = buttons->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveBtn_, &QPushButton::clicked, this, &MachineExportDialog::saveProjectRequested);
    connect(problemsBtn_, &QPushButton::clicked, this, [this] {
        const bool show = problemsView_->isHidden();
        problemsView_->setVisible(show);
        problemsBtn_->setText(show ? tr("Masquer les problèmes") : tr("Voir les problèmes"));
    });
    connect(openAnalysisBtn_, &QPushButton::clicked, this, [this] {
        viewProblems_ = true;
        reject();
    });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(kPreviewDelayMs);
    connect(refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });

    // Réglages mémorisés (par poste).
    QSettings settings;
    const QString wanted = initialFormatId.isEmpty()
                               ? settings.value(kSettingsFormat, "dst").toString()
                               : initialFormatId;
    if (const int idx = formatBox_->findData(wanted); idx >= 0) {
        formatBox_->setCurrentIndex(idx);
    }
    companion_->setChecked(settings.value(kSettingsCompanion, false).toBool());
    if (const int idx = companionKind_->findData(
            settings.value(kSettingsCompanionKind, static_cast<int>(formats::CompanionKind::Csv))
                .toInt());
        idx >= 0) {
        companionKind_->setCurrentIndex(idx);
    }
    trims_->setChecked(settings.value(kSettingsTrims, true).toBool());
    colorChanges_->setChecked(settings.value(kSettingsColors, true).toBool());
    if (const int idx = stops_->findData(settings.value(kSettingsStops, 0).toInt()); idx >= 0) {
        stops_->setCurrentIndex(idx);
    }

    reduceTrims_->setChecked(settings.value(kSettingsReduceTrims, false).toBool());
    sameColour_->setChecked(settings.value(kSettingsSameColour, false).toBool());
    maxBridge_->setValue(settings.value(kSettingsMaxBridge, 15.0).toDouble());
    between_->setChecked(settings.value(kSettingsBetween, true).toBool());
    within_->setChecked(settings.value(kSettingsWithin, true).toBool());
    underlay_->setChecked(settings.value(kSettingsUnderlay, true).toBool());
    regions_->setChecked(settings.value(kSettingsRegions, false).toBool());

    const auto onChange = [this] { scheduleRefresh(); };
    connect(formatBox_, &QComboBox::currentIndexChanged, this, onChange);
    connect(companion_, &QCheckBox::toggled, this, onChange);
    connect(trims_, &QCheckBox::toggled, this, onChange);
    connect(colorChanges_, &QCheckBox::toggled, this, onChange);
    connect(stops_, &QComboBox::currentIndexChanged, this, onChange);
    connect(startMode_, &QComboBox::currentIndexChanged, this, onChange);
    connect(startX_, &QDoubleSpinBox::editingFinished, this, onChange);
    connect(startY_, &QDoubleSpinBox::editingFinished, this, onChange);
    connect(reduceTrims_, &QCheckBox::toggled, this, onChange);
    connect(sameColour_, &QCheckBox::toggled, this, onChange);
    connect(between_, &QCheckBox::toggled, this, onChange);
    connect(within_, &QCheckBox::toggled, this, onChange);
    connect(underlay_, &QCheckBox::toggled, this, onChange);
    connect(regions_, &QCheckBox::toggled, this, onChange);
    connect(maxBridge_, &QDoubleSpinBox::editingFinished, this, onChange);
    connect(this, &QDialog::accepted, this, [this] {
        QSettings s;
        s.setValue(kSettingsFormat, formatBox_->currentData().toString());
        s.setValue(kSettingsCompanion, companion_->isChecked());
        s.setValue(kSettingsCompanionKind, companionKind_->currentData().toInt());
        s.setValue(kSettingsTrims, trims_->isChecked());
        s.setValue(kSettingsColors, colorChanges_->isChecked());
        s.setValue(kSettingsStops, stops_->currentData().toInt());
        s.setValue(kSettingsReduceTrims, reduceTrims_->isChecked());
        s.setValue(kSettingsSameColour, sameColour_->isChecked());
        s.setValue(kSettingsMaxBridge, maxBridge_->value());
        s.setValue(kSettingsBetween, between_->isChecked());
        s.setValue(kSettingsWithin, within_->isChecked());
        s.setValue(kSettingsUnderlay, underlay_->isChecked());
        s.setValue(kSettingsRegions, regions_->isChecked());
    });
    refresh();
}

MachineExportDialog::SavedExport MachineExportDialog::loadSavedExport() {
    QSettings settings;
    SavedExport saved;
    saved.formatId = settings.value(kSettingsFormat, "dst").toString();
    saved.options.trims = settings.value(kSettingsTrims, true).toBool() ? formats::TrimMode::Native
                                                                        : formats::TrimMode::Drop;
    saved.options.color_changes = settings.value(kSettingsColors, true).toBool()
                                      ? formats::ColorChangeMode::Emit
                                      : formats::ColorChangeMode::Drop;
    saved.options.stops = static_cast<formats::StopMode>(settings.value(kSettingsStops, 0).toInt());
    // Le point de départ n'est pas mémorisé (toujours « premier point du motif » à l'ouverture).
    const bool reduce = settings.value(kSettingsReduceTrims, false).toBool();
    saved.optimizations.reduce_trims = reduce;
    saved.optimizations.hidden_travel.allow_same_color_surface =
        reduce && settings.value(kSettingsSameColour, false).toBool();
    saved.optimizations.hidden_travel.max_bridge_length = Micrometers{static_cast<std::int32_t>(
        std::lround(settings.value(kSettingsMaxBridge, 15.0).toDouble() * 1000.0))};
    saved.optimizations.hidden_travel.between_objects =
        settings.value(kSettingsBetween, true).toBool();
    saved.optimizations.hidden_travel.within_object =
        settings.value(kSettingsWithin, true).toBool();
    saved.optimizations.hidden_travel.reorder_underlay =
        settings.value(kSettingsUnderlay, true).toBool();
    saved.optimizations.hidden_travel.reorder_regions =
        settings.value(kSettingsRegions, false).toBool();
    const auto* f = formats::find_format(saved.formatId.toStdString());
    saved.companion.write =
        settings.value(kSettingsCompanion, false).toBool() && f != nullptr && !f->carries_colors;
    saved.companion.kind = static_cast<formats::CompanionKind>(
        settings.value(kSettingsCompanionKind, static_cast<int>(formats::CompanionKind::Csv))
            .toInt());
    return saved;
}

void MachineExportDialog::flushPendingRefresh() {
    if (refreshTimer_ != nullptr && refreshTimer_->isActive()) {
        refreshTimer_->stop();
        refresh();
    }
}

void MachineExportDialog::accept() {
    flushPendingRefresh(); // le résumé affiché (limites du format) est celui des réglages validés
    if (blocking_) {
        return;
    }
    QDialog::accept();
}

const formats::FormatInfo* MachineExportDialog::format() const {
    return formats::find_format(formatBox_->currentData().toString().toStdString());
}

formats::MachineExportOptions MachineExportDialog::options() const {
    formats::MachineExportOptions o;
    o.trims = trims_->isChecked() ? formats::TrimMode::Native : formats::TrimMode::Drop;
    o.color_changes = colorChanges_->isChecked() ? formats::ColorChangeMode::Emit
                                                 : formats::ColorChangeMode::Drop;
    o.stops = static_cast<formats::StopMode>(stops_->currentData().toInt());
    o.start.mode = static_cast<formats::StartMode>(startMode_->currentData().toInt());
    o.start.point =
        Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(startX_->value() * 1000.0))},
               Micrometers{static_cast<std::int32_t>(std::lround(startY_->value() * 1000.0))}};
    return o;
}

project_io::ColorCompanionExport MachineExportDialog::companion() const {
    project_io::ColorCompanionExport c;
    const auto* f = format();
    c.write = companion_->isChecked() && f != nullptr && !f->carries_colors;
    c.kind = static_cast<formats::CompanionKind>(companionKind_->currentData().toInt());
    return c;
}

project_io::ExportOptimizations MachineExportDialog::optimizations() const {
    project_io::ExportOptimizations o;
    o.reduce_trims = reduceTrims_->isChecked();
    o.hidden_travel.allow_same_color_surface =
        reduceTrims_->isChecked() && sameColour_->isChecked();
    o.hidden_travel.max_bridge_length =
        Micrometers{static_cast<std::int32_t>(std::lround(maxBridge_->value() * 1000.0))};
    o.hidden_travel.between_objects = between_->isChecked();
    o.hidden_travel.within_object = within_->isChecked();
    o.hidden_travel.reorder_underlay = underlay_->isChecked();
    o.hidden_travel.reorder_regions = regions_->isChecked();
    return o;
}

void MachineExportDialog::scheduleRefresh() {
    updateEnabledStates();
    summary_->setEnabled(false); // aperçu en attente de recalcul
    refreshTimer_->start();
}

void MachineExportDialog::updateEnabledStates() {
    const auto* f = format();
    if (f == nullptr) {
        return;
    }
    // Le DST garde son propre codage des coupes/arrêts : les options ne s'y appliquent pas.
    const bool dst = f->id == "dst";
    trims_->setEnabled(!dst);
    stops_->setEnabled(!dst);
    colorChanges_->setEnabled(!dst);
    // Réservé aux formats qui n'emportent pas de vraies couleurs (DST, EXP).
    companion_->setEnabled(!f->carries_colors);
    companionKind_->setEnabled(!f->carries_colors && companion_->isChecked());
    const bool customStart = static_cast<formats::StartMode>(startMode_->currentData().toInt()) ==
                             formats::StartMode::Custom;
    startX_->setEnabled(customStart);
    startY_->setEnabled(customStart);
    // Les réglages de ponts n'ont de sens que si l'option est cochée.
    sameColour_->setEnabled(reduceTrims_->isChecked());
    maxBridge_->setEnabled(reduceTrims_->isChecked());
    between_->setEnabled(reduceTrims_->isChecked());
    within_->setEnabled(reduceTrims_->isChecked());
    underlay_->setEnabled(reduceTrims_->isChecked());
    regions_->setEnabled(reduceTrims_->isChecked());
}

void MachineExportDialog::refresh() {
    const auto* f = format();
    if (f == nullptr) {
        return;
    }
    refreshTimer_->stop();
    updateEnabledStates();
    const auto summary = summarize_(*f, options(), optimizations());
    summary_->setText(summary.text);
    summary_->setEnabled(true);
    blocking_ = summary.blocking;
    const bool hasProblems = summary.has_analysis_errors || summary.has_warnings;
    problemsView_->setPlainText(summary.problems);
    problemsBtn_->setVisible(hasProblems);
    openAnalysisBtn_->setVisible(hasProblems);
    if (!hasProblems) {
        problemsView_->hide();
        problemsBtn_->setText(tr("Voir les problèmes"));
    }
    chooseBtn_->setEnabled(!summary.blocking);
    updateDefaultButton(summary.blocking || hasProblems, hasProblems);
}

// Bouton par défaut (Entrée) : « Choisir le fichier… » seulement quand rien ne s'oppose à l'export
// ; sinon une action sans effet sur les fichiers (voir les problèmes, ou annuler si rien à voir).
void MachineExportDialog::updateDefaultButton(bool risky, bool hasProblems) {
    QPushButton* target = chooseBtn_;
    if (risky) {
        target = hasProblems ? problemsBtn_ : cancelBtn_;
    }
    for (QPushButton* b : {chooseBtn_, saveBtn_, problemsBtn_, openAnalysisBtn_, cancelBtn_}) {
        b->setAutoDefault(b == target);
        b->setDefault(b == target);
    }
}

} // namespace openstitch::desktop
