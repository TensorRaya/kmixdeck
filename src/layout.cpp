// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "layout.h"
#include "mixer.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>

namespace kmixdeck {

QString Layout::defaultPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kmixdeck/layout.json"); }
QString Layout::defaultPipewireConfPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/pipewire/pipewire.conf.d/90-kmixdeck.conf"); }

// Entry point a stream should target: the fx entry when a chain is active, the plain sink otherwise (ADR 0008 D1/D3).
QString Layout::channelEntry(const QString &slug) const {
    const auto *c = channel(slug);
    if (c && c->fx.enabled && !c->fx.effects.isEmpty()) return QStringLiteral("kmixdeck.fx.%1").arg(slug);
    return Names::channelNode(slug);
}
QString Layout::mixEntry(const QString &slug) const {
    const auto *m = mix(slug);
    if (m && m->fx.enabled && !m->fx.effects.isEmpty()) return QStringLiteral("kmixdeck.fx.mix.%1").arg(slug);
    return Names::mixNode(slug);
}

Layout Layout::starter() {
    Layout l;
    l.channels = {{QStringLiteral("game"), QStringLiteral("Game"), QStringLiteral("input-gaming")},
                  {QStringLiteral("system"), QStringLiteral("System"), QStringLiteral("computer")},
                  {QStringLiteral("voice"), QStringLiteral("Voice"), QStringLiteral("audio-input-microphone")}};
    l.mixes = {{QStringLiteral("monitor"), QStringLiteral("Monitor"), QStringLiteral("audio-headphones"), {}, {}},
               {QStringLiteral("stream"), QStringLiteral("Stream"), QStringLiteral("camera-video"), {}, {}}};
    return l;
}

LayoutChannel *Layout::channel(const QString &slug) { for (auto &c : channels) if (c.slug == slug) return &c; return nullptr; }
LayoutMix *Layout::mix(const QString &slug) { for (auto &m : mixes) if (m.slug == slug) return &m; return nullptr; }
LayoutInput *Layout::input(const QString &slug) { for (auto &i : inputs) if (i.slug == slug) return &i; return nullptr; }
const LayoutChannel *Layout::channel(const QString &slug) const { for (const auto &c : channels) if (c.slug == slug) return &c; return nullptr; }
const LayoutMix *Layout::mix(const QString &slug) const { for (const auto &m : mixes) if (m.slug == slug) return &m; return nullptr; }
const LayoutInput *Layout::input(const QString &slug) const { for (const auto &i : inputs) if (i.slug == slug) return &i; return nullptr; }
LayoutApp *Layout::app(const QString &key) { for (auto &a : apps) if (a.key == key) return &a; return nullptr; }
const LayoutApp *Layout::app(const QString &key) const { for (const auto &a : apps) if (a.key == key) return &a; return nullptr; }

QJsonObject DeviceRef::toJson() const {
    QJsonObject o{{QStringLiteral("node"), node}, {QStringLiteral("description"), description}};
    if (!positions.isEmpty()) o.insert(QStringLiteral("positions"), QJsonArray::fromStringList(positions));
    return o;
}
DeviceRef DeviceRef::fromJson(const QJsonObject &o) {
    DeviceRef r; r.node = o.value(QStringLiteral("node")).toString(); r.description = o.value(QStringLiteral("description")).toString();
    for (const auto &v : o.value(QStringLiteral("positions")).toArray()) r.positions << v.toString();
    return r;
}

QJsonArray fxChainArray(const fx::Chain &c) { QJsonArray a; for (const auto &e : c.effects) a.append(e.toJson()); return a; }

QJsonObject Layout::toJson() const {
    QJsonArray ch, mx, in;
    for (const auto &c : channels) {
        QJsonObject o{{QStringLiteral("slug"), c.slug}, {QStringLiteral("name"), c.name}, {QStringLiteral("icon"), c.icon}};
        if (!c.fx.effects.isEmpty() || !c.fx.enabled) o.insert(QStringLiteral("fx"), QJsonObject{{QStringLiteral("enabled"), c.fx.enabled}, {QStringLiteral("chain"), fxChainArray(c.fx)}});
        ch.append(o);
    }
    for (const auto &m : mixes) {
        QJsonArray outs; for (const auto &d : m.outputs) outs.append(d.toJson());
        QJsonObject o{{QStringLiteral("slug"), m.slug}, {QStringLiteral("name"), m.name}, {QStringLiteral("icon"), m.icon}, {QStringLiteral("outputs"), outs}};
        if (!outs.isEmpty()) o.insert(QStringLiteral("outputDevice"), m.outputs.first().node);   // v1-compatible view: first entry
        if (!m.fallbackOutput.node.isEmpty()) o.insert(QStringLiteral("fallbackOutput"), m.fallbackOutput.toJson());
        if (!m.fx.effects.isEmpty() || !m.fx.enabled) o.insert(QStringLiteral("fx"), QJsonObject{{QStringLiteral("enabled"), m.fx.enabled}, {QStringLiteral("chain"), fxChainArray(m.fx)}});
        mx.append(o);
    }
    for (const auto &i : inputs) in.append(QJsonObject{{QStringLiteral("slug"), i.slug}, {QStringLiteral("name"), i.name}, {QStringLiteral("device"), i.device.toJson()}, {QStringLiteral("channel"), i.channel}});
    QJsonArray appArr;
    for (const auto &a : apps) appArr.append(QJsonObject{{QStringLiteral("key"), a.key}, {QStringLiteral("nodeName"), a.nodeName}, {QStringLiteral("channels"), QJsonArray::fromStringList(a.channels)}});
    return {{QStringLiteral("version"), 2}, {QStringLiteral("channels"), ch}, {QStringLiteral("mixes"), mx}, {QStringLiteral("inputs"), in},
            {QStringLiteral("apps"), appArr},
            {QStringLiteral("defaultChannel"), defaultChannel}, {QStringLiteral("knownApps"), QJsonArray::fromStringList(knownApps)},
            {QStringLiteral("links"), [this] { QJsonArray a; for (const auto &l : links) a.append(QJsonObject{{QStringLiteral("channel"), l.channel}, {QStringLiteral("mix"), l.mix}, {QStringLiteral("follows"), l.follows}}); return a; }()}};
}
Layout Layout::fromJson(const QJsonObject &o) {
    Layout l;
    auto readFx = [](const QJsonObject &j) { fx::Chain c; if (j.contains(QStringLiteral("fx"))) c = fx::Chain::fromJson(j.value(QStringLiteral("fx")).toObject()); return c; };
    if (o.contains(QStringLiteral("defaultChannel"))) l.defaultChannel = o.value(QStringLiteral("defaultChannel")).toString();
    for (const auto &v : o.value(QStringLiteral("knownApps")).toArray()) l.knownApps << v.toString();
    for (const auto &v : o.value(QStringLiteral("links")).toArray()) { const auto j = v.toObject(); l.links.push_back({j.value(QStringLiteral("channel")).toString(), j.value(QStringLiteral("mix")).toString(), j.value(QStringLiteral("follows")).toString()}); }
    for (const auto &v : o.value(QStringLiteral("channels")).toArray()) { const auto c = v.toObject(); l.channels.push_back({c.value(QStringLiteral("slug")).toString(), c.value(QStringLiteral("name")).toString(), c.value(QStringLiteral("icon")).toString(), readFx(c)}); }
    for (const auto &v : o.value(QStringLiteral("mixes")).toArray()) {
        const auto m = v.toObject(); LayoutMix lm;
        lm.slug = m.value(QStringLiteral("slug")).toString(); lm.name = m.value(QStringLiteral("name")).toString(); lm.icon = m.value(QStringLiteral("icon")).toString();
        for (const auto &d : m.value(QStringLiteral("outputs")).toArray()) lm.outputs.push_back(DeviceRef::fromJson(d.toObject()));
        // version 1 files had a single "outputDevice" string
        const QString legacy = m.value(QStringLiteral("outputDevice")).toString();
        if (lm.outputs.isEmpty() && !legacy.isEmpty()) lm.outputs.push_back({legacy, legacy, {}});
        if (m.contains(QStringLiteral("fallbackOutput"))) lm.fallbackOutput = DeviceRef::fromJson(m.value(QStringLiteral("fallbackOutput")).toObject());
        lm.fx = readFx(m);
        l.mixes.push_back(lm);
    }
    for (const auto &v : o.value(QStringLiteral("inputs")).toArray()) {
        const auto i = v.toObject();
        l.inputs.push_back({i.value(QStringLiteral("slug")).toString(), i.value(QStringLiteral("name")).toString(), DeviceRef::fromJson(i.value(QStringLiteral("device")).toObject()), i.value(QStringLiteral("channel")).toString()});
    }
    for (const auto &v : o.value(QStringLiteral("apps")).toArray()) {
        const auto a = v.toObject(); LayoutApp la;
        la.key = a.value(QStringLiteral("key")).toString(); la.nodeName = a.value(QStringLiteral("nodeName")).toString();
        for (const auto &c : a.value(QStringLiteral("channels")).toArray()) la.channels << c.toString();
        if (!la.key.isEmpty() && !la.channels.isEmpty()) l.apps.push_back(la);
    }
    return l;
}
bool Layout::load(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err; const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;   // CT-7: corrupt config → caller keeps the old layout
    *this = fromJson(doc.object()); return true;
}
bool Layout::save(const QString &path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)); return f.commit();
}

