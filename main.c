#include <stdio.h>
#include <string.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "wayland-client-core.h"
#include "xdg-shell-client-protocol.h"

struct State {
        void *wl_compositor;
        void *xdg_wm_base;
        void *wl_shm;
        void *wl_seat;
        struct wl_registry *registry;
};

static void noop() {}

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          unsigned int version) {
    struct State *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor =
          wl_registry_bind(registry, name, &wl_compositor_interface, version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm =
          wl_registry_bind(registry, name, &wl_shm_interface, version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base =
          wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->wl_seat =
          wl_registry_bind(registry, name, &wl_seat_interface, version);
    }
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = noop,
};

int main() {
    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return 1;
    }

    struct State state = {.registry = wl_display_get_registry(display)};

    wl_registry_add_listener(state.registry, &registry_listener, &state);

    if (wl_display_roundtrip(display) == -1) {
        fprintf(stderr, "failed to create display\n");
        return 1;
    };
    while (wl_display_dispatch(display) != -1) {
    }

    return 0;
}
