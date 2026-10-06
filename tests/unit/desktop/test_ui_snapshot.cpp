// SPDX-License-Identifier: Apache-2.0
// Instantanés headless du design system v2 (plan L2 §4) : rend l'application et les
// dialogues clés dans les 4 combinaisons thème x densité (offscreen) et écrit des PNG
// pour RELECTURE HUMAINE (aucune comparaison de pixels, aucune assertion sur le rendu).
//
//   OPENSTITCH_UI_SNAPSHOT_DIR=<dossier>   écrit les PNG + manifest.json + contrast.md
//                                          (sans : dossier temporaire, test de fumée)
//   QT_SCALE_FACTOR=2                      rendu HiDPI (sous-dossier hidpi/)
//
// Assertions de FUMÉE seulement : PNG non nul, >= 8 couleurs distinctes, aucune alerte
// Qt de feuille de style, aucun avertissement de débordement (manifest.json).
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QTreeWidget>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "ai_preferences_dialog.hpp"
#include "app_theme.hpp"
#include "brightness_dialog.hpp"
#include "canvas_view.hpp"
#include "design_tokens.hpp"
#include "generation_options_dialog.hpp"
#include "help_dialogs.hpp"
#include "import_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "ui_icons.hpp"
#include "ui_style.hpp"

using openstitch::Micrometers;
using openstitch::Vec2um;

namespace openstitch::desktop {

namespace {

// ---- projet de démonstration : 1 région, 1 contour, 1 tatami, 1 satin à rails A/B ----

struct DemoProject {
    document::Project project;
    ObjectId tatamiId{};
    ObjectId contourId{};
    ObjectId satinId{};
    RegionId regionId{};
};

geometry::Path polygon(std::initializer_list<std::pair<int, int>> pointsMm) {
    geometry::Path path;
    path.closed = true;
    for (const auto& [x, y] : pointsMm) {
        path.nodes.push_back(
            geometry::PathNode{Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}},
                               geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return path;
}

DemoProject buildDemoProject() {
    DemoProject demo;
    auto& project = demo.project;
    constexpr int kW = 200;
    constexpr int kH = 150;
    project.original.width = kW;
    project.original.height = kH;
    project.original.rgba.resize(static_cast<std::size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            auto* px = &project.original.rgba[(static_cast<std::size_t>(y) * kW + x) * 4];
            px[0] = static_cast<std::uint8_t>(200 - x / 2);
            px[1] = static_cast<std::uint8_t>(60 + y / 2);
            px[2] = static_cast<std::uint8_t>(40 + x / 3);
            px[3] = 255;
        }
    }

    segmentation::Segmentation seg;
    seg.width = kW;
    seg.height = kH;
    seg.labels.assign(static_cast<std::size_t>(kW) * kH, 0);
    std::size_t count = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW / 2; ++x) {
            seg.labels[static_cast<std::size_t>(y) * kW + x] = 1;
            ++count;
        }
    }
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {200, 60, 40}, count});
    demo.regionId = RegionId{1};
    project.segmentation = std::move(seg);

    // Tatami : rectangle 30 x 20 mm.
    document::VectorObject rect;
    rect.id = project.object_ids.next();
    rect.name = "Rectangle";
    rect.paths.push_back(geometry::PathSet{polygon({{10, 10}, {40, 10}, {40, 30}, {10, 30}}), {}});
    document::EmbroideryObject tatami;
    tatami.id = project.object_ids.next();
    tatami.name = "Rectangle - tatami";
    tatami.source_vector = rect.id;
    tatami.rgb = {200, 60, 40};
    tatami.params = document::TatamiParams{};
    demo.tatamiId = tatami.id;
    project.vector_objects.push_back(rect);
    project.embroidery_objects.push_back(tatami);

    // Contour : hexagone.
    document::VectorObject hex;
    hex.id = project.object_ids.next();
    hex.name = "Hexagone";
    hex.paths.push_back(geometry::PathSet{
        polygon({{50, 12}, {58, 12}, {62, 20}, {58, 28}, {50, 28}, {46, 20}}), {}});
    document::EmbroideryObject contour;
    contour.id = project.object_ids.next();
    contour.name = "Hexagone - contour";
    contour.source_vector = hex.id;
    contour.rgb = {30, 60, 120};
    contour.params = document::RunningStitchParams{};
    demo.contourId = contour.id;
    project.vector_objects.push_back(hex);
    project.embroidery_objects.push_back(contour);

    // Satin : deux rails A/B et trois barreaux.
    document::SatinParams satin;
    satin.rail_a.closed = false;
    satin.rail_b.closed = false;
    satin.rail_a.nodes = {{{Micrometers{10'000}, Micrometers{40'000}}},
                          {{Micrometers{35'000}, Micrometers{44'000}}},
                          {{Micrometers{60'000}, Micrometers{40'000}}}};
    satin.rail_b.nodes = {{{Micrometers{10'000}, Micrometers{46'000}}},
                          {{Micrometers{35'000}, Micrometers{50'000}}},
                          {{Micrometers{60'000}, Micrometers{46'000}}}};
    satin.rungs = {
        {{Micrometers{10'000}, Micrometers{40'000}}, {Micrometers{10'000}, Micrometers{46'000}}},
        {{Micrometers{35'000}, Micrometers{44'000}}, {Micrometers{35'000}, Micrometers{50'000}}},
        {{Micrometers{60'000}, Micrometers{40'000}}, {Micrometers{60'000}, Micrometers{46'000}}}};
    document::EmbroideryObject satinObj;
    satinObj.id = project.object_ids.next();
    satinObj.name = "Satin";
    satinObj.rgb = {20, 120, 80};
    satinObj.params = satin;
    demo.satinId = satinObj.id;
    project.embroidery_objects.push_back(satinObj);
    return demo;
}

