#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <webkit2/webkit2.h>
#include <json-glib/json-glib.h>
#include <libwnck/libwnck.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

gboolean debug_mode = FALSE;
#define DEBUG_LOG(fmt, ...) do { \
    if (debug_mode) { \
        printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        fflush(stdout); \
    } \
} while(0)

/* ═══ Globals ═══ */
static GtkWidget *main_window;
static GtkWidget *webview;
static WnckScreen *wnck_screen;
static char *current_wm_class = NULL;
static char *current_app_name = NULL;
static gboolean bar_visible = TRUE;
static guint hide_timer_id = 0;
static gint current_bar_width = 0;
static gint current_bar_height = 42;
gboolean menu_is_open = FALSE;
static gboolean panel_is_open = FALSE;

static void update_js(const gchar *func, const gchar *v1, const gchar *v2);
static void update_js3(const gchar *func, const gchar *v1, const gchar *v2, const gchar *v3);
static void update_js5(const gchar *func, const gchar *v1, const gchar *v2, const gchar *v3, const gchar *v4, const gchar *v5);
static void run_js(const gchar *script);

/* ═══ Icon → Base64 <img> (GtkIconTheme) ═══ */
static gchar *icon_to_b64(const gchar *name) {
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GtkIconInfo *info = gtk_icon_theme_lookup_icon(theme, name, 16, 0);
    if (!info) return g_strdup("");
    const gchar *fn = gtk_icon_info_get_filename(info);
    if (!fn) { g_object_unref(info); return g_strdup(""); }
    gchar *buf = NULL; gsize len = 0;
    if (!g_file_get_contents(fn, &buf, &len, NULL)) { g_object_unref(info); return g_strdup(""); }
    gchar *b64 = g_base64_encode((const guchar*)buf, len);
    const gchar *mime = g_str_has_suffix(fn, ".svg") ? "image/svg+xml" : "image/png";
    gchar *tag = g_strdup_printf("<img src=\"data:%s;base64,%s\"/>", mime, b64);
    g_free(b64); g_free(buf); g_object_unref(info);
    return tag;
}

/* ═══ GdkPixbuf → Base64 <img> ═══ */
static gchar *pixbuf_to_b64(GdkPixbuf *pb) {
    if (!pb) return g_strdup("");
    gchar *buf = NULL; gsize len = 0;
    if (!gdk_pixbuf_save_to_buffer(pb, &buf, &len, "png", NULL, NULL)) return g_strdup("");
    gchar *b64 = g_base64_encode((const guchar*)buf, len);
    gchar *tag = g_strdup_printf("<img src=\"data:image/png;base64,%s\"/>", b64);
    g_free(b64); g_free(buf);
    return tag;
}

/* ═══ HTML icon token replacer ═══ */
static gboolean icon_cb(const GMatchInfo *mi, GString *res, gpointer d) {
    gchar *n = g_match_info_fetch(mi, 1);
    gchar *t = icon_to_b64(n);
    g_string_append(res, t);
    g_free(t); g_free(n);
    return FALSE;
}
static gchar *process_icons(const gchar *html) {
    GRegex *re = g_regex_new("\\{\\{icon\\.([^}]+)\\}\\}", 0, 0, NULL);
    gchar *out = g_regex_replace_eval(re, html, -1, 0, 0, icon_cb, NULL, NULL);
    g_regex_unref(re);
    return out;
}
static void str_replace(GString *s, const char *f, const char *r) {
    if (!f || !r) return;
    size_t fl = strlen(f), rl = strlen(r);
    char *p = s->str;
    while ((p = strstr(p, f))) {
        gint off = p - s->str;
        g_string_erase(s, off, fl);
        g_string_insert(s, off, r);
        p = s->str + off + rl;
    }
}

