// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace kmixdeck::pw {

class Graph;

/// CT-8: plays registered samples into a soundboard channel's sink, INSIDE the daemon.
///
/// One `pw_stream` per sounding voice (PW_DIRECTION_OUTPUT), the mirror image of the capture streams in
/// meters.cpp. The requirement says "playback runs inside the daemon (pw_stream), not in a spawned process",
/// and that is not decoration: a spawned `pw-play` shows up as its own graph node, every instance is called
/// `pw-play`, and linking those by NAME is exactly the bug that made test_ports.py flaky for months (see
/// CONTRIBUTING.md, "Check the return code of every command you fire at the graph"). A stream we create has
/// an id we know.
///
/// Because the stream targets the channel's sink, the sample passes through the channel's FX chain and every
/// per-mix fader like any other source — which is what CT-8 asks for ("mic only" mixes can exclude it).
class Sampler : public QObject {
    Q_OBJECT
public:
    explicit Sampler(Graph *graph, QObject *parent = nullptr);
    ~Sampler() override;

    /// Decode `path` and report what it is. length in seconds, 0 and a non-empty error on failure.
    /// Reads the header only — registering a sample must not pull a 40 MB file through the decoder.
    struct Probe { double length = 0.0; int rate = 0; int channels = 0; QString error; };
    static Probe probe(const QString &path);

    /// Start a voice: decodes `path` fully into memory, resamples to `rate`, connects a stream to `targetNode`.
    /// Returns a voice id, or 0 with `error` set. `gain` is linear and applied while mixing (CT-8 per-sample gain).
    quint32 play(const QString &name, const QString &path, const QString &targetNode, float gain, QString *error);
    /// Stop one voice, or every voice of `name` when id is 0. Returns how many voices were stopped.
    int stop(const QString &name, quint32 id = 0);
    /// Stop everything (used when the soundboard channel goes away).
    void stopAll();
    /// Names of the samples currently sounding, one entry per voice.
    QStringList sounding() const;

Q_SIGNALS:
    /// A voice reached the end of its data and tore itself down. Not emitted for an explicit stop().
    void finished(const QString &name, quint32 id);

private:
    struct Impl;
    Impl *d;
};

} // namespace kmixdeck::pw