// ---- relevés de fumée --------------------------------------------------------------

QStringList g_styleWarnings;
QtMessageHandler g_previousHandler = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (msg.contains(QLatin1String("Could not parse")) ||
        msg.contains(QLatin1String("Unknown property"))) {
        g_styleWarnings << msg;
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, ctx, msg);
    }
}

int distinctColors(const QImage& image, int cap) {
    QSet<QRgb> seen;
    const QImage rgb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < rgb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            seen.insert(row[x]);
            if (seen.size() >= cap) {
                return static_cast<int>(seen.size());
            }
        }
    }
    return static_cast<int>(seen.size());
}

// Débordements : widget plus petit que son minimum de mise en page (contenu rogné) et,
// pour une fenêtre/dialogue, taille affichée inférieure au sizeHint (contenu comprimé).
// Les zones de défilement et leurs viewports sont exclus (le défilement est voulu).
void collectOverflow(QWidget* root, const QString& scene, bool checkRootAgainstSizeHint,
                     QStringList& out) {
    if (checkRootAgainstSizeHint) {
        const QSize hint = root->sizeHint();
        if (hint.isValid() && (root->width() < hint.width() || root->height() < hint.height())) {
            out << QStringLiteral("[hint] %1: %2 affiche %3x%4 < sizeHint %5x%6")
                       .arg(scene, QString::fromLatin1(root->metaObject()->className()))
                       .arg(root->width())
                       .arg(root->height())
                       .arg(hint.width())
                       .arg(hint.height());
        }
    }
    const auto children = root->findChildren<QWidget*>();
    for (QWidget* w : children) {
        if (!w->isVisibleTo(root) || w->isWindow()) {
            continue;
        }
        bool inScrollArea = false;
        for (QWidget* p = w->parentWidget(); p != nullptr && p != root; p = p->parentWidget()) {
            if (qobject_cast<QAbstractScrollArea*>(p) != nullptr || qobject_cast<QMenu*>(p) ||
                qobject_cast<QToolBar*>(p) != nullptr) {
                inScrollArea = true;
                break;
            }
        }
        if (inScrollArea || qobject_cast<QScrollBar*>(w) != nullptr) {
            continue;
        }
        const bool clippable = qobject_cast<QAbstractButton*>(w) != nullptr ||
                               qobject_cast<QLineEdit*>(w) != nullptr ||
                               qobject_cast<QComboBox*>(w) != nullptr ||
                               qobject_cast<QAbstractSpinBox*>(w) != nullptr;
        if (clippable) {
            const QSize minHint = w->minimumSizeHint();
            if (minHint.isValid() &&
                (w->width() < minHint.width() || w->height() < minHint.height())) {
                out << QStringLiteral("[clip] %1: %2 '%3' %4x%5 < minimumSizeHint %6x%7")
                           .arg(scene, QString::fromLatin1(w->metaObject()->className()),
                                w->objectName())
                           .arg(w->width())
                           .arg(w->height())
                           .arg(minHint.width())
                           .arg(minHint.height());
            }
        }
        QWidget* parent = w->parentWidget();
        if (parent != nullptr && qobject_cast<QMainWindow*>(parent) == nullptr &&
            qobject_cast<QDockWidget*>(parent) == nullptr &&
            !parent->rect().adjusted(-1, -1, 1, 1).contains(w->geometry())) {
            out << QStringLiteral("[clip] %1: %2 '%3' sort de son parent %4")
                       .arg(scene, QString::fromLatin1(w->metaObject()->className()),
                            w->objectName(),
                            QString::fromLatin1(parent->metaObject()->className()));
        }
    }
}

