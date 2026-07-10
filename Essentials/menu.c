#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <libwnck/libwnck.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sni_host.h"

#define DOCK_DIR  ".config/favorit-anak-sd"
#define DOCK_FILE "here.desktop"

static const char *app_dirs[] = {
    "/usr/share/applications",
    "/usr/local/share/applications",
    NULL
};

static GtkWidget *app_label;
static GtkWidget *clock_label;
static GtkWidget *media_label;
static GtkWidget *battery_label;
static GtkWidget *network_btn;
static GtkWidget *volume_btn;
static GtkWidget *tray_box;
static WnckScreen *wnck_screen;
static char *current_wm_class;
static char *current_app_name;

/* ---- Dock helpers ---- */

static char *dock_path(void)
{
    const char *home = getenv("HOME");
    if (!home) home = "/root";
    return g_strdup_printf("%s/%s/%s", home, DOCK_DIR, DOCK_FILE);
}

static GPtrArray *read_dock(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    char *path = dock_path();
    FILE *f = fopen(path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            if (l && line[l-1] == '\n') line[l-1] = '\0';
            if (line[0]) g_ptr_array_add(arr, g_strdup(line));
        }
        fclose(f);
    }
    g_free(path);
    return arr;
}

static void write_dock(GPtrArray *arr)
{
    char *path = dock_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (guint i = 0; i < arr->len; i++)
            fprintf(f, "%s\n", (char *)g_ptr_array_index(arr, i));
        fclose(f);
    }
    g_free(path);
}

static gboolean in_dock(GPtrArray *arr, const char *name)
{
    for (guint i = 0; i < arr->len; i++)
        if (!g_strcmp0((char *)g_ptr_array_index(arr, i), name))
            return TRUE;
    return FALSE;
}

/* ---- Desktop file helpers ---- */

static char *desktop_key(const char *path, const char *key)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char line[512];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            char *v = line + klen + 1;
            size_t l = strlen(v);
            if (l && v[l-1] == '\n') v[l-1] = '\0';
            fclose(f);
            return g_strdup(v);
        }
    }
    fclose(f);
    return NULL;
}

static char *find_desktop_for(const char *wm_class, const char *app_name)
{
    for (int di = 0; app_dirs[di]; di++) {
        GDir *dir = g_dir_open(app_dirs[di], 0, NULL);
        if (!dir) continue;
        const char *fname;
        while ((fname = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(fname, ".desktop")) continue;
            char *fpath = g_build_filename(app_dirs[di], fname, NULL);

            if (wm_class) {
                char *val = desktop_key(fpath, "StartupWMClass");
                if (val && g_ascii_strcasecmp(val, wm_class) == 0) {
                    g_free(val); g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(val);
            }

            if (app_name) {
                char *val = desktop_key(fpath, "Name");
                if (val && g_ascii_strcasecmp(val, app_name) == 0) {
                    g_free(val); g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(val);
                char *lower = g_ascii_strdown(app_name, -1);
                char *lower_fname = g_ascii_strdown(fname, -1);
                char *dash = g_strdelimit(g_strdup(lower), " ", '-');
                if (strstr(lower_fname, dash)) {
                    g_free(dash); g_free(lower_fname); g_free(lower);
                    g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(dash); g_free(lower_fname); g_free(lower);
            }
            g_free(fpath);
        }
        g_dir_close(dir);
    }
    return NULL;
}

/* ---- Logo menu callbacks ---- */

static void launch_terminal(void)   { g_spawn_command_line_async("x-terminal-emulator", NULL); }
static void launch_files(void)
{
    const char *home = getenv("HOME");
    if (!home) home = "/";
    char *cmd = g_strdup_printf("xdg-open %s", home);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}
static void launch_run(void)        { g_spawn_command_line_async("rofi -show drun", NULL); }
static void launch_power(void)      { g_spawn_command_line_async("/usr/essentials/options", NULL); }
static void launch_about(void)      { g_spawn_command_line_async("/usr/essentials/about", NULL); }

static gboolean on_logo_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    if (ev->button != 1) return FALSE;

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *run_i = gtk_menu_item_new_with_label("Run");
    GtkWidget *power_i = gtk_menu_item_new_with_label("Power");
    GtkWidget *files_i = gtk_menu_item_new_with_label("File Explorer");
    GtkWidget *term_i = gtk_menu_item_new_with_label("Shell Prompt");
    GtkWidget *about_i = gtk_menu_item_new_with_label("About Your Device");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), run_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), files_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), term_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), power_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about_i);

    g_signal_connect(term_i, "activate", G_CALLBACK(launch_terminal), NULL);
    g_signal_connect(files_i, "activate", G_CALLBACK(launch_files), NULL);
    g_signal_connect(run_i, "activate", G_CALLBACK(launch_run), NULL);
    g_signal_connect(power_i, "activate", G_CALLBACK(launch_power), NULL);
    g_signal_connect(about_i, "activate", G_CALLBACK(launch_about), NULL);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
    return TRUE;
}

