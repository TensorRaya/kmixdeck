# ADR 0016 — Playback into a channel

**Status:** accepted 2026-10-09 · **Owner:** project owner · **Drives:** CT-10 · **Builds on:** ADR 0010 (one backend,
N frontends), ADR 0011 (web bridge, the daemon never listens on the network)

## Context

A streaming setup plays short clips (text to speech rendered on another machine) into one kmixdeck channel, today
through a separate process that pipes raw PCM into `pacat`. That process has its own failure modes, its own
routing, and no way to tell the client whether and when the clip was heard. The same need exists for music: tracks
or a stream from another program, played one after the other into a `music` channel.

The owner decided to build this as a kmixdeck feature: a client hands kmixdeck an audio stream, kmixdeck plays it
into a channel, through that channel's effects and cells like any other source, and reports the end.

## Options

| Option | Verdict |
|---|---|
| PipeWire's ready modules: `libpipewire-module-protocol-simple`, RTP / ROC, the pipewire-pulse native TCP protocol | Rejected. None has authentication or a per-channel scope, none reports "played" or "stopped" for a clip. pulse TCP would also let any network client **record** every input on the machine (the microphone). |
| Play by path (`Play(s path)`) | Rejected. Every clip would need a file on the host (disk writes per clip, the host's wear rule), and the daemon would open paths a client names. |
| The daemon fetches a URL | Rejected. The daemon grows network access; ADR 0011 forbids a network listener in the daemon and the same reasoning covers outgoing fetches. |
| **The client hands over a file descriptor; the bridge is the network entry** | **Chosen.** |

## Decision

1. **Daemon: `kmixdeck::pw::Player`** (`src/pipewire/player.{h,cpp}`), separate from the CT-8 Sampler.
   - Input is a unix fd (`Channel.Play(h audio, s title) → u id`). The daemon dups it and owns it. A file, a memfd
     and a pipe all work; a pipe streams (music from another program).
   - One FIFO per channel, at most ONE track sounds, the next starts when the current one ended. At most 8 tracks
     per channel (sounding + waiting); beyond that Play is refused.
   - Decoding runs in a worker thread per track with libavformat through a custom `AVIOContext`. Its read callback
     `poll()`s with a 100 ms timeout and checks an atomic stop flag, so a stop works while a pipe stalls.
   - Conversion with swresample to interleaved stereo float at the rate the stream **negotiated** (no rate in
     EnumFormat, read in `param_changed`, same rule as the Sampler, DV-4). The worker fills an SPA ring buffer of
     ~2.7 s; the realtime process callback only copies from it and pads silence on underrun. Memory stays bounded
     for music of any length.
   - At EOF the stream is drained (`pw_stream_flush(drain=true)`) and the track ends on `drained`: the tail is
     never cut.
   - Every track id ends exactly once, signal `PlaybackEnded(u id, s result)`: `played`, `stopped` or
     `error: <reason>`. A broken input ends with an error and the queue goes on.
   - Stream properties: `node.name = kmixdeck.playback.<slug>`, `media.role = Production`,
     `target.object = Mixer::fxTarget(slug)` (the FX entry when a chain is active), `node.dont-fallback = true`
     (never the default sink). NOT `node.dont-reconnect`: when the channel's FX chain changes during a track, Mixer
     **moves** the stream onto the new entry (`Graph::moveStream`) and playback continues through the new chain
     without a restart. `node.linger = true` keeps WirePlumber from destroying the stream in the moment the old
     entry is gone; `state.restore-target = false` keeps it from remembering this stream's target for every other
     "Production" stream.
2. **Opt-in** (requirements opt-in rule): `Channel.Playback` (b, rw), layout field `playback`, off by default,
   written only when on, carried by export/import and undo. Play is refused while it is off, with the command that
   turns it on. Off, or removing the channel, stops everything on that channel.
3. **Network entry only through the web bridge** (`kmixdeck-web --play HOST:PORT`), opt-in, a second listener in
   the bridge process: `POST /channel/<slug>/play`, `GET /healthz`. The daemon still listens on nothing.

## Security model

- **Scope of the play token.** The play endpoint has its own token (`$XDG_CONFIG_HOME/kmixdeck/play-token`, 0600,
  compared in constant time), never the web-UI token, and the UI token does not open it. With it a client can do
  exactly one thing: play audio into a channel whose `Playback` switch the user turned on. It cannot move a fader,
  change routing, read state, or reach any other method. A leaked play token means "someone can play sound into
  the channels you opened for that", nothing more.
- **Refuse before reading.** Token, channel, switch, `Content-Length` (required, no chunked bodies), size limit
  (`--play-max-bytes`, 64 MiB default) and queue room are checked from the headers. A refused request never costs
  an upload. At most 8 requests are in flight.
- **RAM, not disk.** The body goes into a `memfd` and its fd to the daemon. Nothing is written to a disk.
- **No nested opens.** The demuxer's `io_open` refuses every nested open and the allowed formats are plain audio
  containers (wav, w64, aiff, flac, mp3, ogg, aac, mov/mp4/m4a, matroska/webm). An hls/dash/concat playlist in the
  body cannot make the daemon fetch a URL or open a path.
- **No fd over the WebSocket.** The WebSocket allowlist leaves out every method with an `h` argument.
- **A client that gives up is not played late.** The request blocks until the track ended; if the client
  disconnects meanwhile, the bridge calls `StopPlayback(id)`.

## Consequences

- A TTS client needs no PipeWire, no pulse and no access to the host beyond one HTTP port, and it learns whether
  its clip was heard (200 played · 410 stopped · 422 error).
- The daemon links libavformat/libswresample for a second purpose (it already did for CT-8) and runs one decoder
  thread per sounding track — at most one per channel.
- `Channel.Play` needs a bus connection that can pass fds (unix socket transport). A connection without it gets
  `org.kmixdeck1.Error.NoFdPassing`.
- Tier `full`: CLI (`channel playback|play|stop`), KDE window (row menu) and web UI (channel menu, NowPlaying
  badge); the tray has nothing for it.
