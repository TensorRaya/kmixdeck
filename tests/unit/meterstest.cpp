// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
//
// UX-18: the R128 analyser lives as long as the daemon, so its cost must not grow with the audio it has seen.
// These tests build an analyser with exactly the mode flags the daemon uses (pipewire/meters.cpp) and feed it
// minutes of tone in a fraction of a second; no PipeWire involved.
#include "pipewire/meters.h"
#include <QElapsedTimer>
#include <QTest>
#include <cmath>
#include <ebur128.h>
#include <limits>
#include <vector>

using namespace kmixdeck::pw;

namespace {
// The gating blocks libebur128 keeps are 100 ms each, independent of the sample rate. A low rate makes a minute
// of audio cheap to feed; the per-block bookkeeping under test is the same as at 48 kHz.
constexpr unsigned kRate = 8000;

struct Tone {
    std::vector<float> second;   // one second of interleaved stereo, 1 kHz at amplitude 0.1 (≈ -20 LUFS)
    Tone() : second(2 * kRate) {
        for (unsigned i = 0; i < kRate; ++i) {
            const float v = 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kRate);
            second[2 * i] = v; second[2 * i + 1] = v;
        }
    }
    void feed(ebur128_state *st, int seconds) const {
        for (int s = 0; s < seconds; ++s) ebur128_add_frames_float(st, second.data(), kRate);
    }
};

// Cheapest observed time of one integrated reading. The minimum over many batches is what the code costs;
// everything above it is the scheduler, which is why a loaded host cannot make this test flaky in either
// direction.
double readingNs(ebur128_state *st) {
    double best = std::numeric_limits<double>::max();
    for (int batch = 0; batch < 50; ++batch) {
        QElapsedTimer t; t.start();
        double i = 0;
        for (int r = 0; r < 20; ++r) ebur128_loudness_global(st, &i);
        best = std::min(best, double(t.nsecsElapsed()) / 20.0);
    }
    return best;
}
} // namespace

class MetersTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void readingCostDoesNotGrowWithHistory() {
        const Tone tone;
        ebur128_state *st = ebur128_init(2, kRate, loudnessAnalyserMode());
        QVERIFY(st);
        tone.feed(st, 60);
        const double early = readingNs(st);
        tone.feed(st, 540);
        const double late = readingNs(st);
        ebur128_destroy(&st);
        // Without EBUR128_MODE_HISTOGRAM the reading walks every block since the start: 10 minutes of history
        // cost about 9x what 1 minute did (measured 2026-10-06). With it the cost is flat.
        QVERIFY2(late < 3.0 * early,
                 qPrintable(QStringLiteral("integrated reading: %1 ns after 1 min, %2 ns after 10 min")
                                .arg(early, 0, 'f', 0).arg(late, 0, 'f', 0)));
    }

    void integratedLoudnessStaysWithinEbuTolerance() {
        // The histogram trades exactness for bounded cost. EBU Tech 3341 allows ±0.1 LU on the integrated
        // value; the reference is the same library in its exact (block-list) mode.
        const Tone tone;
        ebur128_state *ours = ebur128_init(2, kRate, loudnessAnalyserMode());
        ebur128_state *exact = ebur128_init(2, kRate, EBUR128_MODE_I);
        QVERIFY(ours && exact);
        tone.feed(ours, 20); tone.feed(exact, 20);
        double iOurs = 0, iExact = 0;
        QCOMPARE(ebur128_loudness_global(ours, &iOurs), int(EBUR128_SUCCESS));
        QCOMPARE(ebur128_loudness_global(exact, &iExact), int(EBUR128_SUCCESS));
        ebur128_destroy(&ours); ebur128_destroy(&exact);
        QVERIFY2(std::abs(iOurs - iExact) <= 0.1,
                 qPrintable(QStringLiteral("integrated %1 LUFS vs exact %2 LUFS").arg(iOurs, 0, 'f', 3).arg(iExact, 0, 'f', 3)));
        QVERIFY2(std::abs(iExact + 20.0) < 0.5, qPrintable(QStringLiteral("reference tone reads %1 LUFS").arg(iExact, 0, 'f', 3)));
    }
};

QTEST_GUILESS_MAIN(MetersTest)
#include "meterstest.moc"