void settle() {
    QApplication::processEvents();
    QApplication::processEvents();
}

// ---- galerie : tous les types de contrôles --------------------------------------------

struct Gallery {
    std::unique_ptr<QMainWindow> window;
    QPushButton* focusedButton{nullptr};
};

QPushButton* variantButton(QWidget* parent, const QString& text, ui::ButtonVariant v) {
    auto* b = new QPushButton(text, parent);
    ui::setVariant(b, v);
    return b;
}

Gallery buildGallery() {
    Gallery g;
    g.window = std::make_unique<QMainWindow>();
    auto* win = g.window.get();
    win->setObjectName(QStringLiteral("widgetGallery"));
    win->setWindowTitle(QStringLiteral("OpenStitch - galerie de controles"));

    // Barre d'outils, menu, dock et barre d'état (chrome).
    win->menuBar()->addMenu(QStringLiteral("&Fichier"))->addAction(QStringLiteral("Ouvrir"));
    win->menuBar()->addMenu(QStringLiteral("&Affichage"))->addAction(QStringLiteral("Zoom"));
    auto* toolbar = win->addToolBar(QStringLiteral("Outils"));
    auto* actSelect = toolbar->addAction(icons::select(), QStringLiteral("Selection"));
    actSelect->setCheckable(true);
    actSelect->setChecked(true);
    toolbar->addAction(icons::pan(), QStringLiteral("Deplacer"))->setCheckable(true);
    toolbar->addSeparator();
    toolbar->addAction(icons::undo(), QStringLiteral("Annuler"));
    auto* disabledAct = toolbar->addAction(icons::redo(), QStringLiteral("Retablir"));
    disabledAct->setEnabled(false);
    win->statusBar()->showMessage(QStringLiteral("Pret - barre d'etat"));

    auto* dock = new QDockWidget(QStringLiteral("Panneau ancre"), win);
    auto* dockList = new QListWidget(dock);
    for (int i = 0; i < 40; ++i) {
        dockList->addItem(QStringLiteral("Element de panneau %1").arg(i + 1));
    }
    dockList->setCurrentRow(2);
    dock->setWidget(dockList);
    win->addDockWidget(Qt::RightDockWidgetArea, dock);

    auto* central = new QWidget(win);
    auto* grid = new QGridLayout(central);
    win->setCentralWidget(central);

    // Colonne 0 : boutons, cases, radios, étiquettes.
    auto* buttons = new QGroupBox(QStringLiteral("Boutons"), central);
    ui::styleGroupBox(buttons);
    auto* bl = new QGridLayout(buttons);
    const std::pair<const char*, ui::ButtonVariant> variants[] = {
        {"Principal", ui::ButtonVariant::Primary},
        {"Tonal", ui::ButtonVariant::Tonal},
        {"Discret", ui::ButtonVariant::Ghost},
        {"Danger", ui::ButtonVariant::Danger}};
    int row = 0;
    for (const auto& [label, v] : variants) {
        auto* normal = variantButton(buttons, QString::fromUtf8(label), v);
        auto* off = variantButton(buttons, QString::fromUtf8(label), v);
        off->setEnabled(false);
        auto* checked = variantButton(buttons, QString::fromUtf8(label), v);
        checked->setCheckable(true);
        checked->setChecked(true);
        auto* focus = variantButton(buttons, QString::fromUtf8(label), v);
        if (g.focusedButton == nullptr) {
            g.focusedButton = focus;
        }
        bl->addWidget(normal, row, 0);
        bl->addWidget(off, row, 1);
        bl->addWidget(checked, row, 2);
        bl->addWidget(focus, row, 3);
        ++row;
    }
    grid->addWidget(buttons, 0, 0);

    auto* choices = new QGroupBox(QStringLiteral("Cases et boutons radio"), central);
    ui::styleGroupBox(choices);
    auto* cl = new QGridLayout(choices);
    auto* c1 = new QCheckBox(QStringLiteral("Case decochee"), choices);
    auto* c2 = new QCheckBox(QStringLiteral("Case cochee"), choices);
    c2->setChecked(true);
    auto* c3 = new QCheckBox(QStringLiteral("Etat partiel"), choices);
    c3->setTristate(true);
    c3->setCheckState(Qt::PartiallyChecked);
    auto* c4 = new QCheckBox(QStringLiteral("Case desactivee"), choices);
    c4->setEnabled(false);
    auto* c5 = new QCheckBox(QStringLiteral("Cochee desactivee"), choices);
    c5->setChecked(true);
    c5->setEnabled(false);
    auto* r1 = new QRadioButton(QStringLiteral("Radio choisi"), choices);
    r1->setChecked(true);
    auto* r2 = new QRadioButton(QStringLiteral("Radio libre"), choices);
    auto* r3 = new QRadioButton(QStringLiteral("Radio desactive"), choices);
    r3->setEnabled(false);
    cl->addWidget(c1, 0, 0);
    cl->addWidget(c2, 1, 0);
    cl->addWidget(c3, 2, 0);
    cl->addWidget(c4, 3, 0);
    cl->addWidget(c5, 4, 0);
    cl->addWidget(r1, 0, 1);
    cl->addWidget(r2, 1, 1);
    cl->addWidget(r3, 2, 1);
    grid->addWidget(choices, 1, 0);

    auto* labels = new QGroupBox(QStringLiteral("Etiquettes par role"), central);
    ui::styleGroupBox(labels);
    auto* ll = new QGridLayout(labels);
    const std::pair<const char*, ui::LabelRole> roles[] = {
        {"Titre (title)", ui::LabelRole::Title},
        {"Intertitre (heading)", ui::LabelRole::Heading},
        {"Legende (caption)", ui::LabelRole::Caption},
        {"Monospace 0123 (mono)", ui::LabelRole::Mono},
        {"Avertissement (warning)", ui::LabelRole::Warning},
        {"Erreur (error)", ui::LabelRole::Error},
        {"Succes (success)", ui::LabelRole::Success},
        {"Section (section)", ui::LabelRole::Section}};
    int lr = 0;
    for (const auto& [text, role] : roles) {
        auto* label = new QLabel(QString::fromUtf8(text), labels);
        ui::setRole(label, role);
        ll->addWidget(label, lr++, 0);
    }
    auto* plainLabel = new QLabel(QStringLiteral("Etiquette normale"), labels);
    ll->addWidget(plainLabel, lr++, 0);
    auto* disabledLabel = new QLabel(QStringLiteral("Etiquette desactivee"), labels);
    disabledLabel->setEnabled(false);
    ll->addWidget(disabledLabel, lr++, 0);
    grid->addWidget(labels, 2, 0);

    // Colonne 1 : champs, curseur, progression, onglets.
    auto* fields = new QGroupBox(QStringLiteral("Champs"), central);
    ui::styleGroupBox(fields);
    auto* fl = new QGridLayout(fields);
    auto* edit = new QLineEdit(QStringLiteral("Texte saisi"), fields);
    auto* placeholder = new QLineEdit(fields);
    placeholder->setPlaceholderText(QStringLiteral("Indication (placeholder)"));
    auto* editOff = new QLineEdit(QStringLiteral("Champ desactive"), fields);
    editOff->setEnabled(false);
    auto* combo = new QComboBox(fields);
    combo->addItems({QStringLiteral("Tatami"), QStringLiteral("Satin"), QStringLiteral("Contour")});
    auto* comboOff = new QComboBox(fields);
    comboOff->addItem(QStringLiteral("Combo desactivee"));
    comboOff->setEnabled(false);
    auto* spin = new QSpinBox(fields);
    spin->setRange(0, 100);
    spin->setValue(42);
    auto* dspin = new QDoubleSpinBox(fields);
    dspin->setSuffix(QStringLiteral(" mm"));
    dspin->setValue(3.5);
    auto* mono = new QPlainTextEdit(QStringLiteral("M10 20 L30 40\nZ  // mono"), fields);
    ui::setRole(mono, ui::LabelRole::Mono);
    mono->setMaximumHeight(70);
    fl->addWidget(edit, 0, 0);
    fl->addWidget(placeholder, 1, 0);
    fl->addWidget(editOff, 2, 0);
    fl->addWidget(combo, 3, 0);
    fl->addWidget(comboOff, 4, 0);
    fl->addWidget(spin, 0, 1);
    fl->addWidget(dspin, 1, 1);
    fl->addWidget(mono, 2, 1, 3, 1);
    grid->addWidget(fields, 0, 1);

    auto* ranges = new QGroupBox(QStringLiteral("Curseur et progression"), central);
    ui::styleGroupBox(ranges);
    auto* rl = new QGridLayout(ranges);
    auto* slider = new QSlider(Qt::Horizontal, ranges);
    slider->setValue(60);
    auto* sliderOff = new QSlider(Qt::Horizontal, ranges);
    sliderOff->setValue(30);
    sliderOff->setEnabled(false);
    auto* progress = new QProgressBar(ranges);
    progress->setValue(65);
    rl->addWidget(slider, 0, 0);
    rl->addWidget(sliderOff, 1, 0);
    rl->addWidget(progress, 2, 0);
    grid->addWidget(ranges, 1, 1);

    auto* tabs = new QTabWidget(central);
    for (const char* t : {"Premier", "Deuxieme", "Troisieme"}) {
        auto* page = new QWidget(tabs);
        auto* pl = new QGridLayout(page);
        pl->addWidget(
            new QLabel(QStringLiteral("Contenu de l'onglet %1").arg(QString::fromLatin1(t)), page));
        tabs->addTab(page, QString::fromLatin1(t));
    }
    tabs->setTabEnabled(2, false);
    grid->addWidget(tabs, 2, 1);

    // Colonne 2 : table, arbre, liste (barres de défilement incluses).
    auto* table = new QTableWidget(30, 4, central);
    table->setHorizontalHeaderLabels({QStringLiteral("Nom"), QStringLiteral("Type"),
                                      QStringLiteral("Points"), QStringLiteral("Couleur")});
    for (int r = 0; r < 30; ++r) {
        for (int c = 0; c < 4; ++c) {
            table->setItem(r, c,
                           new QTableWidgetItem(QStringLiteral("R%1 C%2").arg(r + 1).arg(c + 1)));
        }
    }
    table->setAlternatingRowColors(true);
    table->selectRow(2);
    grid->addWidget(table, 0, 2, 1, 1);

    auto* tree = new QTreeWidget(central);
    tree->setHeaderLabels({QStringLiteral("Arbre"), QStringLiteral("Valeur")});
    for (int i = 0; i < 4; ++i) {
        auto* parent = new QTreeWidgetItem(
            tree, {QStringLiteral("Groupe %1").arg(i + 1), QStringLiteral("v")});
        for (int j = 0; j < 4; ++j) {
            new QTreeWidgetItem(parent, {QStringLiteral("Enfant %1.%2").arg(i + 1).arg(j + 1),
                                         QStringLiteral("x")});
        }
    }
    tree->expandAll();
    tree->setCurrentItem(tree->topLevelItem(1)->child(1));
    grid->addWidget(tree, 1, 2, 1, 1);

    auto* list = new QListWidget(central);
    for (int i = 0; i < 25; ++i) {
        list->addItem(QStringLiteral("Element de liste %1").arg(i + 1));
    }
    list->item(3)->setSelected(true);
    list->setCurrentRow(3);
    auto* disabledItem = list->item(5);
    disabledItem->setFlags(disabledItem->flags() & ~Qt::ItemIsEnabled);
    grid->addWidget(list, 2, 2, 1, 1);

    win->resize(1280, 900);
    return g;
}

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void renderAllScenes();

