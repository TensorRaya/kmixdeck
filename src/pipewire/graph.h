// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QVector>
#include <memory>
#include <functional>
#include <optional>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_registry;
struct pw_node;
struct spa_hook;

namespace kmixdeck::pw {

/// One PipeWire node as seen through the registry, with the subset of Props we care about.
struct NodeInfo {
    uint32_t id = 0;
    QString name;          // node.name  (stable, our naming convention)
    QString description;   // node.description (display)
    QString mediaClass;    // Audio/Sink, Stream/Output/Audio, ...
    QString mediaName;     // media.name (WirePlumber stream-restore key)
    QString target;        // node.target if set
    QString appName;       // application.name
    QString appBinary;     // application.process.binary
    QString mediaRole;     // media.role (WirePlumber keys stream state by this FIRST — see ADR 0006)
    uint32_t serial = 0;   // object.serial (what target.object metadata takes)
    float volume = 1.0f;   // channelVolumes[0], linear
    bool mute = false;
    QString state;         // suspended / idle / running
};

/// Thin RAII wrapper around a pw_thread_loop + core + registry that mirrors the graph into
/// Qt-thread-safe signals. Everything PipeWire happens on the loop thread; we marshal to the
/// Qt thread with QMetaObject::invokeMethod(Qt::QueuedConnection).
class Graph : public QObject {
    Q_OBJECT
public:
    explicit Graph(QObject *parent = nullptr);
    ~Graph() override;

    bool connect();
    /// Drop the core connection and all mirrored state (emits nodeRemoved for everything). connect() again to resume.
    void teardown();
    bool isConnected() const { return m_connected; }

    /// Snapshot of all known nodes (Qt thread).
    QVector<NodeInfo> nodes() const;
    std::optional<NodeInfo> node(const QString &name) const;

    /// Set channelVolumes + mute on a node (ADR 0002: the cell fader is the loopback playback stream).
    /// Linear volume 0..1 (cubic mapping is the UI's business).
    void setVolume(uint32_t nodeId, float linear, bool mute);

    /// Create a null sink (channel or mix). Returns immediately; node appears via nodeAdded().
    void createNullSink(const QString &name, const QString &description, bool passive);
    /// Load a loopback module wiring `from` sink's monitor into `to` sink. Returns module id via callback.
    void createLoopback(const QString &name, const QString &description, const QString &from, const QString &to);
    /// Loopback for a mix output: capture from the mix, play to `device` (node.name) or stay unlinked ("").
    /// No dont-reconnect so it can be retargeted later; dont-fallback so it never hits the default sink.
    /// The hidden parking sink unrouted mix outputs play to (never default: priority.session 0, passive).
    void createParkingSink();
    void createMixOutput(const QString &mixSlug, const QString &description, const QString &device);
    /// Loopback exposing a mix as a virtual Audio/Source (for OBS/Discord).
    void createMixSource(const QString &mixSlug, const QString &description);
    void destroyObject(uint32_t id);
    /// Current sink a stream's output ports are linked to (node id), or 0. Derived from Link globals.
    uint32_t streamSink(uint32_t streamId) const;

    /// Route a stream to a sink: sets metadata target.object = <sink serial> on the stream node.
    /// This is exactly what wpctl set-default / pavucontrol do; WirePlumber persists it (restore-target).
    void setStreamTarget(uint32_t streamId, uint32_t sinkSerial);
    /// Remove target.object from a stream; with node.dont-fallback the stream is then unlinked.
    void clearStreamTarget(uint32_t streamId);
    /// Link a stream to a sink by name (looks up serial). Returns false if either is unknown.
    bool moveStream(uint32_t streamId, const QString &sinkNodeName);

Q_SIGNALS:
    void connected();
    void disconnected(const QString &reason);
    void nodeAdded(const kmixdeck::pw::NodeInfo &node);
    void nodeChanged(const kmixdeck::pw::NodeInfo &node);
    void nodeRemoved(uint32_t id);
    /// A stream's output is now linked to a different node (or none = 0).
    void streamRouted(uint32_t streamId, uint32_t sinkId);

public:
    struct Impl;   // public for the C callback trampolines
    /// For sibling PipeWire users in this process (Meters): the loop and core. Only valid while connected().
    pw_thread_loop *threadLoop() const;
    pw_core *core() const;
private:
    std::unique_ptr<Impl> d;
    bool m_connected = false;
};

} // namespace kmixdeck::pw

Q_DECLARE_METATYPE(kmixdeck::pw::NodeInfo)
