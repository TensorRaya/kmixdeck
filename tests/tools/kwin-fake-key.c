/* kwin-fake-key — CT-1 test helper, never installed: presses a key chord through KWin's
 * org_kde_kwin_fake_input (v4+).
 *
 * Usage: kwin-fake-key <evdev-keycode>...   e.g. 29 42 56 67 = Ctrl+Shift+Alt+F9
 * All codes go down in the given order, then up in reverse. Needs WAYLAND_DISPLAY.
 * Why not wtype: KWin 6.6 does not offer zwp_virtual_keyboard_v1; fake_input is KWin's own
 * path for synthetic input and runs through the same input redirection + global shortcut
 * filter as a real keyboard.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "fake-input-client-protocol.h"

static struct org_kde_kwin_fake_input *fake;
static uint32_t fake_version;

static void global_add(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)d;
    if (strcmp(iface, org_kde_kwin_fake_input_interface.name) == 0) {
        fake_version = ver < 4 ? ver : 4;
        fake = wl_registry_bind(r, name, &org_kde_kwin_fake_input_interface, fake_version);
    }
}
static void global_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg = { global_add, global_remove };

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <evdev-keycode>...\n", argv[0]); return 2; }
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fprintf(stderr, "no wayland display\n"); return 3; }
    struct wl_registry *r = wl_display_get_registry(dpy);
    wl_registry_add_listener(r, &reg, NULL);
    wl_display_roundtrip(dpy);
    if (!fake) { fprintf(stderr, "compositor offers no org_kde_kwin_fake_input\n"); return 4; }
    if (fake_version < 4) { fprintf(stderr, "fake_input v%u has no keyboard_key\n", fake_version); return 5; }

    org_kde_kwin_fake_input_authenticate(fake, "kmixdeck-test", "CT-1 shortcut test");
    for (int i = 1; i < argc; i++)
        org_kde_kwin_fake_input_keyboard_key(fake, (uint32_t)atoi(argv[i]), WL_KEYBOARD_KEY_STATE_PRESSED);
    wl_display_roundtrip(dpy);
    for (int i = argc - 1; i >= 1; i--)
        org_kde_kwin_fake_input_keyboard_key(fake, (uint32_t)atoi(argv[i]), WL_KEYBOARD_KEY_STATE_RELEASED);
    wl_display_roundtrip(dpy);
    wl_display_disconnect(dpy);
    printf("sent %d keys via fake_input v%u\n", argc - 1, fake_version);
    return 0;
}
