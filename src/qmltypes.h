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
    // The instance is created in main() and shared with the KDE integration; QML must not own it.
    static void setInstance(kmixdeck::frontend::MixerClient *c) { s_instance = c; }
    static kmixdeck::frontend::MixerClient *create(QQmlEngine *, QJSEngine *) {
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership); return s_instance;
    }
private:
    static inline kmixdeck::frontend::MixerClient *s_instance = nullptr;
};
