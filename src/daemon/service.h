// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QSet>
#include <QTimer>
#include <QDBusContext>
#include <QDebug>
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
    /// Property setters run through QMetaProperty::write, NOT through the D-Bus method dispatcher: there is no
    /// QDBusContext to answer on, and calling sendErrorReply() there dereferences null (segfault — found via
    /// `busctl set-property … Trim d 5.0`). QtDBus answers the Set call itself; the most we can do is refuse the
    /// value and say why in the log. Value-range checks that MUST surface to the client belong on methods
    /// (SetVolumeDb, ToggleMute, MoveTo …), which do have a context.
    void rejectProperty(const QString &name, const QString &why) const { qWarning().noquote() << QStringLiteral("%1: refused %2 (%3)").arg(m_path, name, why); }
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
    Q_PROPERTY(QDBusObjectPath Follows READ follows WRITE setFollows)   // MX-7: mix path this cell mirrors; "/" = none
public:
    CellObject(Mixer *mixer, const QString &ch, const QString &mix, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Cell"); }
    QVariantMap properties() const override;
    QDBusObjectPath channel() const; QDBusObjectPath mix() const;
    double volume() const; void setVolume(double linear);
    bool muted() const; void setMuted(bool m);
    QDBusObjectPath follows() const; void setFollows(const QDBusObjectPath &p);
    void notifyChanged();   // called by the daemon when Mixer says the cell changed
public Q_SLOTS:
    void SetVolumeDb(double db);
    void ToggleMute();
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
    Q_PROPERTY(QString Color READ color WRITE setColor)   // MX-5: "#rrggbb" or "" = theme
    Q_PROPERTY(double Trim READ trim WRITE setTrim)
    Q_PROPERTY(double Pan READ pan WRITE setPan)     // DV-22: −1..+1
    Q_PROPERTY(bool Muted READ muted WRITE setMuted)
    Q_PROPERTY(QString NodeName READ nodeName CONSTANT)
    Q_PROPERTY(QString InputDevice READ inputDevice WRITE setInputDevice)
    Q_PROPERTY(bool InputPresent READ inputPresent)
    Q_PROPERTY(QStringList Inputs READ inputs)     // ADR 0009 B1: all wires, InputDevice == Inputs[0]
    Q_PROPERTY(QString FxChain READ fxChainJson)   // FX-1…FX-7: JSON {enabled, chain:[…]}, "" = none
public:
    ChannelObject(Mixer *mixer, const QString &slug, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Channel"); }
    QVariantMap properties() const override;
    QString slug() const { return m_slug; }
    QString name() const; void setName(const QString &);
    QString icon() const; void setIcon(const QString &i);
    QString color() const; void setColor(const QString &c);
    double trim() const; void setTrim(double);
    double pan() const; void setPan(double);
    bool muted() const; void setMuted(bool);
    QString nodeName() const { return Names::channelNode(m_slug); }
    QString inputDevice() const; void setInputDevice(const QString &);
    QStringList inputs() const;
    bool inputPresent() const;
    QString fxChainJson() const;
public Q_SLOTS:
    bool SetWireTrim(const QString &ref, double trim, bool muted);   // DV-14, input wire by ref (as in Inputs)
    double WireTrim(const QString &ref);   // -1 = no such wire
    bool   WireMuted(const QString &ref);
    void ToggleMute();
    bool AddInput(const QString &ref);              // ADR 0009 B1: one more wire into this channel
    bool RemoveInput(const QString &ref);
    bool SetFx(const QString &chainJson);           // FX-1: replace the chain (validated; false = refused)
    bool SetFxControl(const QString &control, double value);   // FX-3 live
private:
    Mixer *m_mixer; QString m_slug;
};

// ---- org.kmixdeck1.Mix -----------------------------------------------------------------
class MixObject : public ExportedObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Mix")
    Q_PROPERTY(QString Slug READ slug CONSTANT)
    Q_PROPERTY(QString Name READ name WRITE setName)
    Q_PROPERTY(QString Icon READ icon WRITE setIcon)
    Q_PROPERTY(QString Color READ color WRITE setColor)   // MX-5: "#rrggbb" or "" = theme
    Q_PROPERTY(QString OutputDevice READ outputDevice WRITE setOutputDevice)
    Q_PROPERTY(QStringList Outputs READ outputs)                       // MX-9: all hardware outputs, node.name each
    Q_PROPERTY(QStringList OutputDescriptions READ outputDescriptions) // parallel to Outputs: last seen node.description (DV-9: name an unplugged device)
    Q_PROPERTY(QString FallbackOutput READ fallbackOutput WRITE setFallbackOutput)   // DV-15: used while every output is absent
    Q_PROPERTY(QString CaptureSource READ captureSource CONSTANT)
    Q_PROPERTY(QString NodeName READ nodeName CONSTANT)
    Q_PROPERTY(bool OutputPresent READ outputPresent)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)   // master, linear 0..1 (MX-6)
    Q_PROPERTY(bool Muted READ muted WRITE setMuted)
    Q_PROPERTY(QString FxChain READ fxChainJson)   // FX-6: the same chain model on the output side