/* ---- Dock toggle callback ---- */

static void toggle_dock(GtkWidget *mi, gpointer data)
{
    const char *desktop = (const char *)data;
    GPtrArray *entries = read_dock();
    if (in_dock(entries, desktop)) {
        GPtrArray *new_e = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < entries->len; i++) {
            if (g_strcmp0((char *)g_ptr_array_index(entries, i), desktop) != 0)
                g_ptr_array_add(new_e, g_strdup((char *)g_ptr_array_index(entries, i)));
        }
        write_dock(new_e);
        g_ptr_array_unref(new_e);
    } else {
        g_ptr_array_add(entries, g_strdup(desktop));
        write_dock(entries);
    }
    g_ptr_array_unref(entries);
}

/* ---- Active window tracking ---- */

static void update_active(void)
{
    WnckWindow *win = wnck_screen_get_active_window(wnck_screen);
    if (win) {
        WnckApplication *app = wnck_window_get_application(win);
        g_free(current_wm_class);
        current_wm_class = g_strdup(wnck_window_get_class_instance_name(win));
        g_free(current_app_name);
        if (app)
            current_app_name = g_strdup(wnck_application_get_name(app));
        else
            current_app_name = g_strdup(wnck_window_get_name(win));
        gtk_label_set_text(GTK_LABEL(app_label), current_app_name);
    } else {
        gtk_label_set_text(GTK_LABEL(app_label), "Desktop");
    }
}

static void on_active_changed(WnckScreen *s, WnckWindow *prev, gpointer data)
{
    update_active();
}


/* ---- New Indicators (Battery, Volume, Network) ---- */

static gboolean poll_battery(gpointer data)
{
    FILE *f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (f) {
        int cap = 0;
        if (fscanf(f, "%d", &cap) == 1) {
            char *txt = g_strdup_printf("🔋\xEF\xB8\x8E %d%%", cap);
            gtk_label_set_text(GTK_LABEL(battery_label), txt);
            g_free(txt);
        }
        fclose(f);
    } else {
        gtk_label_set_text(GTK_LABEL(battery_label), "🔋\xEF\xB8\x8E --");
    }
    return G_SOURCE_CONTINUE;
}

static void launch_volume(GtkWidget *btn, gpointer data) {
    g_spawn_command_line_async("/usr/essentials/volume", NULL);
}

static void launch_network(GtkWidget *btn, gpointer data) {
    g_spawn_command_line_async("/usr/essentials/networkgui", NULL);
}

/* ---- Clock ---- */

static gboolean tick_clock(gpointer data)
{
    GDateTime *dt = g_date_time_new_now_local();
    char *str = g_date_time_format(dt, "%a %b %d  %H:%M");
    gtk_label_set_text(GTK_LABEL(clock_label), str);
    g_free(str);
    g_date_time_unref(dt);
    return G_SOURCE_CONTINUE;
}

/* ---- MPRIS ---- */

