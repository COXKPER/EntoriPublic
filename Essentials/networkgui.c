#include <gtk/gtk.h>
#include <NetworkManager.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *tree_view;
    GtkListStore *list_store;
    NMClient *client;
    NMDeviceWifi *wifi_device;
} AppData;

enum {
    COL_SSID = 0,
    COL_STRENGTH,
    COL_AP_PTR,
    NUM_COLS
};

static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css_data = 
        "window { background-color: #1e1e1e; color: #eeeeec; }\n"
        "treeview { background-color: #252526; color: #eeeeec; }\n"
        "treeview:selected { background-color: #094771; color: #ffffff; }\n"
        "button { background-image: none; background-color: #0e639c; color: #ffffff; border-radius: 4px; padding: 8px; }\n"
        "button:hover { background-color: #1177bb; }\n"
        "entry { background-color: #3c3c3c; color: #eeeeec; border: 1px solid #000000; }";
    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static gboolean is_same_ssid(GBytes *b1, GBytes *b2) {
    if (!b1 || !b2) return FALSE;
    gsize len1, len2;
    const guint8 *d1 = g_bytes_get_data(b1, &len1);
    const guint8 *d2 = g_bytes_get_data(b2, &len2);
    if (len1 != len2) return FALSE;
    return memcmp(d1, d2, len1) == 0;
}

static void update_ap_list(AppData *app) {
    gtk_list_store_clear(app->list_store);
    if (!app->wifi_device) return;
    
    const GPtrArray *aps = nm_device_wifi_get_access_points(app->wifi_device);
    if (!aps) return;
    
    GHashTable *seen_ssids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    
    for (guint i = 0; i < aps->len; i++) {
        NMAccessPoint *ap = g_ptr_array_index(aps, i);
        GBytes *ssid_bytes = nm_access_point_get_ssid(ap);
        if (!ssid_bytes) continue;
        
        gsize ssid_len;
        const guint8 *ssid_data = g_bytes_get_data(ssid_bytes, &ssid_len);
        if (ssid_len == 0) continue;
        
        char *ssid_str = g_strndup((const char *)ssid_data, ssid_len);
        
        if (g_hash_table_contains(seen_ssids, ssid_str)) {
            g_free(ssid_str);
            continue;
        }
        g_hash_table_add(seen_ssids, g_strdup(ssid_str));
        
        guint8 strength = nm_access_point_get_strength(ap);
        
        GtkTreeIter iter;
        gtk_list_store_append(app->list_store, &iter);
        gtk_list_store_set(app->list_store, &iter,
                           COL_SSID, ssid_str,
                           COL_STRENGTH, (gint)strength,
                           COL_AP_PTR, ap,
                           -1);
        
        g_free(ssid_str);
    }
    
    g_hash_table_destroy(seen_ssids);
}

static void create_and_connect(AppData *app, NMAccessPoint *ap, const char *ssid_str, const char *password) {
    NMConnection *connection = nm_simple_connection_new();
    
    NMSettingConnection *s_con = (NMSettingConnection *)nm_setting_connection_new();
    char *uuid = nm_utils_uuid_generate();
    g_object_set(s_con,
                 NM_SETTING_CONNECTION_ID, ssid_str,
                 NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
                 NM_SETTING_CONNECTION_AUTOCONNECT, TRUE,
                 NULL);
    g_free(uuid);
    nm_connection_add_setting(connection, NM_SETTING(s_con));

    NMSettingWireless *s_wifi = (NMSettingWireless *)nm_setting_wireless_new();
    GBytes *ssid_bytes = nm_access_point_get_ssid(ap);
    g_object_set(s_wifi, NM_SETTING_WIRELESS_SSID, ssid_bytes, NULL);
    nm_connection_add_setting(connection, NM_SETTING(s_wifi));

    if (password && strlen(password) > 0) {
        NMSettingWirelessSecurity *s_wsec = (NMSettingWirelessSecurity *)nm_setting_wireless_security_new();
        NM80211ApFlags flags = nm_access_point_get_flags(ap);
        NM80211ApSecurityFlags wpa_flags = nm_access_point_get_wpa_flags(ap);
        NM80211ApSecurityFlags rsn_flags = nm_access_point_get_rsn_flags(ap);
        
        if ((rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK) ||
            (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_PSK)) {
            g_object_set(s_wsec,
                         NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                         NM_SETTING_WIRELESS_SECURITY_PSK, password,
                         NULL);
        } else if ((rsn_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X) ||
                   (wpa_flags & NM_802_11_AP_SEC_KEY_MGMT_802_1X)) {
            g_object_set(s_wsec,
                         NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-eap",
                         NULL);
        } else if (flags & NM_802_11_AP_FLAGS_PRIVACY) {
            g_object_set(s_wsec,
                         NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "none",
                         NM_SETTING_WIRELESS_SECURITY_WEP_KEY0, password,
                         NM_SETTING_WIRELESS_SECURITY_WEP_KEY_TYPE, 1,
                         NULL);
        }
        nm_connection_add_setting(connection, NM_SETTING(s_wsec));
    }

    nm_client_add_and_activate_connection_async(app->client, connection, NM_DEVICE(app->wifi_device), nm_object_get_path(NM_OBJECT(ap)), NULL, NULL, NULL);
    g_object_unref(connection);
}

