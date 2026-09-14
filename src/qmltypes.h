// SPDX-License-Identifier: GPL-3.0-or-later
// QML registration lives in the app target; kmixdeck_core stays QML-free so tests link it directly.
#pragma once
#include <QQmlEngine>
#include "mixer.h"

struct MixerForeign {
    Q_GADGET
    QML_FOREIGN(kmixdeck::Mixer)
    QML_NAMED_ELEMENT(Mixer)
    QML_SINGLETON
public:
    static kmixdeck::Mixer *create(QQmlEngine *, QJSEngine *) { return new kmixdeck::Mixer; }
};
