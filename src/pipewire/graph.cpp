// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "graph.h"
#include <cerrno>
#include <cstring>
#include "../logging.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <pipewire/extensions/metadata.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>
#include <spa/debug/pod.h>

#include <QMetaObject>
#include <QDebug>
#include <algorithm>
#include <mutex>
#include <optional>

namespace kmixdeck::pw {

// "[ FL FR ]" / "[FL,FR]" → {"FL","FR"}. audio.position on the node props is what the adapter was configured
// with; for ALSA nodes it is the port layout of the active profile (AUX0..AUXn in Pro Audio) — DV-13.
static QStringList parsePositions(const QString &s) {
    QString t = s; t.remove(QLatin1Char('[')).remove(QLatin1Char(']')).replace(QLatin1Char(','), QLatin1Char(' '));
    return t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

struct NodeProxy {
    Graph::Impl *impl = nullptr;
    uint32_t id = 0;
    pw_proxy *proxy = nullptr;
    spa_hook listener{};
    spa_hook proxyListener{};
    NodeInfo info;
    bool announced = false;
};

struct Graph::Impl {
    Graph *q = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    pw_metadata *metadata = nullptr;      // "default" metadata object, for target.object
    spa_hook metadataListener{};
    QString defaultSink, defaultSource;   // guarded by snapshotMutex (UX-3)
    spa_hook registryListener{};
    spa_hook coreListener{};
    QHash<uint32_t, NodeProxy *> nodes;           // loop thread only
    struct LinkInfo { uint32_t out = 0, in = 0; };
    QHash<uint32_t, PortInfo> ports;      // port globals of every mirrored node (DV-13), guarded by snapshotMutex
    QHash<uint32_t, LinkInfo> links;              // loop thread only: link id → (output node, input node)
    QHash<uint32_t, uint32_t> routes;             // guarded by snapshotMutex: stream node → sink node
    mutable std::mutex snapshotMutex;
    QHash<uint32_t, NodeInfo> snapshot;           // guarded by snapshotMutex, read from Qt thread

    // --- helpers
    static QString prop(const spa_dict *d, const char *key) {
        const char *v = d ? spa_dict_lookup(d, key) : nullptr;
        return v ? QString::fromUtf8(v) : QString();
    }

    void publish(NodeProxy *np, bool added) {
        {
            std::lock_guard<std::mutex> g(snapshotMutex);
            snapshot[np->id] = np->info;
        }
        NodeInfo copy = np->info;
        QMetaObject::invokeMethod(q, [q = this->q, copy, added] {
            if (added) Q_EMIT q->nodeAdded(copy); else Q_EMIT q->nodeChanged(copy);
        }, Qt::QueuedConnection);
    }

    // --- node events
    static void onNodeInfo(void *data, const pw_node_info *info) {
        auto *np = static_cast<NodeProxy *>(data);
        if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
            np->info.name = prop(info->props, PW_KEY_NODE_NAME);
            np->info.description = prop(info->props, PW_KEY_NODE_DESCRIPTION);
            np->info.mediaClass = prop(info->props, PW_KEY_MEDIA_CLASS);
            np->info.mediaName = prop(info->props, PW_KEY_MEDIA_NAME);
            np->info.target = prop(info->props, PW_KEY_TARGET_OBJECT);
            np->info.configuredTarget = prop(info->props, PW_KEY_NODE_TARGET);
            np->info.appName = prop(info->props, PW_KEY_APP_NAME);
            np->info.appBinary = prop(info->props, PW_KEY_APP_PROCESS_BINARY);
            np->info.mediaRole = prop(info->props, PW_KEY_MEDIA_ROLE);
            np->info.iconName = prop(info->props, PW_KEY_APP_ICON_NAME);
            { const QString p = prop(info->props, "audio.position"); if (!p.isEmpty()) np->info.positions = parsePositions(p); }
            if (np->info.serial == 0) np->info.serial = prop(info->props, PW_KEY_OBJECT_SERIAL).toUInt();
        }
        if (info->change_mask & PW_NODE_CHANGE_MASK_STATE) {
            np->info.state = QString::fromUtf8(pw_node_state_as_string(info->state));
        }
        if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
            for (uint32_t i = 0; i < info->n_params; ++i) {
                if (info->params[i].id == SPA_PARAM_Props && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                    pw_node_enum_params(reinterpret_cast<pw_node *>(np->proxy), 0, SPA_PARAM_Props, 0, 1, nullptr);
                }
            }
        }
        if (!np->announced && !np->info.name.isEmpty()) {
            np->announced = true;
            np->impl->publish(np, true);
        } else if (np->announced) {
            np->impl->publish(np, false);
        }
    }

