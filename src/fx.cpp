// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx.h"

#include <dlfcn.h>
#include <ladspa.h>

#include <QDebug>
#include <QDir>
#include <QLatin1Char>

namespace kmixdeck::fx {

QJsonObject Effect::toJson() const {
    QJsonObject o{{QStringLiteral("type"), type}, {QStringLiteral("enabled"), enabled}};
    QJsonObject p;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) p.insert(it.key(), it.value());
    o.insert(QStringLiteral("params"), p);
    if (type == QLatin1String("ladspa")) { o.insert(QStringLiteral("plugin"), plugin); if (!label.isEmpty()) o.insert(QStringLiteral("label"), label); }
    return o;
}
Effect Effect::fromJson(const QJsonObject &o) {
    Effect e;
    e.type = o.value(QStringLiteral("type")).toString();
    e.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    const QJsonObject po = o.value(QStringLiteral("params")).toObject();   // local: an iterator over a temporary dangles after the init statement
    for (auto it = po.constBegin(); it != po.constEnd(); ++it)
        e.params.insert(it.key(), it.value().toDouble());
    e.plugin = o.value(QStringLiteral("plugin")).toString();
    e.label = o.value(QStringLiteral("label")).toString();
    return e;
}
QJsonObject Chain::toJson() const {
    QJsonArray a;
    for (const auto &e : effects) a.append(e.toJson());
    return {{QStringLiteral("enabled"), enabled}, {QStringLiteral("chain"), a}};
}
Chain Chain::fromJson(const QJsonObject &o) {
    Chain c;
    c.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    for (const auto &v : o.value(QStringLiteral("chain")).toArray()) c.effects.push_back(Effect::fromJson(v.toObject()));
    return c;
}

