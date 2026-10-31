// SPDX-License-Identifier: Apache-2.0
#include "stitch_type_catalog.hpp"

namespace openstitch::desktop {

QString stitchTypeLabel(int type) {
    static const char* const kLabels[kStitchTypeCount] = {
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Contour cousu"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage tatami"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Colonne satin"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage directionnel"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Blanket / zigzag"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Motif le long du contour"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage à motif"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage concentrique"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage radial"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage en spirale"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Remplissage croisé"),
        QT_TRANSLATE_NOOP("StitchTypeCatalog", "Point de croix")};
    if (type < 0 || type >= kStitchTypeCount) {
        return {};
    }
    return QCoreApplication::translate("StitchTypeCatalog", kLabels[type]);
}

} // namespace openstitch::desktop
