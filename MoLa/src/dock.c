#include "dock.h"
#include "config.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

/* ── forward declarations ─────────────────────────────────────────── */

static GdkPixbuf *load_icon(const char *icon_name, int size);
static void       get_screen_size(int *w, int *h);
static gboolean   on_background_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
static gboolean   on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static gboolean   on_window_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data);
static gboolean   on_window_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data);
static gboolean   on_item_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static void       on_item_clicked(GtkWidget *widget, gpointer data);
static void       recalc_magnification(Dock *dock, int mouse_x);
static void       reset_magnification(Dock *dock);
static gboolean   on_hide_timeout(gpointer data);
static gboolean   on_show_animation(gpointer data);
static gboolean   on_hide_animation(gpointer data);
static int        count_box_children(GtkWidget *box);
static void       update_separator_visibility(Dock *dock);
static void       raise_window(guint32 xid);

/* ── icon loading ─────────────────────────────────────────────────── */

static GdkPixbuf *load_icon(const char *icon_name, int size) {
    if (!icon_name) return NULL;

    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GdkPixbuf *pixbuf = NULL;

    /* absolute path */
    if (g_path_is_absolute(icon_name)) {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_name, size, size, TRUE, NULL);
    }

    /* theme lookup */
    if (!pixbuf) {
        GtkIconInfo *info = gtk_icon_theme_lookup_icon(
            theme, icon_name, size, GTK_ICON_LOOKUP_USE_BUILTIN);
        if (info) {
            pixbuf = gtk_icon_info_load_icon(info, NULL);
            g_object_unref(info);
            if (pixbuf) {
                GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                    pixbuf, size, size, GDK_INTERP_BILINEAR);
                g_object_unref(pixbuf);
                pixbuf = scaled;
            }
        }
    }

    /* fallback: solid grey square */
    if (!pixbuf) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size, size);
        gdk_pixbuf_fill(pixbuf, 0x556677FF);
    }

    return pixbuf;
}

static void get_screen_size(int *w, int *h) {
    GdkDisplay *dpy = gdk_display_get_default();
    GdkMonitor *mon = gdk_display_get_primary_monitor(dpy);
    if (!mon) mon = gdk_display_get_monitor(dpy, 0);
    if (mon) {
        GdkRectangle rect;
        gdk_monitor_get_geometry(mon, &rect);
        *w = rect.width;
        *h = rect.height;
    } else {
        *w = 1920;
        *h = 1080;
    }
}

/* ── dock construction ────────────────────────────────────────────── */