const QVector<TypeSpec> &builtinTypes() {
    static const QVector<TypeSpec> types = {
        {QStringLiteral("highpass"), QStringLiteral("High-pass"), QStringLiteral("cuts rumble below a frequency"),
         {{QStringLiteral("freq"), QStringLiteral("Frequency"), QStringLiteral("Hz"), 20.0, 2000.0, 100.0},
          {QStringLiteral("q"), QStringLiteral("Q"), QString(), 0.4, 2.0, 0.7}}, QString()},
        {QStringLiteral("gate"), QStringLiteral("Noise gate"), QStringLiteral("silence below the threshold"),
         {{QStringLiteral("threshold"), QStringLiteral("Threshold"), QStringLiteral("dB"), -70.0, 0.0, -40.0},
          {QStringLiteral("attack"), QStringLiteral("Attack"), QStringLiteral("ms"), 0.01, 100.0, 5.0},
          {QStringLiteral("hold"), QStringLiteral("Hold"), QStringLiteral("ms"), 2.0, 500.0, 50.0},
          {QStringLiteral("decay"), QStringLiteral("Decay"), QStringLiteral("ms"), 2.0, 1000.0, 100.0},
          {QStringLiteral("range"), QStringLiteral("Range"), QStringLiteral("dB"), -90.0, 0.0, -90.0}}, QStringLiteral("gate_1410")},
        {QStringLiteral("compressor"), QStringLiteral("Compressor"), QStringLiteral("evens out loud and soft"),
         {{QStringLiteral("threshold"), QStringLiteral("Threshold"), QStringLiteral("dB"), -60.0, 0.0, -20.0},
          {QStringLiteral("ratio"), QStringLiteral("Ratio"), QStringLiteral(":1"), 1.0, 20.0, 3.0},
          {QStringLiteral("attack"), QStringLiteral("Attack"), QStringLiteral("ms"), 1.5, 400.0, 10.0},
          {QStringLiteral("release"), QStringLiteral("Release"), QStringLiteral("ms"), 2.0, 800.0, 100.0},
          {QStringLiteral("makeup"), QStringLiteral("Makeup"), QStringLiteral("dB"), 0.0, 24.0, 0.0}}, QStringLiteral("sc4m_1916")},
        {QStringLiteral("eq"), QStringLiteral("EQ (3 bands)"), QStringLiteral("low shelf / mid / high shelf"),
         {{QStringLiteral("lowGain"), QStringLiteral("Low"), QStringLiteral("dB"), -12.0, 12.0, 0.0},
          {QStringLiteral("midGain"), QStringLiteral("Mid"), QStringLiteral("dB"), -12.0, 12.0, 0.0},
          {QStringLiteral("highGain"), QStringLiteral("High"), QStringLiteral("dB"), -12.0, 12.0, 0.0},
          {QStringLiteral("lowFreq"), QStringLiteral("Low frequency"), QStringLiteral("Hz"), 40.0, 400.0, 120.0},
          {QStringLiteral("midFreq"), QStringLiteral("Mid frequency"), QStringLiteral("Hz"), 300.0, 3000.0, 1000.0},
          {QStringLiteral("highFreq"), QStringLiteral("High frequency"), QStringLiteral("Hz"), 2000.0, 12000.0, 6000.0}}, QString()},
        {QStringLiteral("limiter"), QStringLiteral("Limiter"), QStringLiteral("brick wall against peaks"),
         {{QStringLiteral("limit"), QStringLiteral("Limit"), QStringLiteral("dB"), -20.0, 0.0, -3.0},
          {QStringLiteral("release"), QStringLiteral("Release"), QStringLiteral("s"), 0.01, 2.0, 0.5}}, QStringLiteral("fast_lookahead_limiter_1913")},
        // FX-10: the broadcast-safe limiter for MIXES. Same swh plugin as "limiter", but the parameter the user
        // sees is a ceiling in dBTP (Elgato calls this Clipguard) and the stage carries its own input gain so a
        // quiet mix can be driven INTO the ceiling instead of sitting below it doing nothing. Mix-only: enforced
        // in validate(), because channels are supposed to clip before the mix (the limiter is a mix property).
        {QStringLiteral("brickwall"), QStringLiteral("Broadcast limiter"), QStringLiteral("brick wall on the mix sum — protects the stream from clipping"),
         {{QStringLiteral("ceiling"), QStringLiteral("Ceiling"), QStringLiteral("dBTP"), -20.0, 0.0, -1.0},
          {QStringLiteral("gain"), QStringLiteral("Input gain"), QStringLiteral("dB"), -20.0, 20.0, 0.0},
          {QStringLiteral("release"), QStringLiteral("Release"), QStringLiteral("s"), 0.01, 2.0, 0.5}}, QStringLiteral("fast_lookahead_limiter_1913")},
        {QStringLiteral("noise"), QStringLiteral("Noise suppression"), QStringLiteral("RNNoise — ML denoiser for the microphone"),
         {{QStringLiteral("vad"), QStringLiteral("Voice threshold"), QStringLiteral("%"), 0.0, 100.0, 50.0}}, QStringLiteral("librnnoise_ladspa")},
    };
    return types;
}
const TypeSpec *typeSpec(const QString &type) {
    for (const auto &t : builtinTypes()) if (t.type == type) return &t;
    return nullptr;
}

QString ladspaPath(const QString &file) {
    if (file.isEmpty()) return {};
    QStringList dirs;
    for (const QByteArray &p : qgetenv("LADSPA_PATH").split(':')) if (!p.isEmpty()) dirs << QString::fromUtf8(p);
    dirs << QStringLiteral("/usr/lib/ladspa") << QStringLiteral("/usr/lib64/ladspa") << QStringLiteral("/usr/local/lib/ladspa");
#ifdef KMIXDECK_MULTIARCH   // Debian-style /usr/lib/<triplet>/ladspa — the triplet comes from the compiler, not from a guess
    dirs << QStringLiteral("/usr/lib/" KMIXDECK_MULTIARCH "/ladspa");
#endif
    for (const QString &d : dirs) {
        const auto hits = QDir(d).entryList({file + QStringLiteral(".so")}, QDir::Files);
        if (!hits.isEmpty()) return d + QLatin1Char('/') + hits.first();
    }
    return {};
}

bool ladspaAvailable(const QString &file) { return file.isEmpty() || !ladspaPath(file).isEmpty(); }

