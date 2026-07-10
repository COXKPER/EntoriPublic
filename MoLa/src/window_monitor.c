#include "window_monitor.h"
#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gio/gdesktopappinfo.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── state ────────────────────────────────────────────────────────── */

static guint   poll_timer_id = 0;
static Dock   *monitor_dock  = NULL;

/* ── helpers ──────────────────────────────────────────────────────── */

/* Read _NET_CLIENT_LIST from root window.
 * Caller must XFree() the returned array. Returns count via *out_n. */
static Window *get_client_list(Display *dpy, unsigned long *out_n) {
    Atom actual_type;
    int  actual_fmt;
    unsigned long n_items = 0, bytes_left = 0;
    unsigned char *data = NULL;

    Atom net_client = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), net_client,
                           0, 4096, False, XA_WINDOW,
                           &actual_type, &actual_fmt, &n_items,
                           &bytes_left, &data) != Success || !data) {
        *out_n = 0;
        return NULL;
    }
    *out_n = n_items;
    return (Window *)data;
}

/* Get WM_CLASS (res_name) for a window.  Caller must g_free(). */
static char *get_wm_class(Display *dpy, Window w) {
    XClassHint hint;
    memset(&hint, 0, sizeof(hint));
    if (!XGetClassHint(dpy, w, &hint)) return NULL;

    char *result = NULL;
    if (hint.res_class)
        result = g_strdup(hint.res_class);
    else if (hint.res_name)
        result = g_strdup(hint.res_name);

    if (hint.res_name)  XFree(hint.res_name);
    if (hint.res_class) XFree(hint.res_class);
    return result;
}

/* Get _NET_WM_PID for a window. Returns 0 on failure. */
static pid_t get_window_pid(Display *dpy, Window w) {
    Atom actual_type;
    int  actual_fmt;
    unsigned long n_items, bytes_left;
    unsigned char *data = NULL;

    Atom net_pid = XInternAtom(dpy, "_NET_WM_PID", False);
    if (XGetWindowProperty(dpy, w, net_pid, 0, 1, False, XA_CARDINAL,
                           &actual_type, &actual_fmt, &n_items,
                           &bytes_left, &data) != Success || !data)
        return 0;

    pid_t pid = (pid_t)(*(unsigned long *)data);
    XFree(data);
    return pid;
}

/* Check if window has a type we care about (normal window).
 * Skip docks, desktops, splashes, etc. */
static bool is_normal_window(Display *dpy, Window w) {
    Atom actual_type;
    int  actual_fmt;
    unsigned long n_items, bytes_left;
    unsigned char *data = NULL;

    Atom wm_type     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom type_normal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    Atom type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);

    if (XGetWindowProperty(dpy, w, wm_type, 0, 32, False, XA_ATOM,
                           &actual_type, &actual_fmt, &n_items,
                           &bytes_left, &data) != Success || !data) {
        /* No type hint → assume normal */
        return true;
    }

    Atom *types = (Atom *)data;
    bool dominated_by_normal = false;
    for (unsigned long i = 0; i < n_items; i++) {
        if (types[i] == type_normal || types[i] == type_dialog) {
            dominated_by_normal = true;
            break;
        }
    }
    XFree(data);
    /* If no recognized type is found, treat it as normal */
    return dominated_by_normal || (n_items == 0);
}


/* ── desktop file lookup ──────────────────────────────────────────── */

typedef struct {
    char *app_id;        /* normalised wm_class (lower) */
    char *display_name;
    char *icon_name;
    char *desktop_file;  /* basename, e.g. "firefox.desktop" */
} AppInfo;

/* Try to find a .desktop file matching |wm_class|.
 * Strategy: iterate all GDesktopAppInfo entries and match
 * StartupWMClass or desktop-id prefix. */
static AppInfo *lookup_desktop(const char *wm_class) {
    if (!wm_class || !*wm_class) return NULL;

    char *wm_lower = g_ascii_strdown(wm_class, -1);
    GList *all_apps = g_app_info_get_all();
    AppInfo *result = NULL;

    for (GList *l = all_apps; l && !result; l = l->next) {
        GAppInfo *ai = l->data;
        if (!G_IS_DESKTOP_APP_INFO(ai)) continue;
        GDesktopAppInfo *dai = G_DESKTOP_APP_INFO(ai);

        const char *startup_wm = g_desktop_app_info_get_startup_wm_class(dai);
        const char *desktop_id = g_app_info_get_id(ai);

        bool match = false;

        /* Match by StartupWMClass */
        if (startup_wm) {
            char *sw_lower = g_ascii_strdown(startup_wm, -1);
            if (g_strcmp0(sw_lower, wm_lower) == 0) match = true;
            g_free(sw_lower);
        }

        /* Match by desktop-id prefix (e.g. "firefox.desktop" ↔ "firefox") */
        if (!match && desktop_id) {
            char *id_lower = g_ascii_strdown(desktop_id, -1);
            /* strip ".desktop" */
            char *dot = strrchr(id_lower, '.');
            if (dot) *dot = '\0';
            /* strip "org.gnome." etc. prefixes – compare last component */
            char *last_dot = strrchr(id_lower, '.');
            const char *short_id = last_dot ? last_dot + 1 : id_lower;
            if (g_strcmp0(short_id, wm_lower) == 0) match = true;
            g_free(id_lower);
        }

        if (match) {
            result = g_new0(AppInfo, 1);
            result->app_id       = g_strdup(wm_lower);
            result->display_name = g_strdup(g_app_info_get_display_name(ai));
            GIcon *gicon = g_app_info_get_icon(ai);
            if (gicon) {
                result->icon_name = g_icon_to_string(gicon);
            } else {
                result->icon_name = g_strdup("application-x-executable");
            }
            result->desktop_file = g_strdup(desktop_id);
        }
    }

    /* Fallback: no .desktop match – use wm_class directly */
    if (!result) {
        result = g_new0(AppInfo, 1);
        result->app_id       = g_strdup(wm_lower);
        result->display_name = g_strdup(wm_class);   /* keep original case */
        result->icon_name    = g_strdup(wm_lower);    /* try as icon name  */
        result->desktop_file = NULL;
    }

    g_free(wm_lower);
    g_list_free_full(all_apps, g_object_unref);
    return result;
}

