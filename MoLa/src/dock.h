#ifndef MOLA_DOCK_H
#define MOLA_DOCK_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdbool.h>

/* ── macOS-style dock geometry ───────────────────────────────────── */
#define DOCK_ICON_SIZE            48
#define DOCK_MAX_MAGNIFIED_SIZE   72
#define DOCK_ITEM_SPACING         2
#define DOCK_PADDING_H            14
#define DOCK_PADDING_V            8
#define DOCK_MARGIN_BOTTOM        6
#define DOCK_CORNER_RADIUS        18
#define DOCK_SEPARATOR_GAP        8
#define DOCK_INDICATOR_RADIUS     2.5
#define DOCK_INDICATOR_GAP        4

/* ── animation & behaviour ───────────────────────────────────────── */
#define DOCK_HIDE_DELAY_MS        800
#define DOCK_ANIM_STEP_MS         16
#define DOCK_TRIGGER_ZONE_PX      4
#define DOCK_MAGNIFY_RADIUS       130.0
#define DOCK_MAGNIFY_STRENGTH     0.50

/* ── forward declaration ─────────────────────────────────────────── */
typedef struct Dock Dock;

/* ── dock item ───────────────────────────────────────────────────── */
typedef struct DockItem {
    char     *app_id;           /* unique key  (lower-case WM_CLASS or slug) */
    char     *display_name;     /* human-readable name                       */
    char     *icon_name;        /* icon theme name or absolute path          */
    char     *desktop_file;     /* e.g. "firefox.desktop"                    */
    bool      running;
    bool      pinned;
    guint32   xid;              /* X11 window id (0 when not running)        */
    pid_t     pid;              /* process id    (0 when not running)        */

    /* widgets */
    GtkWidget *button;
    GtkWidget *icon_image;
    double     current_scale;

    /* bounce-on-launch */
    int        bounce_count;
    guint      bounce_timer_id;
    double     bounce_offset;
} DockItem;

/* ── dock ────────────────────────────────────────────────────────── */
struct Dock {
    GtkWidget *window;
    GtkWidget *fixed;           /* custom-drawn background layer             */
    GtkWidget *items_box;       /* outer HBox: pinned | sep | running        */
    GtkWidget *pinned_box;
    GtkWidget *separator;       /* vertical divider (GtkSeparator)           */
    GtkWidget *running_box;
    GList     *items;

    int  screen_width;
    int  screen_height;
    int  capsule_height;        /* height of the dock pill                   */
    int  magnify_headroom;      /* extra pixels above pill for magnification */
    int  window_height;         /* capsule_height + magnify_headroom         */

    int  hidden_y;
    int  visible_y;
    int  current_y;

    bool  is_hidden;
    bool  is_hovered;
    bool  is_animating;
    guint hide_timer_id;
    guint anim_timer_id;

    GdkRGBA bg_color;
    GdkRGBA border_color;

    GDBusConnection *dbus_conn;
};

/* ── public API ──────────────────────────────────────────────────── */
Dock      *dock_new(void);
void       dock_free(Dock *dock);

DockItem  *dock_add_item(Dock *dock, const char *app_id,
                         const char *display_name, const char *icon_name,
                         const char *desktop_file, bool pinned, bool running);
void       dock_remove_item(Dock *dock, const char *app_id);
DockItem  *dock_find_item(Dock *dock, const char *app_id);

void       dock_set_item_running(Dock *dock, const char *app_id, bool running);
void       dock_pin_item(Dock *dock, const char *app_id);
void       dock_unpin_item(Dock *dock, const char *app_id);

void       dock_show(Dock *dock);
void       dock_hide(Dock *dock);
void       dock_toggle(Dock *dock);
void       dock_refresh(Dock *dock);
void       dock_recalc_size(Dock *dock);

#endif /* MOLA_DOCK_H */