    static void onNodeParam(void *data, int /*seq*/, uint32_t id, uint32_t /*index*/, uint32_t /*next*/, const spa_pod *param) {
        auto *np = static_cast<NodeProxy *>(data);
        if (id != SPA_PARAM_Props || !param) return;
        bool changed = false;
        const spa_pod_prop *p;
        const auto *obj = reinterpret_cast<const spa_pod_object *>(param);
        SPA_POD_OBJECT_FOREACH(obj, p) {
            switch (p->key) {
            case SPA_PROP_mute: {
                bool m = false;
                if (spa_pod_get_bool(&p->value, &m) == 0 && m != np->info.mute) { np->info.mute = m; changed = true; }
                break;
            }
            case SPA_PROP_channelVolumes: {
                float vols[SPA_AUDIO_MAX_CHANNELS];
                uint32_t n = spa_pod_copy_array(&p->value, SPA_TYPE_Float, vols, SPA_AUDIO_MAX_CHANNELS);
                // DV-22: with a pan applied the two channelVolumes differ; the node's "volume" for us is the louder
                // side (= trim), otherwise a hard-right pan would read back as trim 0 and the next write mutes both.
                float v = 0.f; for (uint32_t i = 0; i < n; i++) v = std::max(v, vols[i]);
                if (n > 0 && v != np->info.volume) { np->info.volume = v; changed = true; }
                break;
            }
            default: break;
            }
        }
        if (changed && np->announced) np->impl->publish(np, false);
    }

    static const pw_node_events nodeEvents;

    static void onProxyRemoved(void *data) {
        auto *np = static_cast<NodeProxy *>(data);
        pw_proxy_destroy(np->proxy);
    }
    static void onProxyDestroy(void *data) {
        auto *np = static_cast<NodeProxy *>(data);
        auto *impl = np->impl;
        spa_hook_remove(&np->listener);
        spa_hook_remove(&np->proxyListener);
        impl->nodes.remove(np->id);
        {
            std::lock_guard<std::mutex> g(impl->snapshotMutex);
            impl->snapshot.remove(np->id);
        }
        const uint32_t id = np->id;
        QMetaObject::invokeMethod(impl->q, [q = impl->q, id] { Q_EMIT q->nodeRemoved(id); }, Qt::QueuedConnection);
        delete np;
    }
    static const pw_proxy_events proxyEvents;

