#include <cairo/cairo.h>
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

#define PADDING 15

#define WINW 400
#define WINH 600

#define PANELH WINW / 3
#define PANELW WINW - PADDING * 2

#define DEGREES 3.14159265358979323846 / 180.0

#define SQUARE_COLOR 0.3137
#define SQUARE_RADIUS 8

typedef struct {
        void *shm_data;
        struct wl_surface *surface;
        struct xdg_toplevel *xdg_toplevel;
        struct wl_shm *shm;
        struct wl_compositor *compositor;
        struct xdg_wm_base *xdg_wm_base;
        struct wl_registry *registry;
        struct wl_buffer *buffer;
        struct wl_seat *seat;
        bool configured;
        bool exit;
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

void draw(state *state);

static void xdg_surface_handle_configure(void *data,
                                         struct xdg_surface *xdg_surface,
                                         uint32_t serial) {
    state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    draw(state);
    state->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_close(void *data,
                                      struct xdg_toplevel *xdg_toplevel) {
    state *state = data;
    state->exit = true;
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
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
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

    return buffer;
}

void create_rounded_rect(cairo_t *context, int width, int height, int x, int y,
                         const char *text, int font_size) {
    double aspect = 1.0, corner_radius = (double)height / SQUARE_RADIUS;
    double radius = corner_radius / aspect;

    cairo_new_sub_path(context);
    cairo_arc(context, x + width - radius, y + radius, radius, -90 * DEGREES,
              0 * DEGREES);
    cairo_arc(context, x + width - radius, y + height - radius, radius,
              0 * DEGREES, 90 * DEGREES);
    cairo_arc(context, x + radius, y + height - radius, radius, 90 * DEGREES,
              180 * DEGREES);
    cairo_arc(context, x + radius, y + radius, radius, 180 * DEGREES,
              270 * DEGREES);
    cairo_close_path(context);

    cairo_set_source_rgb(context, SQUARE_COLOR, SQUARE_COLOR, SQUARE_COLOR);
    cairo_fill_preserve(context);

    cairo_text_extents_t extents;
    cairo_text_extents(context, text, &extents);

    int center_x = x + (width >> 1);
    int center_y = y + (height >> 1);

    double text_x = center_x - (extents.width / 2) - extents.x_bearing;
    double text_y = center_y - (extents.height / 2) - extents.y_bearing;

    cairo_move_to(context, text_x, text_y);
    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_set_font_size(context, font_size);
    cairo_show_text(context, text);
}

static const char *icons[20] = {"%", "C", "CE", "/", "7", "8", "9",
                                "*", "4", "5",  "6", "-", "1", "2",
                                "3", "+", " ",  "0", ".", "="};

static void output_frame_handle_done(void *data, struct wl_callback *callback,
                                     unsigned int time) {
    state *state = data;
    draw(state);
}

static struct wl_callback_listener output_frame_listener = {
  .done = output_frame_handle_done,
};

void draw(state *state) {
    struct wl_callback *frame_callback = wl_surface_frame(state->surface);
    wl_callback_add_listener(frame_callback, &output_frame_listener, state);

    wl_surface_attach(state->surface, state->buffer, 0, 0);
    wl_surface_commit(state->surface);
}

int main() {
    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return 1;
    }

    state state = {
      .registry = wl_display_get_registry(display),
      .exit = false,
      .configured = false,
      .width = WINW,
      .height = WINH,
    };

    wl_registry_add_listener(state.registry, &registry_listener, &state);
    if (wl_display_roundtrip(display) == -1) {
        return 1;
    }

    if (state.shm == NULL || state.compositor == NULL ||
        state.xdg_wm_base == NULL) {
        return 1;
    }

    state.buffer = create_buffer(&state);
    if (state.buffer == NULL) {
        return 1;
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

    cairo_surface_t *surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, state.width, state.height);
    cairo_t *context = cairo_create(surface);
    cairo_set_source_rgb(context, 0, 0, 0);
    cairo_paint_with_alpha(context, 1);

    create_rounded_rect(context, PANELW, PANELH, PADDING, PADDING, icons[16],
                        16);

    memcpy(state.shm_data, cairo_image_surface_get_data(surface),
           state.height * state.width * 4);

    while (wl_display_dispatch(display) != -1 && !state.exit) {
    }

    xdg_toplevel_destroy(state.xdg_toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(state.surface);
    wl_buffer_destroy(state.buffer);

    return EXIT_SUCCESS;
}
