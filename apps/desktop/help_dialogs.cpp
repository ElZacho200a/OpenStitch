// SPDX-License-Identifier: Apache-2.0
#include "help_dialogs.hpp"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include "app_theme.hpp"
#include "openstitch/core/app_info.hpp"

#include <algorithm>

namespace openstitch::desktop {

namespace {

constexpr int kKeyRole = Qt::UserRole + 1; // texte de recherche replié de la ligne

// Minuscules, sans accents : « Échap » et « echap » se correspondent.
QString fold(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c);
        }
    }
    return out.toCaseFolded();
}

class FoldingProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setQuery(const QString& query) {
        static const QRegularExpression whitespace(QStringLiteral("[\\s\\x{00A0}]+"));
        terms_ = fold(query).split(whitespace, Qt::SkipEmptyParts);
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (terms_.isEmpty()) {
            return true;
        }
        const QModelIndex index = sourceModel()->index(row, 0, parent);
        const QString key = sourceModel()->data(index, kKeyRole).toString();
        for (const QString& term : terms_) {
            if (!key.contains(term)) {
                return false;
            }
        }
        return true;
    }

private:
    QStringList terms_;
};

QStandardItem* makeItem(const QString& text) {
    auto* item = new QStandardItem(text);
    item->setEditable(false);
    return item;
}

void appendRow(QStandardItemModel& model, const QString& context, const QString& gesture,
               const QString& action) {
    auto* contextItem = makeItem(context);
    contextItem->setData(fold(context + QLatin1Char(' ') + gesture + QLatin1Char(' ') + action),
                         kKeyRole);
    model.appendRow({contextItem, makeItem(gesture), makeItem(action)});
}

void applyLayoutMetrics(QVBoxLayout* layout) {
    const Tokens& t = AppTheme::instance().tokens();
    layout->setContentsMargins(t.space4, t.space4, t.space4, t.space4);
    layout->setSpacing(t.space3);
}

} // namespace

QString plainActionText(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('&')) {
            if (i + 1 < text.size() && text[i + 1] == QLatin1Char('&')) {
                out.append(QLatin1Char('&'));
                ++i;
            }
            continue;
        }
        out.append(text[i]);
    }
    return out;
}

QString aboutText() {
    const QString name = QString::fromUtf8(openstitch::kAppName).toHtmlEscaped();
    const QString version = QString::fromUtf8(openstitch::kAppVersion).toHtmlEscaped();
    const QString repo = QStringLiteral("https://github.com/ElZacho200a/OpenStitch");
    return QCoreApplication::translate(
               "HelpDialogs", "<h3>%1</h3>"
                              "<p>Version %2</p>"
                              "<p>Numérisation de broderie machine : de l'image au fichier "
                              "DST. Logiciel libre.</p>"
                              "<p>Licence : Apache-2.0.</p>"
                              "<p>Code source : <a href=\"%3\">%3</a></p>")
        .arg(name, version, repo);
}

// ---------------------------------------------------------------- GesturesDialog