    // --- "default" metadata: default.audio.sink / default.audio.source are JSON {"name":"<node.name>"} (UX-3)
    static int onMetadataProperty(void *data, uint32_t /*subject*/, const char *key, const char *type, const char *value) {
        auto *impl = static_cast<Impl *>(data);
        if (!key) return 0;
        const bool sink = strcmp(key, "default.audio.sink") == 0, source = strcmp(key, "default.audio.source") == 0;
        if (!sink && !source) return 0;
        QString name;
        if (value && type && strcmp(type, "Spa:String:JSON") == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(value));
            name = doc.object().value(QStringLiteral("name")).toString();
        }
        QString s, src;
        {
            std::lock_guard<std::mutex> g(impl->snapshotMutex);
            if (sink) impl->defaultSink = name; else impl->defaultSource = name;
            s = impl->defaultSink; src = impl->defaultSource;
        }
        QMetaObject::invokeMethod(impl->q, [q = impl->q, s, src] { Q_EMIT q->defaultDevicesChanged(s, src); }, Qt::QueuedConnection);
        return 0;
    }

    // --- registry
    static void onGlobal(void *data, uint32_t id, uint32_t /*permissions*/, const char *type, uint32_t /*version*/, const spa_dict *props) {
        auto *impl = static_cast<Impl *>(data);
        if (type && strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            if (prop(props, PW_KEY_METADATA_NAME) == QLatin1String("default") && !impl->metadata) {
                impl->metadata = static_cast<pw_metadata *>(pw_registry_bind(impl->registry, id, type, PW_VERSION_METADATA, 0));
                static const pw_metadata_events metadataEvents = { PW_VERSION_METADATA_EVENTS, &Impl::onMetadataProperty };
                pw_metadata_add_listener(impl->metadata, &impl->metadataListener, &metadataEvents, impl);
            }
            return;
        }
        if (type && strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
            const uint32_t out = prop(props, PW_KEY_LINK_OUTPUT_NODE).toUInt(), in = prop(props, PW_KEY_LINK_INPUT_NODE).toUInt();
            if (out && in) { impl->links.insert(id, {out, in}); impl->recomputeRoute(out); }
            return;
        }
        if (type && strcmp(type, PW_TYPE_INTERFACE_Port) == 0) {   // DV-13: remember every port of an audio node
            PortInfo pi; pi.id = id; pi.nodeId = prop(props, PW_KEY_NODE_ID).toUInt();
            pi.name = prop(props, PW_KEY_PORT_NAME); pi.alias = prop(props, PW_KEY_PORT_ALIAS);
            pi.position = prop(props, PW_KEY_AUDIO_CHANNEL);
            pi.input = prop(props, PW_KEY_PORT_DIRECTION) == QLatin1String("in");
            pi.monitor = prop(props, PW_KEY_PORT_MONITOR) == QLatin1String("true");
            if (pi.nodeId) { std::lock_guard<std::mutex> g(impl->snapshotMutex); impl->ports.insert(id, pi); }
            return;
        }
        if (!type || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
        // We only mirror audio nodes.
        const QString mc = prop(props, PW_KEY_MEDIA_CLASS);
        if (!mc.startsWith(QLatin1String("Audio/")) && !mc.startsWith(QLatin1String("Stream/"))) return;

        auto *np = new NodeProxy;
        np->impl = impl; np->id = id;
        np->info.id = id;
        np->info.name = prop(props, PW_KEY_NODE_NAME);
        np->info.description = prop(props, PW_KEY_NODE_DESCRIPTION);
        np->info.mediaClass = mc;
        np->info.mediaName = prop(props, PW_KEY_MEDIA_NAME);
        np->info.appName = prop(props, PW_KEY_APP_NAME);
        np->info.appBinary = prop(props, PW_KEY_APP_PROCESS_BINARY);
        np->info.mediaRole = prop(props, PW_KEY_MEDIA_ROLE);
        np->info.iconName = prop(props, PW_KEY_APP_ICON_NAME);
        np->info.positions = parsePositions(prop(props, "audio.position"));
        np->info.serial = prop(props, PW_KEY_OBJECT_SERIAL).toUInt();
        np->proxy = static_cast<pw_proxy *>(pw_registry_bind(impl->registry, id, type, PW_VERSION_NODE, 0));
        if (!np->proxy) { delete np; return; }
        pw_proxy_add_object_listener(np->proxy, &np->listener, &nodeEvents, np);
        pw_proxy_add_listener(np->proxy, &np->proxyListener, &proxyEvents, np);
        impl->nodes.insert(id, np);
        // ask for Props once; further changes arrive via onNodeInfo(PARAMS) → enum_params
        uint32_t ids[] = {SPA_PARAM_Props};
        pw_node_subscribe_params(reinterpret_cast<pw_node *>(np->proxy), ids, 1);
    }
    bool isMeterNode(uint32_t id) const { auto it = nodes.constFind(id); return it != nodes.constEnd() && it.value()->info.name == QLatin1String("kmixdeck.meter"); }
    void recomputeRoute(uint32_t outNode) {
        // majority target of this node's outgoing links (a stereo stream has 2 links to the same sink). Our own level
        // meter (UX-13) is a second consumer of every metered app node — 2 links, same as the real sink — and won the
        // tie by hash order: "routed → kmixdeck.meter" cleared App.Channels, and the next drop REPLACED instead of
        // adding (test_ux11 red 1 in 3 in the suite). The meter is a tap, never a route.
        QHash<uint32_t, int> count;
        for (const auto &l : links) if (l.out == outNode && !isMeterNode(l.in)) count[l.in]++;
        uint32_t best = 0; int n = 0;
        for (auto it = count.cbegin(); it != count.cend(); ++it) if (it.value() > n) { n = it.value(); best = it.key(); }
        bool changed;
        { std::lock_guard<std::mutex> g(snapshotMutex); changed = routes.value(outNode) != best; if (best) routes[outNode] = best; else routes.remove(outNode); }
        if (changed) QMetaObject::invokeMethod(q, [q = this->q, outNode, best] { Q_EMIT q->streamRouted(outNode, best); }, Qt::QueuedConnection);
    }
    static void onGlobalRemove(void *data, uint32_t id) {
        auto *impl = static_cast<Impl *>(data);
        auto it = impl->links.find(id);
        if (it != impl->links.end()) { const uint32_t out = it->out; impl->links.erase(it); impl->recomputeRoute(out); }
        { std::lock_guard<std::mutex> g(impl->snapshotMutex); impl->ports.remove(id); }
        // nodes are handled by the proxy 'removed' event
    }
    static const pw_registry_events registryEvents;

    static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message) {
        auto *impl = static_cast<Impl *>(data);
        qCWarning(lcPipewire) << "pipewire core error id" << id << "seq" << seq << spa_strerror(res) << message;
        if (id == PW_ID_CORE && res == -EPIPE) {
            QMetaObject::invokeMethod(impl->q, [q = impl->q] { Q_EMIT q->disconnected(QStringLiteral("core EPIPE")); }, Qt::QueuedConnection);
        }
    }
    static const pw_core_events coreEvents;
};