LadspaPorts ladspaPorts(const QString &file, const QString &label) {
    LadspaPorts out;
    const QString so = ladspaPath(file);
    if (so.isEmpty()) return out;
    // Ask the plugin for its port names instead of assuming "In"/"Out". Measured 2026-09-21: assuming them
    // made filter-chain silently discard the whole graph for every multi-channel plugin — the node came up,
    // every control read back as 0.0 and the audio was passed through untouched (specs/fx9-ducking.md).
    void *lib = dlopen(so.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) return out;
    auto descriptor = reinterpret_cast<LADSPA_Descriptor_Function>(dlsym(lib, "ladspa_descriptor"));
    if (descriptor) {
        for (unsigned long i = 0; const LADSPA_Descriptor *d = descriptor(i); ++i) {
            // An empty label means "first plugin in the library", which is what a bare `plugin = x` asks for.
            if (!label.isEmpty() && label != QString::fromUtf8(d->Label)) continue;
            for (unsigned long p = 0; p < d->PortCount; ++p) {
                if (!LADSPA_IS_PORT_AUDIO(d->PortDescriptors[p])) continue;
                const QString n = QString::fromUtf8(d->PortNames[p]);
                (LADSPA_IS_PORT_INPUT(d->PortDescriptors[p]) ? out.inputs : out.outputs) << n;
            }
            break;
        }
    }
    dlclose(lib);
    return out;
}

QString packageHint(const QString &file) {
    if (file == QLatin1String("librnnoise_ladspa"))
        return QStringLiteral("noise-suppression-for-voice (Arch/AUR) or calf-ladspa/rnnoise from your distro");
    return QStringLiteral("swh-plugins");   // gate/compressor/limiter all live in swh
}

QJsonObject presetChains() {
    auto chain = [](std::initializer_list<const char*> types) {
        QJsonArray a;
        for (const char *t : types) a.append(QJsonObject{{QStringLiteral("type"), QString::fromLatin1(t)}, {QStringLiteral("enabled"), true}});
        return QJsonObject{{QStringLiteral("enabled"), true}, {QStringLiteral("chain"), a}};
    };
    QJsonObject voice = chain({QByteArrayLiteral("noise").constData(), QByteArrayLiteral("highpass").constData(), QByteArrayLiteral("gate").constData(), QByteArrayLiteral("compressor").constData(), QByteArrayLiteral("limiter").constData()});
    // FX-8: the denoiser sits FIRST in the chain but ships SWITCHED OFF. Two separate reasons,
    // both of which cost someone a stream if ignored:
    //
    // Off, because RNNoise needs a LADSPA plugin that most distros do not install by default.
    // A preset that enables it would be rejected as a whole on such a machine (validate()
    // refuses an enabled effect whose plugin is missing) — the user asks for a voice chain and
    // gets an error instead of the four effects that would work fine. Off means the preset
    // always applies; the denoiser is one toggle away once the package is there.
    //
    // First, because denoising belongs before the gate: the gate's threshold should see the
    // cleaned signal, otherwise it opens on room noise and the chain fights itself.
    {
        QJsonArray c = voice[QStringLiteral("chain")].toArray();
        QJsonObject erst = c[0].toObject();
        // Not Q_ASSERT: that compiles away in release builds, and this is exactly the kind of
        // invariant that breaks silently when someone reorders the chain above. If the first
        // entry is ever not the denoiser, switching off index 0 would disable the wrong effect
        // and ship a preset that quietly does less than it says.
        if (erst[QStringLiteral("type")].toString() == QLatin1String("noise")) {
            erst[QStringLiteral("enabled")] = false;
            c[0] = erst;
            voice[QStringLiteral("chain")] = c;
        } else {
            qWarning("kmixdeck: preset 'Voice — clean': expected the denoiser first, found '%s' "
                     "— leaving the chain alone rather than disabling the wrong effect",
                     qUtf8Printable(erst[QStringLiteral("type")].toString()));
        }
    }
    // broadcast: tighter, no denoiser (hardware gate does it), stronger compression
    QJsonObject bcast = chain({QByteArrayLiteral("gate").constData(), QByteArrayLiteral("compressor").constData(), QByteArrayLiteral("eq").constData(), QByteArrayLiteral("limiter").constData()});
    return {{QStringLiteral("Voice — clean"), voice}, {QStringLiteral("Voice — broadcast"), bcast}};
}
QString validate(const Chain &c, bool onMix) {
    for (const auto &e : c.effects) {
        // FX-10: the broadcast limiter is a MIX property. Channels are supposed to clip before the mix does, and a
        // per-channel brick wall would hide exactly the overload the streamer needs to see.
        if (e.type == QLatin1String("brickwall") && !onMix)
            return QStringLiteral("'brickwall' is a mix effect (FX-10) — put it on a mix, not on a channel");
        if (e.type == QLatin1String("ladspa")) {
            if (e.plugin.isEmpty()) return QStringLiteral("ladspa effect needs a plugin name");
            // Library presence matters only for effects that will be rendered. A bypassed effect whose library is missing
            // on THIS machine must not make the whole chain unloadable — FX-7 (copy/import from another machine) and FX-5
            // (bypass = the user's way of saying "not now") both depend on that. Found by tests/unit/fxtest.cpp.
            if (e.enabled && !ladspaAvailable(e.plugin))
                return QStringLiteral("LADSPA '%1' not installed — %2").arg(e.plugin, packageHint(e.plugin));
            continue;   // arbitrary controls: anything the plugin exposes is fine
        }
        const TypeSpec *spec = typeSpec(e.type);
        if (!spec) return QStringLiteral("unknown effect type '%1'").arg(e.type);
        if (e.enabled && !ladspaAvailable(spec->ladspaFile))
            return QStringLiteral("effect '%1' needs %2 — %3").arg(e.type, spec->ladspaFile, packageHint(spec->ladspaFile));
        for (auto it = e.params.constBegin(); it != e.params.constEnd(); ++it) {
            const ParamSpec *ps = nullptr;
            for (const auto &p : spec->params) if (p.key == it.key()) { ps = &p; break; }
            if (!ps) return QStringLiteral("'%1' has no parameter '%2'").arg(e.type, it.key());
            if (it.value() < ps->min || it.value() > ps->max)
                return QStringLiteral("'%1.%2' must be within [%3, %4]").arg(e.type, it.key()).arg(ps->min).arg(ps->max);
        }
    }
    return {};
}