static QString q(const QString &s) { QString r = s; r.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\"")); return QLatin1Char('"') + r + QLatin1Char('"'); }
static QString pos(const QStringList &p) { return p.isEmpty() ? QStringLiteral("[ FL FR ]") : QStringLiteral("[ ") + p.join(QLatin1Char(' ')) + QStringLiteral(" ]"); }

QString loopbackArgs(const QString &description,
                     const QString &captureName, const QString &captureTarget, bool captureIsSink, const QStringList &capturePositions, bool captureLinger,
                     const QString &playbackName, const QString &playbackTarget, const QStringList &playbackPositions, bool playbackLinger, bool playbackDontReconnect,
                     const QString &playbackExtra) {
    // Every stream: dont-fallback (never silently the default device — feedback, ADR 0002 trap 3).
    // Device-side streams: linger (wait for an absent device, WirePlumber relinks it — ADR 0007 D3).
    // Cell playbacks: dont-reconnect (a cell must never wander); mix outputs must NOT have it (they follow retargets).
    QString cap = QStringLiteral("capture.props = { node.name = %1 media.name = %1 node.target = %2 audio.position = %3 node.passive = true node.dont-fallback = true %4%5}")
        .arg(q(captureName), q(captureTarget), pos(capturePositions),
             captureIsSink ? QStringLiteral("stream.capture.sink = true node.dont-reconnect = true ") : QString(),
             captureLinger ? QStringLiteral("node.linger = true ") : QString());
    // A virtual source (the mix capture node) has no target at all: it is a device others capture from.
    QString play = QStringLiteral("playback.props = { node.name = %1 media.name = %1 %2audio.position = %3 node.dont-fallback = true %4%5%6}")
        .arg(q(playbackName), playbackTarget.isEmpty() ? QString() : QStringLiteral("node.target = %1 ").arg(q(playbackTarget)), pos(playbackPositions),
             playbackDontReconnect ? QStringLiteral("node.dont-reconnect = true ") : QString(),
             playbackLinger ? QStringLiteral("node.linger = true ") : QString(), playbackExtra);
    return QStringLiteral("{ node.description = %1 %2 %3 }").arg(q(description), cap, play);
}

