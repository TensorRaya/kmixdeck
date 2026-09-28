// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#include "layout.h"
#include "logging.h"
#include "mixer.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>

namespace kmixdeck {

QString Layout::defaultPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/kmixdeck/layout.json"); }
QString Layout::defaultPipewireConfPath() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/pipewire/pipewire.conf.d/90-kmixdeck.conf"); }

// Entry point a stream should target: the fx entry when a chain is active, the plain sink otherwise (ADR 0008 D1/D3).
QString Layout::channelEntry(const QString &slug) const {
    const auto *c = channel(slug);
    if (c && c->fx.isActive()) return QStringLiteral("kmixdeck.fx.%1").arg(slug);
    return Names::channelNode(slug);
}
QString Layout::mixEntry(const QString &slug) const {
    // Cells always sum into the mix sink. Routing them into the chain instead would make the chain the summing
    // bus and leave the sink (which every output edge and meter reads) dangling — measured 2026-09-19: the
    // limiter node existed, got signal, and its output went nowhere while audio kept flowing sink → output edge.
    return Names::mixNode(slug);
}
QString Layout::mixExit(const QString &slug) const {
    const auto *m = mix(slug);
    if (m && m->fx.isActive()) return QStringLiteral("kmixdeck.fx.mix.%1.out").arg(slug);
    return Names::mixNode(slug);
}

Layout Layout::starter() {
    Layout l;
    l.channels = {LayoutChannel::make(QStringLiteral("game"), QStringLiteral("Game"), QStringLiteral("input-gaming")),
                  LayoutChannel::make(QStringLiteral("system"), QStringLiteral("System"), QStringLiteral("computer")),
                  LayoutChannel::make(QStringLiteral("voice"), QStringLiteral("Voice"), QStringLiteral("audio-input-microphone"))};
    l.mixes = {LayoutMix::make(QStringLiteral("monitor"), QStringLiteral("Monitor"), QStringLiteral("audio-headphones")),
               LayoutMix::make(QStringLiteral("stream"), QStringLiteral("Stream"), QStringLiteral("camera-video"))};
    // UX-18: "The Stream mix SHOULD default it on." That is the one mix whose loudness a streamer is judged by.
    for (auto &m : l.mixes) if (m.slug == QLatin1String("stream")) m.loudness = true;
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
    if (!side.isEmpty()) o.insert(QStringLiteral("side"), side);
    if (trim != 1.0) o.insert(QStringLiteral("trim"), trim);
    if (muted) o.insert(QStringLiteral("muted"), true);
    return o;
}
DeviceRef DeviceRef::fromJson(const QJsonObject &o) {
    DeviceRef r; r.node = o.value(QStringLiteral("node")).toString(); r.description = o.value(QStringLiteral("description")).toString();
    for (const auto &v : o.value(QStringLiteral("positions")).toArray()) r.positions << v.toString();
    r.side = o.value(QStringLiteral("side")).toString();
    r.trim = o.contains(QStringLiteral("trim")) ? o.value(QStringLiteral("trim")).toDouble(1.0) : 1.0;
    r.muted = o.value(QStringLiteral("muted")).toBool(false);
    return r;
}

QJsonObject LayoutSample::toJson() const {
    QJsonObject o{{QStringLiteral("name"), name}, {QStringLiteral("path"), path}};
    if (length > 0.0) o.insert(QStringLiteral("length"), length);
    if (gain != 1.0) o.insert(QStringLiteral("gain"), gain);
    return o;
}
LayoutSample LayoutSample::fromJson(const QJsonObject &o) {
    LayoutSample s;
    s.name = o.value(QStringLiteral("name")).toString();
    s.path = o.value(QStringLiteral("path")).toString();
    s.length = o.value(QStringLiteral("length")).toDouble(0.0);
    // Clamped, not trusted: a hand-edited layout with gain 1e9 would blow the mix out on the next button press.
    s.gain = std::clamp(o.value(QStringLiteral("gain")).toDouble(1.0), 0.0, 4.0);
    return s;
}

QJsonArray fxChainArray(const fx::Chain &c) { QJsonArray a; for (const auto &e : c.effects) a.append(e.toJson()); return a; }

QJsonObject Layout::toJson() const {
    QJsonArray ch, mx, in;
    for (const auto &c : channels) {
        QJsonObject o{{QStringLiteral("slug"), c.slug}, {QStringLiteral("name"), c.name}, {QStringLiteral("icon"), c.icon}};
        if (!c.color.isEmpty()) o.insert(QStringLiteral("color"), c.color);
        if (!c.group.isEmpty()) o.insert(QStringLiteral("group"), c.group);
        if (c.pan != 0.0) o.insert(QStringLiteral("pan"), c.pan);   // DV-22
        if (!c.duckedBy.isEmpty()) {   // FX-9: off by default, so an unducked channel writes nothing
            o.insert(QStringLiteral("duckedBy"), c.duckedBy);
            o.insert(QStringLiteral("duckDepth"), c.duckDepth);
            o.insert(QStringLiteral("duckAttack"), c.duckAttack);
            o.insert(QStringLiteral("duckRelease"), c.duckRelease);
            o.insert(QStringLiteral("duckThreshold"), c.duckThreshold);
        }
        if (!c.fx.effects.isEmpty() || !c.fx.enabled) o.insert(QStringLiteral("fx"), QJsonObject{{QStringLiteral("enabled"), c.fx.enabled}, {QStringLiteral("chain"), fxChainArray(c.fx)}});
        // CT-8: a plain channel writes neither key, so an existing layout stays byte-identical and a v2 file
        // written by a build without the soundboard still loads here.
        if (!c.kind.isEmpty()) o.insert(QStringLiteral("kind"), c.kind);
        if (!c.samples.isEmpty()) { QJsonArray sa; for (const auto &s : c.samples) sa.append(s.toJson()); o.insert(QStringLiteral("samples"), sa); }
        ch.append(o);
    }
    for (const auto &m : mixes) {
        QJsonArray outs; for (const auto &d : m.outputs) outs.append(d.toJson());
        QJsonObject o{{QStringLiteral("slug"), m.slug}, {QStringLiteral("name"), m.name}, {QStringLiteral("icon"), m.icon}, {QStringLiteral("outputs"), outs}};
        if (!m.color.isEmpty()) o.insert(QStringLiteral("color"), m.color);
        if (!outs.isEmpty()) o.insert(QStringLiteral("outputDevice"), m.outputs.first().node);   // v1-compatible view: first entry
        if (!m.fallbackOutput.node.isEmpty()) o.insert(QStringLiteral("fallbackOutput"), m.fallbackOutput.toJson());
        if (!m.fx.effects.isEmpty() || !m.fx.enabled) o.insert(QStringLiteral("fx"), QJsonObject{{QStringLiteral("enabled"), m.fx.enabled}, {QStringLiteral("chain"), fxChainArray(m.fx)}});
        // UX-18: only written when it deviates from the default, so existing layouts stay byte-identical
        if (m.loudness) o.insert(QStringLiteral("loudness"), true);
        if (m.loudnessTarget != -14.0) o.insert(QStringLiteral("loudnessTarget"), m.loudnessTarget);
        mx.append(o);
    }
    for (const auto &i : inputs) in.append(QJsonObject{{QStringLiteral("slug"), i.slug}, {QStringLiteral("name"), i.name}, {QStringLiteral("device"), i.device.toJson()}, {QStringLiteral("channel"), i.channel}});
    QJsonArray appArr;
    for (const auto &a : apps) appArr.append(QJsonObject{{QStringLiteral("key"), a.key}, {QStringLiteral("nodeName"), a.nodeName}, {QStringLiteral("channels"), QJsonArray::fromStringList(a.channels)}});
    QJsonArray virtArr;
    for (const auto &v : virtualDevices) virtArr.append(QJsonObject{{QStringLiteral("slug"), v.slug}, {QStringLiteral("name"), v.name}, {QStringLiteral("inputs"), v.inputs}, {QStringLiteral("outputs"), v.outputs}, {QStringLiteral("portPrefix"), v.portPrefix}});
    QJsonArray cellArr;   // ADR 0013: only non-default cells (volume != 1 or muted) — like trim/mute, the rest is unity
    for (const auto &c : cells)
        if (c.volume != 1.0 || c.mute)
            cellArr.append(QJsonObject{{QStringLiteral("channel"), c.channel}, {QStringLiteral("mix"), c.mix}, {QStringLiteral("volume"), c.volume}, {QStringLiteral("mute"), c.mute}});
    return {{QStringLiteral("version"), kLayoutVersion}, {QStringLiteral("channels"), ch}, {QStringLiteral("mixes"), mx}, {QStringLiteral("inputs"), in},
            {QStringLiteral("apps"), appArr}, {QStringLiteral("virtualDevices"), virtArr},
            {QStringLiteral("defaultChannel"), defaultChannel}, {QStringLiteral("listeningDevice"), listeningDevice}, {QStringLiteral("knownApps"), QJsonArray::fromStringList(knownApps)}, {QStringLiteral("hiddenDevices"), QJsonArray::fromStringList(hiddenDevices)},
            {QStringLiteral("links"), [this] { QJsonArray a; for (const auto &l : links) a.append(QJsonObject{{QStringLiteral("channel"), l.channel}, {QStringLiteral("mix"), l.mix}, {QStringLiteral("follows"), l.follows}}); return a; }()},
            {QStringLiteral("cells"), cellArr}};
}
Layout Layout::fromJson(const QJsonObject &o) {
    Layout l;
    auto readFx = [](const QJsonObject &j) { fx::Chain c; if (j.contains(QStringLiteral("fx"))) c = fx::Chain::fromJson(j.value(QStringLiteral("fx")).toObject()); return c; };
    // CT-8: samples of a soundboard channel. Entries without a name or path are dropped — a nameless sample
    // could never be triggered and would just sit in the UI as a dead button.
    auto readSamples = [](const QJsonObject &j) {
        QVector<LayoutSample> v;
        for (const auto &e : j.value(QStringLiteral("samples")).toArray()) {
            const auto s = LayoutSample::fromJson(e.toObject());
            if (!s.name.isEmpty() && !s.path.isEmpty()) v.push_back(s);
        }
        return v;
    };
    if (o.contains(QStringLiteral("defaultChannel"))) l.defaultChannel = o.value(QStringLiteral("defaultChannel")).toString();
    l.listeningDevice = o.value(QStringLiteral("listeningDevice")).toString();
    for (const auto &v : o.value(QStringLiteral("knownApps")).toArray()) l.knownApps << v.toString();
    for (const auto &v : o.value(QStringLiteral("hiddenDevices")).toArray()) l.hiddenDevices << v.toString();
    for (const auto &v : o.value(QStringLiteral("links")).toArray()) { const auto j = v.toObject(); l.links.push_back({j.value(QStringLiteral("channel")).toString(), j.value(QStringLiteral("mix")).toString(), j.value(QStringLiteral("follows")).toString()}); }
    for (const auto &v : o.value(QStringLiteral("channels")).toArray()) { const auto c = v.toObject(); l.channels.push_back({c.value(QStringLiteral("slug")).toString(), c.value(QStringLiteral("name")).toString(), c.value(QStringLiteral("icon")).toString(), readFx(c), std::clamp(c.value(QStringLiteral("pan")).toDouble(0.0), -1.0, 1.0), c.value(QStringLiteral("color")).toString(), c.value(QStringLiteral("group")).toString(),
                                        // FX-9: ducking. Grenzen aus dem SC3-Plugin selbst (analyseplugin sc3_1427:
                                        // Attack 2..400 ms, Release 2..800 ms, Threshold -30..0 dB) — nicht geraten,
                                        // sonst klemmt PipeWire still auf den Plugin-Bereich und die UI zeigt etwas
                                        // anderes als der Graph tut.
                                        c.value(QStringLiteral("duckedBy")).toString(),
                                        std::clamp(c.value(QStringLiteral("duckDepth")).toDouble(-12.0), -60.0, 0.0),
                                        std::clamp(c.value(QStringLiteral("duckAttack")).toDouble(10.0), 2.0, 400.0),
                                        std::clamp(c.value(QStringLiteral("duckRelease")).toDouble(300.0), 2.0, 800.0),
                                        std::clamp(c.value(QStringLiteral("duckThreshold")).toDouble(-40.0), -60.0, 0.0),
                                        // CT-8: kind + samples come LAST, matching the struct order. Only "soundboard"
                                        // is accepted as a kind — an unknown one from a newer file reads as a normal
                                        // channel instead of creating something the daemon cannot reconcile.
                                        c.value(QStringLiteral("kind")).toString() == QLatin1String("soundboard") ? QStringLiteral("soundboard") : QString(),
                                        readSamples(c)}); }
    for (const auto &v : o.value(QStringLiteral("mixes")).toArray()) {
        const auto m = v.toObject(); LayoutMix lm;
        lm.slug = m.value(QStringLiteral("slug")).toString(); lm.name = m.value(QStringLiteral("name")).toString(); lm.icon = m.value(QStringLiteral("icon")).toString();
        lm.color = m.value(QStringLiteral("color")).toString();
        for (const auto &d : m.value(QStringLiteral("outputs")).toArray()) lm.outputs.push_back(DeviceRef::fromJson(d.toObject()));
        // version 1 files had a single "outputDevice" string
        const QString legacy = m.value(QStringLiteral("outputDevice")).toString();
        if (lm.outputs.isEmpty() && !legacy.isEmpty()) lm.outputs.push_back(DeviceRef{legacy, legacy, {}, {}});
        if (m.contains(QStringLiteral("fallbackOutput"))) lm.fallbackOutput = DeviceRef::fromJson(m.value(QStringLiteral("fallbackOutput")).toObject());
        lm.fx = readFx(m);
        lm.loudness = m.value(QStringLiteral("loudness")).toBool(false);                      // UX-18
        lm.loudnessTarget = m.value(QStringLiteral("loudnessTarget")).toDouble(-14.0);
        l.mixes.push_back(lm);
    }
    for (const auto &v : o.value(QStringLiteral("inputs")).toArray()) {
        const auto i = v.toObject();
        l.inputs.push_back({i.value(QStringLiteral("slug")).toString(), i.value(QStringLiteral("name")).toString(), DeviceRef::fromJson(i.value(QStringLiteral("device")).toObject()), i.value(QStringLiteral("channel")).toString()});
    }
    for (const auto &v : o.value(QStringLiteral("virtualDevices")).toArray()) {   // DV-23
        const auto j = v.toObject(); LayoutVirtualDevice d;
        d.slug = j.value(QStringLiteral("slug")).toString(); d.name = j.value(QStringLiteral("name")).toString();
        d.inputs = j.value(QStringLiteral("inputs")).toInt(8); d.outputs = j.value(QStringLiteral("outputs")).toInt(8);
        d.portPrefix = j.value(QStringLiteral("portPrefix")).toString(QStringLiteral("AUX"));
        if (!d.slug.isEmpty()) l.virtualDevices.push_back(d);
    }
    for (const auto &v : o.value(QStringLiteral("apps")).toArray()) {
        const auto a = v.toObject(); LayoutApp la;
        la.key = a.value(QStringLiteral("key")).toString(); la.nodeName = a.value(QStringLiteral("nodeName")).toString();
        for (const auto &c : a.value(QStringLiteral("channels")).toArray()) la.channels << c.toString();
        if (!la.key.isEmpty() && !la.channels.isEmpty()) l.apps.push_back(la);
    }
    for (const auto &v : o.value(QStringLiteral("cells")).toArray()) {   // ADR 0013
        const auto j = v.toObject();
        const QString ch = j.value(QStringLiteral("channel")).toString(), mx = j.value(QStringLiteral("mix")).toString();
        if (ch.isEmpty() || mx.isEmpty()) continue;
        l.cells.push_back({ch, mx, std::clamp(j.value(QStringLiteral("volume")).toDouble(1.0), 0.0, 4.0), j.value(QStringLiteral("mute")).toBool(false)});
    }
    return l;
}
// ADR 0013 cell accessors.
LayoutCell *Layout::cell(const QString &ch, const QString &mix) {
    for (auto &c : cells) if (c.channel == ch && c.mix == mix) return &c;
    return nullptr;
}
const LayoutCell *Layout::cell(const QString &ch, const QString &mix) const {
    for (const auto &c : cells) if (c.channel == ch && c.mix == mix) return &c;
    return nullptr;
}
void Layout::setCell(const QString &ch, const QString &mix, double volume, bool mute) {
    if (auto *c = cell(ch, mix)) { c->volume = std::clamp(volume, 0.0, 4.0); c->mute = mute; return; }
    cells.push_back({ch, mix, std::clamp(volume, 0.0, 4.0), mute});
}
double Layout::cellGain(const QString &ch, const QString &mix) const {
    const LayoutCell *c = cell(ch, mix);
    return c ? (c->mute ? 0.0 : c->volume) : 1.0;   // no entry = unity; mute = 0 (volume kept in the layout)
}
// B2: the watcher must judge the bytes it captured when the event arrived, not whatever is on disk by
// the time it gets round to looking — see Mixer::watchLayoutFile. Same rules as load(), one code path.
bool Layout::loadFromJson(const QByteArray &roh) {
    QJsonParseError err; const auto doc = QJsonDocument::fromJson(roh, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    if (doc.object().value(QStringLiteral("version")).toInt(1) > kLayoutVersion) return false;
    *this = fromJson(doc.object());
    return true;
}

bool Layout::load(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err; const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;   // CT-7: corrupt config → caller keeps the old layout
    // A file written by a NEWER kmixdeck is not corrupt — it is data we do not understand. Reading it as v2 would drop
    // the unknown keys and the next save would write that loss back. Keep the file, step aside, start fresh.
    const int version = doc.object().value(QStringLiteral("version")).toInt(1);
    if (version > kLayoutVersion) {
        const QString aside = path + QStringLiteral(".v%1-from-newer-kmixdeck").arg(version);
        QFile::remove(aside); QFile::rename(path, aside);
        qCWarning(lcMixer, "kmixdeck: %s is layout version %d, this build writes version %d — moved it to %s and starting with the default layout",
                 qUtf8Printable(path), version, kLayoutVersion, qUtf8Printable(aside));
        return false;
    }
    *this = fromJson(doc.object()); return true;
}
bool Layout::save(const QString &path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)); return f.commit();
}

