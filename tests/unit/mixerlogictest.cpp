// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the pure logic in Mixer (no PipeWire needed). Pattern: QTest via ecm_add_test, like plasma-pa.
#include <QTest>
#include "mixer.h"
#include "pipewire/filterchain.h"
#include <QTemporaryDir>
#include <QFile>

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
        QCOMPARE(ADR13::chainNode(QStringLiteral("game")), QStringLiteral("kmixdeck.cells.game"));        // ADR 0013
        QCOMPARE(ADR13::chainNode(QStringLiteral("game"), 1), QStringLiteral("kmixdeck.cells.game@1"));   // MX-1: >32 mixes
        QCOMPARE(ADR13::tapNode(QStringLiteral("stream")), QStringLiteral("kmixdeck.tap.stream"));
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

    // DV-34 / ADR 0015: which filter-chain module the chains use. The bug range comes from checking all release tags.
    void filterChainBugRange_data() {
        QTest::addColumn<QString>("version"); QTest::addColumn<bool>("bug");
        QTest::newRow("1.0.5") << "1.0.5" << false;   QTest::newRow("1.4.2") << "1.4.2" << false;
        QTest::newRow("1.5.85") << "1.5.85" << false; QTest::newRow("1.6.0") << "1.6.0" << true;
        QTest::newRow("1.6.1") << "1.6.1" << true;    QTest::newRow("1.6.2") << "1.6.2" << true;
        QTest::newRow("1.6.3") << "1.6.3" << false;   QTest::newRow("1.6.9") << "1.6.9" << false;
        QTest::newRow("1.7.0") << "1.7.0" << false;
    }
    void filterChainBugRange() {
        QFETCH(QString, version); QFETCH(bool, bug);
        QCOMPARE(pw::hasFilterChainBug5202(version), bug);
    }
    void filterChainChoice() {
        QTemporaryDir empty, withModule;
        QFile so(withModule.filePath(QLatin1String(pw::kFixedFilterChain) + QLatin1String(".so")));
        QVERIFY(so.open(QIODevice::WriteOnly)); so.close();
        const QStringList both{empty.path(), withModule.path()};
        // affected version + module in one of the dirs → ours; not installed → PipeWire's own (the daemon warns)
        QCOMPARE(QLatin1String(pw::chooseFilterChainModule(QStringLiteral("1.6.2"), both)), QLatin1String(pw::kFixedFilterChain));
        QCOMPARE(QLatin1String(pw::chooseFilterChainModule(QStringLiteral("1.6.2"), {empty.path()})), QLatin1String(pw::kStockFilterChain));
        // fixed upstream: PipeWire's own, even when ours is still installed
        QCOMPARE(QLatin1String(pw::chooseFilterChainModule(QStringLiteral("1.6.3"), both)), QLatin1String(pw::kStockFilterChain));
        QCOMPARE(QLatin1String(pw::chooseFilterChainModule(QStringLiteral("1.4.2"), both)), QLatin1String(pw::kStockFilterChain));
    }
    void pipewireModuleDirsFollowTheEnvironment() {
        qputenv("PIPEWIRE_MODULE_DIR", "/a:/b::/c");
        QCOMPARE(pw::pipewireModuleDirs(), QStringList({QStringLiteral("/a"), QStringLiteral("/b"), QStringLiteral("/c")}));
        qunsetenv("PIPEWIRE_MODULE_DIR");
        QCOMPARE(pw::pipewireModuleDirs().size(), 1);   // PipeWire's compiled-in module dir
    }
};

QTEST_GUILESS_MAIN(MixerLogicTest)
#include "mixerlogictest.moc"