Dock *dock_new(void) {
    Dock *dock = g_new0(Dock, 1);

    dock->is_hidden    = false;
    dock->is_hovered   = false;
    dock->is_animating = false;
    dock->hide_timer_id = 0;
    dock->anim_timer_id = 0;

    /* macOS-like dark translucent background */
    dock->bg_color     = (GdkRGBA){0.10, 0.10, 0.12, 0.78};
    dock->border_color = (GdkRGBA){1.0,  1.0,  1.0,  0.12};

    get_screen_size(&dock->screen_width, &dock->screen_height);

    dock->capsule_height   = DOCK_ICON_SIZE + DOCK_PADDING_V * 2
                             + DOCK_INDICATOR_GAP + (int)(DOCK_INDICATOR_RADIUS * 2);
    dock->magnify_headroom = DOCK_MAX_MAGNIFIED_SIZE - DOCK_ICON_SIZE + 4;
    dock->window_height    = dock->capsule_height + dock->magnify_headroom;

    dock->hidden_y  = dock->screen_height + 2;
    dock->visible_y = dock->screen_height - dock->window_height - DOCK_MARGIN_BOTTOM;
    dock->current_y = dock->visible_y;

    /* ── window ───────────────────────────────────────────────────── */
    dock->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dock->window), "MoLa Desktop Dock");
    gtk_window_set_resizable(GTK_WINDOW(dock->window), FALSE);
    gtk_widget_set_app_paintable(dock->window, TRUE);
    gtk_window_set_decorated(GTK_WINDOW(dock->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dock->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(dock->window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(dock->window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_keep_above(GTK_WINDOW(dock->window), TRUE);

    /* RGBA visual for transparency */
    GdkScreen *screen = gdk_screen_get_default();
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(dock->window, visual);

    gtk_widget_add_events(dock->window,
        GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK   | GDK_STRUCTURE_MASK);

    g_signal_connect(dock->window, "enter-notify-event",
                     G_CALLBACK(on_window_enter), dock);
    g_signal_connect(dock->window, "leave-notify-event",
                     G_CALLBACK(on_window_leave), dock);
    g_signal_connect(dock->window, "motion-notify-event",
                     G_CALLBACK(on_motion_notify), dock);

    /* ── layout ───────────────────────────────────────────────────── */
    dock->fixed = gtk_fixed_new();
    gtk_widget_set_app_paintable(dock->fixed, TRUE);
    g_signal_connect(dock->fixed, "draw", G_CALLBACK(on_background_draw), dock);
    gtk_container_add(GTK_CONTAINER(dock->window), dock->fixed);

    /* outer HBox: [pinned_box] [separator] [running_box] */
    dock->items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(dock->items_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dock->items_box, GTK_ALIGN_END);

    dock->pinned_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DOCK_ITEM_SPACING);
    dock->running_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, DOCK_ITEM_SPACING);

    /* vertical separator between pinned and running */
    dock->separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(dock->separator, DOCK_SEPARATOR_GAP);
    gtk_widget_set_margin_end(dock->separator, DOCK_SEPARATOR_GAP);

    /* Style the separator to be subtle */
    {
        GtkCssProvider *sep_css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(sep_css,
            "separator {"
            "  min-width: 1px;"
            "  background-color: rgba(255,255,255,0.20);"
            "  margin-top: 8px;"
            "  margin-bottom: 8px;"
            "}", -1, NULL);
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(dock->separator),
            GTK_STYLE_PROVIDER(sep_css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(sep_css);
    }
    gtk_widget_set_no_show_all(dock->separator, TRUE);
    gtk_widget_hide(dock->separator);

    gtk_box_pack_start(GTK_BOX(dock->items_box), dock->pinned_box,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dock->items_box), dock->separator,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dock->items_box), dock->running_box, FALSE, FALSE, 0);

    gtk_fixed_put(GTK_FIXED(dock->fixed), dock->items_box,
                  DOCK_PADDING_H, 0);

    /* ── global CSS for dock items ────────────────────────────────── */
    {
        static GtkCssProvider *item_css = NULL;
        if (!item_css) {
            item_css = gtk_css_provider_new();
            gtk_css_provider_load_from_data(item_css,
                ".dock-item {"
                "  background: transparent;"
                "  border: none;"
                "  box-shadow: none;"
                "  padding: 4px;"
                "  border-radius: 10px;"
                "  transition: background 150ms ease;"
                "}"
                ".dock-item:hover {"
                "  background: rgba(255,255,255,0.10);"
                "}", -1, NULL);
            gtk_style_context_add_provider_for_screen(
                gdk_screen_get_default(),
                GTK_STYLE_PROVIDER(item_css),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
    }

    return dock;
}

/* ── cleanup ──────────────────────────────────────────────────────── */

void dock_free(Dock *dock) {
    if (!dock) return;

    if (dock->hide_timer_id) g_source_remove(dock->hide_timer_id);
    if (dock->anim_timer_id) g_source_remove(dock->anim_timer_id);

    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        if (item->bounce_timer_id) g_source_remove(item->bounce_timer_id);
        g_free(item->app_id);
        g_free(item->display_name);
        g_free(item->icon_name);
        g_free(item->desktop_file);
        g_free(item);
    }
    g_list_free(dock->items);

    if (dock->dbus_conn) g_object_unref(dock->dbus_conn);
    gtk_widget_destroy(dock->window);
    g_free(dock);
}

/* ── add / remove / find ──────────────────────────────────────────── */

DockItem *dock_add_item(Dock *dock, const char *app_id,
                        const char *display_name, const char *icon_name,
                        const char *desktop_file, bool pinned, bool running) {
    /* Don't add duplicates */
    if (dock_find_item(dock, app_id)) return NULL;

    DockItem *item   = g_new0(DockItem, 1);
    item->app_id       = g_strdup(app_id);
    item->display_name = g_strdup(display_name ? display_name : app_id);
    item->icon_name    = g_strdup(icon_name);
    item->desktop_file = desktop_file ? g_strdup(desktop_file) : NULL;
    item->pinned       = pinned;
    item->running      = running;
    item->current_scale = 1.0;

    /* ── button ───────────────────────────────────────────────────── */
    item->button = gtk_button_new();
    gtk_widget_set_size_request(item->button,
                                DOCK_ICON_SIZE + 8,
                                dock->window_height);
    gtk_widget_set_valign(item->button, GTK_ALIGN_END);

    GtkStyleContext *ctx = gtk_widget_get_style_context(item->button);
    gtk_style_context_add_class(ctx, "dock-item");

    /* tooltip */
    gtk_widget_set_tooltip_text(item->button,
                                item->display_name ? item->display_name : item->app_id);

    /* ── item layout: spacer → icon → indicator gap ───────────────── */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(vbox, GTK_ALIGN_END);

    item->icon_image = gtk_image_new();
    GdkPixbuf *pb = load_icon(item->icon_name, DOCK_ICON_SIZE);
    if (pb) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(item->icon_image), pb);
        g_object_unref(pb);
    }
    gtk_widget_set_size_request(item->icon_image, DOCK_ICON_SIZE, DOCK_ICON_SIZE);

    gtk_box_pack_start(GTK_BOX(vbox), item->icon_image, FALSE, FALSE, 0);

    /* space for the indicator dot (drawn in background_draw) */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, 1,
                                DOCK_INDICATOR_GAP + (int)(DOCK_INDICATOR_RADIUS * 2));
    gtk_box_pack_start(GTK_BOX(vbox), spacer, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(item->button), vbox);

    /* ── signals ──────────────────────────────────────────────────── */
    g_object_set_data(G_OBJECT(item->button), "dock-item", item);
    g_object_set_data(G_OBJECT(item->button), "dock", dock);

    g_signal_connect(item->button, "clicked",
                     G_CALLBACK(on_item_clicked), item);
    g_signal_connect(item->button, "button-press-event",
                     G_CALLBACK(on_item_button_press), dock);

    /* ── pack into correct box ────────────────────────────────────── */
    GtkWidget *target_box = pinned ? dock->pinned_box : dock->running_box;
    gtk_box_pack_start(GTK_BOX(target_box), item->button, FALSE, FALSE, 0);

    dock->items = g_list_append(dock->items, item);

    update_separator_visibility(dock);
    dock_recalc_size(dock);
    gtk_widget_show_all(item->button);

    return item;
}

