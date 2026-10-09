// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 kmixdeck contributors
#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

namespace kmixdeck::pw {

class Graph;

/// CT-10: plays client-supplied audio streams into a channel, INSIDE the daemon.
///
/// Separate from the CT-8 Sampler on purpose. A sample is a registered file, decoded fully into memory and allowed
/// to overlap itself; a track here is a file descriptor handed over by a client (a TTS clip, a song, a pipe from
/// another program), decoded while it plays, and strictly sequential per channel:
///
///  * one FIFO per channel, at most ONE track sounds, the next starts when the current one ended;
///  * at most kQueueLimit tracks per channel (sounding + waiting), beyond that enqueue() refuses;
///  * the input is a file descriptor the Player dups and owns. A worker thread demuxes it with libavformat through
///    a custom AVIOContext whose read callback poll()s (100 ms) and checks an atomic stop flag, so a stop works
///    while a pipe stalls. Pipes (non-seekable) and regular files / memfds (seekable) both work;
///  * nested opens are impossible: the demuxer's io_open is replaced by one that refuses everything and only plain
///    audio container formats are allowed (no hls/dash/concat playlists that would fetch URLs);
///  * the worker converts to interleaved stereo float at the rate the stream NEGOTIATED (no rate in EnumFormat,
///    see the Sampler's DV-4 note) and fills an SPA ring buffer of ~2.7 s; the RT callback only copies from it
///    and pads silence on underrun. Memory is bounded no matter how long the music runs;
///  * at EOF the stream is drained (pw_stream_flush(drain=true)) and the track ends on the `drained` event, so the
///    tail is never cut;
///  * every track id ends exactly once with "played", "stopped" or "error: <reason>".
///
/// The Player knows nothing about the layout. Mixer tells it where a channel is entered (targetResolver) and moves
/// a sounding stream when the channel's FX chain changes (streamNodeId + Graph::moveStream).
class Player : public QObject {
    Q_OBJECT
public:
    static constexpr int kQueueLimit = 8;

    explicit Player(Graph *graph, QObject *parent = nullptr);
    ~Player() override;

    /// Where audio enters a channel right now (Mixer::fxTarget). Asked when a track STARTS, not when it is queued.
    void setTargetResolver(std::function<QString(const QString &channel)> f) { m_target = std::move(f); }

    /// Queue `fd` on `channel`. Takes ownership of `fd` in every case (closed on refusal). Returns the track id, or 0
    /// with *error set (queue full, not connected).
    quint32 enqueue(const QString &channel, int fd, const QString &title, QString *error);
    /// Stop one track of `channel` (sounding or waiting) with result "stopped"; id 0 = the sounding one AND every
    /// waiting one. Returns how many tracks ended.
    int stop(const QString &channel, quint32 id);
    /// Stop everything on every channel (shutdown).
    void stopAll(const QString &result = QStringLiteral("stopped"));

    QString nowPlaying(const QString &channel) const;   // title of the sounding track, "" when idle
    QStringList queue(const QString &channel) const;    // titles waiting, in order
    int count(const QString &channel) const;            // sounding + waiting
    bool hasTrack(const QString &channel, quint32 id) const;
    /// PipeWire node id of the sounding stream of `channel`, 0 when none (or not registered yet).
    uint32_t streamNodeId(const QString &channel) const;
    /// Move the sounding stream of `channel` onto `targetNode` (Graph::moveStream: target.object metadata). False
    /// while there is nothing to move yet (stream not registered) or the target is unknown — the caller retries.
    /// The metadata entry is cleared again when the track ends, so a later node with the same id inherits nothing.
    bool moveTo(const QString &channel, const QString &targetNode);
    QStringList channels() const;                       // channels with at least one track

Q_SIGNALS:
    /// Exactly once per track id.
    void ended(const QString &channel, quint32 id, const QString &result);
    /// NowPlaying or the queue of `channel` changed.
    void changed(const QString &channel);
    /// A new stream started on `channel` (Mixer may need to move it onto a chain that appeared meanwhile).
    void started(const QString &channel, quint32 id);

public:
    struct Track;   // public for the C callbacks
private:
    void startNext(const QString &channel);
    void finish(Track *t, const QString &result, bool startFollowing);
    void reap();
    void destroyStream(Track *t);
    Graph *m_graph = nullptr;
    std::function<QString(const QString &)> m_target;
    QHash<QString, QList<Track *>> m_queues;   // channel → [sounding, waiting…]; front is sounding when started
    quint32 m_nextId = 1;
    QTimer m_reaper;
};

} // namespace kmixdeck::pw