static QString q(const QString &s) { QString r = s; r.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\"")); return QLatin1Char('"') + r + QLatin1Char('"'); }
// ADR 0009 D2/D3: the device-side stream carries the selected port positions; PipeWire links a stream port to the
// device port with the same audio.channel, so this alone selects the subset (AUX3 AUX4 → playback_AUX3/_AUX4,
// first named = the stream's left, second = right; measured on a fake 4-port sink).
static QString pos(const QStringList &p) { return p.isEmpty() ? QStringLiteral("[ FL FR ]") : QStringLiteral("[ ") + p.join(QLatin1Char(' ')) + QStringLiteral(" ]"); }

// ---- ADR 0013: the cell graph (shared by config renderer and runtime — they can never drift) ---------------------
// One filter-chain per channel (per bus group when M > 32). It reads the channel sink (stream.capture.sink +
// node.target: the MONITOR stream, post-trim, post-FX — the exact signal the old per-cell loopbacks captured
// from the plain sink), fans each side out to a builtin "mixer" node per mix, and lands the per-mix outputs on
// the shared bus. The "Gain 1" control of mixer L<idx>/R<idx> IS the cell fader for (channel, mix idx).
// PipeWire sums every channel stream on the bus; one loopback per mix then taps its own AUX2idx/AUX2idx+1 pair
// off the bus monitor into the mix entry (the mix sink, or its FX entry — unchanged). Measured (2026-09-27,
// sandbox): 32 channels × 4 mixes = 73 nodes / 1314 server fds / 39 clients vs 554 / 3000–4100 / ~300 for the
// old 128 loopbacks; cell gain 0.25 → −12.0 dB in exactly that mix, all other mixes untouched; a conf-baked
// gain survives a full pipewire+wireplumber restart.
// >32 mixes (SPA_AUDIO_MAX_CHANNELS = 64 = 2×32): the mixes spill onto additional buses (kmixdeck.bus.k) and
// each channel gets one chain per bus group — cellChainArgs() renders one group.
namespace {
// The builtin node blocks for one channel chain: one mixer per mix of the bus group, absolute indices
// firstIn..firstIn+mixes-1. Node names carry the mix SLUG (L<slug>/R<slug>) — the stable identifier the
// daemon uses for Graph::setControl ("<slug>:Gain 1") — while the AUX pair on the bus stays the absolute
// position AUX2*idx*/AUX2*idx*+1.
void cellChainNodesAndLinks(const int firstIn, const int mixes, const Layout &l, const QString &ch,
                            QStringList *nodeBlocks, QStringList *linkBlocks, QStringList *outPorts) {
    nodeBlocks->clear(); linkBlocks->clear(); outPorts->clear();
    *nodeBlocks << QStringLiteral("{ type = builtin label = copy name = iL }")
               << QStringLiteral("{ type = builtin label = copy name = iR }");
    QStringList names;   // node name per m, used by the links
    for (int m = 0; m < mixes; ++m) {
        const int idx = firstIn + m;
        const double gain = l.cellGain(ch, l.mixes[idx].slug);   // 0.0 when muted — the volume stays in the layout
        for (const QChar side : {QLatin1Char('L'), QLatin1Char('R')}) {
            const QString nm = side + l.mixes[idx].slug;
            names << nm;
            *nodeBlocks << QStringLiteral("{ type = builtin label = mixer name = %1 control = { \"Gain 1\" = %2 } }")
                            .arg(nm).arg(gain, 0, 'g', 10);
            *outPorts << QStringLiteral("\"%1:Out\"").arg(nm);
        }
    }
    for (int m = 0; m < mixes; ++m)
        for (int s = 0; s < 2; ++s)
            *linkBlocks << QStringLiteral("{ output = \"i%1:Out\" input = \"%2:In 1\" }").arg(s ? QLatin1Char('R') : QLatin1Char('L'), names[2 * m + s]);
}
}
QString ADR13::cellChainArgs(const Layout &l, const QString &ch, const int bus, const int nMixesOnBus) {
    const int firstIn = bus * kMixesPerBus;
    QStringList nodes, links, outs;
    cellChainNodesAndLinks(firstIn, nMixesOnBus, l, ch, &nodes, &links, &outs);
    const QString busPos = busPositions(nMixesOnBus).join(QLatin1Char(' '));
    return QStringLiteral("{ node.description = \"%1\" filter.graph = { nodes = [ %2 ] links = [ %3 ] inputs = [ \"iL:In\" \"iR:In\" ] outputs = [ %4 ] } "
                          "capture.props = { node.name = %5 media.name = %5 node.target = %6 audio.position = [ FL FR ] "
                          "stream.capture.sink = true node.passive = true node.dont-fallback = true } "
                          "playback.props = { node.name = %7 node.passive = true audio.position = [ %8 ] target.object = %9 "
                          "node.dont-fallback = true stream.dont-remix = true } }")
        .arg(QStringLiteral("cells ") + ch + QLatin1Char(' ') + chainSignature(l, bus), nodes.join(QLatin1Char(' ')), links.join(QLatin1Char(' ')), outs.join(QLatin1Char(' ')),
             q(chainNode(ch, bus)), q(Names::channelNode(ch)),
             q(chainPlaybackNode(ch, bus)), busPos, q(busNode(bus)));
}
QString ADR13::chainSignature(const Layout &l, const int bus) {
    QStringList s;
    for (int i = bus * kMixesPerBus; i < l.mixes.size() && i < (bus + 1) * kMixesPerBus; ++i) s << l.mixes[i].slug;
    return QLatin1Char('[') + s.join(QLatin1Char(' ')) + QLatin1Char(']');
}
QString ADR13::mixTapArgs(const QString &mix, const int mixIdx, const QString &mixEntry) {
    const int bus = busIndex(mixIdx);
    const int idxInBus = mixIdx - bus * kMixesPerBus;
    const QString a = QStringLiteral("AUX") + QString::number(2 * idxInBus), b = QStringLiteral("AUX") + QString::number(2 * idxInBus + 1);
    return QStringLiteral("{ node.description = \"mix %1 %4 %5\" capture.props = { node.name = %2 media.name = %2 stream.capture.sink = true target.object = %3 "
                          "audio.position = [ %4 %5 ] stream.dont-remix = true node.passive = true node.dont-fallback = true } "
                          "playback.props = { node.name = %6 media.name = %6 target.object = %7 audio.position = [ FL FR ] "
                          "node.dont-fallback = true node.dont-reconnect = true } }")
        .arg(mix, q(tapNode(mix) + QStringLiteral(".in")), q(busNode(bus)), a, b,
             q(tapNode(mix)), q(mixEntry));
}