// ---- rendering -------------------------------------------------------------------------------------------
// Node names stay stable across edits so live Props updates can address them: "hp", "gate", "comp", "lim",
// "eq_low"/"eq_mid"/"eq_high", "noise"; duplicates of the same type count up ("gate2").
namespace {
QString nodePrefix(const Effect &e) {
    if (e.type == QLatin1String("ladspa")) return e.label.isEmpty() ? e.plugin : e.label;
    if (e.type == QLatin1String("highpass")) return QStringLiteral("hp");
    if (e.type == QLatin1String("compressor")) return QStringLiteral("comp");
    if (e.type == QLatin1String("limiter")) return QStringLiteral("lim");
    if (e.type == QLatin1String("brickwall")) return QStringLiteral("bw");
    return e.type;
}
double param(const Effect &e, const QString &key, double def) {
    auto it = e.params.constFind(key); return it == e.params.constEnd() ? def : it.value();
}
} // namespace

QString renderFilterChainArgs(const Chain &c, const QString &description, const QString &entryNode,
                              const QString &exitNode, const QString &mediaName, const QString &targetSink,
                              const QString &idPrefix, bool behindSink) {
    // allIn/allOut: EVERY audio port of the plugin, not just the first. A stereo plugin whose second port is
    // missing from `inputs` makes filter-chain drop the graph silently (measured 2026-09-21, sc3_1427).
    struct Rendered { QString node, inPort, outPort; QStringList extras, allIn, allOut; };
    QVector<Rendered> nodes;
    QHash<QString, QString> uniq;                            // prefix → unique name with count
    auto push = [&](const QString &name, QStringList extras, QString in, QString out,
                    const QStringList &allIn = {}, const QStringList &allOut = {}) {
        if (in.isEmpty()) in = QStringLiteral("In");          // builtin + mono ladspa use In/Out
        if (out.isEmpty()) out = QStringLiteral("Out");
        nodes.push_back({name, in, out, extras, allIn.isEmpty() ? QStringList{in} : allIn,
                         allOut.isEmpty() ? QStringList{out} : allOut});
    };
    for (const auto &e : c.effects) {
        if (!e.enabled) continue;
        QString base = idPrefix + nodePrefix(e);   // per-channel unique so Props keys and destroy-by-prefix stay unambiguous
        QString name = base; int n = 2;
        while (uniq.value(base) == name) name = base + QString::number(n++);
        uniq.insert(base, name);
        auto bq = [&](const char *label, const QString &freqKey, double fdef, const QString &gainKey, double gdef) {
            push(name, {QStringLiteral("type = builtin"), QStringLiteral("label = \"%1\"").arg(QLatin1String(label)),
                        QStringLiteral("control = { \"Freq\" = %1 \"Q\" = %2 \"Gain\" = %3 }")
                            .arg(param(e, freqKey, fdef)).arg(param(e, "q", 0.7)).arg(param(e, gainKey, gdef))}, {}, {});
        };
        if (e.type == QLatin1String("highpass")) bq("bq_highpass", "freq", 100.0, QStringLiteral(""), 0.0);
        else if (e.type == QLatin1String("eq")) {
            // three real nodes; controls carry the node name
            push(name + QStringLiteral("_low"), {QStringLiteral("type = builtin"), QStringLiteral("label = \"bq_lowshelf\""),
                  QStringLiteral("control = { \"Freq\" = %1 \"Q\" = 1.0 \"Gain\" = %2 }").arg(param(e, "lowFreq", 120.0)).arg(param(e, "lowGain", 0.0))}, {}, {});
            push(name + QStringLiteral("_mid"), {QStringLiteral("type = builtin"), QStringLiteral("label = \"bq_peaking\""),
                  QStringLiteral("control = { \"Freq\" = %1 \"Q\" = 1.0 \"Gain\" = %2 }").arg(param(e, "midFreq", 1000.0)).arg(param(e, "midGain", 0.0))}, {}, {});
            push(name + QStringLiteral("_high"), {QStringLiteral("type = builtin"), QStringLiteral("label = \"bq_highshelf\""),
                  QStringLiteral("control = { \"Freq\" = %1 \"Q\" = 1.0 \"Gain\" = %2 }").arg(param(e, "highFreq", 6000.0)).arg(param(e, "highGain", 0.0))}, {}, {});
        }
        else if (e.type == QLatin1String("gate"))
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = gate_1410"), QStringLiteral("label = gate"),
                        QStringLiteral("control = { \"Threshold (dB)\" = %1 \"Attack (ms)\" = %2 \"Hold (ms)\" = %3 \"Decay (ms)\" = %4 \"Range (dB)\" = %5 }")
                            .arg(param(e, "threshold", -40.0)).arg(param(e, "attack", 5.0)).arg(param(e, "hold", 50.0))
                            .arg(param(e, "decay", 100.0)).arg(param(e, "range", -90.0))}, QStringLiteral("Input"), QStringLiteral("Output"));
        else if (e.type == QLatin1String("compressor"))
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = sc4m_1916"), QStringLiteral("label = sc4m"),
                        QStringLiteral("control = { \"Threshold level (dB)\" = %1 \"Ratio (1:n)\" = %2 \"Attack time (ms)\" = %3 \"Release time (ms)\" = %4 \"Makeup gain (dB)\" = %5 }")
                            .arg(param(e, "threshold", -20.0)).arg(param(e, "ratio", 3.0)).arg(param(e, "attack", 10.0))
                            .arg(param(e, "release", 100.0)).arg(param(e, "makeup", 0.0))}, QStringLiteral("Input"), QStringLiteral("Output"));
        else if (e.type == QLatin1String("limiter"))
            // numbered ports on swh's limiter; filter-chain maps FL→Input 1/fr → Output 1 with FR on port 2
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = fast_lookahead_limiter_1913"), QStringLiteral("label = fastLookaheadLimiter"),
                        QStringLiteral("control = { \"Limit (dB)\" = %1 \"Release time (s)\" = %2 }").arg(param(e, "limit", -3.0)).arg(param(e, "release", 0.5))},
                 QStringLiteral("Input 1"), QStringLiteral("Output 1"));
        else if (e.type == QLatin1String("brickwall"))
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = fast_lookahead_limiter_1913"), QStringLiteral("label = fastLookaheadLimiter"),
                        QStringLiteral("control = { \"Input gain (dB)\" = %1 \"Limit (dB)\" = %2 \"Release time (s)\" = %3 }")
                            .arg(param(e, "gain", 0.0)).arg(param(e, "ceiling", -1.0)).arg(param(e, "release", 0.5))},
                 QStringLiteral("Input 1"), QStringLiteral("Output 1"));
        else if (e.type == QLatin1String("noise"))
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = librnnoise_ladspa"), QStringLiteral("label = noise_suppressor_stereo"),
                        QStringLiteral("control = { \"VAD Threshold (%%)\" = %1 }").arg(param(e, "vad", 50.0))}, QStringLiteral("Input"), QStringLiteral("Output"));
        else {   // ladspa: plugin[:label] + arbitrary controls
            QStringList ctrl;
            for (auto it = e.params.constBegin(); it != e.params.constEnd(); ++it)
                ctrl << QStringLiteral("\"%1\" = %2").arg(it.key()).arg(it.value());
            // The port names come from the plugin, NOT from a guess. "In"/"Out" only exist on mono-in/mono-out
            // plugins; for anything else filter-chain cannot resolve the graph, drops it without a word and
            // passes the audio through with every control reading 0.0 (measured 2026-09-21 with sc3_1427 —
            // specs/fx9-ducking.md). Empty when the library is missing: then push()'s own defaults apply and
            // the chain is rejected upstream by validate() anyway.
            LadspaPorts ports = ladspaPorts(e.plugin, e.label);
            // Plugin order is not channel order: sc3_1427 lists "Sidechain" FIRST, so feeding the ports as they
            // come would route FL into the side-chain and lose the right channel. A side-chain input is not a
            // channel of this chain — it is fed separately (FX-9) — so it is dropped here. What remains keeps
            // the plugin's own left/right order.
            const auto istSidechain = [](const QString &n) { return n.compare(QLatin1String("sidechain"), Qt::CaseInsensitive) == 0; };
            ports.inputs.removeIf(istSidechain);
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = %1").arg(e.plugin),
                        e.label.isEmpty() ? QString() : QStringLiteral("label = %1").arg(e.label),
                        ctrl.isEmpty() ? QString() : QStringLiteral("control = { %1 }").arg(ctrl.join(QLatin1Char(' ')))},
                 ports.inputs.value(0), ports.outputs.value(0), ports.inputs, ports.outputs);
        }
    }
    if (nodes.isEmpty()) return {};     // nothing to build — caller keeps the plain sink

    QStringList nodeBlocks, linkBlocks;
    for (int i = 0; i < nodes.size(); ++i) {
        QStringList parts{QStringLiteral("name = %1").arg(nodes[i].node)};
        for (const auto &x : nodes[i].extras) if (!x.isEmpty()) parts << x;
        nodeBlocks << QStringLiteral("{ %1 }").arg(parts.join(QLatin1Char(' ')));
        if (i + 1 < nodes.size())
            linkBlocks << QStringLiteral("{ output = \"%1:%2\" input = \"%3:%4\" }")
                              .arg(nodes[i].node, nodes[i].outPort, nodes[i + 1].node, nodes[i + 1].inPort);
    }

    // capture side = the Audio/Sink streams target; media.name keeps the PLAIN channel/mix name so WirePlumber
    // stream-restore (volume + target per app) is untouched when effects turn on (ADR 0008 D3).
    // FX-6/FX-10: a MIX chain sits BEHIND the mix sink — it captures that sink's monitor like a cell loopback does
    // and leaves its playback side free for the output edges to read (Layout::mixExit). A CHANNEL chain sits IN FRONT
    // of the channel sink: it IS the sink apps target, and its playback side feeds the plain sink (ADR 0008 D3).
    const QString cap = behindSink
        // stream.capture.sink = true is what makes a capture stream read a SINK's monitor instead of looking for a
        // source — without it the stream gets no ports at all and stays suspended (measured 2026-09-19: the chain
        // head had zero ports while the tail was already running). Same flag every cell/output edge uses.
        ? QStringLiteral("capture.props = { node.name = %1 media.name = %2 node.target = %3 audio.position = [ FL FR ] "
                         "stream.capture.sink = true node.passive = true node.dont-fallback = true node.linger = true "
                         "node.dont-reconnect = true node.description = %4 }")
              .arg(QLatin1Char('"') + entryNode + QLatin1Char('"'), QLatin1Char('"') + mediaName + QLatin1Char('"'),
                   QLatin1Char('"') + targetSink + QLatin1Char('"'), QLatin1Char('"') + description + QLatin1Char('"'))
        : QStringLiteral("capture.props = { node.name = %1 media.name = %2 media.class = Audio/Sink audio.position = [ FL FR ] "
                         "node.description = %3 node.dont-fallback = true }")
              .arg(QLatin1Char('"') + entryNode + QLatin1Char('"'), QLatin1Char('"') + mediaName + QLatin1Char('"'),
                   QLatin1Char('"') + description + QLatin1Char('"'));
    const QString play = behindSink
        // NO node.passive on a mix chain's tail: the output edges capture FROM it and are passive themselves
        // (like every cell's capture side). Two passive ends never get linked — measured 2026-09-19: the tail
        // existed, the edge existed, and out.stream.in had zero inputs until this flag was dropped.
        ? QStringLiteral("playback.props = { node.name = %1 media.name = %2 audio.position = [ FL FR ] "
                         "node.linger = true node.dont-fallback = true }")
              .arg(QLatin1Char('"') + exitNode + QLatin1Char('"'), QLatin1Char('"') + mediaName + QLatin1Char('"'))
        : QStringLiteral("playback.props = { node.name = %1 media.name = %2 node.target = %3 "
                         "node.passive = true node.linger = true node.dont-fallback = true }")
              .arg(QLatin1Char('"') + exitNode + QLatin1Char('"'), QLatin1Char('"') + mediaName + QLatin1Char('"'),
                   QLatin1Char('"') + targetSink + QLatin1Char('"'));
    // inputs/outputs list EVERY audio port of the first/last node, qualified with the node name. A single
    // "node:In" only works for mono-in/mono-out plugins; with a stereo plugin filter-chain cannot resolve the
    // graph, discards it silently and passes audio through with all controls at 0.0 — which is exactly why
    // sc3_1427 compressed nothing at ratio 20:1 (measured 2026-09-21, specs/fx9-ducking.md).
    auto qualify = [](const QString &node, const QStringList &ports) {
        QStringList out;
        for (const QString &p : ports) out << QLatin1Char('"') + node + QLatin1Char(':') + p + QLatin1Char('"');
        return out.join(QLatin1Char(' '));
    };
    return QStringLiteral("{ node.description = %1 audio.channels = 2 audio.position = [ FL FR ] "
                          "filter.graph = { nodes = [ %2 ] links = [ %3 ] inputs = [ %4 ] outputs = [ %5 ] } %6 %7 }")
        .arg(QLatin1Char('"') + description + QLatin1Char('"'), nodeBlocks.join(QLatin1Char(' ')), linkBlocks.join(QLatin1Char(' ')),
             qualify(nodes.first().node, nodes.first().allIn), qualify(nodes.last().node, nodes.last().allOut), cap, play);
}

