#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

static void make_transparent(GtkWidget *wv) {
    GdkRGBA rgba = {0,0,0,0};
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(wv), &rgba);
}

static void on_script_msg(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer window) {
    JSCValue *val = webkit_javascript_result_get_js_value(js_result);
    if (jsc_value_is_string(val)) {
        gchar *cmd = jsc_value_to_string(val);
        if (g_strcmp0(cmd, "shutdown") == 0) {
            g_spawn_command_line_async("systemctl poweroff", NULL);
            gtk_main_quit();
        } else if (g_strcmp0(cmd, "restart") == 0) {
            g_spawn_command_line_async("systemctl reboot", NULL);
            gtk_main_quit();
        } else if (g_strcmp0(cmd, "logout") == 0) {
            g_spawn_command_line_async("openbox --exit", NULL);
            gtk_main_quit();
        } else if (g_strcmp0(cmd, "cancel") == 0) {
            gtk_main_quit();
        }
        g_free(cmd);
    }
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Power Options");
    
    // Make window fullscreen and borderless to allow dimming the whole screen
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window));
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);

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

    const gchar *html_content = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "  <style>"
        "    body {"
        "      margin: 0; padding: 0;"
        "      background-color: rgba(0, 0, 0, 0.45);"
        "      display: flex;"
        "      justify-content: center;"
        "      align-items: center;"
        "      height: 100vh;"
        "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
        "      -webkit-user-select: none;"
        "      animation: fadeIn 0.3s ease-out;"
        "    }"
        "    @keyframes fadeIn {"
        "      from { background-color: rgba(0,0,0,0); }"
        "      to { background-color: rgba(0,0,0,0.45); }"
        "    }"
        "    @keyframes popIn {"
        "      from { transform: scale(0.9); opacity: 0; }"
        "      to { transform: scale(1); opacity: 1; }"
        "    }"
        "    .dialog {"
        "      background-color: rgba(245, 245, 247, 0.85);"
        "      border-radius: 14px;"
        "      padding: 24px;"
        "      width: 300px;"
        "      text-align: center;"
        "      box-shadow: 0 10px 40px rgba(0,0,0,0.3);"
        "      backdrop-filter: blur(20px);"
        "      animation: popIn 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);"
        "    }"
        "    .title {"
        "      font-size: 14px;"
        "      font-weight: 600;"
        "      margin-bottom: 8px;"
        "      color: #1d1d1f;"
        "    }"
        "    .subtitle {"
        "      font-size: 13px;"
        "      color: #86868b;"
        "      margin-bottom: 24px;"
        "    }"
        "    .btn-group {"
        "      display: flex;"
        "      flex-direction: column;"
        "      gap: 1px;"
        "      background-color: rgba(0, 0, 0, 0.1);"
        "      border-radius: 10px;"
        "      overflow: hidden;"
        "      margin-bottom: 16px;"
        "    }"
        "    button {"
        "      background-color: rgba(255, 255, 255, 0.6);"
        "      border: none;"
        "      padding: 12px;"
        "      font-size: 14px;"
        "      cursor: pointer;"
        "      transition: background-color 0.1s;"
        "      color: #1d1d1f;"
        "      outline: none;"
        "    }"
        "    button:hover { background-color: rgba(255, 255, 255, 0.9); }"
        "    button:active { background-color: rgba(0, 0, 0, 0.05); }"
        "    .btn-danger { color: #ff3b30; font-weight: 500; }"
        "    .btn-cancel {"
        "      border-radius: 10px;"
        "      background-color: rgba(255, 255, 255, 0.7);"
        "      font-weight: 600;"
        "      color: #007aff;"
        "      width: 100%;"
        "      border: 1px solid rgba(0, 0, 0, 0.05);"
        "    }"
        "    .btn-cancel:hover { background-color: rgba(255, 255, 255, 0.9); }"
        "  </style>"
        "</head>"
        "<body>"
        "  <div class='dialog'>"
        "    <div class='title'>Are you sure you want to shut down your computer now?</div>"
        "    <div class='subtitle'>Select an option to continue.</div>"
        "    <div class='btn-group'>"
        "      <button class='btn-danger' onclick='sendCmd(\"shutdown\")'>Shut Down</button>"
        "      <button onclick='sendCmd(\"restart\")'>Restart</button>"
        "      <button onclick='sendCmd(\"logout\")'>Log Out</button>"
        "    </div>"
        "    <button class='btn-cancel' onclick='sendCmd(\"cancel\")'>Cancel</button>"
        "  </div>"
        "  <script>"
        "    function sendCmd(cmd) {"
        "        window.webkit.messageHandlers.appBridge.postMessage(cmd);"
        "    }"
        "    // Close when clicking the dim background"
        "    document.body.addEventListener('click', function(e) {"
        "        if (e.target === document.body) sendCmd('cancel');"
        "    });"
        "  </script>"
        "</body>"
        "</html>";

    webkit_web_view_load_html(WEBKIT_WEB_VIEW(webview), html_content, NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
