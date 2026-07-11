#include "dock.h"
#include "dbus_server.h"
#include "config.h"
#include "window_monitor.h"
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

static Dock *app_dock = NULL;

static gboolean on_signal_quit(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void kill_existing_instance(void) {
    GError *err = NULL;
    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!conn) return;

    GVariant *result = g_dbus_connection_call_sync(conn,
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixProcessID",
        g_variant_new("(s)", MOLA_DBUS_NAME),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &err);

    if (result) {
        guint32 pid = 0;
        g_variant_get(result, "(u)", &pid);
        g_variant_unref(result);

        if (pid > 0 && pid != (guint32)getpid()) {
            g_print("MoLa: Killing existing instance (PID %u)\n", pid);
            kill((pid_t)pid, SIGTERM);
            g_usleep(200000);
        }
    }

    if (err) g_error_free(err);
    g_object_unref(conn);
}

/* ── load pinned apps from config (or use defaults) ───────────────── */

static void load_pinned_apps(Dock *dock) {
    GList *pinned = config_load_pinned();

    if (!pinned) {
        /* First run: populate with sensible defaults */
        struct {
            const char *id;
            const char *name;
            const char *icon;
            const char *desktop;
        } defaults[] = {
            {"falkon",     "Falkon",       "falkon",                "com.kde.falkon.desktop"},
            {"xfce4-terminal", "Xfce Terminal", "org.xfce.terminal", "xfce4-terminal.desktop"},
            {"pcmanfm",    "PCMan File Manager",  "system-file-manager",     "pcmanfm.desktop"},
        };

        int n = (int)(sizeof(defaults) / sizeof(defaults[0]));
        for (int i = 0; i < n; i++) {
            dock_add_item(dock,
                          defaults[i].id,
                          defaults[i].name,
                          defaults[i].icon,
                          defaults[i].desktop,
                          true,    /* pinned */
                          false);  /* not running yet */
        }

        /* Persist the defaults so next launch reads from config */
        config_save_pinned(dock->items);
        return;
    }

    /* Load from config file */
    for (GList *l = pinned; l; l = l->next) {
        PinnedAppInfo *info = l->data;
        dock_add_item(dock,
                      info->app_id,
                      info->display_name,
                      info->icon_name,
                      info->desktop_file,
                      true,    /* pinned */
                      false);  /* running state set by window monitor */
    }

    config_free_pinned_list(pinned);
}

/* ── pointer polling for auto-show ────────────────────────────────── */

static gboolean pointer_poll(gpointer data) {
    Dock *dock = (Dock *)data;
    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
    if (!seat) return G_SOURCE_CONTINUE;

    GdkDevice *device = gdk_seat_get_pointer(seat);
    if (!device) return G_SOURCE_CONTINUE;

    int mx = 0, my = 0;
    gdk_device_get_position(device, NULL, &mx, &my);

    if (dock->is_hidden && !dock->is_animating) {
        if (my >= dock->screen_height - DOCK_TRIGGER_ZONE_PX) {
            dock_show(dock);
        }
    }

    return G_SOURCE_CONTINUE;
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    gtk_init(NULL, NULL);

    g_unix_signal_add(SIGINT,  on_signal_quit, NULL);
    g_unix_signal_add(SIGTERM, on_signal_quit, NULL);

    kill_existing_instance();

    app_dock = dock_new();
    if (!app_dock) {
        g_printerr("MoLa: Failed to create dock\n");
        return 1;
    }

    load_pinned_apps(app_dock);
    dock_recalc_size(app_dock);
    gtk_widget_show_all(app_dock->window);

    /* Auto-show when pointer hits the bottom edge */
    g_timeout_add(50, pointer_poll, app_dock);

    /* D-Bus interface */
    dbus_server_init(app_dock);

    /* Start monitoring running GUI windows */
    window_monitor_start(app_dock);

    g_print("MoLa Desktop Dock v2.0.0\n");
    g_print("D-Bus: %s @ %s\n", MOLA_DBUS_NAME, MOLA_DBUS_PATH);
    g_print("Config: %s\n", config_get_path());

    gtk_main();

    window_monitor_stop();
    dbus_server_stop();
    dock_free(app_dock);

    return 0;
}
