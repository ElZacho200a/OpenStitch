// SPDX-License-Identifier: Apache-2.0
#include "help_manual.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

// Ressource d'une bibliothèque statique : à enregistrer explicitement, à la portée globale.
static void initManualResource() {
    Q_INIT_RESOURCE(help_manual);
}

namespace openstitch::desktop {

QString manualMarkdown() {
    initManualResource();
    QFile file(QStringLiteral(":/help/user-guide.md"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString manualHeadingForTool(Tool tool) {
    switch (tool) {
    case Tool::Select:
    case Tool::Pan:
    case Tool::Rect:
        return QStringLiteral("Souris et clavier");
    case Tool::DrawRectangle:
    case Tool::DrawEllipse:
    case Tool::DrawPolygon:
    case Tool::DrawPolygonRegular:
    case Tool::DrawBezier:
    case Tool::DrawFreeform:
    case Tool::Cut:
    case Tool::Brush:
    case Tool::Bucket:
        return QStringLiteral("Menu Forme");
    case Tool::DrawSatinColumn:
    case Tool::DrawDirectionGuide:
    case Tool::DrawBreakLine:
        return QStringLiteral("Menu Broderie");
    case Tool::Text:
        return QStringLiteral("Menu Texte (lettrage)");
    case Tool::Measure:
    case Tool::Eyedropper:
        return QStringLiteral("Outils Règle et Pipette");
    case Tool::ClickToStitch:
        return QStringLiteral("Menu Segmentation");
    }
    return QStringLiteral("Souris et clavier");
}

namespace {
struct PanelTopic {
    const char* panel;
    const char* heading;
};
constexpr PanelTopic kPanelTopics[] = {
    {"properties", "Inspecteur de propriétés"},
    {"document", "Panneau Document"},
    {"workflow", "Indicateur de workflow"},
    {"analysis", "Menu Analyse et panneau"},
    {"order", "Panneau Ordre de couture"},
    {"filters", "Panneau Filtres d'affichage"},
    {"threads", "Panneau Fils et menu Fils"},
    {"colors", "Panneau Fils et menu Fils"},
    {"history", "Menu Édition"},
};
} // namespace

QStringList manualPanelHeadings() {
    QStringList out;
    for (const PanelTopic& t : kPanelTopics) {
        out << QString::fromUtf8(t.heading);
    }
    return out;
}

QString manualHeadingForPanel(const QString& panel) {
    for (const PanelTopic& t : kPanelTopics) {
        if (panel == QLatin1String(t.panel)) {
            return QString::fromUtf8(t.heading);
        }
    }
    return {};
}

QStringList markdownHeadings(const QString& markdown) {
    QStringList out;
    bool inFence = false;
    for (const QString& raw : markdown.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QStringLiteral("```"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence) {
            continue;
        }
        for (const QString& prefix : {QStringLiteral("### "), QStringLiteral("## ")}) {
            if (line.startsWith(prefix)) {
                out << line.mid(prefix.size()).trimmed();
                break;
            }
        }
    }
    return out;
}

QImage tutorialSampleImage() {
    QImage image(480, 360, QImage::Format_RGB32);
    image.fill(QColor(250, 250, 250));
    QPainter p(&image);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(200, 40, 50)); // disque rouge
    p.drawEllipse(QRect(40, 60, 200, 200));
    p.setBrush(QColor(40, 90, 190)); // rectangle bleu
    p.drawRect(QRect(260, 50, 170, 120));
    p.setBrush(QColor(240, 190, 30)); // triangle jaune
    const QPoint triangle[3] = {QPoint(300, 320), QPoint(360, 190), QPoint(430, 320)};
    p.drawPolygon(triangle, 3);
    p.end();
    return image;
}

ManualDialog::ManualDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("dialog_manual"));
    setWindowTitle(tr("Manuel d'utilisation"));
    setModal(false);
    resize(860, 640);

    auto* layout = new QVBoxLayout(this);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("edit_manualSearch"));
    search_->setPlaceholderText(tr("Rechercher dans le manuel…"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);

    browser_ = new QTextBrowser(this);
    browser_->setObjectName(QStringLiteral("browser_manual"));
    browser_->setOpenLinks(false);
    browser_->setMarkdown(manualMarkdown());
    layout->addWidget(browser_, 1);

    auto* close = new QPushButton(tr("Fermer"), this);
    close->setObjectName(QStringLiteral("button_manualClose"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    layout->addWidget(close, 0, Qt::AlignRight);

    connect(search_, &QLineEdit::returnPressed, this, [this] { search(search_->text()); });
}

bool ManualDialog::showTopic(const QString& heading) {
    for (QTextBlock block = browser_->document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0 && block.text().trimmed() == heading) {
            browser_->setTextCursor(QTextCursor(block));
            browser_->ensureCursorVisible();
            currentTopic_ = heading;
            return true;
        }
    }
    return false;
}

bool ManualDialog::search(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return false;
    }
    const QTextCursor found = browser_->document()->find(text);
    if (found.isNull()) {
        return false;
    }
    browser_->setTextCursor(found);
    browser_->ensureCursorVisible();
    return true;
}

} // namespace openstitch::desktop
