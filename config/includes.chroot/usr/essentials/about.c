#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/utsname.h>

static void get_cpu_info(char *model, size_t model_sz, char *vendor, size_t vendor_sz)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (!model[0] && g_str_has_prefix(line, "model name")) {
            char *v = strchr(line, ':');
            if (v) { v++; while (*v == ' ') v++; g_strlcpy(model, v, model_sz); }
            size_t l = strlen(model);
            if (l && model[l-1] == '\n') model[l-1] = '\0';
        }
        if (!vendor[0] && g_str_has_prefix(line, "vendor_id")) {
            char *v = strchr(line, ':');
            if (v) { v++; while (*v == ' ') v++; g_strlcpy(vendor, v, vendor_sz); }
            size_t l = strlen(vendor);
            if (l && vendor[l-1] == '\n') vendor[l-1] = '\0';
        }
    }
    fclose(f);
}

static const char *get_cpu_ascii(const char *vendor)
{
    if (!vendor || !vendor[0]) return "Unknown CPU";
    for (const char *p = vendor; *p; p++) {
        if (tolower((unsigned char)*p) == 'a' && tolower((unsigned char)*(p+1)) == 'm' && tolower((unsigned char)*(p+2)) == 'd')
            return
" █████╗ ███╗   ███╗██████╗ \n"
"██╔══██╗████╗ ████║██╔══██╗\n"
"███████║██╔████╔██║██║  ██║\n"
"██╔══██║██║╚██╔╝██║██║  ██║\n"
"██║  ██║██║ ╚═╝ ██║██████╔╝\n"
"╚═╝  ╚═╝╚═╝     ╚═╝╚═════╝";
        if (tolower((unsigned char)*p) == 'i' && tolower((unsigned char)*(p+1)) == 'n' && tolower((unsigned char)*(p+2)) == 't')
            return
"██╗███╗   ██╗████████╗███████╗██╗     \n"
"██║████╗  ██║╚══██╔══╝██╔════╝██║     \n"
"██║██╔██╗ ██║   ██║   █████╗  ██║     \n"
"██║██║╚██╗██║   ██║   ██╔══╝  ██║     \n"
"██║██║ ╚████║   ██║   ███████╗███████╗\n"
"╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚══════╝";
    }
    return "Unknown CPU";
}

static char *get_ram(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return g_strdup("Unknown");
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (g_str_has_prefix(line, "MemTotal")) {
            long kb = atol(strchr(line, ':') + 1);
            fclose(f);
            double gb = kb / 1024.0 / 1024.0;
            return g_strdup_printf("%.1f GB", gb);
        }
    }
    fclose(f);
    return g_strdup("Unknown");
}

static char *get_os(void)
{
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return g_strdup("Linux");
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (g_str_has_prefix(line, "PRETTY_NAME=")) {
            char *v = strchr(line, '=') + 1;
            size_t l = strlen(v);
            if (l && v[l-1] == '\n') v[l-1] = '\0';
            if (v[0] == '"') { v++; v[strlen(v)-1] = '\0'; }
            fclose(f);
            return g_strdup(v);
        }
    }
    fclose(f);
    return g_strdup("Linux");
}

static void load_css(void)
{
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        "window {"
        "  background-color: #1e1e1e;"
        "  color: #ffffff;"
        "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
        "}"
        ".ascii-logo {"
        "  font-family: monospace;"
        "  color: #0A84FF;"
        "  font-weight: bold;"
        "  font-size: 14px;"
        "}"
        ".os-title {"
        "  font-size: 28px;"
        "  font-weight: 700;"
        "  letter-spacing: -0.5px;"
        "  color: #ffffff;"
        "}"
        ".card {"
        "  background-color: rgba(255,255,255,0.06);"
        "  border-radius: 12px;"
        "  padding: 20px;"
        "  border: 1px solid rgba(255,255,255,0.1);"
        "}"
        ".spec-label {"
        "  color: #98989d;"
        "  font-weight: 600;"
        "  font-size: 13px;"
        "}"
        ".spec-value {"
        "  color: #ffffff;"
        "  font-size: 13px;"
        "}"
        "button.mac-btn {"
        "  background-color: #0A84FF;"
        "  color: white;"
        "  border-radius: 6px;"
        "  padding: 8px 24px;"
        "  font-weight: 600;"
        "  font-size: 13px;"
        "  border: none;"
        "}"
        "button.mac-btn:hover { background-color: #0070ea; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void add_spec(GtkGrid *grid, int row, const char *label, const char *value)
{
    GtkWidget *lbl_k = gtk_label_new(label);
    gtk_widget_set_name(lbl_k, "spec-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_k), "spec-label");
    gtk_widget_set_halign(lbl_k, GTK_ALIGN_END);
    gtk_grid_attach(grid, lbl_k, 0, row, 1, 1);

    GtkWidget *lbl_v = gtk_label_new(value);
    gtk_widget_set_name(lbl_v, "spec-value");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_v), "spec-value");
    gtk_widget_set_halign(lbl_v, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_v), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl_v), 30);
    gtk_grid_attach(grid, lbl_v, 1, row, 1, 1);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    load_css();

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "About This System");
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 520);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "System Info");
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), "close:");
    gtk_window_set_titlebar(GTK_WINDOW(win), header);

    char model[256] = {0}, vendor[256] = {0};
    get_cpu_info(model, sizeof(model), vendor, sizeof(vendor));

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 0);
    gtk_widget_set_margin_top(outer, 30);
    gtk_widget_set_margin_bottom(outer, 30);
    gtk_widget_set_margin_start(outer, 40);
    gtk_widget_set_margin_end(outer, 40);
    gtk_container_add(GTK_CONTAINER(win), outer);

    GtkWidget *ascii = gtk_label_new(get_cpu_ascii(vendor));
    gtk_style_context_add_class(gtk_widget_get_style_context(ascii), "ascii-logo");
    gtk_widget_set_halign(ascii, GTK_ALIGN_CENTER);
    gtk_label_set_justify(GTK_LABEL(ascii), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), ascii, FALSE, FALSE, 0);

    char *os_str = get_os();
    GtkWidget *os_lbl = gtk_label_new(os_str);
    g_free(os_str);
    gtk_style_context_add_class(gtk_widget_get_style_context(os_lbl), "os-title");
    gtk_widget_set_halign(os_lbl, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(outer), os_lbl, FALSE, FALSE, 10);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    gtk_box_pack_start(GTK_BOX(outer), card, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(card), grid);

    char *ram = get_ram();
    struct utsname uts;
    uname(&uts);
    add_spec(GTK_GRID(grid), 0, "Processor", model);
    add_spec(GTK_GRID(grid), 1, "Memory", ram);
    g_free(ram);
    add_spec(GTK_GRID(grid), 2, "Kernel", uts.release);
    add_spec(GTK_GRID(grid), 3, "Computer Name", uts.nodename);
    add_spec(GTK_GRID(grid), 4, "Architecture", uts.machine);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);

    GtkWidget *btn = gtk_button_new_with_label("OK");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "mac-btn");
    g_signal_connect(btn, "clicked", G_CALLBACK(gtk_main_quit), NULL);
    gtk_box_pack_start(GTK_BOX(btn_box), btn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(outer), btn_box, FALSE, FALSE, 10);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}