void dock_remove_item(Dock *dock, const char *app_id) {
    GList *l = dock->items;
    while (l) {
        DockItem *item = l->data;
        if (g_ascii_strcasecmp(item->app_id, app_id) == 0) {
            if (item->bounce_timer_id) g_source_remove(item->bounce_timer_id);
            gtk_widget_destroy(item->button);
            g_free(item->app_id);
            g_free(item->display_name);
            g_free(item->icon_name);
            g_free(item->desktop_file);
            dock->items = g_list_delete_link(dock->items, l);
            g_free(item);

            update_separator_visibility(dock);
            dock_recalc_size(dock);
            return;
        }
        l = l->next;
    }
}

DockItem *dock_find_item(Dock *dock, const char *app_id) {
    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        if (g_ascii_strcasecmp(item->app_id, app_id) == 0) return item;
    }
    return NULL;
}

void dock_set_item_running(Dock *dock, const char *app_id, bool running) {
    DockItem *item = dock_find_item(dock, app_id);
    if (item && item->running != running) {
        item->running = running;
        gtk_widget_queue_draw(dock->window);
    }
}

/* ── pin / unpin ──────────────────────────────────────────────────── */

void dock_pin_item(Dock *dock, const char *app_id) {
    DockItem *item = dock_find_item(dock, app_id);
    if (!item || item->pinned) return;

    item->pinned = true;

    /* move widget from running_box → pinned_box */
    g_object_ref(item->button);
    gtk_container_remove(GTK_CONTAINER(dock->running_box), item->button);
    gtk_box_pack_start(GTK_BOX(dock->pinned_box), item->button, FALSE, FALSE, 0);
    g_object_unref(item->button);

    update_separator_visibility(dock);
    dock_recalc_size(dock);
    config_save_pinned(dock->items);
}

