#include "dbus_server.h"
#include <gio/gio.h>

static GDBusNodeInfo *introspection_data = NULL;
static GDBusConnection *global_conn = NULL;
static Dock *global_dock = NULL;

static const char introspection_xml[] =
    "<node>"
    "  <interface name='com.mola.Desktop'>"
    "    <method name='Show'/>"
    "    <method name='Hide'/>"
    "    <method name='Toggle'/>"
    "    <method name='AddApp'>"
    "      <arg direction='in' type='s' name='name'/>"
    "      <arg direction='in' type='s' name='icon'/>"
    "    </method>"
    "    <method name='RemoveApp'>"
    "      <arg direction='in' type='s' name='name'/>"
    "    </method>"
    "    <method name='SetRunning'>"
    "      <arg direction='in' type='s' name='name'/>"
    "      <arg direction='in' type='b' name='running'/>"
    "    </method>"
    "    <method name='Refresh'/>"
    "    <property name='Version' type='s' access='read'/>"
    "    <signal name='DockShown'/>"
    "    <signal name='DockHidden'/>"
    "  </interface>"
    "</node>";

static void handle_method_call(GDBusConnection *connection G_GNUC_UNUSED,
                                const gchar *sender_path G_GNUC_UNUSED,
                                const gchar *object_path G_GNUC_UNUSED,
                                const gchar *interface_name G_GNUC_UNUSED,
                                const gchar *method_name,
                                GVariant *parameters,
                                GDBusMethodInvocation *invocation,
                                gpointer user_data) {
    Dock *dock = (Dock *)user_data;

    if (g_strcmp0(method_name, "Show") == 0) {
        dock_show(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Hide") == 0) {
        dock_hide(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Toggle") == 0) {
        dock_toggle(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "AddApp") == 0) {
        const gchar *name = NULL, *icon = NULL;
        g_variant_get(parameters, "(&s&s)", &name, &icon);
        dock_add_item(dock, name, name, icon, NULL, true, false);
        dock_recalc_size(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "RemoveApp") == 0) {
        const gchar *name = NULL;
        g_variant_get(parameters, "(&s)", &name);
        dock_remove_item(dock, name);
        dock_recalc_size(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "SetRunning") == 0) {
        const gchar *name = NULL;
        gboolean running = FALSE;
        g_variant_get(parameters, "(&sb)", &name, &running);
        dock_set_item_running(dock, name, running);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Refresh") == 0) {
        dock_refresh(dock);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.freedesktop.DBus.Error.UnknownMethod", "Unknown method");
    }
}

static GVariant *handle_get_property(GDBusConnection *connection G_GNUC_UNUSED,
                                      const gchar *sender G_GNUC_UNUSED,
                                      const gchar *object_path G_GNUC_UNUSED,
                                      const gchar *interface_name G_GNUC_UNUSED,
                                      const gchar *property_name,
                                      GError **error,
                                      gpointer user_data G_GNUC_UNUSED) {
    if (g_strcmp0(property_name, "Version") == 0) {
        return g_variant_new_string("1.0.0");
    }
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "Unknown property: %s", property_name);
    return NULL;
}

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call,
    handle_get_property,
    NULL,
    {0}
};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name G_GNUC_UNUSED,
                            gpointer user_data) {
    GError *error = NULL;
    GDBusInterfaceInfo *iface =
        g_dbus_node_info_lookup_interface(introspection_data, MOLA_DBUS_INTERFACE);

    g_dbus_connection_register_object(connection, MOLA_DBUS_PATH, iface,
                                      &interface_vtable, user_data, NULL, &error);

    if (error) {
        g_printerr("MoLa D-Bus: Failed to register object: %s\n", error->message);
        g_error_free(error);
    } else {
        g_print("MoLa D-Bus: Registered at %s\n", MOLA_DBUS_PATH);
    }

    global_conn = connection;
}

static void on_name_lost(GDBusConnection *connection G_GNUC_UNUSED,
                         const gchar *name G_GNUC_UNUSED,
                         gpointer user_data G_GNUC_UNUSED) {
}

bool dbus_server_init(Dock *dock) {
    global_dock = dock;
    GError *error = NULL;

    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (!introspection_data) {
        g_printerr("MoLa D-Bus: Failed to parse introspection XML: %s\n",
                    error->message);
        g_error_free(error);
        return false;
    }

    guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, MOLA_DBUS_NAME,
                                     G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                     G_BUS_NAME_OWNER_FLAGS_REPLACE |
                                     G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                     on_bus_acquired, NULL, on_name_lost,
                                     dock, NULL);

    if (owner_id == 0) {
        g_printerr("MoLa D-Bus: Failed to own bus name\n");
        return false;
    }

    return true;
}

void dbus_server_stop(void) {
    if (introspection_data) {
        g_dbus_node_info_unref(introspection_data);
        introspection_data = NULL;
    }
}