QString virtualPassArgs(const LayoutVirtualDevice &v) {
    // The capture half reads the sink's monitor (stream.capture.sink); the playback half IS the device's input side:
    // media.class Audio/Source with the input positions — the same shape as the mix capture source
    // (kmixdeck.source.<mix>). A separate null source with a loopback merely *targeting* it never got a single link
    // from WirePlumber in the sandbox (measured 2026-09-19: node.target, target.object, passive, always-process — all
    // 0 links, the loopback nodes never even grew ports). Port N of the sink lands on port N of the source (equal
    // position names); the device's usable input count is min(inputs, outputs).
    // node.async = true is not optional: a virtual device is typically BOTH a channel's input and a mix's output
    // ("Games" plays into it, the "Games" channel hears it, the stream mix returns to it for the Ui24R). That is a
    // cycle in the scheduling graph — .out → pass → source → channel edge → channel → mix → .out — and PipeWire
    // cannot order a cyclic graph: every node in the loop stays "running" and never gets a buffer. Measured
    // 2026-09-19 (sandbox): the moment `mix output stream <virt>.out:AUX7,AUX8` landed, every recorder anywhere in
    // that subgraph produced a 44-byte WAV. An async node breaks the cycle with one quantum of delay (~21 ms at
    // 1024/48000), which is fine for a return path and invisible for a monitor.
    const int n = std::min(v.inputs, v.outputs);
    const QStringList pos = v.positions(n);
    return loopbackArgs(v.name + QStringLiteral(" (virtual) pass-through"),
                        EdgeNames::virtualPassNode(v.slug) + QStringLiteral(".in"), v.outputNode(), true, pos, false,
                        v.inputNode(), QString(), pos, false, false,
                        QStringLiteral("node.description = %1 media.class = Audio/Source node.async = true ").arg(q(v.name + QStringLiteral(" (virtual)"))));
}
QString loopbackArgs(const QString &description,
                     const QString &captureName, const QString &captureTarget, bool captureIsSink, const QStringList &capturePositions, bool captureLinger,
                     const QString &playbackName, const QString &playbackTarget, const QStringList &playbackPositions, bool playbackLinger, bool playbackDontReconnect,
                     const QString &playbackExtra) {
    // Every stream: dont-fallback (never silently the default device — feedback, ADR 0002 trap 3).
    // Device-side streams: linger (wait for an absent device, WirePlumber relinks it — ADR 0007 D3).
    // Cell playbacks: dont-reconnect (a cell must never wander); mix outputs must NOT have it (they follow retargets).
    // One-port (mono) device on the capture side: the port name (AUX3) is nothing the channel mixer can spread, so
    // the playback half is declared [MONO] and the far sink's own channelmix puts it on FL and FR (DV-19; with
    // [FL FR] on that side the tone landed on FL only — measured).
    const bool sideCap = capturePositions.size() == 1 && (capturePositions[0] == QLatin1String("FL") || capturePositions[0] == QLatin1String("FR"));
    const bool monoIn = capturePositions.size() == 1 && !sideCap;   // one named side of a stereo node is not "mono"
    // ADR 0009 A2: playback bound to ONE side (FL or FR) — a 1-port capture lands on exactly that side. A 2-port
    // capture into one side is NOT offered: PipeWire 1.6's loopback does not fold two named positions into one
    // (measured: AUX3+AUX4→[FL] silent in every variant — upmix, dont-remix, MONO capture); the daemon
    // refuses that ref (validateDeviceRef) instead of building a silent edge.
    const QString capPos = pos(capturePositions);
    QString cap = QStringLiteral("capture.props = { node.name = %1 media.name = %1 node.target = %2 audio.position = %3 node.passive = true node.dont-fallback = true %4%5}")
        .arg(q(captureName), q(captureTarget), capPos,
             captureIsSink ? QStringLiteral("stream.capture.sink = true node.dont-reconnect = true ") : QString(),
             captureLinger ? QStringLiteral("node.linger = true ") : QString());
    // A virtual source (the mix capture node) has no target at all: it is a device others capture from.
    QString play = QStringLiteral("playback.props = { node.name = %1 media.name = %1 %2audio.position = %3 node.dont-fallback = true %4%5%6}")
        .arg(q(playbackName), playbackTarget.isEmpty() ? QString() : QStringLiteral("node.target = %1 ").arg(q(playbackTarget)),
             monoIn && playbackPositions.isEmpty() ? QStringLiteral("[ MONO ]") : pos(playbackPositions),
             playbackDontReconnect ? QStringLiteral("node.dont-reconnect = true ") : QString(),
             playbackLinger ? QStringLiteral("node.linger = true ") : QString(), playbackExtra);
    return QStringLiteral("{ node.description = %1 %2 %3 }").arg(q(description), cap, play);
}

