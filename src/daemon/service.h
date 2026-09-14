// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusContext>
#include <QHash>
#include <memory>
#include "mixer.h"
#include "objectmanager.h"

namespace kmixdeck::daemon {

constexpr const char *kBusName = "org.kmixdeck1";
constexpr const char *kRootPath = "/org/kmixdeck1";

/// Emits org.freedesktop.DBus.Properties.PropertiesChanged for one interface on one path.
void emitPropertiesChanged(const QString &path, const QString &iface, const QVariantMap &changed);

/// One exported object (channel, mix or cell). Owns its QObject on the bus.
class ExportedObject : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    ExportedObject(const QString &path, QObject *parent) : QObject(parent), m_path(path) {}
    const QString &path() const { return m_path; }
    virtual QString interfaceName() const = 0;
    virtual QVariantMap properties() const = 0;     // for GetManagedObjects / InterfacesAdded
protected:
    QString m_path;
};

// ---- org.kmixdeck1.Cell ----------------------------------------------------------------
class CellObject : public ExportedObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Cell")
    Q_PROPERTY(QDBusObjectPath Channel READ channel CONSTANT)
    Q_PROPERTY(QDBusObjectPath Mix READ mix CONSTANT)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(bool Muted READ muted WRITE setMuted)
public:
    CellObject(Mixer *mixer, const QString &ch, const QString &mix, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Cell"); }
    QVariantMap properties() const override;
    QDBusObjectPath channel() const; QDBusObjectPath mix() const;
    double volume() const; void setVolume(double linear);
    bool muted() const; void setMuted(bool m);
    void notifyChanged();   // called by the daemon when Mixer says the cell changed
public Q_SLOTS:
    void SetVolumeDb(double db);
private:
    Mixer *m_mixer; QString m_ch, m_mix;
};

// ---- org.kmixdeck1.Channel -------------------------------------------------------------
class ChannelObject : public ExportedObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Channel")
    Q_PROPERTY(QString Slug READ slug CONSTANT)
    Q_PROPERTY(QString Name READ name WRITE setName)
    Q_PROPERTY(QString Icon READ icon WRITE setIcon)
    Q_PROPERTY(double Trim READ trim WRITE setTrim)
    Q_PROPERTY(bool Muted READ muted WRITE setMuted)
    Q_PROPERTY(QString NodeName READ nodeName CONSTANT)
public:
    ChannelObject(Mixer *mixer, const QString &slug, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Channel"); }
    QVariantMap properties() const override;
    QString slug() const { return m_slug; }
    QString name() const; void setName(const QString &);
    QString icon() const { return m_icon; } void setIcon(const QString &i);
    double trim() const; void setTrim(double);
    bool muted() const; void setMuted(bool);
    QString nodeName() const { return Names::channelNode(m_slug); }
private:
    Mixer *m_mixer; QString m_slug, m_icon;
};

// ---- org.kmixdeck1.Mix -----------------------------------------------------------------
class MixObject : public ExportedObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Mix")
    Q_PROPERTY(QString Slug READ slug CONSTANT)
    Q_PROPERTY(QString Name READ name WRITE setName)
    Q_PROPERTY(QString Icon READ icon WRITE setIcon)
    Q_PROPERTY(QString OutputDevice READ outputDevice WRITE setOutputDevice)
    Q_PROPERTY(QString CaptureSource READ captureSource CONSTANT)
    Q_PROPERTY(QString NodeName READ nodeName CONSTANT)
public:
    MixObject(Mixer *mixer, const QString &slug, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Mix"); }
    QVariantMap properties() const override;
    QString slug() const { return m_slug; }
    QString name() const; void setName(const QString &);
    QString icon() const { return m_icon; } void setIcon(const QString &i);
    QString outputDevice() const; void setOutputDevice(const QString &);
    QString captureSource() const;
    QString nodeName() const { return Names::mixNode(m_slug); }
private:
    Mixer *m_mixer; QString m_slug, m_icon;
};

// ---- org.kmixdeck1.App ------------------------------------------------------------------
class AppObject : public ExportedObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.App")
    Q_PROPERTY(QString Name READ name CONSTANT)
    Q_PROPERTY(QString Binary READ binary CONSTANT)
    Q_PROPERTY(QString MediaName READ mediaName CONSTANT)
    Q_PROPERTY(QString MediaRole READ mediaRole CONSTANT)
    Q_PROPERTY(uint NodeId READ nodeId CONSTANT)
    Q_PROPERTY(QDBusObjectPath Channel READ channel)
public:
    AppObject(Mixer *mixer, uint32_t id, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.App"); }
    QVariantMap properties() const override;
    QString name() const; QString binary() const; QString mediaName() const; QString mediaRole() const;
    uint nodeId() const { return m_id; }
    QDBusObjectPath channel() const;
    void notifyChanged();
public Q_SLOTS:
    void MoveTo(const QDBusObjectPath &channel);
private:
    Mixer *m_mixer; uint32_t m_id;
};

// ---- org.kmixdeck1.Mixer (root) --------------------------------------------------------
class MixerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Mixer")
    Q_PROPERTY(QString Version READ version CONSTANT)
    Q_PROPERTY(bool Connected READ connected)
    Q_PROPERTY(StringMap OutputDevices READ outputDevices)     // a{ss}: node.name → description
public:
    MixerAdaptor(Mixer *mixer, QObject *parent);
    QString version() const;
    bool connected() const;
    StringMap outputDevices() const;
public Q_SLOTS:
    QDBusObjectPath AddChannel(const QString &name);
    QDBusObjectPath AddMix(const QString &name);
    void RemoveChannel(const QDBusObjectPath &path);
    void RemoveMix(const QDBusObjectPath &path);
    void Save();
private:
    Mixer *m_mixer;
};

/// The service: owns Mixer (the only PipeWire client) and keeps the bus objects in sync with it.
class Service : public QObject {
    Q_OBJECT
public:
    explicit Service(QObject *parent = nullptr);
    bool start();   // register name + root; returns false if the name is taken

    static QString channelPath(const QString &slug) { return QStringLiteral("%1/channel/%2").arg(QLatin1String(kRootPath), slug); }
    static QString mixPath(const QString &slug)     { return QStringLiteral("%1/mix/%2").arg(QLatin1String(kRootPath), slug); }
    static QString appPath(uint32_t id)             { return QStringLiteral("%1/app/%2").arg(QLatin1String(kRootPath)).arg(id); }
    static QString cellPath(const QString &ch, const QString &mix) { return QStringLiteral("%1/cell/%2/%3").arg(QLatin1String(kRootPath), ch, mix); }

private:
    void syncObjects();      // diff Mixer layout against exported objects
    void exportObject(ExportedObject *o);
    void unexportObject(const QString &path);
    ManagedObjects managedObjects() const;

    Mixer m_mixer;
    QObject m_root;
    MixerAdaptor *m_mixerAdaptor = nullptr;
    ObjectManagerAdaptor *m_om = nullptr;
    QHash<QString, ExportedObject *> m_objects;   // path → object
};

} // namespace kmixdeck::daemon
