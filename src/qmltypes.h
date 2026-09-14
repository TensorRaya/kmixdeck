// SPDX-License-Identifier: GPL-3.0-or-later
// QML registration for the KDE frontend. "Mixer" in QML is the D-Bus client mirror, not the in-process model.
#pragma once
#include <QQmlEngine>
#include "frontend/mixerclient.h"

struct MixerForeign {
    Q_GADGET
    QML_FOREIGN(kmixdeck::frontend::MixerClient)
    QML_NAMED_ELEMENT(Mixer)
    QML_SINGLETON
public:
    static kmixdeck::frontend::MixerClient *create(QQmlEngine *, QJSEngine *) { return new kmixdeck::frontend::MixerClient; }
};
