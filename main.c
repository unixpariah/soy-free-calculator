#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "shm.h"
#include "xdg-shell-client-protocol.h"

typedef struct {
        void *shm_data;
        struct wl_surface *surface;
        struct xdg_toplevel *xdg_toplevel;
        struct wl_shm *shm;
        struct wl_compositor *compositor;
        struct xdg_wm_base *xdg_wm_base;
        struct wl_registry *registry;
        bool configured;
        bool running;
        int width;
        int height;
} state;

static void noop() {}

static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                                    uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data,
                                         struct xdg_surface *xdg_surface,
                                         uint32_t serial) {
    state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (state->configured) {
        wl_surface_commit(state->surface);
    }
    state->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_close(void *data,
                                      struct xdg_toplevel *xdg_toplevel) {
    state *state = data;
    state->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = noop,
  .close = xdg_toplevel_handle_close,
};

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
    state *state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        // struct wl_seat *seat =
        //   wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor =
          wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base =
          wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener,
                                 NULL);
    }
}

static const struct wl_registry_listener registry_listener = {
  .global = handle_global,
  .global_remove = noop,
};

static struct wl_buffer *create_buffer(state *state) {
    int stride = state->width * 4;
    int size = stride * state->height;

    int fd = create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
        return NULL;
    }

    state->shm_data =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (state->shm_data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
      pool, 0, state->width, state->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    close(fd);

    memset(state->shm_data, 255, size);

    return buffer;
}

int main() {
    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return EXIT_FAILURE;
    }

    state state = {
      .registry = wl_display_get_registry(display),
      .running = true,
      .configured = false,
      .width = 128,
      .height = 128,
    };

    wl_registry_add_listener(state.registry, &registry_listener, &state);
    if (wl_display_roundtrip(display) == -1) {
        return EXIT_FAILURE;
    }

    if (state.shm == NULL || state.compositor == NULL ||
        state.xdg_wm_base == NULL) {
        fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
        return EXIT_FAILURE;
    }

    state.surface = wl_compositor_create_surface(state.compositor);
    struct xdg_surface *xdg_surface =
      xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &state);
    xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener,
                              &state);

    wl_surface_commit(state.surface);
    while (wl_display_dispatch(display) != -1 && !state.configured) {
    }

    struct wl_buffer *buffer = create_buffer(&state);
    if (buffer == NULL) {
        return EXIT_FAILURE;
    }

    wl_surface_attach(state.surface, buffer, 0, 0);
    wl_surface_commit(state.surface);

    while (wl_display_dispatch(display) != -1 && state.running) {
    }

    xdg_toplevel_destroy(state.xdg_toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(state.surface);
    wl_buffer_destroy(buffer);

    return EXIT_SUCCESS;
}