static void prompt_password_and_connect(AppData *app, NMAccessPoint *ap, const char *ssid_str, gboolean requires_password) {
    if (requires_password) {
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Wi-Fi Password",
                                                        GTK_WINDOW(app->window),
                                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                        "_Cancel", GTK_RESPONSE_CANCEL,
                                                        "_Connect", GTK_RESPONSE_ACCEPT,
                                                        NULL);
        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        
        GtkWidget *label = gtk_label_new("Enter Wi-Fi Password:");
        gtk_box_pack_start(GTK_BOX(content_area), label, FALSE, FALSE, 5);
        
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 5);
        
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        
        gtk_widget_show_all(dialog);
        
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_ACCEPT) {
            const char *password = gtk_entry_get_text(GTK_ENTRY(entry));
            create_and_connect(app, ap, ssid_str, password);
        }
        gtk_widget_destroy(dialog);
    } else {
        create_and_connect(app, ap, ssid_str, NULL);
    }
}

static void on_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
    AppData *app = (AppData *)user_data;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(app->list_store), &iter, path)) {
        char *ssid_str = NULL;
        NMAccessPoint *ap = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(app->list_store), &iter,
                           COL_SSID, &ssid_str,
                           COL_AP_PTR, &ap,
                           -1);
                           
        if (ssid_str && ap) {
            const GPtrArray *connections = nm_client_get_connections(app->client);
            NMConnection *existing_conn = NULL;
            GBytes *ssid_bytes = nm_access_point_get_ssid(ap);
            
            for (guint i = 0; connections && i < connections->len; i++) {
                NMConnection *c = g_ptr_array_index(connections, i);
                NMSettingWireless *s_wifi = nm_connection_get_setting_wireless(c);
                if (s_wifi) {
                    GBytes *c_ssid = nm_setting_wireless_get_ssid(s_wifi);
                    if (is_same_ssid(c_ssid, ssid_bytes)) {
                        existing_conn = c;
                        break;
                    }
                }
            }
            
            if (existing_conn) {
                nm_client_activate_connection_async(app->client, existing_conn, NM_DEVICE(app->wifi_device), nm_object_get_path(NM_OBJECT(ap)), NULL, NULL, NULL);
            } else {
                NM80211ApFlags flags = nm_access_point_get_flags(ap);
                gboolean requires_password = (flags & NM_802_11_AP_FLAGS_PRIVACY) != 0;
                prompt_password_and_connect(app, ap, ssid_str, requires_password);
            }
        }
        g_free(ssid_str);
    }
}

static gboolean on_tree_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    if (event->button == 1) {
        GtkTreePath *path;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y, &path, NULL, NULL, NULL)) {
            on_row_activated(GTK_TREE_VIEW(widget), path, NULL, user_data);
            gtk_tree_path_free(path);
        }
    }
    return FALSE;
}

static void on_bluetooth_clicked(GtkButton *button, gpointer user_data) {
    g_spawn_command_line_async("blueman-manager", NULL);
    gtk_main_quit();
}

static void on_client_new(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GError *error = NULL;
    NMClient *client = nm_client_new_finish(res, &error);
    if (!client) {
        g_printerr("Error initializing NMClient: %s\n", error->message);
        if (error) g_error_free(error);
        return;
    }
    
    AppData *app = (AppData *)user_data;
    app->client = client;
    
    const GPtrArray *devices = nm_client_get_devices(client);
    for (guint i = 0; devices && i < devices->len; i++) {
        NMDevice *dev = g_ptr_array_index(devices, i);
        if (NM_IS_DEVICE_WIFI(dev)) {
            app->wifi_device = NM_DEVICE_WIFI(dev);
            break;
        }
    }
    
    if (app->wifi_device) {
        // Request a new scan asynchronously (errors are ignored as it might require privileges)
        nm_device_wifi_request_scan_async(app->wifi_device, NULL, NULL, NULL);
        
        update_ap_list(app);
        g_signal_connect_swapped(app->wifi_device, "access-point-added", G_CALLBACK(update_ap_list), app);
        g_signal_connect_swapped(app->wifi_device, "access-point-removed", G_CALLBACK(update_ap_list), app);
    } else {
        g_printerr("No Wi-Fi device found.\n");
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_css();

    AppData *app = g_new0(AppData, 1);

    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "Network Manager");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 350, 450);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    app->list_store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_POINTER);
    app->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->list_store));
    g_object_unref(app->list_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col_ssid = gtk_tree_view_column_new_with_attributes("SSID", renderer, "text", COL_SSID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree_view), col_ssid);

    GtkCellRenderer *renderer_strength = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col_strength = gtk_tree_view_column_new_with_attributes("Strength (%)", renderer_strength, "text", COL_STRENGTH, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree_view), col_strength);

    g_signal_connect(app->tree_view, "row-activated", G_CALLBACK(on_row_activated), app);
    g_signal_connect(app->tree_view, "button-release-event", G_CALLBACK(on_tree_button_release), app);
    gtk_container_add(GTK_CONTAINER(scrolled_window), app->tree_view);

    GtkWidget *bt_button = gtk_button_new_with_label("Bluetooth Settings");
    gtk_box_pack_start(GTK_BOX(vbox), bt_button, FALSE, FALSE, 10);
    g_signal_connect(bt_button, "clicked", G_CALLBACK(on_bluetooth_clicked), NULL);

    nm_client_new_async(NULL, on_client_new, app);

    gtk_widget_show_all(app->window);
    gtk_main();

    if (app->client) g_object_unref(app->client);
    g_free(app);

    return 0;
}