QString Layout::toPipewireConf() const {
    QString out;
    out += QStringLiteral("# Generated by kmixdeckd — do not edit; change the layout through kmixdeck/kmixdeck-kde instead.\n"
                          "# ADR 0002: channel = null sink, mix = null sink, cell = loopback whose playback volume is the fader.\n"
                          "# ADR 0008: a channel/mix with effects gets a filter-chain instead of the plain sink; its capture node\n"
                          "# keeps the plain name, everything else targets that name unchanged.\n"
                          "# Every loopback has node.dont-fallback so a missing target never silently becomes the default sink (feedback).\n"
                          "# ADR 0007: device edges (inputs, mix outputs) carry node.linger — they wait for an absent device and\n"
                          "# WirePlumber links them by itself when it appears. A mix with NO output configured parks on kmixdeck.null.\n\n");
    out += QStringLiteral("context.objects = [\n");
    out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = \"kmixdeck.null\" media.name = \"kmixdeck.null\" node.description = \"kmixdeck (unrouted)\" media.class = Audio/Sink object.linger = true audio.position = [ FL FR ] priority.session = 0 priority.driver = 0 node.passive = true } }\n");
    const auto fxModule = [&](const fx::Chain &chain, const QString &desc, const QString &entry, const QString &exit,
                              const QString &mediaName, const QString &target, const QString &prefix) {
        const QString args = fx::renderFilterChainArgs(chain, desc, entry, exit, mediaName, target, prefix);
        if (args.isEmpty()) return false;
        out += QStringLiteral("  { name = libpipewire-module-filter-chain args = %1 }\n").arg(args);
        return true;
    };
    for (const auto &c : channels) {
        // plain sink first — fx-enabled channels keep it as the tail the chain plays into (ADR 0008 D1)
        out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.name = %1 node.description = %2 media.class = Audio/Sink object.linger = true audio.position = [ FL FR ] monitor.channel-volumes = true node.passive = true } }\n")
                   .arg(q(Names::channelNode(c.slug)), q(c.name));
        fxModule(c.fx, c.name, QStringLiteral("kmixdeck.fx.%1").arg(c.slug), QStringLiteral("kmixdeck.fx.%1.out").arg(c.slug),
                 Names::channelNode(c.slug), Names::channelNode(c.slug), c.slug);
    }
    for (const auto &m : mixes) {
        out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.name = %1 node.description = %2 media.class = Audio/Sink object.linger = true audio.position = [ FL FR ] monitor.channel-volumes = true } }\n")
                   .arg(q(Names::mixNode(m.slug)), q(QStringLiteral("Mix: ") + m.name));
        fxModule(m.fx, QStringLiteral("Mix: ") + m.name, QStringLiteral("kmixdeck.fx.mix.%1").arg(m.slug), QStringLiteral("kmixdeck.fx.mix.%1.out").arg(m.slug),
                 Names::mixNode(m.slug), Names::mixNode(m.slug), m.slug);
    }
    out += QStringLiteral("]\n\ncontext.modules = [\n");
    const auto mod = [&](const QString &args) { out += QStringLiteral("  { name = libpipewire-module-loopback args = %1 }\n").arg(args); };
    for (const auto &c : channels)
        for (const auto &m : mixes) {
            const QString cell = Names::cellNode(c.slug, m.slug);
            // cell capture sits on the PLAIN channel sink — that is where processed audio arrives (FX-3);
            // only apps/inputs aim at channelEntry so the chain actually runs in front of everything.
            mod(loopbackArgs(c.name + QStringLiteral(" → ") + m.name, cell + QStringLiteral(".in"), Names::channelNode(c.slug), true, {}, false,
                             cell, mixEntry(m.slug), {}, false, true));
        }
    for (const auto &i : inputs)   // physical input → channel (ADR 0007 D2); capture side waits for the device
        mod(loopbackArgs(QStringLiteral("Input: ") + i.name, EdgeNames::inputNode(i.slug) + QStringLiteral(".in"), i.device.node, false, i.device.positions, true,
                         EdgeNames::inputNode(i.slug), channelEntry(i.channel), {}, false, true));
    for (const auto &a : apps) {   // CH-12: extra channels of a multi-assigned app hear it via relay loopbacks.
        // Capture side sits on the APP's own output node (not the primary channel's monitor — that would carry
        // every other app on that channel too, measured on boreas 2026-09-16). linger: the app may not run yet.
        if (a.channels.isEmpty() || a.nodeName.isEmpty()) continue;
        for (int n = 1; n < a.channels.size(); ++n) {
            const LayoutChannel *to = channel(a.channels[n]);
            if (!to) continue;
            mod(loopbackArgs(QStringLiteral("App: ") + a.key, EdgeNames::relayNode(a.key, to->slug) + QStringLiteral(".in"),
                             a.nodeName, false, {}, true,
                             EdgeNames::relayNode(a.key, to->slug), channelEntry(to->slug), {}, false, true));
        }
    }
    for (const auto &m : mixes) {
        if (m.outputs.isEmpty())   // no output configured → park; linger anyway, the same node is retargeted onto devices later
            mod(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" → output"), EdgeNames::outputNode(m.slug, 0) + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                             EdgeNames::outputNode(m.slug, 0), QStringLiteral("kmixdeck.null"), {}, true, false));
        for (int n = 0; n < m.outputs.size(); ++n)   // one loopback per output (MX-9); playback side waits for the device
            mod(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" → ") + m.outputs[n].description, EdgeNames::outputNode(m.slug, n) + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                             EdgeNames::outputNode(m.slug, n), m.outputs[n].node, m.outputs[n].positions, true, false));
        // virtual capture source so OBS/Discord can pick this mix as an input (MX-3b)
        const QString src = EdgeNames::sourceNode(m.slug);
        mod(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" (capture)"), src + QStringLiteral(".in"), Names::mixNode(m.slug), true, {}, false,
                         src, QString(), {}, false, false,
                         QStringLiteral("node.description = %1 media.class = Audio/Source ").arg(q(QStringLiteral("kmixdeck ") + m.name + QStringLiteral(" Mix")))));
    }
    out += QStringLiteral("]\n");
    return out;
}
bool Layout::writePipewireConf(const QString &path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(toPipewireConf().toUtf8()); return f.commit();
}

} // namespace kmixdeck