private:
    struct Combo {
        ThemeMode mode;
        Density density;
        QString name;
    };

    QString snapshotRoot() const { return root_; }
    // Écrit le widget dans <root>/<combo>/<scene>.png et enregistre le fichier.
    bool save(QWidget* widget, const QString& scene);
    void saveImage(const QImage& image, const QString& scene);
    void sceneDialogs(MainWindow& window);
    void sceneGenerationOptions();
    void sceneMenus(MainWindow& window);
    void sceneTooltip(MainWindow& window);

    QTemporaryDir settingsDir_;
    QTemporaryDir tempOut_;
    QString root_;
    QString comboName_;
    QJsonArray files_;
    QStringList overflow_;
    QStringList skipped_;
    QStringList emptyScenes_;
    QJsonObject setModeMs_;
    int smokeFailures_{0};
};

void MainWindowTest::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QVERIFY(tempOut_.isValid());
    // Même isolation des QSettings que les autres suites : jamais le profil réel.
    QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUISnapshot"));
    QCoreApplication::setApplicationName(QStringLiteral("UiSnapshotTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    QStandardPaths::setTestModeEnabled(true);

    QString dir = qEnvironmentVariable("OPENSTITCH_UI_SNAPSHOT_DIR");
    if (dir.isEmpty()) {
        dir = tempOut_.path();
    }
    const double scale = qEnvironmentVariable("QT_SCALE_FACTOR", QStringLiteral("1")).toDouble();
    root_ = scale > 1.0 ? QDir(dir).filePath(QStringLiteral("hidpi")) : dir;
    QVERIFY(QDir().mkpath(root_));

    g_previousHandler = qInstallMessageHandler(captureMessages);
}

void MainWindowTest::cleanupTestCase() {
    qInstallMessageHandler(g_previousHandler);
}

void MainWindowTest::saveImage(const QImage& image, const QString& scene) {
    const QString comboDir = QDir(root_).filePath(comboName_);
    QDir().mkpath(comboDir);
    const QString rel = comboName_ + QLatin1Char('/') + scene + QStringLiteral(".png");
    const QString path = QDir(root_).filePath(rel);
    const bool saved = image.save(path, "PNG");
    const qint64 bytes = QFileInfo(path).size();
    QJsonObject entry;
    entry[QStringLiteral("file")] = rel;
    entry[QStringLiteral("bytes")] = static_cast<double>(bytes);
    entry[QStringLiteral("width")] = image.width();
    entry[QStringLiteral("height")] = image.height();
    files_.append(entry);
    // Fumée : fichier écrit, non vide, pas une page blanche.
    if (!saved || bytes <= 0 || image.isNull() || distinctColors(image, 8) < 8) {
        ++smokeFailures_;
        emptyScenes_ << rel;
    }
}

bool MainWindowTest::save(QWidget* widget, const QString& scene) {
    settle();
    const QImage image = widget->grab().toImage();
    saveImage(image, scene);
    return !image.isNull();
}

void MainWindowTest::sceneGenerationOptions() {
    QTimer::singleShot(0, [this] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dlg == nullptr) {
            return;
        }
        QStringList found;
        settle();
        collectOverflow(dlg, QStringLiteral("%1/dialog-generation-options").arg(comboName_), true,
                        found);
        overflow_ << found;
        save(dlg, QStringLiteral("dialog-generation-options"));
        dlg->reject();
    });
    (void)editSequenceFinishing(nullptr, document::SequenceFinishing{});
}

