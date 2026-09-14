// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "graph.h"

#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>
#include <spa/debug/pod.h>

#include <QMetaObject>
#include <QDebug>
#include <mutex>
#include <optional>

namespace kmixdeck::pw {

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
    spa_hook registryListener{};
    spa_hook coreListener{};
    QHash<uint32_t, NodeProxy *> nodes;           // loop thread only
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
                if (n > 0 && vols[0] != np->info.volume) { np->info.volume = vols[0]; changed = true; }
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

    // --- registry
    static void onGlobal(void *data, uint32_t id, uint32_t /*permissions*/, const char *type, uint32_t /*version*/, const spa_dict *props) {
        auto *impl = static_cast<Impl *>(data);
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
        np->proxy = static_cast<pw_proxy *>(pw_registry_bind(impl->registry, id, type, PW_VERSION_NODE, 0));
        if (!np->proxy) { delete np; return; }
        pw_proxy_add_object_listener(np->proxy, &np->listener, &nodeEvents, np);
        pw_proxy_add_listener(np->proxy, &np->proxyListener, &proxyEvents, np);
        impl->nodes.insert(id, np);
        // ask for Props once; further changes arrive via onNodeInfo(PARAMS) → enum_params
        pw_node_subscribe_params(reinterpret_cast<pw_node *>(np->proxy), (uint32_t[]){SPA_PARAM_Props}, 1);
    }
    static void onGlobalRemove(void * /*data*/, uint32_t /*id*/) { /* handled by proxy 'removed' */ }
    static const pw_registry_events registryEvents;

    static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message) {
        auto *impl = static_cast<Impl *>(data);
        qWarning() << "pipewire core error id" << id << "seq" << seq << spa_strerror(res) << message;
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
    d->loop = pw_thread_loop_new("kmixdeck-pw", nullptr);
    if (!d->loop) return false;
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), nullptr, 0);
    if (!d->context) return false;
    pw_thread_loop_start(d->loop);
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

QVector<NodeInfo> Graph::nodes() const {
    std::lock_guard<std::mutex> g(d->snapshotMutex);
    QVector<NodeInfo> out; out.reserve(d->snapshot.size());
    for (const auto &n : d->snapshot) out.push_back(n);
    return out;
}

std::optional<NodeInfo> Graph::node(const QString &name) const {
    std::lock_guard<std::mutex> g(d->snapshotMutex);
    for (const auto &n : d->snapshot) if (n.name == name) return n;
    return std::nullopt;
}

void Graph::setVolume(uint32_t nodeId, float linear, bool mute) {
    pw_thread_loop_lock(d->loop);
    auto it = d->nodes.find(nodeId);
    if (it != d->nodes.end()) {
        uint8_t buffer[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        float vols[2] = {linear, linear};
        const spa_pod *param = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
            SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
            SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, 2, vols),
            SPA_PROP_mute, SPA_POD_Bool(mute)));
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

void Graph::createLoopback(const QString &name, const QString &description, const QString &from, const QString &to) {
    // Modules are loaded in *our* context — they live as long as the app. That's fine for runtime
    // edits; the persistent graph is written to pipewire.conf.d by the config writer (DV-1/DV-5).
    pw_thread_loop_lock(d->loop);
    const QString args = QStringLiteral(
        "{ node.description = \"%1\" "
        "capture.props = { node.name = \"%2.in\" media.name = \"%2.in\" node.target = \"%3\" stream.capture.sink = true node.passive = true node.dont-reconnect = true } "
        "playback.props = { node.name = \"%2\" media.name = \"%2\" node.target = \"%4\" node.dont-reconnect = true } }")
        .arg(description, name, from, to);
    pw_context_load_module(d->context, "libpipewire-module-loopback", args.toUtf8().constData(), nullptr);
    pw_thread_loop_unlock(d->loop);
}

void Graph::destroyObject(uint32_t id) {
    pw_thread_loop_lock(d->loop);
    pw_registry_destroy(d->registry, id);
    pw_thread_loop_unlock(d->loop);
}

} // namespace kmixdeck::pw
