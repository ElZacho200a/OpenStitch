// SPDX-License-Identifier: Apache-2.0
// Lettrage (HP-TXT-*) : outil Texte (T), menu Texte, création / édition / suppression d'un
// texte. Aucune géométrie ni règle de broderie ici : les lettres viennent de
// lettering::build_text_objects (libs/lettering), l'application au document de
// commands::SetTextObjectCommand (un seul pas d'annulation), l'aperçu et les avertissements
// du dialogue (text_dialog.cpp) de la même bibliothèque.
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <memory>

#include "font_catalog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/text_commands.hpp"
#include "openstitch/lettering/lettering.hpp"
#include "properties_panel.hpp"
#include "text_dialog.hpp"
#include "ui_icons.hpp"

namespace openstitch::desktop {

namespace {

// Réglages proposés à la création d'un texte : ceux du dernier texte validé de la session
// (police, hauteur, type de point, couleur...), pour enchaîner plusieurs textes sans tout refaire.
document::TextObject& lastTextSettings() {
    static document::TextObject last = [] {
        document::TextObject t;
        t.font.builtin = "vera-sans-bold"; // traits assez épais pour le satin dès 10 mm
        t.font.family = "Bitstream Vera Sans";
        t.cap_height = Micrometers{12'000};
        return t;
    }();
    return last;
}

Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

} // namespace

void MainWindow::buildTextMenu() {
    textMenu_ = menuBar()->addMenu(tr("Te&xte"));

    newTextAct_ = textMenu_->addAction(icons::text(), tr("&Nouveau texte…"));
    newTextAct_->setObjectName(QStringLiteral("action_textNew"));
    newTextAct_->setToolTip(
        tr("Crée un texte au centre du cadre (ou cliquez sur le canevas avec l'outil Texte, T)."));
    connect(newTextAct_, &QAction::triggered, this, [this] { placeTextAt(QPointF(0.0, 0.0)); });

    // Outil de la palette : inséré juste après le Couteau. Il n'appartient pas au groupe
    // exclusif de la palette ; setTool() le synchronise comme les autres outils.
    toolTextAct_ = new QAction(icons::text(), tr("Texte"), this);
    toolTextAct_->setObjectName(QStringLiteral("tool_text"));
    toolTextAct_->setCheckable(true);
    toolTextAct_->setShortcut(QKeySequence(Qt::Key_T));
    toolTextAct_->setToolTip(tr("Texte (T)\nCliquez sur le canevas pour poser un texte brodé."));
    connect(toolTextAct_, &QAction::triggered, this, [this] { setTool(Tool::Text); });
    if (toolPalette_ != nullptr) {
        const QList<QAction*> actions = toolPalette_->actions();
        const int cut =
            toolCutAct_ != nullptr ? static_cast<int>(actions.indexOf(toolCutAct_)) : -1;
        QAction* before = (cut >= 0 && cut + 1 < actions.size()) ? actions.at(cut + 1) : nullptr;
        toolPalette_->insertAction(before, toolTextAct_);
    }
    textMenu_->addAction(toolTextAct_);

    textMenu_->addSeparator();
    editTextAct_ = textMenu_->addAction(tr("&Modifier le texte…"));
    editTextAct_->setObjectName(QStringLiteral("action_textEdit"));
    editTextAct_->setShortcut(QKeySequence(Qt::Key_F2));
    editTextAct_->setToolTip(
        tr("Rouvre le texte de la lettre sélectionnée (aussi : double-clic sur une lettre). Les "
           "lettres sont régénérées ; le tout reste annulable."));
    connect(editTextAct_, &QAction::triggered, this, &MainWindow::editSelectedText);
    removeTextAct_ = textMenu_->addAction(tr("&Supprimer le texte"));
    removeTextAct_->setObjectName(QStringLiteral("action_textRemove"));
    removeTextAct_->setToolTip(tr("Supprime le texte entier (toutes ses lettres)."));
    connect(removeTextAct_, &QAction::triggered, this, &MainWindow::removeSelectedText);

    if (propertiesPanel_ != nullptr) {
        connect(propertiesPanel_, &PropertiesPanel::editTextRequested, this,
                &MainWindow::editSelectedText);
    }
    updateTextActions();
}

std::optional<ObjectId> MainWindow::selectedTextId() const {
    for (const ObjectId id : selectedObjectIds()) {
        const auto* vector = project_.findObject(id);
        if (vector != nullptr && vector->text_owner && project_.findText(*vector->text_owner)) {
            return *vector->text_owner;
        }
    }
    if (selectedEmbroidery_) {
        const auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
        if (emb != nullptr) {
            if (const auto* text = project_.textOwnerOf(*emb)) {
                return text->id;
            }
        }
    }
    return std::nullopt;
}

void MainWindow::updateTextActions() {
    if (editTextAct_ == nullptr) {
        return;
    }
    const auto id = selectedTextId();
    const QString why = tr("Sélectionnez une lettre d'un texte.");
    for (QAction* act : {editTextAct_, removeTextAct_}) {
        act->setEnabled(id.has_value());
        act->setStatusTip(id ? QString() : why);
    }
    if (propertiesPanel_ != nullptr) {
        QString summary;
        if (id) {
            const document::TextObject* text = project_.findText(*id);
            const QString first =
                QString::fromStdString(text->text).section(QLatin1Char('\n'), 0, 0);
            summary = tr("Texte « %1 » — %2, %3 mm")
                          .arg(first.size() > 24 ? first.left(23) + QStringLiteral("…") : first,
                               QString::fromStdString(text->font.family),
                               QString::number(to_millimeters(text->cap_height).value, 'f', 1));
        }
        propertiesPanel_->setTextInfo(summary);
    }
}

void MainWindow::placeTextAt(QPointF posMm) {
    document::TextObject initial = lastTextSettings();
    initial.text.clear();
    initial.origin = sceneToModel(posMm);
    initial.rotation = Angle{0.0};
    if (openTextDialog(std::move(initial), /*isNew=*/true)) {
        // Un texte posé : retour à la sélection (comme Hatch), les lettres sont sélectionnées.
        setTool(Tool::Select);
    }
}

bool MainWindow::openTextDialog(document::TextObject initial, bool isNew) {
    TextDialog dialog(initial, isNew, this);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    return applyText(dialog.textObject(), isNew);
}

void MainWindow::editSelectedText() {
    const auto id = selectedTextId();
    if (!id) {
        statusBar()->showMessage(tr("Sélectionnez une lettre d'un texte pour le modifier."), 6000);
        return;
    }
    document::TextObject initial = *project_.findText(*id);
    // Les lettres ont pu être déplacées au canevas : l'édition repart de leur position visible.
    QString ignored;
    if (const auto font = FontCatalog::instance().load(initial.font, &ignored)) {
        if (const auto moved = lettering::text_displacement(project_, initial, *font)) {
            initial.origin = initial.origin + *moved;
        }
    }
    openTextDialog(std::move(initial), /*isNew=*/false);
}

bool MainWindow::applyText(document::TextObject text, bool isNew) {
    QString error;
    const auto font = FontCatalog::instance().load(text.font, &error);
    if (!font) {
        QMessageBox::warning(this, tr("Texte"), error);
        return false;
    }
    if (isNew) {
        text.id = project_.object_ids.next();
    } else if (project_.findText(text.id) == nullptr) {
        statusBar()->showMessage(tr("Ce texte n'existe plus."), 6000);
        return false;
    }
    auto built = lettering::build_text_objects(*font, text, project_.object_ids);
    if (!built) {
        QMessageBox::warning(this, tr("Texte"), QString::fromStdString(built.error().message));
        return false;
    }
    if (built->vectors.empty()) {
        statusBar()->showMessage(
            tr("Aucune lettre à broder : le texte est vide ou la police n'en contient aucune."),
            8000);
        return false;
    }
    std::vector<ObjectId> letters;
    letters.reserve(built->vectors.size());
    for (const auto& v : built->vectors) {
        letters.push_back(v.id);
    }
    const std::size_t count = built->layout.glyphs.size();
    undoStack_.execute(std::make_unique<commands::SetTextObjectCommand>(
                           text, std::move(built->vectors), std::move(built->embroideries),
                           isNew ? "Créer un texte" : "Modifier le texte"),
                       project_);
    lastTextSettings() = text;
    lastTextSettings().id = ObjectId{};
    showStitchesAct_->setChecked(true);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = letters});
    refreshImage();
    updateActions();

    QString message = isNew ? tr("Texte créé : %n lettre(s).", nullptr, static_cast<int>(count))
                            : tr("Texte modifié : %n lettre(s).", nullptr, static_cast<int>(count));
    if (!built->warnings.empty()) {
        QStringList notes;
        for (const auto& w : built->warnings) {
            notes << QString::fromStdString(w.message);
        }
        message += QStringLiteral(" ") + tr("Attention : ") + notes.join(QStringLiteral(" "));
    }
    statusBar()->showMessage(message + tr(" Ctrl+Z pour annuler."), 15000);
    return true;
}

void MainWindow::removeSelectedText() {
    const auto id = selectedTextId();
    if (!id) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveTextObjectCommand>(*id), project_);
    setSelection({});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Texte supprimé. Ctrl+Z pour annuler."), 8000);
}

} // namespace openstitch::desktop