/* ═══ JS helpers ═══ */
static gchar *js_esc(const gchar *s) {
    if (!s) return g_strdup("null");
    GString *o = g_string_new("\"");
    for (const gchar *p = s; *p; p++) {
        switch (*p) {
            case '"':  g_string_append(o, "\\\""); break;
            case '\\': g_string_append(o, "\\\\"); break;
            case '\n': g_string_append(o, "\\n");  break;
            case '\r': g_string_append(o, "\\r");  break;
            default:   g_string_append_c(o, *p);   break;
        }
    }
    g_string_append_c(o, '"');
    return g_string_free(o, FALSE);
}
static void run_js(const gchar *script) {
    if (!webview) return;
    webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(webview), script, NULL, NULL, NULL);
}
static void update_js(const gchar *func, const gchar *v1, const gchar *v2) {
    if (!webview) return;
    gchar *j1 = js_esc(v1);
    gchar *script;
    if (v2) {
        gchar *j2 = js_esc(v2);
        script = g_strdup_printf("if(window.NavbarAPI)NavbarAPI.%s(%s,%s);", func, j1, j2);
        g_free(j2);
    } else {
        script = g_strdup_printf("if(window.NavbarAPI)NavbarAPI.%s(%s);", func, j1);
    }
    run_js(script);
    g_free(script); g_free(j1);
}

static void update_js3(const gchar *func, const gchar *v1, const gchar *v2, const gchar *v3) {
    if (!webview) return;
    gchar *j1 = js_esc(v1);
    gchar *j2 = js_esc(v2);
    gchar *j3 = js_esc(v3);
    gchar *script = g_strdup_printf("if(window.NavbarAPI && window.NavbarAPI.%s) NavbarAPI.%s(%s,%s,%s);", func, func, j1, j2, j3);
    run_js(script);
    g_free(script);
    g_free(j1); g_free(j2); g_free(j3);
}

static void update_js5(const gchar *func, const gchar *v1, const gchar *v2, const gchar *v3, const gchar *v4, const gchar *v5) {
    if (!webview) return;
    gchar *j1 = js_esc(v1); gchar *j2 = js_esc(v2); gchar *j3 = js_esc(v3);
    gchar *j4 = js_esc(v4); gchar *j5 = js_esc(v5);
    gchar *script = g_strdup_printf("if(window.NavbarAPI && window.NavbarAPI.%s) NavbarAPI.%s(%s,%s,%s,%s,%s);", func, func, j1, j2, j3, j4, j5);
    run_js(script);
    g_free(script);
    g_free(j1); g_free(j2); g_free(j3); g_free(j4); g_free(j5);
}

static void update_js_json(const gchar *func, const gchar *json_str) {
    if (!webview) return;
    gchar *script = g_strdup_printf("if(window.NavbarAPI && window.NavbarAPI.%s) NavbarAPI.%s(%s);", func, func, json_str);
    run_js(script);
    g_free(script);
}

/* ═══ Menu helpers ═══ */
#include "menu_helpers.c"

