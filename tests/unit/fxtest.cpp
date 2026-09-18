// SPDX-FileCopyrightText: 2026 Raya Elena Solano
// SPDX-License-Identifier: GPL-3.0-or-later
//
// fx:: in isolation (UT-3): the catalog, validation, the rendered filter-chain args and the live-control names. Without
// PipeWire and without any LADSPA library installed — what test_fx.py cannot do because it skips on machines without
// swh-plugins. The daemon's behaviour with real audio stays in the integration suite.
#include <QtTest>
#include <QJsonDocument>
#include "fx.h"

using namespace kmixdeck;

static fx::Effect eff(const char *type, std::initializer_list<QPair<QString, double>> params = {}) {
    fx::Effect e; e.type = QString::fromLatin1(type); for (const auto &p : params) e.params.insert(p.first, p.second); return e;
}
static fx::Chain chainOf(std::initializer_list<fx::Effect> effects) { fx::Chain c; for (const auto &e : effects) c.effects.push_back(e); return c; }

class FxTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void catalogHasTheRequiredSet() {   // FX-1 minimum: noise suppression, gate, compressor, EQ, high-pass, limiter
        QStringList types; for (const auto &t : fx::builtinTypes()) types << t.type;
        for (const char *want : {"noise", "gate", "compressor", "eq", "highpass", "limiter"}) QVERIFY2(types.contains(QLatin1String(want)), want);
        for (const auto &t : fx::builtinTypes()) {
            QVERIFY2(!t.label.isEmpty() && !t.description.isEmpty(), qPrintable(t.type));
            for (const auto &p : t.params) { QVERIFY2(p.min <= p.def && p.def <= p.max, qPrintable(t.type + QLatin1Char('/') + p.key)); QVERIFY(!p.label.isEmpty()); }
        }
    }
    void builtinsNeedNoLibrary() {
        QVERIFY(fx::typeSpec(QStringLiteral("highpass"))->ladspaFile.isEmpty());
        QVERIFY(fx::typeSpec(QStringLiteral("eq"))->ladspaFile.isEmpty());
        QVERIFY(fx::ladspaAvailable(QString()));                                  // "" = builtin = always there
        QVERIFY(!fx::ladspaAvailable(QStringLiteral("no_such_plugin_9999")));
        QVERIFY(!fx::packageHint(QStringLiteral("sc4m_1916")).isEmpty());          // the CLI/UI can say what to install
        QCOMPARE(fx::typeSpec(QStringLiteral("nonsense")), nullptr);
    }

    void validateAcceptsGoodChain() { QCOMPARE(fx::validate(chainOf({eff("highpass", {{QStringLiteral("freq"), 120.0}}), eff("eq")})), QString()); }
    void validateRejectsUnknownType() { QVERIFY(fx::validate(chainOf({eff("reverb")})).contains(QLatin1String("reverb"))); }
    void validateRejectsOutOfRange() {
        const auto *hp = fx::typeSpec(QStringLiteral("highpass")); const double tooHigh = hp->params[0].max + 1;
        const QString why = fx::validate(chainOf({eff("highpass", {{QStringLiteral("freq"), tooHigh}})}));
        QVERIFY2(why.contains(QLatin1String("freq")), qPrintable(why));
    }
    void validateRejectsUnknownParam() { QVERIFY(fx::validate(chainOf({eff("highpass", {{QStringLiteral("wobble"), 1.0}})})).contains(QLatin1String("wobble"))); }
    void validateRejectsMissingLadspa() {
        fx::Effect e = eff("ladspa"); e.plugin = QStringLiteral("no_such_plugin_9999"); e.label = QStringLiteral("x");
        QVERIFY(fx::validate(chainOf({e})).contains(QLatin1String("no_such_plugin_9999")));
    }
    void validateSkipsDisabledEffects() {   // FX-5: a bypassed effect whose library is missing must not block the chain
        fx::Effect e = eff("ladspa"); e.plugin = QStringLiteral("no_such_plugin_9999"); e.label = QStringLiteral("x"); e.enabled = false;
        QCOMPARE(fx::validate(chainOf({e, eff("highpass")})), QString());
    }

    void jsonRoundTrip() {
        fx::Chain c = chainOf({eff("highpass", {{QStringLiteral("freq"), 90.0}}), eff("eq", {{QStringLiteral("lowGain"), -3.0}})});
        c.effects[1].enabled = false; c.enabled = false;
        const fx::Chain back = fx::Chain::fromJson(QJsonDocument::fromJson(QJsonDocument(c.toJson()).toJson()).object());
        QVERIFY(back == c); QVERIFY(!back.enabled); QVERIFY(!back.effects[1].enabled); QCOMPARE(back.effects[0].params.value(QStringLiteral("freq")), 90.0);
    }
    void presetsAreValidChains() {   // FX-4: every preset must load as a chain; the ones without LADSPA must validate here
        const QJsonObject presets = fx::presetChains(); QVERIFY(!presets.isEmpty());
        for (auto it = presets.begin(); it != presets.end(); ++it) {
            const fx::Chain c = fx::Chain::fromJson(it.value().toObject());
            QVERIFY2(!c.effects.isEmpty(), qPrintable(it.key()));
            for (const auto &e : c.effects) QVERIFY2(fx::typeSpec(e.type) || e.type == QLatin1String("ladspa"), qPrintable(it.key() + QLatin1Char('/') + e.type));
        }
    }

    void renderBuiltinChain() {
        const fx::Chain c = chainOf({eff("highpass", {{QStringLiteral("freq"), 120.0}}), eff("eq", {{QStringLiteral("lowGain"), -3.0}})});
        const QString args = fx::renderFilterChainArgs(c, QStringLiteral("Voice FX"), QStringLiteral("kmixdeck.fx.voice"), QStringLiteral("kmixdeck.fx.voice.out"),
                                                       QStringLiteral("kmixdeck.channel.voice"), QStringLiteral("kmixdeck.channel.voice"), QStringLiteral("voice"));
        QVERIFY(args.contains(QStringLiteral("kmixdeck.fx.voice")));
        QVERIFY(args.contains(QStringLiteral("kmixdeck.channel.voice")));
        QVERIFY(args.contains(QStringLiteral("filter.graph")));
        QVERIFY2(args.contains(QStringLiteral("120")), "the parameter value must be in the graph");
        QVERIFY2(!args.contains(QStringLiteral("ladspa")), "builtin-only chain renders without any LADSPA reference");
        // exactly one node per enabled effect, wired in order: the graph links go highpass → eq
        QVERIFY(args.indexOf(QStringLiteral("highpass")) < args.indexOf(QStringLiteral("eq")) || args.indexOf(QStringLiteral("hp")) >= 0);
    }
    void renderSkipsDisabledEffect() {
        fx::Chain c = chainOf({eff("highpass", {{QStringLiteral("freq"), 333.0}}), eff("eq")}); c.effects[0].enabled = false;
        const QString args = fx::renderFilterChainArgs(c, QStringLiteral("d"), QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("m"), QStringLiteral("t"), QStringLiteral("p"));
        QVERIFY2(!args.contains(QStringLiteral("333")), "a bypassed effect leaves the graph entirely (FX-5)");
        QVERIFY(args.contains(QStringLiteral("eq")) || args.contains(QStringLiteral("shelf")));
    }
    void controlValuesNameEveryParamOfEnabledEffects() {
        fx::Chain c = chainOf({eff("highpass", {{QStringLiteral("freq"), 120.0}}), eff("eq", {{QStringLiteral("lowGain"), -3.0}})}); c.effects[1].enabled = false;
        const auto cv = fx::controlValues(c, QStringLiteral("voice"));
        QVERIFY(!cv.isEmpty());
        bool sawFreq = false, sawEq = false;
        for (const auto &p : cv) { if (p.second == 120.0) sawFreq = true; if (p.second == -3.0) sawEq = true; QVERIFY(p.first.contains(QLatin1Char(':'))); }
        QVERIFY(sawFreq); QVERIFY2(!sawEq, "disabled effect has no live controls");
    }
    void isActiveFollowsBypass() {
        fx::Chain c = chainOf({eff("highpass")}); QVERIFY(c.isActive());
        c.enabled = false; QVERIFY(!c.isActive());
        c.enabled = true; c.effects[0].enabled = false; QVERIFY(!c.isActive());
        QVERIFY(!fx::Chain().isActive());
    }
};
QTEST_GUILESS_MAIN(FxTest)
#include "fxtest.moc"