GesturesDialog::GesturesDialog(QObject* actionRoot, QWidget* parent)
    : QDialog(parent), actionRoot_(actionRoot) {
    setObjectName(QStringLiteral("gesturesDialog"));
    setWindowTitle(tr("Gestes souris et clavier"));
    setAccessibleName(tr("Gestes souris et clavier"));
    setModal(false);
    setSizeGripEnabled(true);
    resize(760, 540);
    setMinimumSize(460, 320);

    auto* root = new QVBoxLayout(this);
    applyLayoutMetrics(root);

    auto* top = new QHBoxLayout();
    top->setSpacing(AppTheme::instance().tokens().space3);

    auto* searchLabel = new QLabel(tr("&Rechercher :"), this);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("gesturesSearch"));
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(tr("Geste, touche ou commande…"));
    search_->setAccessibleName(tr("Rechercher un geste ou un raccourci"));
    searchLabel->setBuddy(search_);

    auto* presetLabel = new QLabel(tr("&Préréglage de navigation :"), this);
    presetCombo_ = new QComboBox(this);
    presetCombo_->setObjectName(QStringLiteral("gesturesPreset"));
    presetCombo_->setAccessibleName(tr("Préréglage de navigation"));
    presetCombo_->addItem(tr("OpenStitch"), static_cast<int>(Preset::OpenStitch));
    presetCombo_->addItem(tr("Pavé tactile"), static_cast<int>(Preset::Touchpad));
    presetCombo_->setCurrentIndex(InteractionMap::preset() == Preset::Touchpad ? 1 : 0);
    presetLabel->setBuddy(presetCombo_);

    top->addWidget(searchLabel);
    top->addWidget(search_, 1);
    top->addWidget(presetLabel);
    top->addWidget(presetCombo_);
    root->addLayout(top);

    model_ = new QStandardItemModel(this);
    proxy_ = new FoldingProxy(this);
    proxy_->setSourceModel(model_);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("gesturesTable"));
    table_->setAccessibleName(tr("Table des gestes et raccourcis"));
    table_->setAccessibleDescription(
        tr("Trois colonnes : contexte, geste ou touche, action. Flèches pour parcourir."));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(true);
    table_->setTabKeyNavigation(false); // Tab quitte le tableau (accessibilité)
    table_->setSortingEnabled(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    root->addWidget(table_, 1);

    summary_ = new QLabel(this);
    summary_->setAccessibleName(tr("Nombre de lignes affichées"));
    root->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    closeButton_ = buttons->button(QDialogButtonBox::Close);
    closeButton_->setText(tr("Fermer"));
    closeButton_->setAccessibleName(tr("Fermer"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    setTabOrder(search_, presetCombo_);
    setTabOrder(presetCombo_, table_);
    setTabOrder(table_, closeButton_);

    connect(search_, &QLineEdit::textChanged, this, &GesturesDialog::setFilterText);
    connect(presetCombo_, &QComboBox::currentIndexChanged, this,
            &GesturesDialog::onPresetIndexChanged);

    rebuildModel();
    search_->setFocus();
}

QString GesturesDialog::commandsGroupName() {
    return tr("Raccourcis des commandes");
}

int GesturesDialog::totalRowCount() const {
    return model_->rowCount();
}

int GesturesDialog::visibleRowCount() const {
    return proxy_->rowCount();
}

void GesturesDialog::setFilterText(const QString& text) {
    static_cast<FoldingProxy*>(proxy_)->setQuery(text);
    updateSummary();
}

void GesturesDialog::refresh() {
    const QSignalBlocker blocker(presetCombo_);
    presetCombo_->setCurrentIndex(InteractionMap::preset() == Preset::Touchpad ? 1 : 0);
    rebuildModel();
}

void GesturesDialog::rebuildModel() {
    model_->clear();
    model_->setHorizontalHeaderLabels({tr("Contexte"), tr("Geste"), tr("Action")});

    for (const Row* row : InteractionMap::allRows(false)) {
        appendRow(*model_, InteractionMap::contextName(row->context),
                  InteractionMap::describe(row->gesture), InteractionMap::label(*row));
    }

    if (actionRoot_ != nullptr) {
        const QString group = commandsGroupName();
        struct Entry {
            QString text;
            QString keys;
        };
        QList<Entry> entries;
        const auto actions = actionRoot_->findChildren<QAction*>();
        for (const QAction* action : actions) {
            const QString text = plainActionText(action->text());
            const QList<QKeySequence> shortcuts = action->shortcuts();
            if (text.isEmpty() || shortcuts.isEmpty() || action->isSeparator()) {
                continue;
            }
            QStringList names;
            for (const QKeySequence& seq : shortcuts) {
                if (!seq.isEmpty()) {
                    names << seq.toString(QKeySequence::NativeText);
                }
            }
            if (names.isEmpty()) {
                continue;
            }
            entries.append({text, names.join(QStringLiteral(" / "))});
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.text.localeAwareCompare(b.text) < 0;
        });
        for (const Entry& e : entries) {
            appendRow(*model_, group, e.keys, e.text);
        }
    }

    table_->resizeColumnToContents(0);
    table_->resizeColumnToContents(1);
    updateSummary();
}

void GesturesDialog::updateSummary() {
    summary_->setText(tr("%1 sur %2 lignes").arg(proxy_->rowCount()).arg(model_->rowCount()));
}

void GesturesDialog::onPresetIndexChanged(int index) {
    const Preset preset =
        presetCombo_->itemData(index).toInt() == static_cast<int>(Preset::Touchpad)
            ? Preset::Touchpad
            : Preset::OpenStitch;
    InteractionMap::setPreset(preset);
    InteractionMap::savePreset();
    rebuildModel();
    emit presetChanged(preset);
}

// -------------------------------------------------------------- QuickStartDialog

std::vector<QuickStartStep> QuickStartDialog::defaultSteps() {
    // Textes des 6 étapes. MainWindow passe ses QAction membres via le constructeur
    // à étapes explicites (copier ces étapes et renseigner `action`) ; sans cela,
    // seules les étapes ayant un objectName existant (`actionNames`) ont un bouton.
    return {
        {tr("Ouvrir une image"),
         tr("Importez la photo ou le dessin à broder (menu Fichier, Ctrl+O). Vous pouvez "
            "ensuite régler la luminosité, quantifier les couleurs ou recadrer (menu Image)."),
         nullptr,
         {},
         true},
        {tr("Segmenter en régions"),
         tr("Regroupez les couleurs en régions (menu Segmentation). Fusionnez, recolorez ou "
            "supprimez les régions parasites avant de continuer."),
         nullptr,
         {},
         true},
        {tr("Vectoriser ou numériser automatiquement"),
         tr("Convertissez une région en objet vectoriel modifiable, ou laissez OpenStitch "
            "proposer les objets de broderie (Numérisation automatique, formes pleines ou "
            "mode Contours pour un dessin au trait)."),
         nullptr,
         {},
         true},
        {tr("Choisir le type de point"),
         tr("Pour chaque objet, choisissez le type de point (clic droit) : remplissage tatami, "
            "colonne satin ou point de contour. Retouchez les nœuds ou les points au besoin."),
         nullptr,
         {QStringLiteral("action_createStitch")}},
        {tr("Analyser le motif"),
         tr("Lancez l'analyse (F5) pour repérer les problèmes avant la broderie."),
         nullptr,
         {},
         true},
        {tr("Exporter en DST"),
         tr("Exportez le fichier DST pour votre machine (menu Fichier). Les raccourcis sont "
            "listés dans Aide, Gestes."),
         nullptr,
         {},
         true},
    };
}

QuickStartDialog::QuickStartDialog(QObject* actionRoot, QWidget* parent)
    : QuickStartDialog(actionRoot, defaultSteps(), parent) {}

QuickStartDialog::QuickStartDialog(QObject* actionRoot, std::vector<QuickStartStep> steps,
                                   QWidget* parent)
    : QDialog(parent), steps_(std::move(steps)) {
    setObjectName(QStringLiteral("quickStartDialog"));
    setWindowTitle(tr("Guide de prise en main"));
    setAccessibleName(tr("Guide de prise en main"));
    setModal(false);
    setSizeGripEnabled(true);
    resize(560, 560);
    setMinimumSize(380, 300);
    build(actionRoot);
}

QuickStartDialog::QuickStartDialog(std::vector<QuickStartStep> steps, QWidget* parent)
    : QuickStartDialog(nullptr, std::move(steps), parent) {}

int QuickStartDialog::stepButtonCount(int step) const {
    if (step < 0 || step >= static_cast<int>(buttons_.size())) {
        return 0;
    }
    return static_cast<int>(buttons_[static_cast<std::size_t>(step)].size());
}

QPushButton* QuickStartDialog::stepButton(int step, int index) const {
    if (index < 0 || index >= stepButtonCount(step)) {
        return nullptr;
    }
    return buttons_[static_cast<std::size_t>(step)][index];
}

void QuickStartDialog::build(QObject* actionRoot) {
    auto* root = new QVBoxLayout(this);
    applyLayoutMetrics(root);

    auto* intro = new QLabel(
        tr("De l'image au fichier DST en six étapes. Cette fenêtre reste ouverte pendant que "
           "vous travaillez."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("quickStartScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAccessibleName(tr("Étapes du guide"));
    auto* content = new QWidget(scroll);
    auto* steps = new QVBoxLayout(content);
    steps->setContentsMargins(0, 0, 0, 0);
    steps->setSpacing(AppTheme::instance().tokens().space4);

    QWidget* previous = nullptr;
    buttons_.assign(steps_.size(), {});
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        const QuickStartStep& step = steps_[i];
        auto* box = new QFrame(content);
        box->setFrameShape(QFrame::NoFrame);
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(AppTheme::instance().tokens().space2);

        auto* title = new QLabel(tr("%1. %2").arg(i + 1).arg(step.title), box);
        QFont bold = title->font();
        bold.setBold(true);
        title->setFont(bold);
        title->setAccessibleName(tr("Étape %1 : %2").arg(i + 1).arg(step.title));
        auto* body = new QLabel(step.body, box);
        body->setWordWrap(true);
        body->setAccessibleName(tr("Description de l'étape %1").arg(i + 1));
        boxLayout->addWidget(title);
        boxLayout->addWidget(body);

        struct Binding {
            QAction* action;
            QString name;
        };
        QList<Binding> bindings;
        if (step.action != nullptr) {
            bindings.append({step.action, step.title});
        }
        for (const QString& name : step.actionNames) {
            bindings.append(
                {actionRoot != nullptr ? actionRoot->findChild<QAction*>(name) : nullptr, name});
        }
        if (bindings.isEmpty() && !step.informative) {
            bindings.append({nullptr, step.title}); // action attendue mais absente
        }
        for (const Binding& binding : bindings) {
            QAction* action = binding.action;
            const QString& name = binding.name;
            auto* button = new QPushButton(box);
            button->setObjectName(QStringLiteral("quickStartStep%1Button").arg(i + 1));
            button->setAccessibleDescription(step.body);
            bindButton(button, action, name);
            boxLayout->addWidget(button, 0, Qt::AlignLeft);
            buttons_[i].append(button);
            if (previous != nullptr) {
                setTabOrder(previous, button);
            }
            previous = button;
        }
        steps->addWidget(box);
    }
    steps->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    closeButton_ = buttonBox->button(QDialogButtonBox::Close);
    closeButton_->setText(tr("Fermer"));
    closeButton_->setAccessibleName(tr("Fermer"));
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttonBox);
    if (previous != nullptr) {
        setTabOrder(previous, closeButton_);
    }
}

void QuickStartDialog::bindButton(QPushButton* button, QAction* action, const QString& actionName) {
    if (action == nullptr) {
        button->setText(actionName);
        button->setAccessibleName(tr("Commande introuvable : %1").arg(actionName));
        button->setEnabled(false);
        button->setToolTip(tr("Commande introuvable dans cette fenêtre."));
        return;
    }
    QPointer<QAction> guarded(action);
    const auto sync = [button, guarded] {
        if (guarded.isNull()) {
            button->setEnabled(false);
            button->setToolTip(tr("Commande introuvable dans cette fenêtre."));
            return;
        }
        const QString text = plainActionText(guarded->text());
        button->setText(text);
        button->setAccessibleName(tr("Lancer : %1").arg(text));
        const bool enabled = guarded->isEnabled();
        button->setEnabled(enabled);
        button->setToolTip(enabled ? tr("Lance la commande « %1 ».").arg(text)
                                   : tr("Indisponible pour le moment : la commande « %1 » est "
                                        "grisée (ouvrez une image ou sélectionnez un objet "
                                        "d'abord).")
                                         .arg(text));
    };
    sync();
    connect(action, &QAction::changed, button, sync);
    connect(action, &QObject::destroyed, button, sync);
    connect(button, &QPushButton::clicked, button, [guarded] {
        if (!guarded.isNull() && guarded->isEnabled()) {
            guarded->trigger();
        }
    });
}

} // namespace openstitch::desktop
