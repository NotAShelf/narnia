#include "clipboard.h"
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef HAVE_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#ifdef HAVE_WAYLAND
#include <sys/epoll.h>
#include <sys/mman.h>
#include <wayland-client.h>
#endif

typedef enum {
  CLIPBOARD_NONE,
  CLIPBOARD_X11,
  CLIPBOARD_WAYLAND
} clipboard_type_t;

static clipboard_type_t clipboard_type = CLIPBOARD_NONE;

#ifdef HAVE_X11
static Display *x11_display = NULL;
static Window x11_window;
static Atom xa_clipboard, xa_targets, xa_string, xa_utf8_string, xa_text;
static char *x11_clipboard_data = NULL;
static int x11_running = 0;

static int x11_handle_selection_request(XEvent *event) {
  XSelectionRequestEvent *req = &event->xselectionrequest;
  XSelectionEvent notify;

  notify.type = SelectionNotify;
  notify.requestor = req->requestor;
  notify.selection = req->selection;
  notify.target = req->target;
  notify.property = req->property;
  notify.time = req->time;

  if (req->target == xa_targets) {
    Atom targets[] = {xa_targets, xa_string, xa_utf8_string, xa_text};
    XChangeProperty(x11_display, req->requestor, req->property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)targets, 4);
  } else if (req->target == xa_string || req->target == xa_utf8_string ||
             req->target == xa_text) {
    if (x11_clipboard_data) {
      XChangeProperty(x11_display, req->requestor, req->property, req->target,
                      8, PropModeReplace, (unsigned char *)x11_clipboard_data,
                      strlen(x11_clipboard_data));
    } else {
      notify.property = None;
    }
  } else {
    notify.property = None;
  }

  XSendEvent(x11_display, req->requestor, False, 0, (XEvent *)&notify);
  XFlush(x11_display);
  return 0;
}

