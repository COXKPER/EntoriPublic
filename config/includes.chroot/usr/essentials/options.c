#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

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
        ".header-subtitle {"
        "  font-size: 13px; color: #98989d; margin-bottom: 15px;"
        "}"
        ".action-card {"
        "  background-color: rgba(255,255,255,0.06);"
        "  border-radius: 14px; padding: 8px;"
        "  border: 1px solid rgba(255,255,255,0.1);"
        "}"
        "button.mac-btn {"
        "  background-color: transparent; color: #ffffff;"
        "  border-radius: 8px; padding: 12px;"
        "  font-weight: 500; font-size: 14px; border: none;"
        "}"
        "button.mac-btn:hover { background-color: rgba(255,255,255,0.1); }"
        ".btn-danger { color: #FF3B30; font-weight: 600; }"
        ".btn-danger:hover { background-color: rgba(255,59,48,0.15); }"
        ".btn-cancel {"
        "  background-color: rgba(255,255,255,0.06); color: #0A84FF;"
        "  font-weight: 600; border-radius: 12px; margin-top: 10px;"
        "}"
        ".btn-cancel:hover { background-color: rgba(255,255,255,0.12); }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static gboolean confirm(GtkWindow *parent, const char *text)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", text);
    gint r = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return r == GTK_RESPONSE_YES;
}

static void on_shutdown(GtkWidget *btn, GtkWindow *parent)
{
    if (confirm(parent, "Are you sure you want to shut down your computer now?"))
        g_spawn_command_line_async("systemctl poweroff", NULL);
}

static void on_restart(GtkWidget *btn, GtkWindow *parent)
{
    if (confirm(parent, "Are you sure you want to restart your computer now?"))
        g_spawn_command_line_async("systemctl reboot", NULL);
}

static void on_logout(GtkWidget *btn, GtkWindow *parent)
{
    if (confirm(parent, "Are you sure you want to log out from system?"))
        g_spawn_command_line_async("openbox --exit", NULL);
}

static void on_cancel(GtkWidget *btn, GtkWindow *win)
{
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    load_css();

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Power Options");
    gtk_window_set_default_size(GTK_WINDOW(win), 320, 360);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), "close:");
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "System");
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    gtk_widget_set_margin_start(outer, 30);
    gtk_widget_set_margin_end(outer, 30);
    gtk_container_add(GTK_CONTAINER(win), outer);

    GtkWidget *title = gtk_label_new("Power Options");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "header-title");
    gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("What do you want to do?");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "header-subtitle");
    gtk_box_pack_start(GTK_BOX(outer), subtitle, FALSE, FALSE, 0);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "action-card");
    gtk_box_pack_start(GTK_BOX(outer), card, FALSE, FALSE, 10);

    GtkWidget *shutdown_btn = gtk_button_new_with_label("Shut Down");
    gtk_style_context_add_class(gtk_widget_get_style_context(shutdown_btn), "mac-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(shutdown_btn), "btn-danger");
    g_signal_connect(shutdown_btn, "clicked", G_CALLBACK(on_shutdown), win);
    gtk_box_pack_start(GTK_BOX(card), shutdown_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *restart_btn = gtk_button_new_with_label("Restart");
    gtk_style_context_add_class(gtk_widget_get_style_context(restart_btn), "mac-btn");
    g_signal_connect(restart_btn, "clicked", G_CALLBACK(on_restart), win);
    gtk_box_pack_start(GTK_BOX(card), restart_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(card), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    GtkWidget *logout_btn = gtk_button_new_with_label("Log Out");
    gtk_style_context_add_class(gtk_widget_get_style_context(logout_btn), "mac-btn");
    g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout), win);
    gtk_box_pack_start(GTK_BOX(card), logout_btn, TRUE, TRUE, 0);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "mac-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(cancel_btn), "btn-cancel");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel), win);
    gtk_box_pack_end(GTK_BOX(outer), cancel_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}