QString Layout::toPipewireConf() const {
    QString out;
    out += QStringLiteral("# Generated by kmixdeckd — do not edit; change the layout through kmixdeck/kmixdeck-kde instead.\n"
                          "# ADR 0013: channel = null sink, mix = null sink, cell = the \"Gain 1\" of a per-channel\n"
                          "# filter-chain (kmixdeck.cells.<ch>) that feeds the shared cell bus (kmixdeck.bus); one loopback\n"
                          "# per mix taps its AUX pair off the bus into the mix. The chain's cell gains come from the layout.\n"
                          "# ADR 0008: a channel/mix with effects gets a filter-chain instead of the plain sink; its capture node\n"
                          "# keeps the plain name, everything else targets that name unchanged.\n"
                          "# Every loopback has node.dont-fallback so a missing target never silently becomes the default sink (feedback).\n"
                          "# ADR 0007: device edges (inputs, mix outputs) carry node.linger — they wait for an absent device and\n"
                          "# WirePlumber links them by itself when it appears. A mix with NO output configured parks on kmixdeck.null.\n\n");
    out += QStringLiteral("context.objects = [\n");
    out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = \"kmixdeck.null\" media.name = \"kmixdeck.null\" node.description = \"kmixdeck (unrouted)\" media.class = Audio/Sink object.linger = true audio.position = [ FL FR ] priority.session = 0 priority.driver = 0 node.passive = true } }\n");
    for (const auto &v : virtualDevices) {   // DV-23: virtual multichannel device = one sink (its outputs) + one virtual source (its inputs)
        out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.name = %1 node.description = %2 media.class = Audio/Sink object.linger = true audio.position = %3 monitor.channel-volumes = true } }\n")
                   .arg(q(v.outputNode()), q(v.name + QStringLiteral(" (virtual) outputs")), pos(v.positions(v.outputs)));
        if (v.outputs <= 0)   // no output side: the input side stays a standalone virtual source (feed its input_* ports = mic emulation)
            out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.name = %1 node.description = %2 media.class = Audio/Source/Virtual object.linger = true audio.position = %3 } }\n")
                       .arg(q(v.inputNode()), q(v.name + QStringLiteral(" (virtual)")), pos(v.positions(v.inputs)));
        // otherwise the input side is the playback half of the pass-through loopback below (DV-29)
    }
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
        // FX-9 needs NO module here. Ducking is a live factor on the channel sink's gain, driven by the
        // daemon's meter tick (Mixer::tickDucking → applyChannelGain). Until 2026-09-22 this rendered a
        // filter-chain that read the channel monitor and played into `kmixdeck.null` — a branch beside the
        // signal path, which cannot attenuate the path. Leaving it in the config fragment would rebuild
        // that dead node at every login and make `pw-dump` look as if ducking were wired up.
    }
    for (const auto &m : mixes) {
        out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.name = %1 node.description = %2 media.class = Audio/Sink object.linger = true audio.position = [ FL FR ] monitor.channel-volumes = true } }\n")
                   .arg(q(Names::mixNode(m.slug)), q(QStringLiteral("Mix: ") + m.name));
        fxModule(m.fx, QStringLiteral("Mix: ") + m.name, QStringLiteral("kmixdeck.fx.mix.%1").arg(m.slug), QStringLiteral("kmixdeck.fx.mix.%1.out").arg(m.slug),
                 Names::mixNode(m.slug), Names::mixNode(m.slug), m.slug);
    }
    // ADR 0013: the shared cell bus. PipeWire sums every channel chain's playback stream here (2 channels per
    // mix: AUX2i/AUX2i+1). A summing BUS, not a device — priority.session = 0 keeps it out of the pickers, like
    // every other kmixdeck sink (ADR 0007). >32 mixes spill onto kmixdeck.bus.k (SPA_AUDIO_MAX_CHANNELS = 64).
    const int buses = mixes.isEmpty() ? 0 : ADR13::busCount(mixes.size());   // no mix → no bus (same rule as the daemon)
    for (int b = 0; b < buses; ++b) {
        out += QStringLiteral("  { factory = adapter args = { factory.name = support.null-audio-sink node.name = %1 media.class = Audio/Sink object.linger = true audio.position = [ %2 ] priority.session = 0 priority.driver = 0 } }\n")
                   .arg(q(ADR13::busNode(b)), ADR13::busPositions(ADR13::kMixesPerBus).join(QLatin1Char(' ')));   // always 64: adding a mix never rebuilds the bus (measured 2026-09-27: 32x4 on a 64-ch bus = 75 nodes / 1314 fds, -12.0 dB)
    }
    out += QStringLiteral("]\n\ncontext.modules = [\n");
    const auto mod = [&](const QString &args) { out += QStringLiteral("  { name = libpipewire-module-loopback args = %1 }\n").arg(args); };
    // ADR 0013: the cell graph — one filter-chain per (channel, bus group) replaces the per-cell loopbacks,
    // and one bus tap per mix reads the mix's AUX pair off the shared bus into the mix entry.
    for (const auto &c : channels)
        for (int b = 0; b < buses; ++b) {
            const int first = b * ADR13::kMixesPerBus;
            const int n = qMin(mixes.size() - first, ADR13::kMixesPerBus);
            if (n <= 0) continue;
            out += QStringLiteral("  { name = libpipewire-module-filter-chain args = %1 }\n").arg(ADR13::cellChainArgs(*this, c.slug, b, n));
        }
    for (int i = 0; i < mixes.size(); ++i)
        mod(ADR13::mixTapArgs(mixes[i].slug, i, mixEntry(mixes[i].slug)));
    for (const auto &v : virtualDevices)   // DV-29: .out port N → source port N; the loopback's playback half IS the source
        if (v.outputs > 0 && v.inputs > 0)
            mod(virtualPassArgs(v));
    for (const auto &i : inputs)   // physical input → channel (ADR 0007 D2); capture side waits for the device
        mod(loopbackArgs(QStringLiteral("Input: ") + i.name, EdgeNames::inputNode(i.slug) + QStringLiteral(".in"), i.device.node, false, i.device.positions, true,
                         EdgeNames::inputNode(i.slug), channelEntry(i.channel), i.device.channelSidePositions(), false, true));
    for (const auto &a : apps) {   // CH-12: extra channels of a multi-assigned app hear it via relay loopbacks.
        // Capture side sits on the APP's own output node (not the primary channel's monitor — that would carry
        // every other app on that channel too, measured on the dev machine). linger: the app may not run yet.
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
            mod(loopbackArgs(QStringLiteral("Mix: ") + m.name + QStringLiteral(" → ") + m.outputs[n].description, EdgeNames::outputNode(m.slug, n) + QStringLiteral(".in"), Names::mixNode(m.slug), true, m.outputs[n].channelSidePositions(), false,
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