const pw_node_events Graph::Impl::nodeEvents = { .version = PW_VERSION_NODE_EVENTS, .info = &Graph::Impl::onNodeInfo, .param = &Graph::Impl::onNodeParam };  // NOLINT
const pw_proxy_events Graph::Impl::proxyEvents = { .version = PW_VERSION_PROXY_EVENTS, .destroy = &Graph::Impl::onProxyDestroy, .bound = nullptr, .removed = &Graph::Impl::onProxyRemoved, .done = nullptr, .error = nullptr, .bound_props = nullptr };
const pw_registry_events Graph::Impl::registryEvents = { .version = PW_VERSION_REGISTRY_EVENTS, .global = &Graph::Impl::onGlobal, .global_remove = &Graph::Impl::onGlobalRemove };
const pw_core_events Graph::Impl::coreEvents = { .version = PW_VERSION_CORE_EVENTS, .info = nullptr, .done = nullptr, .ping = nullptr, .error = &Graph::Impl::onCoreError, .remove_id = nullptr, .bound_id = nullptr, .add_mem = nullptr, .remove_mem = nullptr, .bound_props = nullptr };

Graph::Graph(QObject *parent) : QObject(parent), d(std::make_unique<Impl>()) {
    d->q = this;
    qRegisterMetaType<NodeInfo>();
    pw_init(nullptr, nullptr);
}

Graph::~Graph() {
    if (d->loop) {
        pw_thread_loop_lock(d->loop);
        if (d->metadata) pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->metadata));
        if (d->registry) pw_proxy_destroy(reinterpret_cast<pw_proxy *>(d->registry));
        if (d->core) pw_core_disconnect(d->core);
        pw_thread_loop_unlock(d->loop);
        pw_thread_loop_stop(d->loop);
        if (d->context) pw_context_destroy(d->context);
        pw_thread_loop_destroy(d->loop);
    }
    pw_deinit();
}

bool Graph::connect() {
    if (!d->loop) {
        d->loop = pw_thread_loop_new("kmixdeck-pw", nullptr);
        if (!d->loop) return false;
        d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
        if (!d->context) return false;
        pw_thread_loop_start(d->loop);
    }
    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context, nullptr, 0);
    if (!d->core) { pw_thread_loop_unlock(d->loop); return false; }
    pw_core_add_listener(d->core, &d->coreListener, &Impl::coreEvents, d.get());
    d->registry = pw_core_get_registry(d->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(d->registry, &d->registryListener, &Impl::registryEvents, d.get());
    pw_thread_loop_unlock(d->loop);
    m_connected = true;
    Q_EMIT connected();
    return true;
}

