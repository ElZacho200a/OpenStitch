// SPDX-License-Identifier: Apache-2.0
#include "print_preview.hpp"

#include <QDialog>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>

namespace openstitch::desktop {

bool showPrintPreview(QWidget* parent, const QString& title, QPrinter& printer,
                      const std::function<void(QPrinter*)>& paint) {
    QPrintPreviewDialog preview(&printer, parent);
    preview.setObjectName(QStringLiteral("printPreviewDialog"));
    preview.setWindowTitle(title);
    QObject::connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
                     [&paint](QPrinter* p) { paint(p); });
    return preview.exec() == QDialog::Accepted;
}

bool printWithDialog(QWidget* parent, const QString& title, QPrinter& printer,
                     const std::function<void(QPrinter*)>& paint) {
    QPrintDialog dialog(&printer, parent);
    dialog.setWindowTitle(title);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    paint(&printer);
    return true;
}

} // namespace openstitch::desktop
