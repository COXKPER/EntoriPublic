#include "sni_host.h"
#include <gio/gio.h>
#include <libdbusmenu-gtk/dbusmenu-gtk.h>

static GtkWidget *g_tray_container = NULL;
static GDBusConnection *g_bus = NULL;
static guint watcher_id = 0;
static GHashTable *items = NULL; // path -> GtkWidget*

static const gchar *watcher_xml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg type='s' name='arg0'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg type='s' name='arg0'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

typedef struct {
    gchar *service;
    GtkWidget *menu;
} SNIItem;

static void sni_item_free(SNIItem *item) {
    g_free(item->service);
    if (item->menu) gtk_widget_destroy(item->menu);
    g_free(item);
}

static gboolean item_btn_pressed(GtkWidget *btn, GdkEventButton *event, gpointer data) {
    SNIItem *item = (SNIItem *)data;
    if (event->button == 1) {
        g_dbus_connection_call(g_bus, item->service, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate",
            g_variant_new("(ii)", event->x_root, event->y_root), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
        return TRUE;
    } else if (event->button == 3) {
        if (item->menu) {
            gtk_widget_show_all(item->menu);
            gtk_menu_popup_at_pointer(GTK_MENU(item->menu), (GdkEvent *)event);
            return TRUE;
        } else {
            g_dbus_connection_call(g_bus, item->service, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "ContextMenu",
                g_variant_new("(ii)", event->x_root, event->y_root), NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
            return TRUE;
        }
    }
    return FALSE;
}

static void add_item(const gchar *service) {
    if (g_hash_table_contains(items, service)) return;

    SNIItem *sni = g_new0(SNIItem, 1);
    sni->service = g_strdup(service);

    // Fetch icon name
    GVariant *res = g_dbus_connection_call_sync(g_bus, service, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.kde.StatusNotifierItem", "IconName"), NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    
    GtkWidget *img;
    if (res) {
        GVariant *v = g_variant_get_child_value(res, 0);
        const gchar *icon_name = g_variant_get_string(v, NULL);
        img = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        g_variant_unref(v);
        g_variant_unref(res);
    } else {
        img = gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_MENU);
    }

    // Fetch Menu property
    GVariant *res_menu = g_dbus_connection_call_sync(g_bus, service, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.kde.StatusNotifierItem", "Menu"), NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
    
    if (res_menu) {
        GVariant *v = g_variant_get_child_value(res_menu, 0);
        const gchar *menu_path = g_variant_get_string(v, NULL);
        if (menu_path && menu_path[0] == '/') {
            // Check if service name is full path or just DBus name
            gchar *dbus_name = g_strdup(service);
            gchar *slash = strchr(dbus_name, '/');
            if (slash) *slash = '\0';
            sni->menu = GTK_WIDGET(dbusmenu_gtkmenu_new(dbus_name, (gchar *)menu_path));
            g_free(dbus_name);
        }
        g_variant_unref(v);
        g_variant_unref(res_menu);
    }

    GtkWidget *btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(btn), img);
    
    g_signal_connect(btn, "button-press-event", G_CALLBACK(item_btn_pressed), sni);
    gtk_widget_add_events(btn, GDK_BUTTON_PRESS_MASK);

    gtk_box_pack_start(GTK_BOX(g_tray_container), btn, FALSE, FALSE, 2);
    gtk_widget_show_all(btn);
    
    g_object_set_data_full(G_OBJECT(btn), "sni-item", sni, (GDestroyNotify)sni_item_free);
    g_hash_table_insert(items, g_strdup(service), btn);
}

static void handle_method_call(GDBusConnection *conn, const gchar *sender, const gchar *obj, const gchar *iface,
    const gchar *method, GVariant *params, GDBusMethodInvocation *inv, gpointer data) {
    if (g_strcmp0(method, "RegisterStatusNotifierItem") == 0) {
        const gchar *service;
        g_variant_get(params, "(&s)", &service);
        
        // If it's just a path, construct full address
        gchar *full_service = g_strdup(service);
        if (service[0] == '/') {
            g_free(full_service);
            full_service = g_strdup_printf("%s%s", sender, service);
        }
        
        add_item(full_service);
        
        g_dbus_connection_emit_signal(conn, NULL, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
            "StatusNotifierItemRegistered", g_variant_new("(s)", full_service), NULL);
        
        g_dbus_method_invocation_return_value(inv, NULL);
        g_free(full_service);
    }
}

static GVariant *handle_get_prop(GDBusConnection *conn, const gchar *sender, const gchar *obj, const gchar *iface,
    const gchar *prop, GError **err, gpointer data) {
    if (g_strcmp0(prop, "IsStatusNotifierHostRegistered") == 0) {
        return g_variant_new_boolean(TRUE);
    }
    if (g_strcmp0(prop, "RegisteredStatusNotifierItems") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
        GHashTableIter iter;
        gpointer key, val;
        g_hash_table_iter_init(&iter, items);
        while (g_hash_table_iter_next(&iter, &key, &val)) {
            g_variant_builder_add(&b, "s", (const gchar *)key);
        }
        return g_variant_builder_end(&b);
    }
    return NULL;
}

static const GDBusInterfaceVTable watcher_vtable = {
    handle_method_call, handle_get_prop, NULL
};

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer data) {
    g_bus = conn;
    GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(watcher_xml, NULL);
    g_dbus_connection_register_object(conn, "/StatusNotifierWatcher", node_info->interfaces[0],
        &watcher_vtable, NULL, NULL, NULL);
    g_dbus_node_info_unref(node_info);
}

void sni_host_init(GtkWidget *tray_container) {
    g_tray_container = tray_container;
    items = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)gtk_widget_destroy);
    watcher_id = g_bus_own_name(G_BUS_TYPE_SESSION, "org.kde.StatusNotifierWatcher",
        G_BUS_NAME_OWNER_FLAGS_REPLACE, on_bus_acquired, NULL, NULL, NULL, NULL);
}