static void x11_run_event_loop(void) {
  XEvent event;
  int x11_fd = ConnectionNumber(x11_display);
  fd_set read_fds;
  struct timeval timeout;

  x11_running = 1;

  while (x11_running) {
    FD_ZERO(&read_fds);
    FD_SET(x11_fd, &read_fds);

    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    int result = select(x11_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (result > 0 && FD_ISSET(x11_fd, &read_fds)) {
      while (XPending(x11_display)) {
        XNextEvent(x11_display, &event);

        switch (event.type) {
        case SelectionRequest:
          x11_handle_selection_request(&event);
          break;
        case SelectionClear:
          x11_running = 0;
          break;
        }
      }
    } else if (result == 0) {
      break;
    }
  }
}
#endif

#ifdef HAVE_WAYLAND
static struct wl_display *wl_display = NULL;
static struct wl_registry *wl_registry = NULL;
static struct wl_data_device_manager *data_device_manager = NULL;
static struct wl_seat *seat = NULL;
static struct wl_data_device *data_device = NULL;
static struct wl_data_source *data_source = NULL;
static char *wayland_clipboard_data = NULL;
static int wayland_running = 0;

static void data_source_target(void *data, struct wl_data_source *source,
                               const char *mime_type) {
  (void)data;
  (void)source;
  (void)mime_type;
}

static void data_source_send(void *data, struct wl_data_source *source,
                             const char *mime_type, int32_t fd) {
  (void)data;
  (void)source;

  if (wayland_clipboard_data &&
      (strcmp(mime_type, "text/plain") == 0 ||
       strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
       strcmp(mime_type, "TEXT") == 0 || strcmp(mime_type, "STRING") == 0)) {

    size_t len = strlen(wayland_clipboard_data);
    write(fd, wayland_clipboard_data, len);
  }
  close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source) {
  (void)data;
  wayland_running = 0;
  if (source == data_source) {
    data_source = NULL;
  }
  wl_data_source_destroy(source);
}

static void data_source_dnd_drop_performed(void *data,
                                           struct wl_data_source *source) {
  (void)data;
  (void)source;
}

static void data_source_dnd_finished(void *data,
                                     struct wl_data_source *source) {
  (void)data;
  (void)source;
}

static void data_source_action(void *data, struct wl_data_source *source,
                               uint32_t dnd_action) {
  (void)data;
  (void)source;
  (void)dnd_action;
}

static const struct wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished = data_source_dnd_finished,
    .action = data_source_action,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities) {
  (void)data;
  (void)seat;
  (void)capabilities;
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version) {
  (void)data;

  if (strcmp(interface, "wl_data_device_manager") == 0) {
    data_device_manager =
        wl_registry_bind(registry, id, &wl_data_device_manager_interface,
                         version >= 3 ? 3 : version);
  } else if (strcmp(interface, "wl_seat") == 0) {
    seat = wl_registry_bind(registry, id, &wl_seat_interface,
                            version >= 2 ? 2 : version);
    wl_seat_add_listener(seat, &seat_listener, NULL);
  }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t id) {
  (void)data;
  (void)registry;
  (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void wayland_run_event_loop(void) {
  int wl_fd = wl_display_get_fd(wl_display);
  struct pollfd pfd;

  wayland_running = 1;

  while (wayland_running) {
    while (wl_display_prepare_read(wl_display) != 0) {
      wl_display_dispatch_pending(wl_display);
    }

    wl_display_flush(wl_display);

    pfd.fd = wl_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    if (poll(&pfd, 1, 100) > 0) {
      wl_display_read_events(wl_display);
      wl_display_dispatch_pending(wl_display);
    } else {
      wl_display_cancel_read(wl_display);
      break;
    }
  }
}
#endif

int clipboard_init(void) {
  const char *wayland_display = getenv("WAYLAND_DISPLAY");
  const char *x11_display_env = getenv("DISPLAY");

#ifdef HAVE_WAYLAND
  if (wayland_display) {
    wl_display = wl_display_connect(NULL);
    if (wl_display) {
      wl_registry = wl_display_get_registry(wl_display);
      if (wl_registry) {
        wl_registry_add_listener(wl_registry, &registry_listener, NULL);
        wl_display_roundtrip(wl_display);

        if (data_device_manager && seat) {
          data_device =
              wl_data_device_manager_get_data_device(data_device_manager, seat);
          if (data_device) {
            clipboard_type = CLIPBOARD_WAYLAND;
            return 0;
          }
        }
      }
      wl_display_disconnect(wl_display);
      wl_display = NULL;
    }
  }
#endif

#ifdef HAVE_X11
  if (x11_display_env) {
    x11_display = XOpenDisplay(NULL);
    if (x11_display) {
      int screen = DefaultScreen(x11_display);
      x11_window = XCreateSimpleWindow(
          x11_display, RootWindow(x11_display, screen), 0, 0, 1, 1, 0,
          BlackPixel(x11_display, screen), WhitePixel(x11_display, screen));

      XSelectInput(x11_display, x11_window, PropertyChangeMask);

      xa_clipboard = XInternAtom(x11_display, "CLIPBOARD", False);
      xa_targets = XInternAtom(x11_display, "TARGETS", False);
      xa_string = XInternAtom(x11_display, "STRING", False);
      xa_utf8_string = XInternAtom(x11_display, "UTF8_STRING", False);
      xa_text = XInternAtom(x11_display, "TEXT", False);

      if (xa_clipboard != None) {
        clipboard_type = CLIPBOARD_X11;
        return 0;
      }
      XDestroyWindow(x11_display, x11_window);
      XCloseDisplay(x11_display);
      x11_display = NULL;
    }
  }
#endif

  clipboard_type = CLIPBOARD_NONE;
  return -1;
}

void clipboard_cleanup(void) {
#ifdef HAVE_WAYLAND
  if (clipboard_type == CLIPBOARD_WAYLAND) {
    wayland_running = 0;
    if (wayland_clipboard_data) {
      free(wayland_clipboard_data);
      wayland_clipboard_data = NULL;
    }
    if (data_source) {
      wl_data_source_destroy(data_source);
      data_source = NULL;
    }
    if (data_device) {
      wl_data_device_destroy(data_device);
      data_device = NULL;
    }
    if (seat) {
      wl_seat_destroy(seat);
      seat = NULL;
    }
    if (data_device_manager) {
      wl_data_device_manager_destroy(data_device_manager);
      data_device_manager = NULL;
    }
    if (wl_registry) {
      wl_registry_destroy(wl_registry);
      wl_registry = NULL;
    }
    if (wl_display) {
      wl_display_disconnect(wl_display);
      wl_display = NULL;
    }
  }
#endif

#ifdef HAVE_X11
  if (clipboard_type == CLIPBOARD_X11) {
    x11_running = 0;
    if (x11_clipboard_data) {
      free(x11_clipboard_data);
      x11_clipboard_data = NULL;
    }
    if (x11_display) {
      XDestroyWindow(x11_display, x11_window);
      XCloseDisplay(x11_display);
      x11_display = NULL;
    }
  }
#endif

  clipboard_type = CLIPBOARD_NONE;
}

int clipboard_set_text(const char *text) {
  if (!text)
    return -1;

#ifdef HAVE_WAYLAND
  if (clipboard_type == CLIPBOARD_WAYLAND) {
    if (wayland_clipboard_data) {
      free(wayland_clipboard_data);
    }
    wayland_clipboard_data = strdup(text);
    if (!wayland_clipboard_data)
      return -1;

    if (data_source) {
      wl_data_source_destroy(data_source);
    }

    data_source =
        wl_data_device_manager_create_data_source(data_device_manager);
    if (!data_source)
      return -1;

    wl_data_source_add_listener(data_source, &data_source_listener, NULL);
    wl_data_source_offer(data_source, "text/plain");
    wl_data_source_offer(data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(data_source, "TEXT");
    wl_data_source_offer(data_source, "STRING");
    wl_data_source_offer(data_source, "UTF8_STRING");

    wl_data_device_set_selection(data_device, data_source, 0);
    wl_display_flush(wl_display);

    wayland_run_event_loop();

    return 0;
  }
#endif

#ifdef HAVE_X11
  if (clipboard_type == CLIPBOARD_X11) {
    if (x11_clipboard_data) {
      free(x11_clipboard_data);
    }
    x11_clipboard_data = strdup(text);
    if (!x11_clipboard_data)
      return -1;

    XSetSelectionOwner(x11_display, xa_clipboard, x11_window, CurrentTime);
    if (XGetSelectionOwner(x11_display, xa_clipboard) != x11_window) {
      return -1;
    }

    XFlush(x11_display);
    x11_run_event_loop();

    return 0;
  }
#endif

  return -1;
}