void dock_unpin_item(Dock *dock, const char *app_id) {
    DockItem *item = dock_find_item(dock, app_id);
    if (!item || !item->pinned) return;

    item->pinned = false;

    if (item->running) {
        /* still running → move to running_box */
        g_object_ref(item->button);
        gtk_container_remove(GTK_CONTAINER(dock->pinned_box), item->button);
        gtk_box_pack_start(GTK_BOX(dock->running_box), item->button, FALSE, FALSE, 0);
        g_object_unref(item->button);
    } else {
        /* not running → remove entirely */
        dock_remove_item(dock, app_id);
    }

    update_separator_visibility(dock);
    dock_recalc_size(dock);
    config_save_pinned(dock->items);
}

/* ── separator visibility ─────────────────────────────────────────── */

static int count_box_children(GtkWidget *box) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(box));
    int n = (int)g_list_length(children);
    g_list_free(children);
    return n;
}

static void update_separator_visibility(Dock *dock) {
    int n_pinned  = count_box_children(dock->pinned_box);
    int n_running = count_box_children(dock->running_box);

    if (n_pinned > 0 && n_running > 0) {
        gtk_widget_set_no_show_all(dock->separator, FALSE);
        gtk_widget_show(dock->separator);
    } else {
        gtk_widget_hide(dock->separator);
        gtk_widget_set_no_show_all(dock->separator, TRUE);
    }
}

/* ── recalculate window size & centre ─────────────────────────────── */

void dock_recalc_size(Dock *dock) {
    int n_items = (int)g_list_length(dock->items);
    if (n_items == 0) return;

    int n_pinned  = count_box_children(dock->pinned_box);
    int n_running = count_box_children(dock->running_box);
    bool has_sep  = (n_pinned > 0 && n_running > 0);

    int item_width = DOCK_ICON_SIZE + 8 + DOCK_ITEM_SPACING;
    int content_w  = n_items * item_width;
    if (has_sep) content_w += DOCK_SEPARATOR_GAP * 2 + 2;  /* separator */
    content_w += DOCK_PADDING_H * 2;

    /* add magnification overflow margin */
    int mag_margin = (DOCK_MAX_MAGNIFIED_SIZE - DOCK_ICON_SIZE) + 8;
    int total_w = content_w + mag_margin;

    /* clamp to screen */
    if (total_w > dock->screen_width - 40) total_w = dock->screen_width - 40;

    int x = (dock->screen_width - total_w) / 2;

    gtk_window_set_default_size(GTK_WINDOW(dock->window), total_w, dock->window_height);
    gtk_window_resize(GTK_WINDOW(dock->window), total_w, dock->window_height);
    gtk_window_move(GTK_WINDOW(dock->window), x, dock->current_y);

    /* reposition items_box centred inside the fixed container */
    int box_x = (total_w - content_w) / 2 + DOCK_PADDING_H;
    gtk_fixed_move(GTK_FIXED(dock->fixed), dock->items_box, box_x, 0);

    gtk_widget_queue_draw(dock->window);
}