static gboolean poll_media(gpointer data)
{
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) { gtk_label_set_text(GTK_LABEL(media_label), ""); return G_SOURCE_CONTINUE; }

    GError *err = NULL;
    GVariant *res = g_dbus_connection_call_sync(bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &err);
    if (!res) { g_object_unref(bus); gtk_label_set_text(GTK_LABEL(media_label), ""); return G_SOURCE_CONTINUE; }

    GVariantIter *iter;
    g_variant_get(res, "(as)", &iter);
    const gchar *name;
    gboolean found = FALSE;
    while (g_variant_iter_loop(iter, "&s", &name)) {
        if (!g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) continue;

        GVariant *md = g_dbus_connection_call_sync(bus, name,
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            "Get",
            g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Metadata"),
            NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
        if (!md) continue;

        GVariant *v = g_variant_get_child_value(md, 0);
        GVariant *title_v = g_variant_lookup_value(v, "xesam:title", G_VARIANT_TYPE_STRING);
        GVariant *artist_v = g_variant_lookup_value(v, "xesam:artist", G_VARIANT_TYPE_ARRAY);

        const char *title = title_v ? g_variant_get_string(title_v, NULL) : "";
        const char *artist = "";
        if (artist_v) {
            GVariantIter *aiter;
            g_variant_get(artist_v, "as", &aiter);
            if (g_variant_iter_loop(aiter, "&s", &artist)) {}
            g_variant_iter_free(aiter);
        }

        if (title && title[0]) {
            char *txt = g_strdup_printf("♫ %s — %s", artist, title);
            gtk_label_set_text(GTK_LABEL(media_label), txt);
            g_free(txt);
            found = TRUE;
        }
        if (title_v) g_variant_unref(title_v);
        if (artist_v) g_variant_unref(artist_v);
        g_variant_unref(v);
        g_variant_unref(md);
        break;
    }
    g_variant_iter_free(iter);
    g_variant_unref(res);
    g_object_unref(bus);
    if (!found)
        gtk_label_set_text(GTK_LABEL(media_label), "");
    return G_SOURCE_CONTINUE;
}

/* ---- Right-click menu ---- */

static gboolean on_app_label_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
    if (ev->button != 3) return FALSE;

    GtkWidget *menu = gtk_menu_new();
    char *desktop = find_desktop_for(current_wm_class, current_app_name);

    if (desktop) {
        GPtrArray *entries = read_dock();
        gboolean pinned = in_dock(entries, desktop);
        const char *label = pinned ? "Remove from Dock" : "Add to Dock";

        GtkWidget *item = gtk_menu_item_new_with_label(label);
        g_ptr_array_unref(entries);

        g_signal_connect_data(item, "activate", G_CALLBACK(toggle_dock),
            g_strdup(desktop), (GClosureNotify)g_free, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_free(desktop);
    }

    GtkWidget *quit_i = gtk_menu_item_new_with_label("Quit Menubar");
    g_signal_connect(quit_i, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_i);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)ev);
    return TRUE;
}

/* ---- Window strut ---- */

static void set_strut(GtkWidget *win)
{
    GdkWindow *gdk_win = gtk_widget_get_window(win);
    if (!gdk_win) return;
    guint32 xid = gdk_x11_window_get_xid(gdk_win);
    GdkRectangle geo;
    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), 0, &geo);

    char xid_str[32];
    snprintf(xid_str, sizeof(xid_str), "%u", xid);

    char partial[256];
    snprintf(partial, sizeof(partial),
        "0,0,28,0, 0,0,0,0, %d,%d, 0,0",
        geo.x, geo.x + geo.width - 1);

    char *c1 = g_strdup_printf("xprop -id %s -f _NET_WM_STRUT_PARTIAL 32c -set _NET_WM_STRUT_PARTIAL '%s'", xid_str, partial);
    char *c2 = g_strdup_printf("xprop -id %s -f _NET_WM_STRUT 32c -set _NET_WM_STRUT '0,0,28,0'", xid_str);
    g_spawn_command_line_async(c1, NULL);
    g_spawn_command_line_async(c2, NULL);
    g_free(c1);
    g_free(c2);
}