void MainWindowTest::sceneDialogs(MainWindow& window) {
    auto shoot = [this](QDialog& dlg, const QString& scene) {
        dlg.show();
        settle();
        collectOverflow(&dlg, QStringLiteral("%1/%2").arg(comboName_, scene), true, overflow_);
        save(&dlg, scene);
        dlg.close();
    };
    QImage preview(120, 90, QImage::Format_RGB32);
    preview.fill(QColor(120, 160, 200)); // pixels de démonstration (aperçu), pas une couleur d'UI
    {
        ImportDialog dlg(1600, 1200, preview, QSizeF(100.0, 100.0));
        shoot(dlg, QStringLiteral("dialog-import"));
    }
    sceneGenerationOptions();
    {
        BrightnessDialog dlg;
        shoot(dlg, QStringLiteral("dialog-brightness"));
    }
    {
        GesturesDialog dlg(&window);
        shoot(dlg, QStringLiteral("dialog-gestures"));
    }
    {
        QuickStartDialog dlg(&window);
        shoot(dlg, QStringLiteral("dialog-quickstart"));
    }
    {
        AiPreferencesDialog dlg;
        shoot(dlg, QStringLiteral("dialog-ai-preferences"));
    }
}

void MainWindowTest::sceneMenus(MainWindow& window) {
    QMenuBar* bar = window.menuBar();
    struct Wanted {
        const char* needle;
        const char* scene;
    };
    for (const Wanted w : {Wanted{"Fichier", "menu-file"}, Wanted{"Affichage", "menu-view"}}) {
        QMenu* menu = nullptr;
        for (QAction* a : bar->actions()) {
            if (a->menu() != nullptr && a->text().contains(QLatin1String(w.needle))) {
                menu = a->menu();
            }
        }
        if (menu == nullptr) {
            skipped_ << QString::fromLatin1(w.scene);
            continue;
        }
        menu->popup(window.mapToGlobal(QPoint(40, 40)));
        settle();
        if (!menu->isVisible()) {
            skipped_ << QString::fromLatin1(w.scene);
            continue;
        }
        // Une entrée survolée (état :selected) pour juger le survol.
        const auto actions = menu->actions();
        for (QAction* a : actions) {
            if (a->isEnabled() && !a->isSeparator()) {
                menu->setActiveAction(a);
                break;
            }
        }
        save(menu, QString::fromLatin1(w.scene));
        menu->close();
        settle();
    }
}