/* ── show / hide / toggle ─────────────────────────────────────────── */

void dock_show(Dock *dock) {
    if (!dock->is_hidden && !dock->is_animating) return;
    dock->is_hidden = false;

    if (dock->hide_timer_id) {
        g_source_remove(dock->hide_timer_id);
        dock->hide_timer_id = 0;
    }
    if (dock->anim_timer_id) {
        g_source_remove(dock->anim_timer_id);
        dock->anim_timer_id = 0;
    }

    dock->current_y = dock->hidden_y;
    dock_recalc_size(dock);
    gtk_widget_show_all(dock->window);
    update_separator_visibility(dock);

    dock->is_animating = true;
    dock->anim_timer_id = g_timeout_add(DOCK_ANIM_STEP_MS, on_show_animation, dock);
}

void dock_hide(Dock *dock) {
    if (dock->is_hidden || dock->is_animating) return;

    if (dock->hide_timer_id) {
        g_source_remove(dock->hide_timer_id);
        dock->hide_timer_id = 0;
    }
    if (dock->anim_timer_id) {
        g_source_remove(dock->anim_timer_id);
        dock->anim_timer_id = 0;
    }

    dock->is_animating = true;
    dock->anim_timer_id = g_timeout_add(DOCK_ANIM_STEP_MS, on_hide_animation, dock);
}

void dock_toggle(Dock *dock) {
    if (dock->is_hidden) dock_show(dock);
    else dock_hide(dock);
}

void dock_refresh(Dock *dock) {
    gtk_widget_queue_draw(dock->window);
}

/* ── magnification ────────────────────────────────────────────────── */

static void recalc_magnification(Dock *dock, int mouse_x) {
    if (!dock->items) return;

    /* Get the window x position so we can compute relative mouse pos */
    GdkWindow *win = gtk_widget_get_window(dock->window);
    if (!win) return;
    int win_x;
    gdk_window_get_position(win, &win_x, NULL);

    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        GtkAllocation alloc;
        gtk_widget_get_allocation(item->button, &alloc);

        /* Button centre in screen coordinates */
        int btn_center_x = win_x + alloc.x + alloc.width / 2;
        double dist = fabs((double)(mouse_x - btn_center_x));
        double scale = 1.0;

        if (dist < DOCK_MAGNIFY_RADIUS) {
            double norm = 1.0 - (dist / DOCK_MAGNIFY_RADIUS);
            scale = 1.0 + DOCK_MAGNIFY_STRENGTH * norm * norm;
        }

        double max_scale = (double)DOCK_MAX_MAGNIFIED_SIZE / DOCK_ICON_SIZE;
        if (scale > max_scale) scale = max_scale;

        if (fabs(scale - item->current_scale) < 0.01) {
            l = l->next ? l : l;   /* avoid unnecessary redraw */
            continue;
        }

        item->current_scale = scale;
        int new_size = (int)(DOCK_ICON_SIZE * scale);
        GdkPixbuf *pb = load_icon(item->icon_name, new_size);
        if (pb) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(item->icon_image), pb);
            g_object_unref(pb);
        }
        gtk_widget_set_size_request(item->icon_image, new_size, new_size);
    }
}

static void reset_magnification(Dock *dock) {
    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        item->current_scale = 1.0;
        GdkPixbuf *pb = load_icon(item->icon_name, DOCK_ICON_SIZE);
        if (pb) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(item->icon_image), pb);
            g_object_unref(pb);
        }
        gtk_widget_set_size_request(item->icon_image, DOCK_ICON_SIZE, DOCK_ICON_SIZE);
    }
    gtk_widget_queue_draw(dock->window);
}

/* ── event handlers ───────────────────────────────────────────────── */