/* ═══ IPC: JS → C ═══ */
static void on_script_msg(WebKitUserContentManager *mgr,
                          WebKitJavascriptResult *result, gpointer ud)
{
    JSCValue *val = webkit_javascript_result_get_js_value(result);
    gchar *str = jsc_value_to_string(val);
    JsonParser *p = json_parser_new();
    if (!json_parser_load_from_data(p, str, -1, NULL)) goto done;
    JsonNode *root = json_parser_get_root(p);
    if (json_node_get_node_type(root) != JSON_NODE_OBJECT) goto done;
    JsonObject *o = json_node_get_object(root);
    const gchar *act = json_object_has_member(o,"action") ? json_object_get_string_member(o,"action") : NULL;
    const gchar *pay = json_object_has_member(o,"payload") ? json_object_get_string_member(o,"payload") : NULL;
    if (!act) goto done;

    DEBUG_LOG("IPC Received: act='%s', pay='%s'", act, pay ? pay : "(null)");

    if (g_strcmp0(act,"exec")==0 && pay && pay[0]) {
        g_spawn_command_line_async(pay, NULL);
    } else if (g_strcmp0(act,"open_main_menu")==0) {
        popup_main_menu();
    } else if (g_strcmp0(act,"media_play_pause")==0 || g_strcmp0(act,"media_next")==0 || g_strcmp0(act,"media_prev")==0) {
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
        if (bus) {
            GVariant *nv = g_dbus_connection_call_sync(bus,
                "org.freedesktop.DBus","/org/freedesktop/DBus",
                "org.freedesktop.DBus","ListNames",
                NULL,NULL,G_DBUS_CALL_FLAGS_NONE,500,NULL,NULL);
            if (nv) {
                GVariantIter *it; g_variant_get(nv,"(as)",&it);
                const gchar *n;
                while (g_variant_iter_loop(it,"&s",&n)) {
                    if (g_str_has_prefix(n,"org.mpris.MediaPlayer2.")) {
                        const gchar *method = "PlayPause";
                        if (g_strcmp0(act,"media_next")==0) method = "Next";
                        else if (g_strcmp0(act,"media_prev")==0) method = "Previous";
                        g_dbus_connection_call_sync(bus,n,
                            "/org/mpris/MediaPlayer2",
                            "org.mpris.MediaPlayer2.Player", method,
                            NULL,NULL,G_DBUS_CALL_FLAGS_NONE,500,NULL,NULL);
                        break;
                    }
                }
                g_variant_iter_free(it); g_variant_unref(nv);
            }
            g_object_unref(bus);
        }
    } else if (g_strcmp0(act,"media_seek")==0 && pay && pay[0]) {
        gchar **parts = g_strsplit(pay, "|", 3);
        if (g_strv_length(parts) == 3) {
            gint64 pos = g_ascii_strtoll(parts[0], NULL, 10);
            const gchar *trackid = parts[1];
            const gchar *player = parts[2];
            
            GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
            if (bus) {
                g_dbus_connection_call_sync(bus, player,
                    "/org/mpris/MediaPlayer2",
                    "org.mpris.MediaPlayer2.Player", "SetPosition",
                    g_variant_new("(ox)", trackid, pos),
                    NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
                g_object_unref(bus);
            }
        }
        g_strfreev(parts);
    } else if (g_strcmp0(act,"resize_bar")==0 && pay && pay[0]) {
        gint new_w = 0, new_h = 42;
        sscanf(pay, "%d,%d", &new_w, &new_h);
        if (new_w > 100) {
            if (new_w != current_bar_width || new_h != current_bar_height) {
                current_bar_width = new_w;
                current_bar_height = new_h;
                GdkRectangle geo;
                GdkDisplay *display = gdk_display_get_default();
                GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
                if (!monitor) monitor = gdk_display_get_monitor(display, 0);
                if (monitor) {
                    gdk_monitor_get_geometry(monitor, &geo);
                } else {
                    geo.x = 0; geo.y = 0; geo.width = 1920; 
                }
                gint x = geo.x + (geo.width - new_w) / 2;
                if (bar_visible) gtk_window_resize(GTK_WINDOW(main_window), new_w, new_h);
                gtk_window_move(GTK_WINDOW(main_window), x, geo.y);
            }
        }
    } else if (g_strcmp0(act,"set_volume")==0 && pay && pay[0]) {
        gchar *cmd = g_strdup_printf("pactl set-sink-volume @DEFAULT_SINK@ %s%%", pay);
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);
    } else if (g_strcmp0(act,"set_brightness")==0 && pay && pay[0]) {
        gboolean success = FALSE;
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
        if (bus) {
            gint32 target = atoi(pay);
            GVariant *res = g_dbus_connection_call_sync(bus,
                "org.gnome.SettingsDaemon.Power", "/org/gnome/SettingsDaemon/Power",
                "org.freedesktop.DBus.Properties", "Set",
                g_variant_new("(ssv)", "org.gnome.SettingsDaemon.Power.Screen", "Brightness", g_variant_new_int32(target)),
                NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
            if (res) {
                success = TRUE;
                g_variant_unref(res);
            }
            g_object_unref(bus);
        }
        
        if (!success) {
            // Fallback for Openbox/i3 using brightnessctl
            gchar *cmd = g_strdup_printf("brightnessctl s %s%%", pay);
            g_spawn_command_line_async(cmd, NULL);
            g_free(cmd);
        }
    } else if (g_strcmp0(act,"set_panel_open")==0 && pay && pay[0]) {
        panel_is_open = (g_strcmp0(pay, "true") == 0);
    }
done:
    g_object_unref(p); g_free(str);
}

/* ═══ Pollers ═══ */
static void on_active_changed(WnckScreen *s, WnckWindow *prev, gpointer d) {
    WnckWindow *active = wnck_screen_get_active_window(s);
    if (!active) {
        DEBUG_LOG("Active window changed: (none)");
        return;
    }
    const char *title = wnck_window_get_name(active);
    DEBUG_LOG("Active window changed: %s", title ? title : "(null)");
}

static gboolean poll_battery(gpointer d) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    gboolean is_laptop = FALSE;
    double percentage = 100.0;
    guint32 state = 0;
    const gchar *icon_name = "battery-good-symbolic";
    gchar *status_str = g_strdup("Unknown");
    
    if (bus) {
        GVariant *res = g_dbus_connection_call_sync(bus,
            "org.freedesktop.UPower", "/org/freedesktop/UPower/devices/DisplayDevice",
            "org.freedesktop.DBus.Properties", "GetAll",
            g_variant_new("(s)", "org.freedesktop.UPower.Device"),
            NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
            
        if (res) {
            GVariant *outer = g_variant_get_child_value(res, 0);
            GVariant *pct_v = g_variant_lookup_value(outer, "Percentage", G_VARIANT_TYPE_DOUBLE);
            GVariant *state_v = g_variant_lookup_value(outer, "State", G_VARIANT_TYPE_UINT32);
            GVariant *present_v = g_variant_lookup_value(outer, "IsPresent", G_VARIANT_TYPE_BOOLEAN);
            
            if (present_v) is_laptop = g_variant_get_boolean(present_v);
            else is_laptop = TRUE;
            
            if (pct_v) percentage = g_variant_get_double(pct_v);
            if (state_v) state = g_variant_get_uint32(state_v);
            
            g_free(status_str);
            if (state == 1) status_str = g_strdup("Charging");
            else if (state == 2) status_str = g_strdup("Discharging");
            else if (state == 4) status_str = g_strdup("Fully Charged");
            else status_str = g_strdup("Unknown");
            
            if (pct_v) g_variant_unref(pct_v);
            if (state_v) g_variant_unref(state_v);
            if (present_v) g_variant_unref(present_v);
            g_variant_unref(outer);
            g_variant_unref(res);
        }
        g_object_unref(bus);
    }
    
    // Polling brightness from GNOME SettingsDaemon or fallback
    gint32 current_brightness = 50;
    gboolean b_success = FALSE;
    GDBusConnection *session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (session_bus) {
        GVariant *b_res = g_dbus_connection_call_sync(session_bus,
            "org.gnome.SettingsDaemon.Power", "/org/gnome/SettingsDaemon/Power",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.gnome.SettingsDaemon.Power.Screen", "Brightness"),
            NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
        if (b_res) {
            GVariant *v = g_variant_get_child_value(b_res, 0);
            if (v) {
                GVariant *val = g_variant_get_variant(v);
                current_brightness = g_variant_get_int32(val);
                b_success = TRUE;
                g_variant_unref(val);
                g_variant_unref(v);
            }
            g_variant_unref(b_res);
        }
        g_object_unref(session_bus);
    }
    
    if (!b_success) {
        // Fallback: Read sysfs (safe, read-only, no permissions required)
        GDir *dir = g_dir_open("/sys/class/backlight", 0, NULL);
        if (dir) {
            const gchar *bname;
            if ((bname = g_dir_read_name(dir)) != NULL) {
                gchar *max_path = g_build_filename("/sys/class/backlight", bname, "max_brightness", NULL);
                gchar *cur_path = g_build_filename("/sys/class/backlight", bname, "brightness", NULL);
                gchar *max_str = NULL, *cur_str = NULL;
                if (g_file_get_contents(max_path, &max_str, NULL, NULL) &&
                    g_file_get_contents(cur_path, &cur_str, NULL, NULL)) {
                    int m = atoi(max_str);
                    int c = atoi(cur_str);
                    if (m > 0) current_brightness = (c * 100) / m;
                }
                if (max_str) g_free(max_str);
                if (cur_str) g_free(cur_str);
                g_free(max_path); g_free(cur_path);
            }
            g_dir_close(dir);
        }
    }
    
    int lv = (int)percentage;
    gchar *lvl_str = g_strdup_printf("%d%%", lv);
    
    if (lv < 20) icon_name = "battery-empty-symbolic";
    else if (lv < 60) icon_name = "battery-low-symbolic";
    else if (lv >= 95) icon_name = "battery-full-charged-symbolic";
    
    gchar *ih = icon_to_b64(icon_name);
    
    gchar *script = g_strdup_printf(
        "if(window.NavbarAPI) { NavbarAPI.updateBattery('%s', '%s', '%s', '%s'); NavbarAPI.updateBrightness(%d); }", 
        lvl_str, ih ? ih : "", status_str, is_laptop ? "true" : "false", current_brightness);
    run_js(script);
    
    g_free(script);
    if (ih) g_free(ih);
    g_free(lvl_str);
    g_free(status_str);
    
    return G_SOURCE_CONTINUE;
}

static gboolean poll_media(gpointer d) {
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,NULL);
    if (!bus) { update_js5("updateMedia","","","","",""); return G_SOURCE_CONTINUE; }
    GVariant *res = g_dbus_connection_call_sync(bus,
        "org.freedesktop.DBus","/org/freedesktop/DBus",
        "org.freedesktop.DBus","ListNames",
        NULL,NULL,G_DBUS_CALL_FLAGS_NONE,1000,NULL,NULL);
    if (!res) { g_object_unref(bus); return G_SOURCE_CONTINUE; }
    GVariantIter *iter; g_variant_get(res,"(as)",&iter);
    const gchar *name; gboolean found = FALSE;
    while (g_variant_iter_loop(iter,"&s",&name)) {
        if (!g_str_has_prefix(name,"org.mpris.MediaPlayer2.")) continue;
        GVariant *props_res = g_dbus_connection_call_sync(bus,name,
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties","GetAll",
            g_variant_new("(s)","org.mpris.MediaPlayer2.Player"),
            NULL,G_DBUS_CALL_FLAGS_NONE,1000,NULL,NULL);
        if (!props_res) continue;
        
        GVariant *outer = g_variant_get_child_value(props_res,0);
        GVariant *status_v = g_variant_lookup_value(outer,"PlaybackStatus",G_VARIANT_TYPE_STRING);
        GVariant *next_v = g_variant_lookup_value(outer,"CanGoNext",G_VARIANT_TYPE_BOOLEAN);
        GVariant *prev_v = g_variant_lookup_value(outer,"CanGoPrevious",G_VARIANT_TYPE_BOOLEAN);
        GVariant *md_v = g_variant_lookup_value(outer,"Metadata",G_VARIANT_TYPE_VARDICT);
        
        const char *status = status_v ? g_variant_get_string(status_v,NULL) : "Stopped";
        gboolean canNext = next_v ? g_variant_get_boolean(next_v) : FALSE;
        gboolean canPrev = prev_v ? g_variant_get_boolean(prev_v) : FALSE;
        
        if (!md_v) {
            if (status_v) g_variant_unref(status_v);
            if (next_v) g_variant_unref(next_v);
            if (prev_v) g_variant_unref(prev_v);
            g_variant_unref(outer); g_variant_unref(props_res);
            continue;
        }

        GVariant *tv = g_variant_lookup_value(md_v,"xesam:title",G_VARIANT_TYPE_STRING);
        GVariant *av = g_variant_lookup_value(md_v,"xesam:artist",G_VARIANT_TYPE_STRING_ARRAY);
        GVariant *art_v = g_variant_lookup_value(md_v,"mpris:artUrl",G_VARIANT_TYPE_STRING);
        GVariant *len_v = g_variant_lookup_value(md_v,"mpris:length",G_VARIANT_TYPE_INT64);
        GVariant *tid_v = g_variant_lookup_value(md_v,"mpris:trackid",G_VARIANT_TYPE_OBJECT_PATH);
        GVariant *pos_v = g_variant_lookup_value(outer,"Position",G_VARIANT_TYPE_INT64);
        
        const char *title = tv ? g_variant_get_string(tv,NULL) : "";
        const char *artist = "";
        const char *artUrl = "";
        gint64 length = len_v ? g_variant_get_int64(len_v) : 0;
        gint64 position = pos_v ? g_variant_get_int64(pos_v) : 0;
        const char *trackid = tid_v ? g_variant_get_string(tid_v,NULL) : "";
        
        if (av) { GVariantIter ai; g_variant_iter_init(&ai,av); g_variant_iter_next(&ai,"&s",&artist); }
        if (art_v) { artUrl = g_variant_get_string(art_v,NULL); }
        
        if (title && title[0]) {
            gchar *txt = (artist && artist[0]) ? g_strdup_printf("%s — %s",artist,title) : g_strdup(title);
            gchar *j_title = js_esc(txt);
            gchar *j_trackName = js_esc(title);
            gchar *j_artistName = js_esc(artist);
            gchar *j_art = js_esc(artUrl);
            gchar *j_status = js_esc(status);
            gchar *j_tid = js_esc(trackid);
            gchar *j_name = js_esc(name);
            
            gchar *json = g_strdup_printf(
                "{\"title\":%s, \"trackName\":%s, \"artistName\":%s, \"artUrl\":%s, \"status\":%s, \"canNext\":%s, \"canPrev\":%s, "
                "\"position\":%" G_GINT64_FORMAT ", \"length\":%" G_GINT64_FORMAT ", "
                "\"trackId\":%s, \"playerName\":%s}",
                j_title, j_trackName, j_artistName, j_art, j_status, canNext ? "true" : "false", canPrev ? "true" : "false",
                position, length, j_tid, j_name
            );
            
            update_js_json("updateMedia", json);
            
            g_free(json); g_free(j_title); g_free(j_trackName); g_free(j_artistName); g_free(j_art); g_free(j_status); g_free(j_tid); g_free(j_name);
            g_free(txt); found = TRUE;
        }
        if (tv) g_variant_unref(tv);
        if (av) g_variant_unref(av);
        if (art_v) g_variant_unref(art_v);
        if (len_v) g_variant_unref(len_v);
        if (tid_v) g_variant_unref(tid_v);
        if (pos_v) g_variant_unref(pos_v);
        if (status_v) g_variant_unref(status_v);
        if (next_v) g_variant_unref(next_v);
        if (prev_v) g_variant_unref(prev_v);
        g_variant_unref(md_v); g_variant_unref(outer); g_variant_unref(props_res);
        if (found) break;
    }
    g_variant_iter_free(iter); g_variant_unref(res); g_object_unref(bus);
    if (!found) update_js_json("updateMedia", "null");
    return G_SOURCE_CONTINUE;
}

static gboolean check_video_in_proc(void) {
    gboolean found = FALSE;
    GDir *dir = g_dir_open("/proc", 0, NULL);
    if (!dir) return FALSE;
    const gchar *pid;
    while ((pid = g_dir_read_name(dir)) != NULL) {
        if (pid[0] >= '1' && pid[0] <= '9') {
            gchar *fd_path = g_strdup_printf("/proc/%s/fd", pid);
            GDir *fdir = g_dir_open(fd_path, 0, NULL);
            g_free(fd_path);
            if (fdir) {
                const gchar *fd;
                while ((fd = g_dir_read_name(fdir)) != NULL) {
                    if (fd[0] >= '0' && fd[0] <= '9') {
                        gchar *link_path = g_strdup_printf("/proc/%s/fd/%s", pid, fd);
                        gchar *target = g_file_read_link(link_path, NULL);
                        g_free(link_path);
                        if (target) {
                            if (g_str_has_prefix(target, "/dev/video")) found = TRUE;
                            g_free(target);
                        }
                    }
                    if (found) break;
                }
                g_dir_close(fdir);
            }
        }
        if (found) break;
    }
    g_dir_close(dir);
    return found;
}

static gboolean check_mic_in_alsa(void) {
    gboolean active = FALSE;
    GDir *dir = g_dir_open("/proc/asound", 0, NULL);
    if (!dir) return FALSE;
    const gchar *card;
    while ((card = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_prefix(card, "card")) {
            gchar *card_path = g_strdup_printf("/proc/asound/%s", card);
            GDir *cdir = g_dir_open(card_path, 0, NULL);
            if (cdir) {
                const gchar *pcm;
                while ((pcm = g_dir_read_name(cdir)) != NULL) {
                    if (g_str_has_prefix(pcm, "pcm") && g_str_has_suffix(pcm, "c")) { // Capture device
                        gchar *pcm_path = g_strdup_printf("%s/%s", card_path, pcm);
                        GDir *sdir = g_dir_open(pcm_path, 0, NULL);
                        if (sdir) {
                            const gchar *sub;
                            while ((sub = g_dir_read_name(sdir)) != NULL) {
                                if (g_str_has_prefix(sub, "sub")) {
                                    gchar *status_path = g_strdup_printf("%s/%s/status", pcm_path, sub);
                                    gchar *content = NULL;
                                    if (g_file_get_contents(status_path, &content, NULL, NULL)) {
                                        if (strstr(content, "RUNNING")) active = TRUE;
                                        g_free(content);
                                    }
                                    g_free(status_path);
                                }
                                if (active) break;
                            }
                            g_dir_close(sdir);
                        }
                        g_free(pcm_path);
                    }
                    if (active) break;
                }
                g_dir_close(cdir);
            }
            g_free(card_path);
        }
        if (active) break;
    }
    g_dir_close(dir);
    return active;
}

static gboolean poll_sensors(gpointer d) {
    gboolean cam = check_video_in_proc();
    gboolean mic = check_mic_in_alsa();
    if (debug_mode && (cam || mic)) DEBUG_LOG("Sensors - Cam: %s, Mic: %s", cam ? "YES" : "NO", mic ? "YES" : "NO");
    update_js("updateSensors", cam ? "true" : "false", mic ? "true" : "false");
    return G_SOURCE_CONTINUE;
}

static gboolean poll_volume(gpointer d) {
    gchar *out = NULL;
    if (g_spawn_command_line_sync("sh -c \"pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -Po '[0-9]+(?=%)' | head -n 1\"", &out, NULL, NULL, NULL)) {
        g_strchomp(out);
        if (out[0]) update_js("updateVolumeSlider", out, "");
        g_free(out);
    }
    return G_SOURCE_CONTINUE;
}

/* ═══ Auto-hide: CSS-driven smooth transitions ═══
 * The GTK window shrinks to 1px height when hidden so it only triggers
 * when the cursor is at the very top edge ("sampai mentok").
 */
static gboolean shrink_window(gpointer d) {
    if (!bar_visible) gtk_window_resize(GTK_WINDOW(main_window), current_bar_width, 1);
    return FALSE;
}

static gboolean do_hide(gpointer d) {
    if (menu_is_open || panel_is_open) return G_SOURCE_CONTINUE;
    hide_timer_id = 0;
    if (bar_visible) {
        DEBUG_LOG("do_hide - Window hidden");
        bar_visible = FALSE;
        run_js("if(window.NavbarAPI)NavbarAPI.barHide();");
        g_timeout_add(450, shrink_window, NULL);
    }
    return FALSE;
}

static gboolean on_enter(GtkWidget *w, GdkEventCrossing *ev, gpointer d) {
    if (hide_timer_id) { g_source_remove(hide_timer_id); hide_timer_id = 0; }
    if (!bar_visible) {
        DEBUG_LOG("on_enter - Window revealed");
        gtk_window_resize(GTK_WINDOW(main_window), current_bar_width, current_bar_height);
        bar_visible = TRUE;
        run_js("if(window.NavbarAPI)NavbarAPI.barShow();");
    }
    if (gtk_widget_get_window(w)) gdk_window_raise(gtk_widget_get_window(w));
    return FALSE;
}

static gboolean on_leave(GtkWidget *w, GdkEventCrossing *ev, gpointer d) {
    if (ev->detail == GDK_NOTIFY_INFERIOR) return FALSE;
    if (hide_timer_id) g_source_remove(hide_timer_id);
    DEBUG_LOG("on_leave - Hide timer started");
    hide_timer_id = g_timeout_add(1200, do_hide, NULL);
    return FALSE;
}

/* ═══ Transparent WebView ═══ */
static void make_transparent(GtkWidget *wv) {
    GdkRGBA rgba = {0,0,0,0};
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(wv), &rgba);
}

