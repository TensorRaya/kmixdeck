# Wave Link 3 — menus and dialogs, as observed in video frames

Method: three YouTube walkthroughs (yt-dlp, 720p) sampled at 0.25 fps into `/tmp/wl3-frames/<video-id>/frame_NNN.jpg`
(frame N ≈ second 4·N), contact sheets + single frames read with a vision model. Only what was actually
visible is listed; everything else is under "Not observed". No feature below is inferred.

Sources
- `R_mPkRAao-k` — German sponsored tutorial ("WERBUNG"), Wave Link 3 beta on Windows 11, presenter with Wave:1.
- `AUy4w57P-1Y` — English review/tutorial, Wave Link 3 with Wave:3 MK.2, includes first-run onboarding and Settings.
- `EjiF5QEVNtI` — third walkthrough, only skimmed (contact sheet), used for corroboration only.

## 1. Application shell

- Window title bar: hamburger icon, small **green** rounded app icon, "Wave Link", standard Windows buttons. (AUy4w57P-1Y f80)
- **Left navigation rail**, dark:
  - "Your devices" / "Elgato devices" → one entry per Elgato device, e.g. "Elgato Wave:1", "Elgato Wave:3 MK.2" (mic icon)
  - "Mixes & effects" → "Mixes" (active item has a thin accent bar on its left edge)
  - bottom: "Beta version — Share your feedback" (beta build only), "Marketplace" (bag icon), "Settings" (gear). (R_mPkRAao-k f165, AUy4w57P-1Y f80)
- Page header "Mixes"; top right an **output/monitoring device dropdown** with headphone icon, e.g. "Headphones (Elgato Wave:3 MK.2) ⌄", "PG27UCDM (NVIDIA High Definition Audio)", plus a share icon and an expand icon. (both videos)
- Mix columns across the top, each a card: **number badge (1…5)**, name, "N outputs" subtitle, small icon per mix (monitor badge on the personal mix, "((o))" on Stream Mix, people icon on Chat Mix). Observed names: "ICH", "Chat Mix", "Stream Mix", "Record Mix", "Aux Mix"; "Personal Mix / 1 output", "Stream Mix", "Chat Mix", "4 M…". Horizontal scrollbar appears when mixes overflow. (R_mPkRAao-k f165, AUy4w57P-1Y f100)
- Channel rows on the left: square app/source icon, name (ellipsised), speaker/mute button, horizontal fader with **inline level meter (green → yellow → orange)**, round handle; in the personal-mix column an extra monitor/link icon. Below the list: "+ Create channel". (AUy4w57P-1Y f88, R_mPkRAao-k f165)
- Empty routing cells are dark rounded rectangles. (R_mPkRAao-k f165)

## 2. Channel settings — one modal dialog, not a context menu

Opened from a channel row. Large rounded panel over a dimmed mixer. (R_mPkRAao-k f162–f166, f176; AUy4w57P-1Y f80–f86, f92, f108)

- **Top-left: the channel name is an inline text field** (underline; "Musik", "Hades II" retyped to "Games" with caret and ✕ clear button). Rename is not a separate dialog. (AUy4w57P-1Y f86)
- **Top-right: ✕ close.**
- **Left half — icon area**
  - A very large icon preview (Spotify logo, Discord logo, Hades II skull, Windows Settings gear, cyan concentric-circle "Aux" icon for an empty channel).
  - When several apps are assigned, **the icon area shows the icons of the assigned apps side by side and the user picks one of them as the channel icon**; the selected one has a red/magenta selection outline (Hades II / VALORANT / HELLDIVERS 2 with VALORANT selected). A thin horizontal scrollbar sits under the icon strip. (AUy4w57P-1Y f84–f86, R_mPkRAao-k f166 shows Spotify + WhatsApp side by side)
  - A "…" overflow button above the icon area. Its menu was **not** opened in any frame.
  - Under the icon: a horizontal **live level meter** of the channel. (R_mPkRAao-k f165)
- **Right half — two tabs: "Apps" | "Audio effects"** (active tab brighter with underline).
  - **Apps tab:** list of assigned apps, each row = app icon + name + trash icon on the right; unavailable (not running) apps are labelled "(Unavailable)". Full-width secondary button **"+ Add app"**. (AUy4w57P-1Y f80, f84, f92)
  - **"+ Add app" dropdown:** dark popup. Section label "Apps" followed by currently unassigned running apps (e.g. "WhatsApp"); section label **"Transfer app from other channel"** followed by apps that are already assigned elsewhere (Adobe Audition 2025, Discord, Einstellungen). Hover row is lighter grey. In the Wave:3 video the list is a searchable list (Discord, Firefox, Microsoft Edge, Microsoft Teams, Spotify, Stream Deck, VALORANT, 7-Zip File Manager…) with footer "Transfer app from other channel ›". (R_mPkRAao-k f165, AUy4w57P-1Y f108)
- **Footer:** left "🗑 Remove channel" (dim, destructive-secondary); right a filled accent button **"Close window"** (blue in the beta video, green in the release video). (both)