static gboolean on_motion_notify(GtkWidget *widget G_GNUC_UNUSED,
                                  GdkEventMotion *event G_GNUC_UNUSED,
                                  gpointer data) {
    Dock *dock = (Dock *)data;
    if (!dock->is_hovered) return FALSE;

    int mx = 0;
    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
    GdkDevice *device = seat ? gdk_seat_get_pointer(seat) : NULL;
    if (device) gdk_device_get_position(device, NULL, &mx, NULL);

    recalc_magnification(dock, mx);
    return FALSE;
}

static gboolean on_window_enter(GtkWidget *widget G_GNUC_UNUSED,
                                 GdkEventCrossing *event G_GNUC_UNUSED,
                                 gpointer data) {
    Dock *dock = (Dock *)data;
    dock->is_hovered = true;

    if (dock->hide_timer_id) {
        g_source_remove(dock->hide_timer_id);
        dock->hide_timer_id = 0;
    }

    int mx = 0;
    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
    GdkDevice *device = seat ? gdk_seat_get_pointer(seat) : NULL;
    if (device) gdk_device_get_position(device, NULL, &mx, NULL);

    recalc_magnification(dock, mx);
    return FALSE;
}

static gboolean on_window_leave(GtkWidget *widget G_GNUC_UNUSED,
                                 GdkEventCrossing *event,
                                 gpointer data) {
    Dock *dock = (Dock *)data;

    /* Ignore sub-window crossings (moving between buttons inside dock) */
    if (event->detail == GDK_NOTIFY_INFERIOR) return FALSE;

    /* Double-check the pointer is really outside our window */
    GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
    GdkDevice *device = seat ? gdk_seat_get_pointer(seat) : NULL;
    if (device) {
        int mx, my;
        gdk_device_get_position(device, NULL, &mx, &my);
        GdkWindow *win = gtk_widget_get_window(dock->window);
        if (win) {
            int wx, wy, ww, wh;
            gdk_window_get_position(win, &wx, &wy);
            ww = gdk_window_get_width(win);
            wh = gdk_window_get_height(win);
            if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + wh)
                return FALSE;
        }
    }

    dock->is_hovered = false;
    reset_magnification(dock);

    if (!dock->hide_timer_id) {
        dock->hide_timer_id = g_timeout_add(DOCK_HIDE_DELAY_MS, on_hide_timeout, dock);
    }
    return FALSE;
}

/* ── right-click context menu ─────────────────────────────────────── */

typedef struct {
    Dock     *dock;
    DockItem *item;
} MenuCtx;

static void on_menu_pin(GtkMenuItem *mi G_GNUC_UNUSED, gpointer data) {
    MenuCtx *mc = data;
    dock_pin_item(mc->dock, mc->item->app_id);
}

static void on_menu_unpin(GtkMenuItem *mi G_GNUC_UNUSED, gpointer data) {
    MenuCtx *mc = data;
    dock_unpin_item(mc->dock, mc->item->app_id);
}

static void on_menu_quit(GtkMenuItem *mi G_GNUC_UNUSED, gpointer data) {
    MenuCtx *mc = data;
    DockItem *item = mc->item;

    if (item->xid) {
        /* Send _NET_CLOSE_WINDOW request */
        GdkDisplay *gdpy = gdk_display_get_default();
        Display *dpy = gdk_x11_display_get_xdisplay(gdpy);

        XEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = (Window)item->xid;
        ev.xclient.message_type = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = CurrentTime;
        ev.xclient.data.l[1] = 2;  /* source = pager */

        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &ev);
        XFlush(dpy);
    } else if (item->pid > 0) {
        kill(item->pid, SIGTERM);
    }
}

static void on_menu_destroy(GtkWidget *menu G_GNUC_UNUSED, gpointer data) {
    g_free(data);  /* free MenuCtx */
}

