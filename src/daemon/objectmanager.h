// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QVariantMap>
#include <QMap>

namespace kmixdeck::daemon {

using InterfaceMap = QMap<QString, QVariantMap>;                 // a{sa{sv}}
using ManagedObjects = QMap<QDBusObjectPath, InterfaceMap>;      // a{oa{sa{sv}}}

/// org.freedesktop.DBus.ObjectManager on the root object. QtDBus ships no implementation, so this
/// one is hand-written to the spec: GetManagedObjects + InterfacesAdded/Removed.
class ObjectManagerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
public:
    using Provider = std::function<ManagedObjects()>;
    explicit ObjectManagerAdaptor(QObject *parent, Provider provider);

public Q_SLOTS:
    ManagedObjects GetManagedObjects();

Q_SIGNALS:
    void InterfacesAdded(const QDBusObjectPath &object_path, const InterfaceMap &interfaces_and_properties);
    void InterfacesRemoved(const QDBusObjectPath &object_path, const QStringList &interfaces);

private:
    Provider m_provider;
};

} // namespace kmixdeck::daemon

Q_DECLARE_METATYPE(kmixdeck::daemon::InterfaceMap)
Q_DECLARE_METATYPE(kmixdeck::daemon::ManagedObjects)