QString duckerNode(const QString &slug) { return slug.isEmpty() ? QString() : QStringLiteral("kmixdeck.duck.%1").arg(slug); }

QString renderDuckerArgs(const QString &slug, const QString &description, const QString &channelNode,
                         const QString &triggerNode, double depthDb, double attackMs, double releaseMs,
                         double thresholdDb) {
    if (slug.isEmpty() || channelNode.isEmpty() || triggerNode.isEmpty()) return {};
    // SC3 gives us threshold + ratio, not a "duck by N dB" knob. The depth the user asks for is reached by
    // the ratio: with the trigger driving the sidechain above `threshold`, a ratio of r reduces by
    // (threshold - inputLevel) * (1 - 1/r). Rather than pretend a formula is exact for unknown material,
    // map depth onto the ratio monotonically over SC3's real range (1..10, read from analyseplugin) and let
    // the reported gain reduction be the truth the UI shows. -12 dB (our default) lands at ratio 4.
    const double tiefe = std::clamp(-depthDb, 0.0, 60.0);      // 0 … 60 dB of wanted reduction
    const double ratio = std::clamp(1.0 + tiefe / 4.0, 1.0, 10.0);
    const QString name = QStringLiteral("duck_") + slug;
    // audio.channels = 3: FL/FR carry the ducked audio, AUX0 the trigger. filter-chain maps the graph's
    // inputs positionally onto the capture ports, so the third input IS the sidechain — SC3's port order
    // ("Sidechain", "Left input", "Right input") is NOT the port order we want, hence the explicit list.
    return QStringLiteral(
               "{ node.description = %1 audio.channels = 3 audio.position = [ FL FR AUX0 ] "
               "filter.graph = { nodes = [ { name = %2 type = ladspa plugin = sc3_1427 label = sc3 "
               "control = { \"Threshold level (dB)\" = %3 \"Ratio (1:n)\" = %4 \"Attack time (ms)\" = %5 "
               "\"Release time (ms)\" = %6 \"Chain balance\" = 1 } } ] links = [ ] "
               "inputs = [ \"%2:Left input\" \"%2:Right input\" \"%2:Sidechain\" ] "
               "outputs = [ \"%2:Left output\" \"%2:Right output\" null ] } "
               // The ducked audio is read from the channel sink's monitor (stream.capture.sink), exactly like a
               // cell loopback does. The trigger side is linked by the daemon, not by node.target: one capture
               // stream cannot target two different nodes.
               "capture.props = { node.name = %7 media.name = %8 node.target = %9 audio.position = [ FL FR AUX0 ] "
               "stream.capture.sink = true node.passive = true node.dont-fallback = true node.linger = true "
               "node.dont-reconnect = true node.description = %1 } "
               "playback.props = { node.name = %10 media.name = %8 audio.position = [ FL FR AUX0 ] "
               "node.linger = true node.dont-fallback = true } }")
        .arg(QLatin1Char('"') + description + QLatin1Char('"'), name)
        .arg(std::clamp(thresholdDb, -30.0, 0.0)).arg(ratio)
        .arg(std::clamp(attackMs, 2.0, 400.0)).arg(std::clamp(releaseMs, 2.0, 800.0))
        .arg(QLatin1Char('"') + duckerNode(slug) + QLatin1Char('"'),
             QLatin1Char('"') + description + QLatin1Char('"'),
             QLatin1Char('"') + channelNode + QLatin1Char('"'),
             QLatin1Char('"') + duckerNode(slug) + QStringLiteral(".out\""));
}