void MainWindowTest::sceneTooltip(MainWindow& window) {
    QToolTip::showText(window.mapToGlobal(QPoint(300, 200)),
                       QStringLiteral("Infobulle : texte d'aide du design system"), &window);
    settle();
    QWidget* tip = nullptr;
    for (QWidget* w : QApplication::allWidgets()) {
        if (w->isVisible() && w->inherits("QTipLabel")) {
            tip = w;
            break;
        }
    }
    if (tip == nullptr) {
        qWarning("tooltip: QTipLabel introuvable, scene sautee");
        skipped_ << QStringLiteral("tooltip");
        return;
    }
    save(tip, QStringLiteral("tooltip"));
    QToolTip::hideText();
    settle();
}

void MainWindowTest::renderAllScenes() {
    auto& theme = AppTheme::instance();
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    QVERIFY(app != nullptr);
    theme.setThemeChoice(ThemeChoice::Light);
    theme.setDensity(Density::Comfortable);
    theme.applyToApp(*app);

    const std::vector<Combo> combos = {
        {ThemeMode::Light, Density::Comfortable, QStringLiteral("light-comfortable")},
        {ThemeMode::Light, Density::Compact, QStringLiteral("light-compact")},
        {ThemeMode::Dark, Density::Comfortable, QStringLiteral("dark-comfortable")},
        {ThemeMode::Dark, Density::Compact, QStringLiteral("dark-compact")}};

    // Fenêtre principale chargée et vivante pendant toutes les bascules (le coût mesuré
    // est celui d'un re-polissage d'une vraie application).
    DemoProject demo = buildDemoProject();
    MainWindow projectWindow;
    projectWindow.resize(1280, 800);
    projectWindow.applyLoadedProject(demo.project);
    for (QDockWidget* dock : projectWindow.findChildren<QDockWidget*>()) {
        dock->setVisible(true);
    }
    projectWindow.show();
    settle();

    for (const Combo& combo : combos) {
        comboName_ = combo.name;
        QElapsedTimer timer;
        timer.start();
        theme.setMode(combo.mode);
        theme.setDensity(combo.density);
        settle();
        setModeMs_[combo.name] = static_cast<double>(timer.elapsed());

        // 1. fenêtre principale, état vide.
        {
            MainWindow empty;
            empty.resize(1280, 800);
            empty.show();
            settle();
            collectOverflow(&empty, QStringLiteral("%1/main-empty").arg(comboName_), false,
                            overflow_);
            if (empty.minimumSizeHint().width() > empty.width() ||
                empty.minimumSizeHint().height() > empty.height()) {
                overflow_ << QStringLiteral("[clip] %1/main-empty: minimumSizeHint %2x%3 > fenetre")
                                 .arg(comboName_)
                                 .arg(empty.minimumSizeHint().width())
                                 .arg(empty.minimumSizeHint().height());
            }
            save(&empty, QStringLiteral("main-empty"));
        }

        // 2. projet chargé, tous les docks visibles, un objet sélectionné.
        projectWindow.setSelection(
            {.region = std::nullopt, .embroidery = demo.tatamiId, .objects = {}});
        projectWindow.updateActions();
        if (auto* view = projectWindow.findChild<CanvasView*>()) {
            view->fitCanvas();
        }
        for (QDockWidget* dock : projectWindow.findChildren<QDockWidget*>()) {
            dock->setVisible(true);
        }
        settle();
        collectOverflow(&projectWindow, QStringLiteral("%1/main-project").arg(comboName_), false,
                        overflow_);
        save(&projectWindow, QStringLiteral("main-project"));

        // 3. édition satin (rails A/B, poignées).
        projectWindow.setSelection(
            {.region = std::nullopt, .embroidery = demo.satinId, .objects = {}});
        projectWindow.updateActions();
        if (projectWindow.satinEditModeAct_ != nullptr) {
            projectWindow.satinEditModeAct_->setChecked(true);
        }
        settle();
        save(&projectWindow, QStringLiteral("main-satin-edit"));
        if (projectWindow.satinEditModeAct_ != nullptr) {
            projectWindow.satinEditModeAct_->setChecked(false);
        }

        // 4. menus ouverts + infobulle.
        sceneMenus(projectWindow);
        sceneTooltip(projectWindow);

        // 5. dialogues.
        sceneDialogs(projectWindow);

        // 6. galerie de contrôles.
        {
            Gallery gallery = buildGallery();
            gallery.window->show();
            gallery.window->activateWindow();
            settle();
            if (gallery.focusedButton != nullptr) {
                gallery.focusedButton->setFocus(Qt::TabFocusReason);
            }
            settle();
            const QSize hint = gallery.window->minimumSizeHint();
            if (hint.width() > gallery.window->width() ||
                hint.height() > gallery.window->height()) {
                gallery.window->resize(hint.expandedTo(gallery.window->size()));
            }
            settle();
            collectOverflow(gallery.window.get(),
                            QStringLiteral("%1/widget-gallery").arg(comboName_), false, overflow_);
            save(gallery.window.get(), QStringLiteral("widget-gallery"));
        }
    }
    projectWindow.close();

    // ---- contrast.md + manifest.json ----
    {
        QFile md(QDir(root_).filePath(QStringLiteral("contrast.md")));
        QVERIFY(md.open(QIODevice::WriteOnly | QIODevice::Truncate));
        md.write(contrast_report_markdown().toUtf8());
    }
    QJsonObject manifest;
    manifest[QStringLiteral("qtRuntime")] = QString::fromLatin1(qVersion());
    manifest[QStringLiteral("qtCompiled")] = QStringLiteral(QT_VERSION_STR);
    manifest[QStringLiteral("platform")] = QGuiApplication::platformName();
    manifest[QStringLiteral("devicePixelRatio")] =
        QGuiApplication::primaryScreen() != nullptr
            ? QGuiApplication::primaryScreen()->devicePixelRatio()
            : 1.0;
    manifest[QStringLiteral("qtScaleFactor")] =
        qEnvironmentVariable("QT_SCALE_FACTOR", QStringLiteral("1"));
    manifest[QStringLiteral("style")] =
        app->style() != nullptr ? app->style()->objectName() : QString();
    manifest[QStringLiteral("appFontResolved")] = QFontInfo(app->font()).family();
    QJsonObject fonts;
    const QStringList installed = QFontDatabase::families();
    QJsonArray uiAvailable;
    for (const QString& f : font_families()) {
        if (installed.contains(f)) {
            uiAvailable.append(f);
        }
    }
    QJsonArray monoAvailable;
    for (const QString& f : mono_font_families()) {
        if (installed.contains(f)) {
            monoAvailable.append(f);
        }
    }
    fonts[QStringLiteral("uiStackAvailable")] = uiAvailable;
    fonts[QStringLiteral("monoStackAvailable")] = monoAvailable;
    fonts[QStringLiteral("installedCount")] = static_cast<int>(installed.size());
    manifest[QStringLiteral("fonts")] = fonts;
    manifest[QStringLiteral("setModeMs")] = setModeMs_;
    manifest[QStringLiteral("fileCount")] = static_cast<int>(files_.size());
    manifest[QStringLiteral("files")] = files_;
    manifest[QStringLiteral("skippedScenes")] = QJsonArray::fromStringList(skipped_);
    manifest[QStringLiteral("overflowWarnings")] = QJsonArray::fromStringList(overflow_);
    manifest[QStringLiteral("styleWarnings")] = QJsonArray::fromStringList(g_styleWarnings);
    manifest[QStringLiteral("smokeFailures")] = QJsonArray::fromStringList(emptyScenes_);
    QFile mf(QDir(root_).filePath(QStringLiteral("manifest.json")));
    QVERIFY(mf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    mf.close();

    // ---- assertions de fumée (jamais sur les pixels) ----
    QVERIFY2(files_.size() >= 48, qPrintable(QStringLiteral("fichiers: %1").arg(files_.size())));
    QVERIFY2(emptyScenes_.isEmpty(), qPrintable(emptyScenes_.join(QLatin1Char('\n'))));
    QVERIFY2(g_styleWarnings.isEmpty(), qPrintable(g_styleWarnings.join(QLatin1Char('\n'))));
    // Rognage reel ([clip]) : toujours fatal. Fenetre plus petite que son sizeHint ([hint],
    // ex. resize() de dialogue trop petit : Qt agrandit alors la fenetre) : signale dans
    // manifest.json et en QWARN ; fatal avec OPENSTITCH_UI_SNAPSHOT_STRICT=1.
    const bool strict = qEnvironmentVariableIntValue("OPENSTITCH_UI_SNAPSHOT_STRICT") != 0;
    QStringList fatal;
    for (const QString& w : std::as_const(overflow_)) {
        if (strict || w.startsWith(QLatin1String("[clip]"))) {
            fatal << w;
        } else {
            qWarning("%s", qPrintable(w));
        }
    }
    QVERIFY2(fatal.isEmpty(), qPrintable(fatal.join(QLatin1Char('\n'))));
}

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_ui_snapshot.moc"