/* ═══ DBus ═══ */
static void setup_dbus(void) {
    g_bus_own_name(G_BUS_TYPE_SESSION,"com.menubar.Dock",
        G_BUS_NAME_OWNER_FLAGS_NONE,NULL,NULL,NULL,NULL,NULL);
}

/* ═══ Main ═══ */
int main(int argc, char **argv) {
    gchar *custom_html_url = NULL;
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "-d") == 0 || g_strcmp0(argv[i], "--debug") == 0) {
            debug_mode = TRUE;
        } else if (g_strcmp0(argv[i], "-nurl") == 0 && i + 1 < argc) {
            custom_html_url = g_strdup(argv[i + 1]);
            i++;
        }
    }
    gtk_init(&argc, &argv);

    DEBUG_LOG("Starting navbar_app in DEBUG MODE...");

    wnck_screen = wnck_screen_get_default();
    g_signal_connect(wnck_screen,"active-window-changed",G_CALLBACK(on_active_changed),NULL);

    GdkScreen *screen = gdk_screen_get_default();
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) monitor = gdk_display_get_monitor(display, 0);
    GdkRectangle geo;
    if (monitor) gdk_monitor_get_geometry(monitor, &geo);
    else { geo.x = 0; geo.y = 0; geo.width = 1920; }

    /* ── Window ── */
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "navbar");
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(main_window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
    gtk_window_stick(GTK_WINDOW(main_window));
    gtk_window_set_accept_focus(GTK_WINDOW(main_window), FALSE);
    gtk_widget_set_size_request(main_window, 10, 42);
    gtk_window_set_default_size(GTK_WINDOW(main_window), 380, 42);

    GdkVisual *vis = gdk_screen_get_rgba_visual(screen);
    if (vis) gtk_widget_set_visual(main_window, vis);
    gtk_widget_set_app_paintable(main_window, TRUE);

    gtk_widget_add_events(main_window, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(main_window,"enter-notify-event",G_CALLBACK(on_enter),NULL);
    g_signal_connect(main_window,"leave-notify-event",G_CALLBACK(on_leave),NULL);
    g_signal_connect(main_window,"destroy",G_CALLBACK(gtk_main_quit),NULL);

    /* ── WebKit ── */
    webview = webkit_web_view_new();
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(webview));
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    make_transparent(webview);
    gtk_container_add(GTK_CONTAINER(main_window), webview);

    WebKitUserContentManager *mgr = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(webview));
    g_signal_connect(mgr,"script-message-received::appBridge",G_CALLBACK(on_script_msg),NULL);
    webkit_user_content_manager_register_script_message_handler(mgr,"appBridge");

    /* ── Load HTML ── */
    gchar *html_raw = NULL;
    gchar *html_path = NULL;
    
    if (custom_html_url) {
        html_path = g_strdup(custom_html_url);
        if (!g_file_get_contents(html_path, &html_raw, NULL, NULL)) {
            g_printerr("ERROR: Could not read custom navbar HTML: %s\n", html_path);
        }
    } else {
        gchar *self_dir = g_path_get_dirname(argv[0]);
        html_path = g_build_filename(self_dir,"navbar.html",NULL);
        if (!g_file_get_contents(html_path, &html_raw, NULL, NULL))
            g_file_get_contents("navbar.html", &html_raw, NULL, NULL);
        g_free(self_dir);
    }
    
    if (custom_html_url) g_free(custom_html_url);
    if (html_path) g_free(html_path);

    if (html_raw) {
        gchar *processed = process_icons(html_raw);
        GString *final = g_string_new(processed);
        g_free(processed); g_free(html_raw);

        str_replace(final, "{{ACTIVE_APP_NAME}}", "");
        str_replace(final, "{{BATTERY_LEVEL}}", "--");
        str_replace(final, "{{MEDIA_TITLE}}", "");

        GDateTime *dt = g_date_time_new_now_local();
        gchar *ts = g_date_time_format(dt, "%H:%M");
        str_replace(final, "{{CURRENT_TIME}}", ts);
        g_free(ts); g_date_time_unref(dt);

        gchar *cwd = g_get_current_dir();
        gchar *base = g_filename_to_uri(cwd, NULL, NULL);
        webkit_web_view_load_html(WEBKIT_WEB_VIEW(webview), final->str, base);
        g_free(base); g_free(cwd);
        g_string_free(final, TRUE);
    } else {
        g_printerr("ERROR: navbar.html not found!\n");
        return 1;
    }

    gtk_widget_show_all(main_window);
    gtk_window_move(GTK_WINDOW(main_window), geo.x + (geo.width - 380) / 2, geo.y);

    /* ── Pollers (faster intervals for realtime feel) ── */
    g_timeout_add_seconds(2, poll_media, NULL);
    poll_media(NULL);
    g_timeout_add_seconds(3, poll_battery, NULL);
    poll_battery(NULL);
    g_timeout_add_seconds(2, poll_sensors, NULL);
    poll_sensors(NULL);
    g_timeout_add_seconds(1, poll_volume, NULL);
    poll_volume(NULL);

    setup_dbus();
    gtk_main();

    g_free(current_wm_class);
    g_free(current_app_name);
    return 0;
}
