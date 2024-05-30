#include <stdio.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "wayland-client-core.h"

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {}

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          unsigned int version) {}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = handle_global_remove,
};

int main() {
    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    while (wl_display_dispatch(display) != -1) {
    }

    return 0;
}
