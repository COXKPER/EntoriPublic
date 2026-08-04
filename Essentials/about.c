#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

gchar *get_os_name() {
    gchar *content = NULL;
    if (g_file_get_contents("/etc/os-release", &content, NULL, NULL)) {
        gchar **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "PRETTY_NAME=")) {
                gchar *name = g_strdup(lines[i] + 12);
                g_strdelimit(name, "\"", ' ');
                g_strstrip(name);
                g_strfreev(lines);
                g_free(content);
                return name;
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    return g_strdup("Linux System");
}

gchar *get_cpu_name() {
    gchar *content = NULL;
    if (g_file_get_contents("/proc/cpuinfo", &content, NULL, NULL)) {
        gchar **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "model name")) {
                gchar **parts = g_strsplit(lines[i], ":", 2);
                if (parts[1]) {
                    gchar *cpu = g_strdup(parts[1]);
                    g_strstrip(cpu);
                    g_strfreev(parts);
                    g_strfreev(lines);
                    g_free(content);
                    return cpu;
                }
                g_strfreev(parts);
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    return g_strdup("Unknown CPU");
}

gchar *get_memory_gb() {
    gchar *content = NULL;
    if (g_file_get_contents("/proc/meminfo", &content, NULL, NULL)) {
        gchar **lines = g_strsplit(content, "\n", -1);
        for (int i = 0; lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "MemTotal:")) {
                gchar **parts = g_strsplit(lines[i], ":", 2);
                if (parts[1]) {
                    long kb = atol(parts[1]);
                    gchar *mem = g_strdup_printf("%.1f GB", (float)kb / (1024.0 * 1024.0));
                    g_strfreev(parts);
                    g_strfreev(lines);
                    g_free(content);
                    return mem;
                }
                g_strfreev(parts);
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    return g_strdup("Unknown");
}

static void make_transparent(GtkWidget *wv) {
    GdkRGBA rgba = {0,0,0,0};
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(wv), &rgba);
}

static void on_script_msg(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer window) {
    gtk_widget_destroy(GTK_WIDGET(window));
}

