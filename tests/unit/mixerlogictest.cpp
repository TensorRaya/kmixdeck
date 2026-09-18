// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the pure logic in Mixer (no PipeWire needed). Pattern: QTest via ecm_add_test, like plasma-pa.
#include <QTest>
#include "mixer.h"

using namespace kmixdeck;

class MixerLogicTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void slugify_data() {
        QTest::addColumn<QString>("in"); QTest::addColumn<QString>("out");
        QTest::newRow("simple")     << "Game" << "game";
        QTest::newRow("spaces")     << "Voice Chat" << "voice_chat";
        QTest::newRow("unicode")    << "Musik – Ünïcode" << "musik_unicode";
        QTest::newRow("symbols")    << "OBS (Stream) #2!" << "obs_stream_2";
        QTest::newRow("trim")       << "  --Mix--  " << "mix";
        QTest::newRow("empty")      << "!!!" << "";
        QTest::newRow("dashes")     << "---" << "";
        QTest::newRow("dbus-safe")  << "Mix #1 – Ünd_so" << "mix_1_und_so";
    }
    void slugify() {
        QFETCH(QString, in); QFETCH(QString, out);
        QCOMPARE(Names::slugify(in), out);
    }

    void names() {
        QCOMPARE(Names::channelNode(QStringLiteral("game")), QStringLiteral("kmixdeck.channel.game"));
        QCOMPARE(Names::mixNode(QStringLiteral("stream")), QStringLiteral("kmixdeck.mix.stream"));
        QCOMPARE(Names::cellNode(QStringLiteral("game"), QStringLiteral("stream")), QStringLiteral("kmixdeck.link.game.stream"));
    }

    // UX-7: the UI fader is cubic like Plasma/PulseAudio; PipeWire wants linear. Round-trips and known points.
    void volumeCurve() {
        QCOMPARE(Mixer::cubicToLinear(1.0), 1.0f);
        QCOMPARE(Mixer::cubicToLinear(0.0), 0.0f);
        QVERIFY(qFuzzyCompare(Mixer::cubicToLinear(0.5), 0.125f));            // 0.5 cubic = -18.06 dB
        QVERIFY(qFuzzyCompare(float(Mixer::linearToCubic(0.25f)), 0.62996054f)); // 0.25 linear = -12.04 dB
        for (double c : {0.01, 0.1, 0.33, 0.5, 0.75, 0.99})
            QVERIFY2(qAbs(Mixer::linearToCubic(Mixer::cubicToLinear(c)) - c) < 1e-6, qPrintable(QString::number(c)));
    }
};

QTEST_GUILESS_MAIN(MixerLogicTest)
#include "mixerlogictest.moc"
