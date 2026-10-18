// SPDX-License-Identifier: Apache-2.0
#pragma once

// Outils de mise en page pour petits écrans (HP-UX-024 à 027) : ni un dialogue ni un panneau
// ne doit imposer une taille supérieure à celle de l'écran. Gestes :
//   - `scrollDialogBody` : le contenu long d'un dialogue défile, les boutons restent visibles ;
//   - `fitDialogToScreen` : à l'affichage, un dialogue est ramené à 92 % de la zone utile ;
//   - `scrollableDockContent` : le contenu d'un dock défile au lieu d'imposer sa taille ;
//   - `ElidedTextLabel` : étiquette dont le texte s'élide au lieu d'imposer sa largeur.
// Aucune logique métier ici : uniquement de la géométrie.
#include <QBoxLayout>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QObject>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QWidget>

namespace openstitch::desktop {

// Part de la zone utile de l'écran qu'un dialogue peut occuper.
inline constexpr double kDialogScreenFraction = 0.92;

// Déplace tout le contenu du layout racine (un QBoxLayout) du dialogue, sauf `footer`, dans une
// zone défilante transparente ; `footer` (barre de boutons) reste hors de la zone défilante.
// À appeler une fois le layout racine entièrement construit. Les facteurs d'étirement sont
// conservés. Renvoie la zone défilante (ou nullptr si le dialogue n'a pas de layout boîte).
inline QScrollArea* scrollDialogBody(QDialog* dialog, QWidget* footer = nullptr) {
    auto* root = qobject_cast<QBoxLayout*>(dialog->layout());
    if (root == nullptr) {
        return nullptr;
    }
    auto* body = new QWidget(dialog);
    auto* inner = new QBoxLayout(root->direction(), body);
    inner->setSpacing(root->spacing());
    inner->setContentsMargins(0, 0, 0, 0);
    QLayoutItem* footerItem = nullptr;
    while (root->count() > 0) {
        const int stretch = root->stretch(0);
        QLayoutItem* item = root->takeAt(0);
        if (footer != nullptr && item->widget() == footer) {
            footerItem = item;
            continue;
        }
        inner->addItem(item);
        inner->setStretch(inner->count() - 1, stretch);
    }
    auto* scroll = new QScrollArea(dialog);
    scroll->setObjectName(QStringLiteral("dialogScrollBody"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    body->setAutoFillBackground(false);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
    if (footerItem != nullptr) {
        root->addItem(footerItem);
    }
    return scroll;
}

// Enveloppe `content` dans une zone défilante transparente (redimensionnement automatique du
// contenu) à donner à `QDockWidget::setWidget` : le dock peut alors rétrécir sous la taille
// que son contenu réclamerait, une barre de défilement apparaît à la place.
inline QScrollArea* scrollableDockContent(QWidget* content, QWidget* parent) {
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("dockScrollBody"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    return scroll;
}

// Étiquette à texte élidé : sa taille souhaitée est celle du texte complet, mais elle accepte
// d'être réduite (minimum étroit) et le texte complet reste en infobulle. À utiliser dans les
// barres d'outils et la barre d'état, où une étiquette ordinaire fixe la largeur minimale de
// la fenêtre.
class ElidedTextLabel : public QLabel {
public:
    explicit ElidedTextLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent) {
        setToolTip(text);
    }
    explicit ElidedTextLabel(QWidget* parent = nullptr) : QLabel(parent) {}

    [[nodiscard]] QSize minimumSizeHint() const override {
        return {kMinWidthPx, QLabel::minimumSizeHint().height()};
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        const QRect area = contentsRect();
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, area.width());
        style()->drawItemText(&painter, area, static_cast<int>(alignment()) | Qt::TextSingleLine,
                              palette(), isEnabled(), shown, foregroundRole());
    }

private:
    static constexpr int kMinWidthPx = 24;
};

namespace detail {
class DialogScreenFitter : public QObject {
public:
    explicit DialogScreenFitter(QDialog* dialog) : QObject(dialog), dialog_(dialog) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == dialog_ && event->type() == QEvent::Show) {
            const QScreen* screen = dialog_->screen();
            if (screen == nullptr) {
                screen = QGuiApplication::primaryScreen();
            }
            if (screen != nullptr) {
                const QSize area = screen->availableGeometry().size();
                const QSize limit(static_cast<int>(area.width() * kDialogScreenFraction),
                                  static_cast<int>(area.height() * kDialogScreenFraction));
                dialog_->resize(dialog_->size().boundedTo(limit));
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QDialog* dialog_;
};
} // namespace detail

// Ramène le dialogue à `kDialogScreenFraction` de la zone utile de son écran à chaque affichage
// (la taille mémorisée d'une autre session ou d'un autre écran ne le fait jamais déborder).
inline void fitDialogToScreen(QDialog* dialog) {
    dialog->installEventFilter(new detail::DialogScreenFitter(dialog));
}

} // namespace openstitch::desktop