static gboolean on_drag_button_press(GtkWidget *widget, GdkEventButton *event, GdkWindowEdge edge) {
    if (event->button == 1) { // Left click
        gtk_window_begin_move_drag(GTK_WINDOW(widget),
                                   event->button,
                                   event->x_root,
                                   event->y_root,
                                   event->time);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "About This System");
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 320);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    
    // Allow dragging anywhere on the window (since it's an About window)
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(window, "button-press-event", G_CALLBACK(on_drag_button_press), NULL);
    
    // Make window transparent
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(window, visual);
    }
    gtk_widget_set_app_paintable(window, TRUE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *webview = webkit_web_view_new();
    make_transparent(webview);
    gtk_container_add(GTK_CONTAINER(window), webview);

    WebKitUserContentManager *mgr = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(webview));
    g_signal_connect(mgr, "script-message-received::appBridge", G_CALLBACK(on_script_msg), window);
    webkit_user_content_manager_register_script_message_handler(mgr, "appBridge");

    gchar *os_name = get_os_name();
    gchar *cpu_name = get_cpu_name();
    gchar *mem_gb = get_memory_gb();

    // Built-in HTML for macOS "About This Mac" clone
    const gchar *html_template = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "  <style>"
        "    body {"
        "      margin: 0; padding: 0;"
        "      background-color: rgba(240, 240, 240, 0.95);"
        "      border-radius: 12px;"
        "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
        "      color: #333;"
        "      display: flex;"
        "      height: 320px;"
        "      overflow: hidden;"
        "      -webkit-user-select: none;"
        "      backdrop-filter: blur(20px);"
        "    }"
        "    .container {"
        "      display: flex;"
        "      width: 100%;"
        "      padding: 40px 30px;"
        "    }"
        "    .logo-container {"
        "      width: 140px;"
        "      display: flex;"
        "      justify-content: center;"
        "      align-items: flex-start;"
        "    }"
        "    .logo {"
        "      width: 90px;"
        "      height: 90px;"
        "      background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);"
        "      border-radius: 50%;"
        "      box-shadow: 0 10px 20px rgba(0,0,0,0.15);"
        "    }"
        "    .info-container {"
        "      flex: 1;"
        "      padding-left: 10px;"
        "    }"
        "    .os-name {"
        "      font-size: 28px;"
        "      font-weight: 300;"
        "      margin-bottom: 2px;"
        "      letter-spacing: -0.5px;"
        "    }"
        "    .os-version {"
        "      font-size: 13px;"
        "      color: #666;"
        "      margin-bottom: 25px;"
        "    }"
        "    .spec-row {"
        "      display: flex;"
        "      margin-bottom: 8px;"
        "      font-size: 12px;"
        "      line-height: 1.4;"
        "    }"
        "    .spec-label {"
        "      width: 75px;"
        "      text-align: right;"
        "      font-weight: 600;"
        "      margin-right: 12px;"
        "    }"
        "    .spec-value {"
        "      flex: 1;"
        "    }"
        "    .buttons {"
        "      margin-top: 30px;"
        "      display: flex;"
        "      gap: 10px;"
        "    }"
        "    button {"
        "      background-color: #fff;"
        "      border: 1px solid #d1d1d1;"
        "      border-radius: 6px;"
        "      padding: 4px 16px;"
        "      font-size: 12px;"
        "      box-shadow: 0 1px 1px rgba(0,0,0,0.05);"
        "      cursor: pointer;"
        "      transition: all 0.1s;"
        "    }"
        "    button:active {"
        "      background-color: #e5e5e5;"
        "    }"
        "    /* Fake window controls */"
        "    .titlebar {"
        "      position: absolute;"
        "      top: 0; left: 0; right: 0;"
        "      height: 28px;"
        "      display: flex;"
        "      align-items: center;"
        "      padding-left: 12px;"
        "      gap: 8px;"
        "      -webkit-app-region: drag;"
        "    }"
        "    .btn-mac {"
        "      width: 12px; height: 12px; border-radius: 50%;"
        "      -webkit-app-region: no-drag;"
        "    }"
        "    .btn-close { background-color: #ff5f56; border: 1px solid #e0443e; cursor: pointer; }"
        "    .btn-min { background-color: #ffbd2e; border: 1px solid #dea123; }"
        "    .btn-max { background-color: #27c93f; border: 1px solid #1aab29; }"
        "  </style>"
        "</head>"
        "<body>"
        "  <div class='titlebar'>"
        "    <div class='btn-mac btn-close' onclick='window.webkit.messageHandlers.appBridge.postMessage(\"close\")'></div>"
        "    <div class='btn-mac btn-min'></div>"
        "    <div class='btn-mac btn-max'></div>"
        "  </div>"
        "  <div class='container'>"
        "    <div class='logo-container'>"
        "      <div class='logo'></div>"
        "    </div>"
        "    <div class='info-container'>"
        "      <div class='os-name'>%s</div>"
        "      <div class='os-version'>Version 1.0</div>"
        "      <div class='spec-row'>"
        "        <div class='spec-label'>Processor</div>"
        "        <div class='spec-value'>%s</div>"
        "      </div>"
        "      <div class='spec-row'>"
        "        <div class='spec-label'>Memory</div>"
        "        <div class='spec-value'>%s</div>"
        "      </div>"
        "      <div class='spec-row'>"
        "        <div class='spec-label'>Graphics</div>"
        "        <div class='spec-value'>System Graphics</div>"
        "      </div>"
        "      <div class='buttons'>"
        "        <button>System Report...</button>"
        "        <button>Software Update...</button>"
        "      </div>"
        "    </div>"
        "  </div>"
        "  <script>"
        "    // If window.close() doesn't work in WebKitGTK directly without JS bindings, we can do nothing for now"
        "  </script>"
        "</body>"
        "</html>";

    gchar *final_html = g_strdup_printf(html_template, os_name, cpu_name, mem_gb);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(webview), final_html, NULL);

    g_free(os_name);
    g_free(cpu_name);
    g_free(mem_gb);
    g_free(final_html);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
