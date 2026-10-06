// SPDX-License-Identifier: Apache-2.0
#pragma once

// Unique point de construction d'un QNativeGestureEvent pour les tests : la
// signature du constructeur diffère selon la version de Qt (le constructeur à
// « quint64 intArgument » est déprécié depuis 6.2 ; celui avec nombre de doigts
// et delta est vérifié sur Qt 6.4.2 ; la CI construit avec 6.8.3). Ne pas construire l'évènement
// ailleurs.

#include <QNativeGestureEvent>
#include <QPointingDevice>

#include <memory>

inline std::unique_ptr<QNativeGestureEvent> makeNativeGesture(Qt::NativeGestureType type,
                                                              const QPointF& localPos, qreal value,
                                                              const QPointF& delta = QPointF()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    return std::make_unique<QNativeGestureEvent>(type, QPointingDevice::primaryPointingDevice(), 2,
                                                 localPos, localPos, localPos, value, delta);
#else
#error "Qt >= 6.2 requis pour QNativeGestureEvent"
#endif
}
