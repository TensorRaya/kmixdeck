# Elgato Wave Link 3.x — feature inventory (from official Elgato sources)

Collected 2026-09-14 from Elgato's help center (setup guide, release notes 3.0–3.3 beta, support articles). Parity baseline for `[wavelink]`-tagged requirements. Not every feature is a goal — see `docs/spec/requirements.md`. Items the collector could not confirm are marked *(uncertain)*.

# Elgato Wave Link 3.x — Complete Feature Inventory
# (Windows + macOS), built from official Elgato sources

| # | Feature | Description | Category | Source |
|---|---------|-------------|----------|--------|
| 1 | App to Channel Routing | Route any installed application (Game, System, Discord, Music, Mic) into its own virtual mixer channel. | Routing | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 2 | Multiple Sources Per Input Channel | Combine multiple input sources (microphone, capture card, software) into a single input channel. | Routing | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 3 | Up to 5 Output Mixes | Create up to 5 output mixes (Personal Mix, Stream Mix, recording, chat, dual-PC), each with independent routing and effects. | Mixes | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 4 | Per-Channel Volume Per Mix | Each mix has independent per-channel volumes, routing, and effects (basis: 5 mixes each with independent routing/effects; channel volume controls in the mixer). | Mixes | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 5 | Mix Names & Custom Mix Icons | Editable mix names and custom icons, send a mix to multiple hardware outputs simultaneously. | Mixes | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 6 | Mix Monitoring (Ear Icon) | Click the ear icon to monitor a mix directly (usually the Personal Mix). | Mixes | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup |
| 7 | Know-Your-Monitored-Mix Toolbar Control | Always-visible mixer toolbar control showing which mix is monitored and which output device plays through it; one-click switching; indicates when no mix is selected or the device is unavailable. | UX | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog |
| 8 | Muted Mix Indicator | A muted mix header turns red and reads "Muted", consistent with muted channels. | UX | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 9 | Mix Reorder (Drag-And-Drop) | Drag-and-drop channel and mix reordering with a clear visual drag handle. | UX | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 10 | App To Channel Dropdown With Categories | The "Add app to channel" dropdown organizes apps into categories to speed up finding the right app. | Routing | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 10 | Drag App Onto Channel Card | Drag-and-drop apps directly onto channel cards, or move apps via the Create Channel button. | Routing | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes |
| 11 | Channel & Mix Deletion With Undo | Delete channels and mixes from the context menu with undo support; channels can be removed from the Mixer by right-click. | Channels | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Notes |
| 12 | Remove Wave Device Channels | Remove a Wave device channel without disconnecting the hardware (3.2.x), and by right-click on the Mixer (3.3 beta 2). | Devices | https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Release-Notes |
| 13 | Hide Unused Audio Devices | Deactivate extra inputs/outputs (from monitors, webcams, peripherals) in-app, avoiding digging through OS sound settings. | Devices | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 14 | Categorized App Sorting | Apps in input channels are sorted alphabetically; output mix level meters only show activity when audio is routed. | Channels | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |
| 15 | Log File & Usage-Data Privacy | Keep local usage data for a week (auto-clear old entries); don't log more Marketplace account info than needed. | UX | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog |
| 15 | Language Auto-Adapt | Automatically switch UI language (e.g., Japanese) when the system language is Japanese. | UX | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 16 | Last Viewed Tab Memory | Remember the last viewed tab (Mixer or Devices) and open that tab. | UX | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 16 | Volume Percent Display | Show volume percentages on output devices and mix cards. | UX | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes |
| 17 | Volume Slider Logarithmic Curve | Volume sliders follow a logarithmic curve for precise low-volume adjustments matching perceived loudness. | UX | https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Release-Notes |
| 17 | Improved Audio Engine | Rebuilt audio engine with fewer dropouts/clicks, less monitoring delay, and a stable mixer on device changes. | Devices | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog |
| 18 | Wave Link 2 Config Import | Installer detects previous Wave Link 2 installation, migrating channels/mixes/routing automatically. | Routing | https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Release-Notes |
| 18 | System Output Warning (XLR Pro) | Detect when a Wave XLR Pro is the macOS default output, with a one-click warning + dropdown to pick a different device. | Devices | https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Release-Notes |
| 19 | Settings Backup & Restore | Auto-backup of the whole configuration plus on-demand snapshot in Settings > Backup and Restore, including pre-update auto-backup. | UX | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog |
| 20 | OBS Studio Integration | Connect via WebSocket (port + password), auto-connect toggle; back up/restore OBS audio settings across updates (OBS 28+ required). | Control-Integration | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta--2-for-Windows-Changelog |
| 21 | OBS Studio Audio Backup | Save OBS audio setup before Wave Link updates to avoid losing audio input due to Windows device-ID changes. | Control-Integration | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog |
| 22 | Stream Deck Plugin | Volume/mute/mix control for Stream Deck; press-and-hold to activate effects (e.g., voice changer) while held. | Control-Integration | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |
| 22 | Export To Stream Deck | Export Wave Link configurations to a Stream Deck layout (fixed: don't list unsupported Stream Deck Mini/Neo). | Control-Integration | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog |
| 23 | Siri & Apple Shortcuts | Control Wave Link hands-free: mute channels, adjust volumes, switch mixes; App Intents with Spotlight search and undo (macOS). | Control-Integration | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes |
| 24 | Marketplace Presets/Plugins | Browse and install audio presets and VST plugins from the Elgato Marketplace directly in-app. | Effects | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes |
| 24 | Marketplace Login Fix | Fixed marketplace login with expired tokens. | UX | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes |
| 25 | VST Effect Support | Scan/install VST audio effect plugins with progress indicator, handle broken plugins, and warn if the VST2 directory path is non-standard. | Effects | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 25 | VST Preset Folders | Store and manually install XML preset files: Mac ~/Library/Audio/Presets/Elgato/, Windows %USERPROFILE%\Documents\VST3 Presets\Elgato. | Effects | https://help.elgato.com/hc/en-us/articles/48083988523537-Wave-Link-3-VST-Preset-Locations |
| 26 | Effect Organization | Rename (double-click), copy/paste (Cmd+C/Cmd+V), multi-select effects for batch operations; copy effects between channels/devices; names carried to Stream Deck plugin. | Effects | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS--Release-Notes |
| 26 | Copy Effects From Unavailable Channels | Copy effects from a deleted/unavailable channel into a new one (KB5101650 workaround). | Effects | https://help.elgato.com/hc/en-us/articles/49089802625809-Elgato-Wave-Link-3-2-8-Windows-Release-Notes |
| 27 | Sound Check | On-device recording/playback to verify audio; improved stability (3.2.5) and fixed issues (recording not audible, playback not stopping on leave, output device pre-selection). | Devices | https://help.elgato.com/hc/en-us/articles/48564639825041-Elgato-Wave-Link-3-2-5-Windows-Release-Notes |
| 27 | Output Channel Effects | Expander, Compressor, and Sound Check effects on output channels, on/off states saved between sessions. | Effects | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes |
| 28 | Wave Next Devices | Full support for Wave:3 MK.2, Wave XLR MK.2, XLR Dock MK.2, Wave XLR Pro. | Devices | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |
| 29 | Wave XLR Pro Hardware Mixer | Dual XLR inputs, dual USB-C, line in/out, on-device mixer; show a badge on the mix header when the hardware mixer is in use. | Devices | https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Release-Notes |
| 29 | Firmware Update Flow | Prompt to install a firmware update on connecting a Wave device; show progress popup; improve version detection (3.0 macOS). | Devices | https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Release-Notes |
| 30 | Device Settings | Display firmware version in My Devices, High Power Mode toggle, headphone output options, monitor volume slider (XLR Dock MK.1). | Devices | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |
| 30 | Auto-Detect & Onboarding Tour | On first launch, wait for device/app discovery before configuring defaults; improved onboarding with updated device imagery. | UX | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link--0-macOS-Release-Notes |
| 31 | Device Connection Errors | Show an error message when communication with a Wave device fails (XLR Pro: improved stability; 3.2.3: fixed connection error blocking the UI). | Devices | https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Release-Notes |
| 31 | Sleep/Wake Recovery | Recover reliably after sleep/wake (3.1 Windows); fixed issue of app channels disappearing after sleep (3.2.3 Windows XLR Pro). | UX | https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows--Notes |
| 32 | Menu Bar Main Output Device List | Show only actually connected devices in the menu bar list (3.2.3 macOS fix). | Devices | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog |
| 32 | Log File Hygiene | Prevent log files growing larger than they should (3.2.3 macOS). | UX | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link--3037-Beta-for-macOS-Changelog |
| 33 | Windows Installers | x64 and ARM64 .msix installers for all 3.x releases. | UX | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |
| 33 | Free Application | Wave Link 3 is free, works with non-Elgato microphones. | UX | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes |

## Notes On Uncertain Items
- **AU effects**: Only VST support is documented in the official sources; AU (Audio Units) is not explicitly mentioned — mark as uncertain. | https://help.elgato.com/hc/en-us/articles/48083988523537-Wave-Link-3-VST-Preset-Locations
- **Hotkeys**: No dedicated official article covering hotkeys or shortcuts; some keyboard shortcuts (Cmd+C/Cmd+V for effects) are documented. | https://help.elgato.com/hc/en-us/articles/4440845211505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes
- **Profiles**: Not documented in official sources; only "Settings Backup & Restore" (3.3 beta 2) is available. Mark as uncertain. | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog
- **Bluetooth**: Only a one-line fix "Bluetooth earbuds volume control now works correctly" — suggests basic Bluetooth support. | https://help.elgato.com/hc/en-us/articles/4440845211505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes

# Elgato Wave Link 3.x — Documented Limitations & Design Decisions

| # | Limitation / Design Decision | Detail | Source |
|---|----------|--------|--------|
| L1 | Max 5 output mixes | Up to 5 output mixes (Personal Mix, Stream Mix, recording, chat, dual-PC). | https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial--Setup |
| L1 | 4 External Hardware Input Limit | Channels removed from the Mixes view still count toward the limit of 4 external hardware inputs (fixed in 3.3 beta 2). | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog |
| L2 | Max 8 Channels Per Mix (XLR Pro) | Users with Wave XLR Pro could be limited to fewer than 8 channels (fixed in 3.2.4). | https://help.elgato.com/hc/en-us/articles/47883204686097-Elgato-Wave-Link-3-2-4-Windows-Release-Notes |
| L3 | Max Channels Per Mix | A channel cannot be dragged to the last position in a mix that already has the max channels (fixed in 3.2.1 macOS). | https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS--Notes |
| L4 | Wave Link 2 End-of-Life | Wave Link 2 is EOL but still downloadable; focus is on Wave Link 3. | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Notes |
| L5 | Console Connectivity | Only PS4/PS5; compatible: Wave:1, Wave:3 (MK.1), Wave XLR; incompatible: Wave Neo, Wave XLR Dock, Wave XLR MK2 Dock, Wave:3 MK2, Wave XLR MK2. | https://help.elgato.com/hc/en-us/articles/47686183975697-Wave-Devices-and-Console-Connectivity |
| L6 | Discord Screenshare Echo | Screenshare captures all audio regardless of Wave Link routing; use the Devices option or mute desktop audio. | https://help.elgato.com/hc/en-us/articles/46471850961297-Wave-Link-3-0-Discord-Screensharing-Audio-Echo-Issues |
| L7 | Driver Update Affects OBS/Discord Device IDs | Updates including a new audio driver can make virtual devices appear differently in OBS/Discord; check app audio settings. | https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Notes |
| L8 | KB5101650 Breaks Device Matching | A specific Windows update can break device matching; fixed in 3.2.7. | https://help.elgato.com/hc/en-us/articles/49089683395217-Elgato-Wave-Link-3-2-7-Windows-Notes |
| L9 | Config File Corruption | A single bad or duplicate entry used to reject the whole file; saves protected against interruptions. | https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog |
| L10 | Wave Link 2 Migration (macOS) | Installer detects and migrates Wave Link 2 channels/mixes/routing automatically. | https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Notes |
| L11 | VST Preset Folder Locations | Mac: ~/Library/Audio/Presets/Elgato/, Windows: %USERPROFILE%\Documents\VST3 Presets\Elgato; manually install presets. | https://help.elgato.com/hc/en-us/articles/48083988523537-Wave-Link-3-VST-Preset-Locations |
| L12 | Log Files Contain Usage Data | Keep local usage data for 1 week (auto-clear old entries); minimize Marketplace account info. | https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog |
| L13 | Windows ARM64 Installers | Provide both x64 and ARM64 installers for all 3.x releases. | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Notes |
| L14 | Free Application | Wave Link 3 is free and works with non-Elgato microphones. | https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Notes |
| L15 | Bluetooth Support | Basic Bluetooth support implied by the one-line fix. | https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Notes |
| L16 | Wave Link 2 Articles | Sample rate switching (2.0); Low Latency Mode (1.x); MicrophoneFX as soundboard (2.0); Capture card support (2.0); Multiple Wave mics not supported (2.0); Gain Lock (2.0). | https://help.elgato.com/hc/en-us/sections/360009124412-Wave-Link |

## Sources fetched

- https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Notes
- https://help.elgato.com/hc/en-us/articles/44408325264017-Elgato-Wave-Link-3-0-0-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link--0-macOS-Release-Notes
- https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS--Release-Notes
- https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Notes
- https://help.elgato.com/hc/en-us/articles/44408452111505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes
- https://help.elgato.com/hc/en-us/articles/4440845211505-Elgato-Wave-Link-3-0-0-macOS-Release-Notes
- https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows--Notes
- https://help.elgato.com/hc/en-us/articles/45962094845329-Elgato-Wave-Link-3-1-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/46471850961297-Wave-Link-3-0-Discord-Screensharing-Audio-Echo-Issues
- https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial--Setup
- https://help.elgato.com/hc/en-us/articles/46941178829585-Wave-Link-3-0-Software-Initial-Setup
- https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Notes
- https://help.elgato.com/hc/en-us/articles/47156413046289-Elgato-Wave-Link-3-2-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/47196378703633-Elgato-Wave-Link-3-2-1-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/47603985610897-Elgato-Wave-Link-3-2-3-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/47686183975697-Wave-Devices-and-Console-Connectivity
- https://help.elgato.com/hc/en-us/articles/47883204686097-Elgato-Wave-Link-3-2-4-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/48083988523537-Wave-Link-3-VST-Preset-Locations
- https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS--Notes
- https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Notes
- https://help.elgato.com/hc/en-us/articles/48522843138065-Elgato-Wave-Link-3-2-1-macOS-Release-Notes
- https://help.elgato.com/hc/en-us/articles/48564639825041-Elgato-Wave-Link-3-2-5-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/48602717332369-Elgato-Wave-Link-3-2-2-macOS-Release-Notes
- https://help.elgato.com/hc/en-us/articles/48965301638033-Elgato-Wave-Link-3-2-6-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/49089683395217-Elgato-Wave-Link-3-2-7-Windows-Notes
- https://help.elgato.com/hc/en-us/articles/49089683395217-Elgato-Wave-Link-3-2-7-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/49089802625809-Elgato-Wave-Link-3-2-8-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/49318466620049-Elgato-Wave-Link-3-2-9-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/49526614077713-Elgato-Wave-Link-3-2-10-Windows-Release-Notes
- https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link--3037-Beta-for-macOS-Changelog
- https://help.elgato.com/hc/en-us/articles/50078276604817-Wave-Link-3-2-3-3037-Beta-for-macOS-Changelog
- https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta--2-for-Windows-Changelog
- https://help.elgato.com/hc/en-us/articles/50514969952401-Wave-Link-3-3-0-4334-Beta-2-for-Windows-Changelog
- https://help.elgato.com/hc/en-us/categories/360003420852-Wave
- https://help.elgato.com/hc/en-us/sections/25178731372173-Wave-Link-Beta-Changelogs
- https://help.elgato.com/hc/en-us/sections/360009124412-Wave-Link
- https://help.elgato.com/hc/en-us/sections/4913442828941-Wave-Link-Release-Notes
