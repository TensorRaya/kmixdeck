# Findings while building HY-5 (2026-10-06/07), to fix after it, each with its own red/green test
1. KdeIntegration::listeningMix() ignores Mixer.ListeningDevice: "first mix with a present output". Used by tray
   middle click (mute), listenNext (CT-1 switch monitoring mix), tray "Listening to" menu.
2. CT-1 volume-up hotkey (KdeIntegration step): lin * 10^(db/20) -> stuck at 0 once the master is at -inf.
3. UX-2 "which mixes reach the listening device": exact ref match in CLI listen / QML hearing bar / web,
   node-part match in MixerClient DV-27 monitors. A port ref (dev:AUX3,AUX4) is "not heard" in 3 of 4.
   Probe: Outputs ['fake.headphones:FL,FR'], listen -> mixes [].
4. dbus-descriptions ListeningDevice: "empty for the PipeWire default" vs every frontend: empty = none.
5. Loudness signal: analysers run with no subscriber, but Meters::setTargets({}) stops the tick -> no reading
   is ever published without a Levels subscriber (probe: 0 signals in 2 s, 50 while `levels` runs).
6. kmixdeckd 3.4-4.0 % CPU with no subscriber while a tone plays into the Stream mix (analyser on).
   Header comment claims ~1 % per analyser. Measure TRUE_PEAK share.
7. `kmixdeck loudness` still Subscribe()s to Levels: with the tick fix it no longer needs to, and the
   subscription starts every peak meter (12-14.6 % daemon CPU vs 3.4-4.0 %).
8. Volume steps are read-modify-write in every client (Stream Deck nudge, KDE hotkey, waybar up/down):
   a burst of concurrent steps can lose steps. Needs a daemon-side Mix.StepVolume(dB) (contract change).
9. test_frontends_sync UX-5 runs tools/extract-messages.sh IN the source tree: every gate leaves po/kmixdeck.pot and
   po/de/kmixdeck.po modified (POT-Creation-Date + #: line refs). A test must not write into the checkout.

## 10. Levels.Peaks: wire signature a{sv}, contract says a{sd}; CLI segfaults on a{sd} (2026-10-07)
- `busctl monitor` against the real kmixdeckd: `MESSAGE "a{sv}"` for Peaks (QVariantMap), `a{sad}` for Loudness.
- interfaces/org.kmixdeck1.Levels.xml and docs/dbus-api.md document Peaks as `a{sd}`.
- A client that follows the XML and sends/expects a{sd} breaks: `kmixdeck --json levels --once` against a stand-in
  emitting a{sd} Peaks -> SIGSEGV (rc 139), every build (old, new). No gdb on the dev box, backtrace not taken yet.
- test_ar2_contract_matches_shipped_xml does not compare signal signatures on the wire.
- Fix direction: emit a real a{sd} (QMap<QString,double> + qDBusRegisterMetaType, like Loudness) OR change the XML
  to a{sv}; then a wire-signature check in test_ar2. And the CLI must not crash on an unexpected signature.
- Backtrace (container, Debug build, gdb 2026-10-07): QDBusArgument::operator>>(QDBusVariant&) <- operator>>(QDBusArgument, QVariant&)
  <- QDBusMetaType::demarshall <- QObject::event (queued signal delivery) -> SIGSEGV in libdbus-1. Root cause:
  QDBusConnection::connect() WITHOUT a signature delivers every signature to a QVariantMap slot, and QtDBus
  demarshals a{sd} as if it were a{sv}. Same pattern in MixerClient::onPeaks (KDE window) and anywhere else that
  connects Peaks without a signature.