public:
    MixObject(Mixer *mixer, const QString &slug, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.Mix"); }
    QVariantMap properties() const override;
    QString slug() const { return m_slug; }
    QString name() const; void setName(const QString &);
    QString icon() const; void setIcon(const QString &i);
    QString color() const; void setColor(const QString &c);
    QString outputDevice() const; void setOutputDevice(const QString &);
    QString captureSource() const;
    QString nodeName() const { return Names::mixNode(m_slug); }
    bool outputPresent() const;
    double volume() const; void setVolume(double);
    bool muted() const; void setMuted(bool);
    QStringList outputs() const;
    QStringList outputDescriptions() const;
    QString fallbackOutput() const; void setFallbackOutput(const QString &);
    QString fxChainJson() const;
public Q_SLOTS:
    void ToggleMute();
    void AddOutput(const QString &nodeName);
    void RemoveOutput(const QString &nodeName);
    /// DV-14: trim (cubic 0..1) and mute of ONE output wire, addressed by its ref (as listed in Outputs). Lives on the wire.
    bool SetWireTrim(const QString &ref, double trim, bool muted);
    double WireTrim(const QString &ref);   // -1 = no such wire
    bool   WireMuted(const QString &ref);
    bool SetFx(const QString &chainJson);
    bool SetFxControl(const QString &control, double value);
private:
    Mixer *m_mixer; QString m_slug;
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
    // UX-10: the app's own icon (application.icon-name), whether it is producing sound, CH-12 full assignment
    Q_PROPERTY(QString Icon READ icon CONSTANT)
    Q_PROPERTY(bool Running READ running)
    Q_PROPERTY(QStringList Channels READ channels)
public:
    AppObject(Mixer *mixer, uint32_t id, QObject *parent);
    QString interfaceName() const override { return QStringLiteral("org.kmixdeck1.App"); }
    QVariantMap properties() const override;
    QString name() const; QString binary() const; QString mediaName() const; QString mediaRole() const;
    QString icon() const; bool running() const; QStringList channels() const;
    uint nodeId() const { return m_id; }
    QDBusObjectPath channel() const;
    void notifyChanged();
public Q_SLOTS:
    void MoveTo(const QDBusObjectPath &channel);
    /// CH-12: assign to several channels at once; AddOn=true keeps existing ones and appends (UX-11 drop).
    void Assign(const QStringList &channelPaths, bool addOn);
private:
    Mixer *m_mixer; uint32_t m_id;
};

// ---- org.kmixdeck1.Levels (root object, ADR 0006) ----------------------------------------------
class LevelsAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Levels")
    Q_PROPERTY(uint Rate READ rate)
    Q_PROPERTY(uint Subscribers READ subscribers)
public:
    LevelsAdaptor(Mixer *mixer, QObject *parent);
    uint rate() const;
    uint subscribers() const { return static_cast<uint>(m_subscribers.size()); }
public Q_SLOTS:
    void Subscribe(const QDBusMessage &msg);
    void Unsubscribe(const QDBusMessage &msg);
Q_SIGNALS:
    void Peaks(const QVariantMap &peaks);
private Q_SLOTS:
    void onNameOwnerChangedSlot(const QString &name, const QString &oldOwner, const QString &newOwner) { onNameOwnerChanged(name, oldOwner, newOwner); }
private:
    void syncTargets();
    QString meterKey(const QString &nodeName) const;
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    Mixer *m_mixer;
    QSet<QString> m_subscribers;      // unique bus names
    QHash<QString, uint32_t> m_appNodes;   // UX-13: app node name → app id (bus key app/<id>)
    QTimer m_teardown;                // grace period after the last unsubscribe
};

// ---- org.kmixdeck1.Mixer (root) --------------------------------------------------------
/// The registered root object. QDBusContext only works on the object that was registerObject()'ed — an
/// adaptor calling sendErrorReply() on itself dereferences a null context (segfault, found by test
/// test_add_channel_with_empty_or_symbol_only_name_is_rejected). Adaptors reach the context through this.
class RootObject : public QObject, public QDBusContext {
    Q_OBJECT
public:
    using QObject::QObject;
    void replyError(const QString &name, const QString &msg) { if (calledFromDBus()) sendErrorReply(name, msg); }
};

class MixerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kmixdeck1.Mixer")
    Q_PROPERTY(QString Version READ version CONSTANT)
    Q_PROPERTY(bool Connected READ connected)
    Q_PROPERTY(StringMap OutputDevices READ outputDevices)     // a{ss}: node.name → description
    Q_PROPERTY(StringMap InputDevices READ inputDevices)
    Q_PROPERTY(PortMap DevicePorts READ devicePorts)     // ADR 0009 D4: node → ["POS|port.name|port.alias", …]
    Q_PROPERTY(QStringList VirtualDevices READ virtualDevices)   // DV-23: node.names (input side) of our virtual devices
    Q_PROPERTY(QStringList HiddenDevices READ hiddenDevices)     // CH-11: node.names every picker leaves out
    Q_PROPERTY(bool FirstRun READ firstRun)                      // UX-3: no layout on disk when the daemon started
    Q_PROPERTY(QString DefaultSink READ defaultSink)             // UX-3: session default output (node.name)
    Q_PROPERTY(QString DefaultSource READ defaultSource)         // UX-3: session default input (node.name)
    Q_PROPERTY(QDBusObjectPath DefaultChannel READ defaultChannel WRITE setDefaultChannel)   // CH-5; "/" = off
    Q_PROPERTY(QString ListeningDevice READ listeningDevice WRITE setListeningDevice)         // UX-2; node.name or ""
    Q_PROPERTY(QString UndoDescription READ undoDescription)   // CH-9: "" = nothing to undo, else e.g. channel “Music”
    Q_PROPERTY(QStringList ChannelOrder READ channelOrder)     // UX-9: display order, slugs
    Q_PROPERTY(QStringList MixOrder READ mixOrder)
    Q_PROPERTY(QString FxTypes READ fxTypes CONSTANT)          // FX-4: built-in catalog as JSON [{type,label,params:[…]}]
    Q_PROPERTY(QString FxPresets READ fxPresets CONSTANT)      // FX-4: name → chain JSON
public:
    MixerAdaptor(Mixer *mixer, QObject *parent);
    QString version() const;
    bool connected() const;
    StringMap outputDevices() const;
    StringMap inputDevices() const;
    PortMap devicePorts() const;
    QStringList virtualDevices() const;
    QStringList hiddenDevices() const { return m_mixer->hiddenDevices(); }
    bool firstRun() const { return m_mixer->firstRun(); }
    QString defaultSink() const { return m_mixer->firstRunPlan().value(QStringLiteral("defaultSink")).toString(); }
    QString defaultSource() const { return m_mixer->firstRunPlan().value(QStringLiteral("defaultSource")).toString(); }
    QDBusObjectPath defaultChannel() const;
    void setDefaultChannel(const QDBusObjectPath &p);
    QString listeningDevice() const { return m_mixer->listeningDevice(); }
    void setListeningDevice(const QString &node) { m_mixer->setListeningDevice(node); }
    QString undoDescription() const { return m_mixer->undoDescription(); }
    QString fxTypes() const;
    QString fxPresets() const;
    QStringList channelOrder() const;
    QStringList mixOrder() const;
public Q_SLOTS:
    void Undo();                                      // CH-9: restore the last removed channel/mix
    QString Export();                                 // CT-7
    QString FirstRunPlan();                           // UX-3: JSON of what FirstRunApply would do
    QString FirstRunApply();                          // UX-3: does it, returns JSON of what was done
    QDBusObjectPath DuplicateMix(const QDBusObjectPath &source, const QString &name);   // MX-8
    void SetDeviceHidden(const QString &node, bool hidden);                              // CH-11
    void Import(const QString &json);                 // CT-7
    /// UX-12 solo audition: hold = exactly one entity reaches the main output, release restores previous state.
    void Audition(const QDBusObjectPath &path);       // channel or mix path; empty path = stop
    void MoveChannel(const QDBusObjectPath &path, int index);   // UX-9
    void MoveMix(const QDBusObjectPath &path, int index);
    QDBusObjectPath AddChannel(const QString &name);
    QDBusObjectPath AddMix(const QString &name);
    QString AddVirtualDevice(const QString &name, int inputs, int outputs);   // DV-23 → node.name of the input side
    void RemoveVirtualDevice(const QString &slugOrNode);
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
    RootObject m_root;
    MixerAdaptor *m_mixerAdaptor = nullptr;
    LevelsAdaptor *m_levelsAdaptor = nullptr;
    ObjectManagerAdaptor *m_om = nullptr;
    QHash<QString, ExportedObject *> m_objects;   // path → object
};

} // namespace kmixdeck::daemon