/// Tear down the core connection (after EPIPE) and forget every mirrored object, emitting nodeRemoved
/// for each so the model above stays truthful. Loop and context survive; connect() re-attaches.
void Graph::teardown() {
    if (!d->loop) return;
    QList<uint32_t> ids;
    pw_thread_loop_lock(d->loop);
    ids = d->nodes.keys();
    for (auto *np : d->nodes) { spa_hook_remove(&np->listener); spa_hook_remove(&np->proxyListener); delete np; }   // proxies die with the core
    d->nodes.clear(); d->links.clear();
    d->metadata = nullptr; d->registry = nullptr;
    spa_hook_remove(&d->coreListener); spa_hook_remove(&d->registryListener);
    if (d->core) { pw_core_disconnect(d->core); d->core = nullptr; }
    pw_thread_loop_unlock(d->loop);
    { std::lock_guard<std::mutex> g(d->snapshotMutex); d->snapshot.clear(); d->routes.clear(); }
    m_connected = false;
    for (uint32_t id : ids) Q_EMIT nodeRemoved(id);
}

QVector<NodeInfo> Graph::nodes() const {
    std::lock_guard<std::mutex> g(d->snapshotMutex);
    QVector<NodeInfo> out; out.reserve(d->snapshot.size());
    for (const auto &n : d->snapshot) out.push_back(n);
    return out;
}

QVector<PortInfo> Graph::ports(uint32_t nodeId) const {
    QVector<PortInfo> out;
    // Sinks: their playback (input) ports — monitor_* are a mirror, never a device channel. Sources: their capture
    // (output) ports; on a virtual source (Audio/Source/Virtual, Duplex) those are flagged port.monitor too, and
    // ARE the device's channels, so the monitor flag only filters on sinks.
    bool isSink = false;
    for (const auto &n : nodes()) if (n.id == nodeId) { isSink = n.mediaClass.startsWith(QLatin1String("Audio/Sink")); break; }
    { std::lock_guard<std::mutex> g(d->snapshotMutex); for (const auto &p : d->ports) if (p.nodeId == nodeId && (isSink ? (p.input && !p.monitor) : !p.input)) out.append(p); }
    std::sort(out.begin(), out.end(), [](const PortInfo &a, const PortInfo &b) { return a.id < b.id; });
    return out;
}
std::optional<NodeInfo> Graph::node(const QString &name) const {
    std::lock_guard<std::mutex> g(d->snapshotMutex);
    for (const auto &n : d->snapshot) if (n.name == name) return n;
    return std::nullopt;
}

void Graph::setVolume(uint32_t nodeId, float linear, bool mute) { setVolumeLR(nodeId, linear, linear, mute); }
void Graph::setVolumeLR(uint32_t nodeId, float left, float right, bool mute) {
    pw_thread_loop_lock(d->loop);
    auto it = d->nodes.find(nodeId);
    if (it != d->nodes.end()) {
        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        float vols[2] = {left, right};
        const spa_pod *param = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
            SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
            SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, 2, vols),
            SPA_PROP_mute, SPA_POD_Bool(mute)));
        pw_node_set_param(reinterpret_cast<pw_node *>(it.value()->proxy), SPA_PARAM_Props, 0, param);
    }
    pw_thread_loop_unlock(d->loop);
}