static gboolean on_item_button_press(GtkWidget *widget,
                                      GdkEventButton *event,
                                      gpointer data) {
    if (event->button != 3) return FALSE;   /* only right-click */

    Dock     *dock = (Dock *)data;
    DockItem *item = g_object_get_data(G_OBJECT(widget), "dock-item");
    if (!item) return FALSE;

    MenuCtx *mc = g_new(MenuCtx, 1);
    mc->dock = dock;
    mc->item = item;

    GtkWidget *menu = gtk_menu_new();
    g_signal_connect(menu, "destroy", G_CALLBACK(on_menu_destroy), mc);

    if (item->pinned) {
        GtkWidget *mi_unpin = gtk_menu_item_new_with_label("Unpin from Dock");
        g_signal_connect(mi_unpin, "activate", G_CALLBACK(on_menu_unpin), mc);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_unpin);
    } else {
        GtkWidget *mi_pin = gtk_menu_item_new_with_label("Pin to Dock");
        g_signal_connect(mi_pin, "activate", G_CALLBACK(on_menu_pin), mc);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_pin);
    }

    if (item->running) {
        GtkWidget *sep = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

        GtkWidget *mi_quit = gtk_menu_item_new_with_label("Quit");
        g_signal_connect(mi_quit, "activate", G_CALLBACK(on_menu_quit), mc);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi_quit);
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
                             GDK_GRAVITY_NORTH, GDK_GRAVITY_SOUTH,
                             (GdkEvent *)event);
    return TRUE;   /* consume the event */
}

/* ── item click (left-click) ──────────────────────────────────────── */