static void app_info_free(AppInfo *ai) {
    if (!ai) return;
    g_free(ai->app_id);
    g_free(ai->display_name);
    g_free(ai->icon_name);
    g_free(ai->desktop_file);
    g_free(ai);
}

/* ── scan & reconcile ─────────────────────────────────────────────── */

static gboolean on_poll(gpointer data) {
    Dock *dock = (Dock *)data;

    GdkDisplay *gdpy = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gdpy)) return G_SOURCE_CONTINUE;

    Display *dpy = gdk_x11_display_get_xdisplay(gdpy);
    unsigned long n_windows = 0;
    Window *windows = get_client_list(dpy, &n_windows);
    if (!windows && n_windows == 0) return G_SOURCE_CONTINUE;

    /* Build a hash table of running apps:  app_id → AppInfo* */
    GHashTable *running = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                 NULL, NULL);

    /* Also keep xid/pid per app_id */
    typedef struct { guint32 xid; pid_t pid; AppInfo *info; } RunEntry;
    GList *entries = NULL;

    for (unsigned long i = 0; i < n_windows; i++) {
        if (!is_normal_window(dpy, windows[i])) continue;

        char *wm_class = get_wm_class(dpy, windows[i]);
        if (!wm_class) continue;

        char *key = g_ascii_strdown(wm_class, -1);

        /* Skip our own dock window */
        if (g_strcmp0(key, "mola desktop dock") == 0 ||
            g_strcmp0(key, "mola-desktop") == 0) {
            g_free(wm_class);
            g_free(key);
            continue;
        }

        if (!g_hash_table_contains(running, key)) {
            AppInfo *ai  = lookup_desktop(wm_class);
            RunEntry *re = g_new0(RunEntry, 1);
            re->xid  = (guint32)windows[i];
            re->pid  = get_window_pid(dpy, windows[i]);
            re->info = ai;
            g_hash_table_insert(running, ai->app_id, re);
            entries = g_list_append(entries, re);
        }

        g_free(wm_class);
        g_free(key);
    }
    if (windows) XFree(windows);

    /* ── Reconcile with dock ──────────────────────────────────────── */

    /* 1) Walk existing dock items – update running state or remove */
    GList *to_remove = NULL;
    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        RunEntry *re = g_hash_table_lookup(running, item->app_id);

        if (re) {
            /* App is running – update */
            if (!item->running)
                dock_set_item_running(dock, item->app_id, true);
            item->xid = re->xid;
            item->pid = re->pid;
            g_hash_table_remove(running, item->app_id);
        } else {
            /* App is NOT running */
            if (item->pinned) {
                if (item->running) {
                    dock_set_item_running(dock, item->app_id, false);
                    item->xid = 0;
                    item->pid = 0;
                }
            } else {
                to_remove = g_list_append(to_remove, g_strdup(item->app_id));
            }
        }
    }

    /* Remove non-pinned items that are no longer running */
    for (GList *l = to_remove; l; l = l->next) {
        dock_remove_item(dock, (char *)l->data);
        g_free(l->data);
    }
    g_list_free(to_remove);

    /* 2) Add newly discovered running apps (not yet on dock) */
    GHashTableIter iter;
    gpointer hkey, hval;
    g_hash_table_iter_init(&iter, running);
    while (g_hash_table_iter_next(&iter, &hkey, &hval)) {
        RunEntry *re = hval;
        DockItem *added = dock_add_item(dock, re->info->app_id,
                                        re->info->display_name,
                                        re->info->icon_name,
                                        re->info->desktop_file,
                                        false, true);
        if (added) {
            added->xid = re->xid;
            added->pid = re->pid;
        }
    }

    /* Cleanup */
    for (GList *l = entries; l; l = l->next) {
        RunEntry *re = l->data;
        app_info_free(re->info);
        g_free(re);
    }
    g_list_free(entries);
    g_hash_table_destroy(running);

    return G_SOURCE_CONTINUE;
}

/* ── public ───────────────────────────────────────────────────────── */

void window_monitor_start(Dock *dock) {
    if (poll_timer_id) return;
    monitor_dock  = dock;
    /* Do an immediate first scan, then every 2 seconds */
    on_poll(dock);
    poll_timer_id = g_timeout_add(2000, on_poll, dock);
}

void window_monitor_stop(void) {
    if (poll_timer_id) {
        g_source_remove(poll_timer_id);
        poll_timer_id = 0;
    }
    monitor_dock = NULL;
}
