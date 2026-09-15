// SPDX-License-Identifier: GPL-3.0-or-later
#include "fx.h"

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
        {QStringLiteral("noise"), QStringLiteral("Noise suppression"), QStringLiteral("RNNoise — ML denoiser for the microphone"),
         {{QStringLiteral("vad"), QStringLiteral("Voice threshold"), QStringLiteral("%"), 0.0, 100.0, 50.0}}, QStringLiteral("librnnoise_ladspa")},
    };
    return types;
}
const TypeSpec *typeSpec(const QString &type) {
    for (const auto &t : builtinTypes()) if (t.type == type) return &t;
    return nullptr;
}

bool ladspaAvailable(const QString &file) {
    if (file.isEmpty()) return true;
    QStringList dirs;
    for (const QByteArray &p : qgetenv("LADSPA_PATH").split(':')) if (!p.isEmpty()) dirs << QString::fromUtf8(p);
    dirs << QStringLiteral("/usr/lib/ladspa") << QStringLiteral("/usr/lib64/ladspa")
         << QStringLiteral("/usr/lib/x86_64-linux-gnu/ladspa") << QStringLiteral("/usr/local/lib/ladspa");
    for (const QString &d : dirs)
        if (QDir(d).entryList({file + QStringLiteral(".so")}, QDir::Files).size() > 0) return true;
    return false;
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
    // broadcast: tighter, no denoiser (hardware gate does it), stronger compression
    QJsonObject bcast = chain({QByteArrayLiteral("gate").constData(), QByteArrayLiteral("compressor").constData(), QByteArrayLiteral("eq").constData(), QByteArrayLiteral("limiter").constData()});
    return {{QStringLiteral("Voice — clean"), voice}, {QStringLiteral("Voice — broadcast"), bcast}};
}
QString validate(const Chain &c) {
    for (const auto &e : c.effects) {
        if (e.type == QLatin1String("ladspa")) {
            if (e.plugin.isEmpty()) return QStringLiteral("ladspa effect needs a plugin name");
            if (!ladspaAvailable(e.plugin))
                return QStringLiteral("LADSPA '%1' not installed — %2").arg(e.plugin, packageHint(e.plugin));
            continue;   // arbitrary controls: anything the plugin exposes is fine
        }
        const TypeSpec *spec = typeSpec(e.type);
        if (!spec) return QStringLiteral("unknown effect type '%1'").arg(e.type);
        if (!ladspaAvailable(spec->ladspaFile))
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
    return e.type;
}
double param(const Effect &e, const QString &key, double def) {
    auto it = e.params.constFind(key); return it == e.params.constEnd() ? def : it.value();
}
} // namespace

QString renderFilterChainArgs(const Chain &c, const QString &description, const QString &entryNode,
                              const QString &exitNode, const QString &mediaName, const QString &targetSink,
                              const QString &idPrefix) {
    struct Rendered { QString node, inPort, outPort; QStringList extras; };   // one graph node per entry
    QVector<Rendered> nodes;
    QHash<QString, QString> uniq;                            // prefix → unique name with count
    auto push = [&](const QString &name, QStringList extras, QString in, QString out) {
        if (in.isEmpty()) in = QStringLiteral("In");          // builtin + mono ladspa use In/Out
        if (out.isEmpty()) out = QStringLiteral("Out");
        nodes.push_back({name, in, out, extras});
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
        else if (e.type == QLatin1String("noise"))
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = librnnoise_ladspa"), QStringLiteral("label = noise_suppressor_stereo"),
                        QStringLiteral("control = { \"VAD Threshold (%%)\" = %1 }").arg(param(e, "vad", 50.0))}, QStringLiteral("Input"), QStringLiteral("Output"));
        else {   // ladspa: plugin[:label] + arbitrary controls
            QStringList ctrl;
            for (auto it = e.params.constBegin(); it != e.params.constEnd(); ++it)
                ctrl << QStringLiteral("\"%1\" = %2").arg(it.key()).arg(it.value());
            push(name, {QStringLiteral("type = ladspa"), QStringLiteral("plugin = %1").arg(e.plugin),
                        e.label.isEmpty() ? QString() : QStringLiteral("label = %1").arg(e.label),
                        ctrl.isEmpty() ? QString() : QStringLiteral("control = { %1 }").arg(ctrl.join(QLatin1Char(' ')))}, {}, {});
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
    QString args = QStringLiteral("{ node.description = %1 audio.channels = 2 audio.position = [ FL FR ] "
                                  "filter.graph = { nodes = [ %2 ] links = [ %3 ] inputs = [ \"%4:%5\" ] outputs = [ \"%6:%7\" ] } "
                                  "capture.props = { node.name = %8 media.name = %9 media.class = Audio/Sink audio.position = [ FL FR ] node.description = %1 node.dont-fallback = true } "
                                  "playback.props = { node.name = %10 media.name = %9 node.target = %11 node.passive = true node.linger = true node.dont-fallback = true } }")
                       .arg(QLatin1Char('"') + description + QLatin1Char('"'), nodeBlocks.join(QLatin1Char(' ')), linkBlocks.join(QLatin1Char(' ')),
                            nodes.first().node, nodes.first().inPort, nodes.last().node, nodes.last().outPort,
                            QLatin1Char('"') + entryNode + QLatin1Char('"'), QLatin1Char('"') + mediaName + QLatin1Char('"'),
                            QLatin1Char('"') + exitNode + QLatin1Char('"'), QLatin1Char('"') + targetSink + QLatin1Char('"'));
    return args;
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
        else if (e.type == QLatin1String("noise")) emit(name, "vad", "VAD Threshold (%)", 50.0);
        else for (auto it = e.params.constBegin(); it != e.params.constEnd(); ++it) out.append({name + QLatin1Char(':') + it.key(), it.value()});
    }
    return out;
}

} // namespace kmixdeck::fx