void Graph::setControl(uint32_t nodeId, const QString &control, double value) {
    pw_thread_loop_lock(d->loop);
    auto it = d->nodes.find(nodeId);
    if (it != d->nodes.end()) {
        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const QByteArray key = control.toUtf8();
        // filter-chain exposes its controls as Props.params = [ "<node>:<Control>" <value> … ] — the same shape
        // `pw-cli set-param <id> Props '{ params = [ "gate:Threshold (dB)" 0 ] }'` sends. The previous version passed
        // the key string where add_object() expects a uint32 prop id: rc 0, value never changed (ADR 0008 §Open).
        spa_pod_frame f[2];
        spa_pod_builder_push_object(&b, &f[0], SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
        spa_pod_builder_prop(&b, SPA_PROP_params, 0);
        spa_pod_builder_push_struct(&b, &f[1]);
        spa_pod_builder_string(&b, key.constData());
        spa_pod_builder_float(&b, static_cast<float>(value));
        spa_pod_builder_pop(&b, &f[1]);
        const spa_pod *param = static_cast<const spa_pod *>(spa_pod_builder_pop(&b, &f[0]));
        pw_node_set_param(reinterpret_cast<pw_node *>(it.value()->proxy), SPA_PARAM_Props, 0, param);
    }
    pw_thread_loop_unlock(d->loop);
}

void Graph::createNullSink(const QString &name, const QString &description, bool passive) {
    pw_thread_loop_lock(d->loop);
    pw_properties *props = pw_properties_new(
        PW_KEY_FACTORY_NAME, "support.null-audio-sink",
        PW_KEY_NODE_NAME, name.toUtf8().constData(),
        PW_KEY_MEDIA_NAME, name.toUtf8().constData(),   // ADR 0002: stream-restore key
        PW_KEY_NODE_DESCRIPTION, description.toUtf8().constData(),
        PW_KEY_MEDIA_CLASS, "Audio/Sink",
        "audio.position", "[ FL FR ]",
        "monitor.channel-volumes", "true",
        PW_KEY_OBJECT_LINGER, "true",
        nullptr);
    if (passive) pw_properties_set(props, PW_KEY_NODE_PASSIVE, "true");
    pw_proxy *p = static_cast<pw_proxy *>(pw_core_create_object(d->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);
    if (p) pw_proxy_destroy(p);   // linger=true keeps the node alive server-side
    pw_thread_loop_unlock(d->loop);
}

void Graph::createNullNode(const QString &name, const QString &description, const QString &mediaClass, const QStringList &positions) {
    pw_thread_loop_lock(d->loop);
    const QByteArray pos = (QStringLiteral("[ ") + positions.join(QLatin1Char(' ')) + QStringLiteral(" ]")).toUtf8();
    pw_properties *props = pw_properties_new(
        PW_KEY_FACTORY_NAME, "support.null-audio-sink",
        PW_KEY_NODE_NAME, name.toUtf8().constData(),
        PW_KEY_MEDIA_NAME, name.toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, description.toUtf8().constData(),
        PW_KEY_MEDIA_CLASS, mediaClass.toUtf8().constData(),
        "audio.position", pos.constData(),
        "monitor.channel-volumes", "true",
        PW_KEY_OBJECT_LINGER, "true",
        nullptr);
    pw_proxy *p = static_cast<pw_proxy *>(pw_core_create_object(d->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);
    if (p) pw_proxy_destroy(p);
    pw_thread_loop_unlock(d->loop);
}

bool Graph::linkPorts(const QString &outNode, const QString &outPort, const QString &inNode, const QString &inPort) {
    const auto a = node(outNode); const auto b = node(inNode);
    if (!a || !b) return false;
    uint32_t aus = 0, ein = 0;
    {
        std::lock_guard<std::mutex> g(d->snapshotMutex);
        for (const auto &p : d->ports) {
            if (p.nodeId == a->id && !p.input && p.name == outPort) aus = p.id;
            if (p.nodeId == b->id && p.input && p.name == inPort) ein = p.id;
        }
    }
    if (!aus || !ein) return false;
    // The "link-factory" FACTORY creates the link — not pw_context_load_module. Measured 2026-09-21:
    // loading the module returned a valid handle and produced no link at all, while pw_core_create_object
    // (what pw-link itself calls) linked the ports immediately. object.linger keeps the link after the
    // proxy goes away. link.passive is deliberately unset: a passive link between two passive nodes
    // never starts — the same trap the FX chain's playback.props documents.
    pw_thread_loop_lock(d->loop);
    pw_properties *props = pw_properties_new(PW_KEY_LINK_OUTPUT_PORT, QByteArray::number(aus).constData(),
                                             PW_KEY_LINK_INPUT_PORT, QByteArray::number(ein).constData(),
                                             PW_KEY_OBJECT_LINGER, "true", nullptr);
    pw_proxy *p = static_cast<pw_proxy *>(pw_core_create_object(d->core, "link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &props->dict, 0));
    pw_properties_free(props);
    if (p) pw_proxy_destroy(p);
    pw_thread_loop_unlock(d->loop);
    if (!p) { qCWarning(lcPipewire) << "linkPorts failed:" << outNode << outPort << "->" << inNode << inPort; return false; }
    return true;
}

void Graph::loadLoopback(const QString &args, const char *module) {
    pw_thread_loop_lock(d->loop);
    pw_impl_module *m = pw_context_load_module(d->context, module, args.toUtf8().constData(), nullptr);
    pw_thread_loop_unlock(d->loop);
    if (!m) {
        // Seen 2026-09-18 on a 32x32 desk: EMFILE (fd soft limit) — PipeWire only logged "Protocol error" and the
        // edge was silently missing. Name the edge and the errno so the journal tells the story in one line.
        const int e = errno;
        qCCritical(lcPipewire).noquote() << "failed to load" << module << "-" << strerror(e)
                                         << (e == EMFILE ? "(raise the open-files limit: systemd LimitNOFILE / ulimit -n)" : "") << "\n  args:" << args.left(200);
        QMetaObject::invokeMethod(this, [this, args] { Q_EMIT moduleLoadFailed(args); }, Qt::QueuedConnection);
    }
}

pw_thread_loop *Graph::threadLoop() const { return d->loop; }
pw_core *Graph::core() const { return d->core; }

void Graph::createParkingSink() {
    pw_thread_loop_lock(d->loop);
    pw_properties *props = pw_properties_new(
        PW_KEY_FACTORY_NAME, "support.null-audio-sink", PW_KEY_NODE_NAME, "kmixdeck.null", PW_KEY_MEDIA_NAME, "kmixdeck.null",
        PW_KEY_NODE_DESCRIPTION, "kmixdeck (unrouted)", PW_KEY_MEDIA_CLASS, "Audio/Sink", "audio.position", "[ FL FR ]",
        PW_KEY_OBJECT_LINGER, "true", PW_KEY_PRIORITY_SESSION, "0", PW_KEY_PRIORITY_DRIVER, "0", PW_KEY_NODE_PASSIVE, "true", nullptr);
    pw_proxy *p = static_cast<pw_proxy *>(pw_core_create_object(d->core, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);
    if (p) pw_proxy_destroy(p);
    pw_thread_loop_unlock(d->loop);
}
uint32_t Graph::streamSink(uint32_t streamId) const {
    std::lock_guard<std::mutex> g(d->snapshotMutex);
    return d->routes.value(streamId, 0);
}

void Graph::setStreamTarget(uint32_t streamId, uint32_t sinkSerial) {
    pw_thread_loop_lock(d->loop);
    if (d->metadata) {
        const QByteArray v = QByteArray::number(sinkSerial);
        pw_metadata_set_property(d->metadata, streamId, "target.object", "Spa:Id", v.constData());
    } else {
        qCWarning(lcPipewire) << "no 'default' metadata object yet; cannot route stream" << streamId;
    }
    pw_thread_loop_unlock(d->loop);
}

void Graph::clearStreamTarget(uint32_t streamId) {
    pw_thread_loop_lock(d->loop);
    if (d->metadata) pw_metadata_set_property(d->metadata, streamId, "target.object", nullptr, nullptr);
    pw_thread_loop_unlock(d->loop);
}

QString Graph::defaultSink() const { std::lock_guard<std::mutex> g(d->snapshotMutex); return d->defaultSink; }
QString Graph::defaultSource() const { std::lock_guard<std::mutex> g(d->snapshotMutex); return d->defaultSource; }

bool Graph::moveStream(uint32_t streamId, const QString &sinkNodeName) {
    const auto sink = node(sinkNodeName);
    if (!sink || sink->serial == 0) return false;
    { std::lock_guard<std::mutex> g(d->snapshotMutex); if (!d->snapshot.contains(streamId)) return false; }
    setStreamTarget(streamId, sink->serial);
    return true;
}

void Graph::destroyObject(uint32_t id) {
    pw_thread_loop_lock(d->loop);
    pw_registry_destroy(d->registry, id);
    pw_thread_loop_unlock(d->loop);
}

} // namespace kmixdeck::pw