static void on_map(GtkWidget *win, gpointer data) { set_strut(win); }

static void on_realize(GtkWidget *win, gpointer data)
{
    GdkRectangle geo;
    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), 0, &geo);
    gtk_window_move(GTK_WINDOW(win), geo.x, geo.y);
}

/* ---- DBus service (name claim) ---- */

static void setup_dbus(void)
{
    g_bus_own_name(G_BUS_TYPE_SESSION, "com.menubar.Dock",
        G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL, NULL);
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    wnck_screen = wnck_screen_get_default();
    g_signal_connect(wnck_screen, "active-window-changed", G_CALLBACK(on_active_changed), NULL);

    GdkScreen *screen = gdk_screen_get_default();
    GdkRectangle geo;
    gdk_screen_get_monitor_geometry(screen, 0, &geo);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    gtk_widget_set_size_request(win, geo.width, 28);

    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(win, visual);

    g_signal_connect(win, "realize", G_CALLBACK(on_realize), NULL);
    g_signal_connect(win, "map", G_CALLBACK(on_map), NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(win), main_box);

    /* Left: logo + app label */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), left, FALSE, FALSE, 12);

    GtkWidget *logo = gtk_label_new("☰");
    GtkWidget *logo_eb = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(logo_eb), logo);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(logo_eb), FALSE);
    gtk_box_pack_start(GTK_BOX(left), logo_eb, FALSE, FALSE, 0);
    g_signal_connect(logo_eb, "button-press-event", G_CALLBACK(on_logo_press), NULL);

    app_label = gtk_label_new("Desktop");
    gtk_box_pack_start(GTK_BOX(left), app_label, FALSE, FALSE, 0);

    /* Spacer */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), spacer, TRUE, TRUE, 0);

    /* Right: media + clock + indicators */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_pack_end(GTK_BOX(main_box), right, FALSE, FALSE, 12);

    /* Tray Container (SNI Host) */
    tray_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_pack_start(GTK_BOX(right), tray_box, FALSE, FALSE, 0);
    sni_host_init(tray_box);

    /* Battery */
    battery_label = gtk_label_new("🔋\xEF\xB8\x8E --");
    gtk_box_pack_start(GTK_BOX(right), battery_label, FALSE, FALSE, 0);

    /* Network */
    network_btn = gtk_button_new_with_label("📶\xEF\xB8\x8E");
    gtk_button_set_relief(GTK_BUTTON(network_btn), GTK_RELIEF_NONE);
    g_signal_connect(network_btn, "clicked", G_CALLBACK(launch_network), NULL);
    gtk_box_pack_start(GTK_BOX(right), network_btn, FALSE, FALSE, 0);

    /* Volume */
    volume_btn = gtk_button_new_with_label("🔊\xEF\xB8\x8E");
    gtk_button_set_relief(GTK_BUTTON(volume_btn), GTK_RELIEF_NONE);
    g_signal_connect(volume_btn, "clicked", G_CALLBACK(launch_volume), NULL);
    gtk_box_pack_start(GTK_BOX(right), volume_btn, FALSE, FALSE, 0);

    media_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(right), media_label, FALSE, FALSE, 0);

    clock_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(right), clock_label, FALSE, FALSE, 0);

    /* Events */
    g_signal_connect(app_label, "button-press-event", G_CALLBACK(on_app_label_press), NULL);
    gtk_widget_add_events(app_label, GDK_BUTTON_PRESS_MASK);

    /* Timers */
    g_timeout_add_seconds(1, tick_clock, NULL);
    tick_clock(NULL);

    g_timeout_add_seconds(3, poll_media, NULL);
    poll_media(NULL);

    g_timeout_add_seconds(5, poll_battery, NULL);
    poll_battery(NULL);

    /* DBus */
    setup_dbus();

    gtk_widget_show_all(win);
    gtk_main();

    g_free(current_wm_class);
    g_free(current_app_name);
    return 0;
}
