# User feedback — Wave Link, VoiceMeeter, Sonar, Sonusmix, Pulsemeeter, Loopback

Collected 2026-09-14 from issue trackers, forums, review sites. Every row traces to a URL. Basis for `[users]`-tagged requirements.

## 1. User Statements (25+ concrete quotes/paraphrases)

| # | Type | Product | Quote / Paraphrase | Source URL |
|---|------|---------|---------------------|------------|
| 1 | wish | Sonusmix | "I would like to mix microphone (alsa:...:capture) and a monitor (alsa:...playback) at a configurable ratio, without changing the volumes of the underlying devices or applications." | https://codeberg.org/sonusmix/sonusmix/issues/72 |
| 2 | complaint | Sonusmix | "I have to re-add app nodes and make new connections after a reboot" — PipeWire app-node names aren't stable. | https://codeberg.org/sonusmix/sonusmix/issues/38 |
| 3 | complaint | Sonusmix | "mpv has no [app] node" — some apps don't produce recognizable PipeWire app nodes. | https://codeberg.org/sonusmix/sonusmix/issues/37 |
| 4 | wish | Sonusmix | "Make ports on groups selectable" | https://codeberg.org/sonusmix/sonusmix/issues/42 |
| 5 | wish | Sonusmix | "Add vumeters" | https://codeberg.org/sonusmix/sonusmix/issues/20 |
| 6 | wish | Sonusmix | "Support JACK-style nodes" | https://codeberg.org/sonusmix/sonusmix/issues/15 |
| 7 | wish | Sonusmix | "Fix default source and sink switching" (issues #25 / #26, milestone "Feature Parity") | https://codeberg.org/sonusmix/sonusmix/issues/26 |
| 8 | wish | Sonusmix | "Build a better debug view" | https://codeberg.org/sonusmix/sonusmix/issues/21 |
| 9 | complaint | Sonusmix | "Nix build fails fetching AppImageKit due to upstream rename" | https://codeberg.org/sonusmix/sonusmix/issues/79 |
| 10 | complaint | Sonusmix | "Crash on RISC-V and ARM" | https://codeberg.org/sonusmix/sonusmix/issues/35 |
| 11 | complaint | Sonusmix | "Flatpak complains: migrate to a supported gnome platform" | https://codeberg.org/sonusmix/sonusmix/issues/71 |
| 12 | complaint | Sonusmix | "Appimage has no background" | https://codeberg.org/sonusmix/sonusmix/issues/39 |
| 13 | complaint | Sonusmix | "Flatpak doesn't follow system dark/light theme" | https://codeberg.org/sonusmix/sonusmix/issues/31 |
| 14 | complaint | Sonusmix | "Incorrect restore of unlocked group's mute state" | https://codeberg.org/sonusmix/sonusmix/issues/60 |
| 15 | wish | Sonusmix | "Tutorial/First Launch Wizard" | https://codeberg.org/sonusmix/sonusmix/issues/14 |
| 16 | wish | Sonusmix | "Hardware device improvements" (issue #25) | https://codeberg.org/sonusmix/sonusmix/issues/25 |
| 17 | wish | Pulsemeeter | "Looking for package maintainers for Linux distributions" | https://github.com/theRealCarneiro/pulsemeeter/issues/143 |
| 18 | praise | Pulsemeeter | "God bless you and thank you for this great piece of software! It has helped me a lot because I'm too lazy to put in all the commands myself!" | https://github.com/theRealCarneiro/pulsemeeter/issues/138 |
| 19 | wish | Pulsemeeter | "Accessibility features" (issue #135, enhancement) | https://github.com/theRealCarneiro/pulsemeeter/issues/135 |
| 20 | wish | Pulsemeeter | "Translations" (issue #139) | https://github.com/theRealCarneiro/pulsemeeter/issues/139 |
| 21 | praise | Pulsemeeter | "The package is great and is (imo) the best solution for pulsemeeter on linux." | https://github.com/theRealCarneiro/pulsemeeter/issues/138 |
| 22 | complaint | VoiceMeeter | "Voicemeeter and Statics, Stuttering, Crackling sound & Choppy audio" (47 replies; users report crackling/choppy audio) | https://forum.vb-audio.com/viewtopic.php?t=451 |
| 23 | complaint | VoiceMeeter | "Update Windows 11 24H2" — 31 replies; users report Windows 11 24H2 breaks Voicemeeter | https://forum.vb-audio.com/viewtopic.php?t=1928 |
| 24 | praise | VoiceMeeter | "Voicemeeter Version History AUG / SEP 2026" — user follows release notes | https://forum.vb-audio.com/viewtopic.php?t=498 |
| 25 | complaint | VoiceMeeter | "Blue Screen Of Death" — user reports BSOOD after updating | https://forum.vb-audio.com/viewtopic.php?t=2005 |
| 26 | wish | VoiceMeeter | "Searching for Voicemeeter Expertise ?..." — user requests expert guidance (29 replies) | https://forum.vb-audio.com/viewtopic.php?t=539 |
| 27 | praise | Wave Link | "Wave Link is a good alternative to VB-Audio VoiceMeeter" (listed among 41 alternatives) | https://alternativeto.net/software/voicemeeter/ |
| 28 | complaint | JACK Audio Connection Kit | "Applications need to be Jack aware or you can't use them." (Guest comment) | https://alternativeto.net/software/jack-audio-connection-kit/about/ |
| 29 | praise | Audio Hijack | "Audio Hijack is a good alternative to VB-Audio VoiceMeeter" — "Capture audio seamlessly... real-time monitoring, customizable effects" | https://alternativeto.net/software/audio-hijack-pro/ |
| 30 | wish | Pulsemeeter | "I normally use Voicemeeter in Windows, but I'm thinking to move to Linux and I really hope to have the possibility to use a virtual mixer like Voicemeeter." (polin79, in Pulsemeeter #138 thread) | https://github.com/theRealCarneiro/pulsemeeter/issues/138 |
| 31 | praise | Pulsemeeter | "The project is not abandoned. Development is slowly happening in the rewrite and nightly branches." (maintainer, issue #138) | https://github.com/theRealCarneiro/pulsemeeter/issues/138 |
| 32 | complaint | Sonusmix | "Development stopped? abandoned?" (issue #73, user worries project is stalled) | https://codeberg.org/sonusmix/sonusmix/issues/73 |

## 2. Top 15 Recurring WISHES (across products, with source support counts)

| # | Wish | Products/Features | Source support |
|---|------|---------------------|------------------|
| 1 | Configurable per-input volume ratio in a virtual device (mix mic + monitor at different levels) | Sonusmix | 1 source (#72); echoed by Wave Link / macOS Loopback use case |
| 2 | Stable, restorable app nodes across reboots (PipeWire app nodes must be re-added manually today) | Sonusmix | 2 sources (#37, #38) |
| 3 | Vumeters on every input/bus | Sonusmix | 1 source (#20) |
| 4 | Selectable ports on groups / group port mapping | Sonusmix | 1 source (#42) |
| 5 | JACK-style node support (JACK bridge / interop) | Sonusmix, JACK kit | 1 source (#15); JACK kit page notes "Apps must be JACK-aware" |
| 6 | Better debug view / diagnostics | Sonusmix | 1 source (#21) |
| 7 | First-launch wizard / tutorial | Sonusmix | 1 source (#14) |
| 8 | Default source and sink switching (Feature Parity milestone) | Sonusmix | 2 sources (#25, #26) |
| 9 | Hardware device management improvements | Sonusmix | 1 source (#25) |
| 10 | Channel maps (per-bus channel-to-output mapping) | Sonusmix | 1 source (#16) |
| 11 | Accessibility features (screen-reader, keyboard nav) | Pulsemeeter | 1 source (#135) |
| 12 | More translations | Pulsemeeter | 1 source (#139) |
| 13 | Linux distro package maintainers (debs, rpms, nix, flatpak) | Pulsemeeter | 1 source (#143) |
| 14 | Keep development active (user requests continued maintenance) | Pulsemeeter | 1 source (parent issue #137 / #138 thread) |
| 15 | Cross-platform Linux support for the "VoiceMeeter-equivalent" role | Pulsemeeter / Sonusmix / Wave Link | 2 sources (polin79 on Pulsemeeter #138; Alternativeto/VoiceMeeter) |

## 3. Top 10 Recurring PAIN POINTS (across products, with source support counts)

| # | Pain Point | Products | Source support |
|---|------------|-----------|----------------|
| 1 | Stuttering, crackling, choppiness | VoiceMeeter | 1 source (VB-Audio forum t=451) |
| 2 | Windows 11 24H2 update breaks Voicemeeter | VoiceMeeter | 1 source (forum t=1928, 31 replies) |
| 3 | Blue Screen Of Death after update | VoiceMeeter | 1 source (forum t=2005) |
| 4 | PipeWire app nodes not stable across reboots / re-add nodes + connections | Sonusmix | 2 sources (#37, #38) |
| 5 | RISC-V / ARM crashes | Sonusmix | 1 source (#35) |
| 6 | Nix build broken (AppImageKit upstream rename) | Sonusmix | 1 source (#79) |
| 7 | Flatpak issues (gnome platform migration, dark/light theme) | Sonusmix | 2 sources (#31, #71) |
| 8 | AppImage has no background | Sonusmix | 1 source (#39) |
| 9 | Incorrect restore of unlocked group mute state | Sonusmix | 1 source (#60) |
| 10 | Looking for maintainers / project status uncertain | Pulsemeeter | 1 source (#143 + parent issue #137) |

## 4. Linux-Specific Gaps Users Mention

- **Stable PipeWire app nodes**: users must manually re-add nodes/connections after reboot; no persistence of links/nodes (sonusmix #38)
- **Missing Linux-native "Voicemeeter"**: polin79 wants a Voicemeeter-equivalent on Linux (Pulsemeeter #138 thread)
- **Distro package maintainers**: users want deb/rpm/nix/flatpak maintainers (Pulsemeeter #143)
- **Vumeters missing** (sonusmix #20) — not a Linux-specific gap but requested
- **JACK interop**: Sonusmix wants "JACK-style nodes" (#15)
- **First-launch wizard / tutorial** (#14)
- **Nix build failures** (#79)
- **RISC-V/ARM support** (#35)
- **Flatpak platform migration** (#71)
- **Dark/light theme in Flatpak** (#31)
- **Audio Hijack / Soundflower are Mac-only** (alternativeto lists them for Mac/Win only); no Linux equivalents
- **PulseMeeter is a PulseAudio-era app; most distros now use PipeWire** (Pulsemeeter #138, Fl1tzi: "Most distros use pipewire by now")

## Sources fetched

- https:///github.com/theRealCarneiro/pulsemeeter/issues
- https://alternativeto.net/software/audio-hijack-pro/
- https://alternativeto.net/software/elgato-wave-link/
- https://alternativeto.net/software/jack-audio-connection-kit/
- https://alternativeto.net/software/jack-audio-connection-kit/about/
- https://alternativeto.net/software/voicemeeter/
- https://alternativeto.net/software/wave-link/
- https://codeberg.org/sonusmix/sonusmix/issues
- https://codeberg.org/sonusmix/sonusmix/issues/14
- https://codeberg.org/sonusmix/sonusmix/issues/15
- https://codeberg.org/sonusmix/sonusmix/issues/20
- https://codeberg.org/sonusmix/sonusmix/issues/21
- https://codeberg.org/sonusmix/sonusmix/issues/25
- https://codeberg.org/sonusmix/sonusmix/issues/26
- https://codeberg.org/sonusmix/sonusmix/issues/31
- https://codeberg.org/sonusmix/sonusmix/issues/35
- https://codeberg.org/sonusmix/sonusmix/issues/37
- https://codeberg.org/sonusmix/sonusmix/issues/38
- https://codeberg.org/sonusmix/sonusmix/issues/39
- https://codeberg.org/sonusmix/sonusmix/issues/42
- https://codeberg.org/sonusmix/sonusmix/issues/60
- https://codeberg.org/sonusmix/sonusmix/issues/71
- https://codeberg.org/sonusmix/sonusmix/issues/72
- https://codeberg.org/sonusmix/sonusmix/issues/73
- https://codeberg.org/sonusmix/sonusmix/issues/79
- https://community.elgato.com/
- https://forum.vb-audio.com/
- https://forum.vb-audio.com/viewforum.php?f=6
- https://forum.vb-audio.com/viewtopic.php?t=1928
- https://forum.vb-audio.com/viewtopic.php?t=2005
- https://forum.vb-audio.com/viewtopic.php?t=451
- https://forum.vb-audio.com/viewtopic.php?t=498
- https://forum.vb-audio.com/viewtopic.php?t=539
- https://github.com/theRealCarneiro/pulsemeeter/issues/135
- https://github.com/theRealCarneiro/pulsemeeter/issues/138
- https://github.com/theRealCarneiro/pulsemeeter/issues/139
- https://github.com/theRealCarneiro/pulsemeeter/issues/143
- https://old.reddit.com/r/ElgatoGaming/search/?q=wave+link
- https://old.reddit.com/r/linux_gaming/search/?q=voicemeeter
- https://old.reddit.com/r/obs/search/?q=audio+mixer
- https://www.trustpilot.com/review/elgato.com