Observation for kmixdeck: Wave Link has **no per-channel context menu at all** in the frames — everything (rename, icon, apps, effects, remove) lives in this one dialog. Icon choice is limited to the icons of the assigned apps; no icon library and no custom upload were visible.

## 3. "Create channel" dropdown

Opened from "+ Create channel". Search box "Search"; list mixing **hardware inputs** ("Elgato Wave XLR Pro USB Aux (2- Elga…", "Microphone (HyperX QuadCast 2)", "Game Capture 4K60 Pro MK.2 Audio (…", "Microphone (Yeti Stereo Microphone)") and **apps** (Firefox, Microsoft Edge, Spotify, 7-Zip File Manager); highlighted footer item **"Add empty channel ›"**. A new empty channel is named "Aux 1" with a cyan "Aux" icon and opens its settings dialog immediately. (AUy4w57P-1Y f104, f108)

## 4. Settings dialog

Modal over the Mixes page. **Left sidebar: General / Updates / Effects / Help.** General page: (AUy4w57P-1Y f120, f124)
- "Personalization": **Color theme** "Same as system" (dropdown); **Language** "English".
- "Windows settings": toggles **"Start app at login"** (on), **"Lock default input device"** (on) with **"Preferred input" = "MicFX (Elgato Virtual…"**; **"Lock default output device"** — switching it on reveals **"Preferred output" = "System (Elgato Virtual…"**.
- Footer "Copyright © 2026, Corsair Memory, Inc. All rights reserved."
Updates / Effects / Help pages were not opened in the frames.

## 5. Onboarding (first run with a new Elgato mic)

Wizard pages (AUy4w57P-1Y f0–f12): "Set up your Wave:3 MK.2" with 3-D render, **"Get started"** + "Skip"; step "Adjust output volume" with hotspot diagram, slider, page dots, green **"Continue"**; final "Wave:3 MK.2 is ready!" with green **"Done"** and "Restart setup" link. Device-specific; only shown when an Elgato device is present.

## 6. Colours, typography, density

- Background near-black charcoal (~#1A1A1C) for chrome/sidebar, content and modal slightly lighter (~#222–#2A2A2D); cards/rows ~#2B2B2E; hover one step lighter. (R_mPkRAao-k f165)
- Accent: **green** in the release build (app icon, primary button, active-monitor badge), light blue in the beta build. Meters green → yellow → orange, dark track, round light handle.
- Text: primary white, secondary medium grey (subtitles "1 output", section labels "Apps", "Transfer app from other channel").
- Muted / inactive: desaturated, greyed icon and dimmed text (Elgato Wave:1 channel while inactive, inactive tab). No red tint on muted channels was observed.
- Density: channel row ≈ one icon-tile high (~44–48 px at 1080p); the modal is roughly 60 % of the window width and split 40/60 between icon area and tabs.

## 7. Not observed (do not assume)

- The **"Audio effects" tab content** — no frame shows it open (effect names, order, add/bypass/presets, parameter widgets: unknown from these videos).
- The **"…" menu** above the icon area (possible custom icon / colour options).
- Any **mix settings dialog** (rename mix, mix icon, output device per mix, remove mix) — mixes were only seen as column headers.
- **Reordering** channels or mixes (drag handles, arrows) — nothing visible.
- Routing/link widgets between faders beyond the per-cell fader; no "link/lock" glyph was seen up close.
- Settings pages Updates / Effects / Help; hotkeys; Stream Deck integration page; sample rate.
- Icon **library**, colour picker or custom image upload for channel icons.

## 8. Most informative frames

1. `/tmp/wl3-frames/R_mPkRAao-k/frame_165.jpg` — channel dialog "Musik", Add-app dropdown open, full shell
2. `/tmp/wl3-frames/R_mPkRAao-k/frame_166.jpg` — two assigned apps, two icons side by side
3. `/tmp/wl3-frames/R_mPkRAao-k/frame_176.jpg` — channel dialog for a system app ("Einstellungen")
4. `/tmp/wl3-frames/AUy4w57P-1Y/frame_084.jpg` — icon selection among assigned apps (VALORANT outlined)
5. `/tmp/wl3-frames/AUy4w57P-1Y/frame_086.jpg` — inline rename with caret and ✕
6. `/tmp/wl3-frames/AUy4w57P-1Y/frame_104.jpg` — "Create channel" dropdown with search and "Add empty channel ›"
7. `/tmp/wl3-frames/AUy4w57P-1Y/frame_108.jpg` — empty channel "Aux 1", add-app list with "Transfer app from other channel ›"
8. `/tmp/wl3-frames/AUy4w57P-1Y/frame_120.jpg` — Settings › General
9. `/tmp/wl3-frames/AUy4w57P-1Y/frame_124.jpg` — Settings with "Preferred output" revealed
10. `/tmp/wl3-frames/AUy4w57P-1Y/frame_000.jpg` — onboarding page 1
