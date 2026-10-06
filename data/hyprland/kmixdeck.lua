-- SPDX-License-Identifier: GPL-3.0-or-later
-- kmixdeck for Hyprland >= 0.55 (Lua config). ADR 0014 HY-3.
--
-- Load it from your hyprland.lua (the directory is <prefix>/share/kmixdeck/hyprland, /usr for a distribution package):
--
--     package.path = "/usr/share/kmixdeck/hyprland/?.lua;" .. package.path
--     local kmixdeck = require("kmixdeck")
--
-- What loading it does: kmixdeck's windows float. The main window opens at the size the app asks for, centred in the
-- work area; its dialogs float as well. Nothing else: no key is bound and nothing in your config is changed. The rule
-- is named, so one line turns it off again:
--
--     kmixdeck.rules.window:set_enabled(false)        -- tile the main window like any other
--
-- Global shortcuts: kmixdeck registers its actions with the GlobalShortcuts portal (xdg-desktop-portal-hyprland);
-- `hyprctl globalshortcuts` lists them as org.kmixdeck.kmixdeck:<id>. Bind a key to one with
--
--     kmixdeck.bind("SUPER + F9", "mute-channel-voice")
--
-- Ids: mute-channel-<channel>, mute-mix-<mix>, volume-up-mix-<mix>, volume-down-mix-<mix>, listen-next-mix
-- (<channel>/<mix> are the slugs `kmixdeck channels` / `kmixdeck mixes` print).

local M = {}

M.app_id = "org.kmixdeck.kmixdeck"

-- Only `float`, on the class alone (measured with Hyprland 0.56.2, ADR 0014 HY-3 result):
--  * a floating window opens centred at the size it asks for (1152x648 at gridUnit 18), so `center` and `size` add
--    nothing;
--  * the dialogs have the same class and the same initial title as the main window, so no prop tells them apart;
--  * a match on initial_title made the main window float at the full monitor size (1920x1080) instead of its own.
M.rules = {
    window = hl.window_rule({
        name = "kmixdeck-window",
        match = { class = "^org\\.kmixdeck\\.kmixdeck$" },
        float = true,
    }),
}

--- Bind a key chord to one of kmixdeck's global shortcut ids, e.g. bind("SUPER + F9", "mute-channel-voice").
function M.bind(keys, id, opts)
    return hl.bind(keys, hl.dsp.global(M.app_id .. ":" .. id), opts)
end

return M