QVector<QPair<QString, double>> controlValues(const Chain &c, const QString &idPrefix) {
    QVector<QPair<QString, double>> out;
    QHash<QString, int> counts;
    for (const auto &e : c.effects) {
        if (!e.enabled) continue;
        QString base = idPrefix + nodePrefix(e);
        const int idx = ++counts[base];
        const QString name = idx == 1 ? base : base + QString::number(idx);
        auto emit = [&](const QString &node, const QString &key, const QString &ctl, double def) {
            out.append({node + QLatin1Char(':') + ctl, param(e, key, def)});
        };
        if (e.type == QLatin1String("highpass")) emit(name, "freq", "Freq", 100.0);
        else if (e.type == QLatin1String("eq")) { emit(name + "_low", "lowGain", "Gain", 0.0); emit(name + "_mid", "midGain", "Gain", 0.0); emit(name + "_high", "highGain", "Gain", 0.0); }
        else if (e.type == QLatin1String("gate")) { emit(name, "threshold", "Threshold (dB)", -40.0); emit(name, "attack", "Attack (ms)", 5.0); emit(name, "hold", "Hold (ms)", 50.0); emit(name, "decay", "Decay (ms)", 100.0); emit(name, "range", "Range (dB)", -90.0); }
        else if (e.type == QLatin1String("compressor")) { emit(name, "threshold", "Threshold level (dB)", -20.0); emit(name, "ratio", "Ratio (1:n)", 3.0); emit(name, "attack", "Attack time (ms)", 10.0); emit(name, "release", "Release time (ms)", 100.0); emit(name, "makeup", "Makeup gain (dB)", 0.0); }
        else if (e.type == QLatin1String("limiter")) { emit(name, "limit", "Limit (dB)", -3.0); emit(name, "release", "Release time (s)", 0.5); }
        else if (e.type == QLatin1String("brickwall")) { emit(name, "ceiling", "Limit (dB)", -1.0); emit(name, "gain", "Input gain (dB)", 0.0); emit(name, "release", "Release time (s)", 0.5); }
        else if (e.type == QLatin1String("noise")) emit(name, "vad", "VAD Threshold (%)", 50.0);
        else for (auto it = e.params.constBegin(); it != e.params.constEnd(); ++it) out.append({name + QLatin1Char(':') + it.key(), it.value()});
    }
    return out;
}

} // namespace kmixdeck::fx
