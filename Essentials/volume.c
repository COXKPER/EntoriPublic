#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdio.h>
#include <string.h>

static pa_glib_mainloop *glib_mainloop = NULL;
static pa_mainloop_api *pa_api = NULL;
static pa_context *pa_ctx = NULL;

static GtkWidget *scale;
static GtkWidget *mute_btn;

static char *default_sink_name = NULL;
static pa_cvolume current_cvolume;
static gboolean ignore_ui_events = FALSE;

static void load_css(void)
{
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        "window {"
        "  background-color: #1e1e1e;"
        "  color: #ffffff;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
        "}"
        ".header-title {"
        "  font-size: 18px; font-weight: 600; color: #ffffff; margin-bottom: 5px;"
        "}"
        ".action-card {"
        "  background-color: rgba(255,255,255,0.06);"
        "  border-radius: 14px; padding: 16px;"
        "  border: 1px solid rgba(255,255,255,0.1);"
        "}"
        "scale {"
        "  padding: 10px;"
        "}"
        "scale trough {"
        "  background-color: rgba(255,255,255,0.2); border-radius: 4px;"
        "}"
        "scale highlight {"
        "  background-color: #0A84FF; border-radius: 4px;"
        "}"
        "scale slider {"
        "  min-width: 16px; min-height: 16px; border-radius: 8px;"
        "  background-color: #ffffff;"
        "}"
        ".mute-btn {"
        "  background-color: rgba(255,255,255,0.06); color: #ffffff;"
        "  border-radius: 8px; padding: 10px;"
        "  font-weight: 500; font-size: 14px; border: 1px solid rgba(255,255,255,0.1);"
        "}"
        ".mute-btn:hover { background-color: rgba(255,255,255,0.12); }"
        ".mute-btn:checked {"
        "  background-color: rgba(255,59,48,0.2); color: #FF3B30;"
        "  border: 1px solid rgba(255,59,48,0.5);"
        "}",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void update_volume_ui(const pa_sink_info *info)
{
    ignore_ui_events = TRUE;
    
    pa_volume_t vol = pa_cvolume_avg(&info->volume);
    double pct = (double)vol * 100.0 / PA_VOLUME_NORM;
    
    gtk_range_set_value(GTK_RANGE(scale), pct);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute_btn), info->mute ? TRUE : FALSE);
    
    ignore_ui_events = FALSE;
}

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    if (eol > 0 || !i) return;
    if (default_sink_name && strcmp(i->name, default_sink_name) == 0) {
        current_cvolume = i->volume;
        update_volume_ui(i);
    }
}

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
    if (!i) return;
    if (default_sink_name) {
        g_free(default_sink_name);
    }
    default_sink_name = g_strdup(i->default_sink_name);
    
    pa_operation *o = pa_context_get_sink_info_by_name(c, default_sink_name, sink_info_cb, NULL);
    if (o) pa_operation_unref(o);
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK) {
        pa_operation *o = pa_context_get_server_info(c, server_info_cb, NULL);
        if (o) pa_operation_unref(o);
    }
}

static void pa_state_cb(pa_context *c, void *userdata)
{
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY: {
            pa_operation *o;
            o = pa_context_get_server_info(c, server_info_cb, NULL);
            if (o) pa_operation_unref(o);
            
            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
            o = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
            if (o) pa_operation_unref(o);
            break;
        }
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            break;
        default:
            break;
    }
}

static void on_scale_value_changed(GtkRange *range, gpointer user_data)
{
    if (ignore_ui_events || !pa_ctx || !default_sink_name) return;
    if (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) return;

    double pct = gtk_range_get_value(range);
    pa_volume_t vol = (pa_volume_t)((pct * PA_VOLUME_NORM) / 100.0);
    
    pa_cvolume new_cvol = current_cvolume;
    pa_cvolume_set(&new_cvol, current_cvolume.channels, vol);
    
    pa_operation *o = pa_context_set_sink_volume_by_name(pa_ctx, default_sink_name, &new_cvol, NULL, NULL);
    if (o) pa_operation_unref(o);
}

static void on_mute_toggled(GtkToggleButton *button, gpointer user_data)
{
    if (ignore_ui_events || !pa_ctx || !default_sink_name) return;
    if (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) return;

    int mute = gtk_toggle_button_get_active(button) ? 1 : 0;
    pa_operation *o = pa_context_set_sink_mute_by_name(pa_ctx, default_sink_name, mute, NULL, NULL);
    if (o) pa_operation_unref(o);
}

static void on_destroy(GtkWidget *widget, gpointer data)
{
    if (pa_ctx) {
        pa_context_disconnect(pa_ctx);
        pa_context_unref(pa_ctx);
    }
    if (glib_mainloop) {
        pa_glib_mainloop_free(glib_mainloop);
    }
    if (default_sink_name) {
        g_free(default_sink_name);
    }
    gtk_main_quit();
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    load_css();

    glib_mainloop = pa_glib_mainloop_new(NULL);
    pa_api = pa_glib_mainloop_get_api(glib_mainloop);
    pa_ctx = pa_context_new(pa_api, "Volume Control");
    pa_context_set_state_callback(pa_ctx, pa_state_cb, NULL);
    pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Volume");
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 150);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Volume");
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    gtk_widget_set_margin_start(outer, 20);
    gtk_widget_set_margin_end(outer, 20);
    gtk_container_add(GTK_CONTAINER(win), outer);

    GtkWidget *title = gtk_label_new("Master Volume");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "header-title");
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "action-card");
    gtk_box_pack_start(GTK_BOX(outer), card, FALSE, FALSE, 0);

    scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 1.0);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_scale_value_changed), NULL);
    gtk_box_pack_start(GTK_BOX(card), scale, FALSE, FALSE, 0);

    mute_btn = gtk_toggle_button_new_with_label("Mute");
    gtk_style_context_add_class(gtk_widget_get_style_context(mute_btn), "mute-btn");
    g_signal_connect(mute_btn, "toggled", G_CALLBACK(on_mute_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(card), mute_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    gtk_main();

    return 0;
}