static void raise_window(guint32 xid) {
    GdkDisplay *gdpy = gdk_display_get_default();
    Display *dpy = gdk_x11_display_get_xdisplay(gdpy);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = (Window)xid;
    ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;   /* source = pager */
    ev.xclient.data.l[1] = CurrentTime;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

/* Bounce animation for launched apps */
static gboolean on_bounce_tick(gpointer data) {
    DockItem *item = (DockItem *)data;
    item->bounce_count++;

    double phase = (item->bounce_count % 16) / 16.0 * G_PI * 2.0;
    item->bounce_offset = sin(phase) * 12.0;

    /* Apply the bounce as top-margin on the icon */
    int margin = (int)fabs(item->bounce_offset);
    gtk_widget_set_margin_bottom(item->icon_image, margin);

    if (item->bounce_count >= 48) {
        /* Stop bouncing */
        item->bounce_timer_id = 0;
        item->bounce_offset = 0;
        item->bounce_count = 0;
        gtk_widget_set_margin_bottom(item->icon_image, 0);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void on_item_clicked(GtkWidget *widget G_GNUC_UNUSED, gpointer data) {
    DockItem *item = (DockItem *)data;

    if (item->running && item->xid) {
        /* Already running: raise the window */
        raise_window(item->xid);
    } else if (item->desktop_file) {
        /* Launch the application */
        char *cmd = g_strdup_printf("gtk-launch %s", item->desktop_file);
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);

        /* Start bounce animation */
        if (!item->bounce_timer_id) {
            item->bounce_count = 0;
            item->bounce_timer_id = g_timeout_add(25, on_bounce_tick, item);
        }
    }
}

/* ── background drawing (capsule + indicators) ────────────────────── */

static gboolean on_background_draw(GtkWidget *widget G_GNUC_UNUSED,
                                    cairo_t *cr, gpointer data) {
    Dock *dock = (Dock *)data;

    int alloc_w = gtk_widget_get_allocated_width(dock->fixed);
    int capsule_y = dock->magnify_headroom;
    int capsule_h = dock->capsule_height;
    double r = DOCK_CORNER_RADIUS;

    /* ── draw capsule ─────────────────────────────────────────────── */
    cairo_save(cr);

    double x0 = 2.0, x1 = alloc_w - 2.0;
    double y0 = capsule_y + 0.5, y1 = capsule_y + capsule_h - 0.5;

    cairo_move_to(cr, x0 + r, y0);
    cairo_line_to(cr, x1 - r, y0);
    cairo_arc(cr, x1 - r, y0 + r, r, -G_PI_2, 0);
    cairo_line_to(cr, x1, y1 - r);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, G_PI_2);
    cairo_line_to(cr, x0 + r, y1);
    cairo_arc(cr, x0 + r, y1 - r, r, G_PI_2, G_PI);
    cairo_line_to(cr, x0, y0 + r);
    cairo_arc(cr, x0 + r, y0 + r, r, G_PI, -G_PI_2);
    cairo_close_path(cr);

    /* fill */
    cairo_set_source_rgba(cr,
        dock->bg_color.red, dock->bg_color.green,
        dock->bg_color.blue, dock->bg_color.alpha);
    cairo_fill_preserve(cr);

    /* subtle border */
    cairo_set_source_rgba(cr,
        dock->border_color.red, dock->border_color.green,
        dock->border_color.blue, dock->border_color.alpha);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_restore(cr);

    /* ── draw indicator dots ──────────────────────────────────────── */
    for (GList *l = dock->items; l; l = l->next) {
        DockItem *item = l->data;
        if (!item->running) continue;

        GtkAllocation alloc;
        gtk_widget_get_allocation(item->button, &alloc);

        double dot_x = alloc.x + alloc.width / 2.0;
        double dot_y = capsule_y + capsule_h - DOCK_PADDING_V / 2.0 - DOCK_INDICATOR_RADIUS;

        cairo_set_source_rgba(cr, 0.95, 0.95, 0.95, 0.90);
        cairo_arc(cr, dot_x, dot_y, DOCK_INDICATOR_RADIUS, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    return FALSE;
}

/* ── hide timeout ─────────────────────────────────────────────────── */

static gboolean on_hide_timeout(gpointer data) {
    Dock *dock = (Dock *)data;
    dock->hide_timer_id = 0;
    if (!dock->is_hovered) dock_hide(dock);
    return G_SOURCE_REMOVE;
}

/* ── show / hide animations ───────────────────────────────────────── */

static gboolean on_show_animation(gpointer data) {
    Dock *dock = (Dock *)data;
    int target = dock->visible_y;

    /* ease-out cubic */
    double t = 1.0 - (double)(dock->current_y - target)
                    / (double)(dock->hidden_y  - target);
    t = t + 0.12;
    if (t > 1.0) t = 1.0;

    int new_y = (int)(dock->hidden_y - (dock->hidden_y - target) * t);
    if (new_y <= target) new_y = target;

    dock->current_y = new_y;

    /* Reposition (keep centred) */
    int win_w = 0;
    gtk_window_get_size(GTK_WINDOW(dock->window), &win_w, NULL);
    int x = (dock->screen_width - win_w) / 2;
    gtk_window_move(GTK_WINDOW(dock->window), x, dock->current_y);

    if (dock->current_y <= target) {
        dock->is_animating = false;
        dock->anim_timer_id = 0;
        gtk_widget_queue_draw(dock->window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_hide_animation(gpointer data) {
    Dock *dock = (Dock *)data;
    int target = dock->hidden_y;

    /* ease-in quad */
    double t = (double)(dock->current_y - dock->visible_y)
             / (double)(target          - dock->visible_y);
    t = t + 0.12;
    if (t > 1.0) t = 1.0;

    int new_y = (int)(dock->visible_y + (target - dock->visible_y) * t);
    if (new_y >= target) new_y = target;

    dock->current_y = new_y;

    int win_w = 0;
    gtk_window_get_size(GTK_WINDOW(dock->window), &win_w, NULL);
    int x = (dock->screen_width - win_w) / 2;
    gtk_window_move(GTK_WINDOW(dock->window), x, dock->current_y);

    if (dock->current_y >= target) {
        dock->is_hidden = true;
        dock->is_animating = false;
        dock->anim_timer_id = 0;
        gtk_widget_hide(dock->window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
