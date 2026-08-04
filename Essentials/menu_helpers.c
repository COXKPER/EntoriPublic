/* ════════════════════════════════════════════════
 *  Menu helpers (ported from menu.c)
 *  Included directly into main.c
 * ════════════════════════════════════════════════ */
#define DOCK_DIR  ".config/favorit-anak-sd"
#define DOCK_FILE "here.desktop"

static const char *app_dirs[] = {
    "/usr/share/applications",
    "/usr/local/share/applications",
    NULL
};

static char *dock_path(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/root";
    return g_strdup_printf("%s/%s/%s", home, DOCK_DIR, DOCK_FILE);
}

static GPtrArray *read_dock(void) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    char *path = dock_path();
    FILE *f = fopen(path, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            if (l && line[l-1] == '\n') line[l-1] = '\0';
            if (line[0]) g_ptr_array_add(arr, g_strdup(line));
        }
        fclose(f);
    }
    g_free(path);
    return arr;
}

static void write_dock(GPtrArray *arr) {
    char *path = dock_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    FILE *f = fopen(path, "w");
    if (f) {
        for (guint i = 0; i < arr->len; i++)
            fprintf(f, "%s\n", (char *)g_ptr_array_index(arr, i));
        fclose(f);
    }
    g_free(path);
}

static gboolean in_dock(GPtrArray *arr, const char *name) {
    for (guint i = 0; i < arr->len; i++)
        if (!g_strcmp0((char *)g_ptr_array_index(arr, i), name)) return TRUE;
    return FALSE;
}

static void toggle_dock(GtkWidget *mi, gpointer data) {
    const char *desktop = (const char *)data;
    GPtrArray *entries = read_dock();
    if (in_dock(entries, desktop)) {
        GPtrArray *new_e = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < entries->len; i++) {
            if (g_strcmp0((char *)g_ptr_array_index(entries, i), desktop) != 0)
                g_ptr_array_add(new_e, g_strdup((char *)g_ptr_array_index(entries, i)));
        }
        write_dock(new_e);
        g_ptr_array_unref(new_e);
    } else {
        g_ptr_array_add(entries, g_strdup(desktop));
        write_dock(entries);
    }
    g_ptr_array_unref(entries);
}

static char *desktop_key(const char *path, const char *key) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char line[512];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            char *v = line + klen + 1;
            size_t l = strlen(v);
            if (l && v[l-1] == '\n') v[l-1] = '\0';
            fclose(f);
            return g_strdup(v);
        }
    }
    fclose(f);
    return NULL;
}

static char *find_desktop_for(const char *wm_class, const char *app_name) {
    for (int di = 0; app_dirs[di]; di++) {
        GDir *dir = g_dir_open(app_dirs[di], 0, NULL);
        if (!dir) continue;
        const char *fname;
        while ((fname = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(fname, ".desktop")) continue;
            char *fpath = g_build_filename(app_dirs[di], fname, NULL);

            if (wm_class) {
                char *val = desktop_key(fpath, "StartupWMClass");
                if (val && g_ascii_strcasecmp(val, wm_class) == 0) {
                    g_free(val); g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(val);
            }

            if (app_name) {
                char *val = desktop_key(fpath, "Name");
                if (val && g_ascii_strcasecmp(val, app_name) == 0) {
                    g_free(val); g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(val);
                char *lower = g_ascii_strdown(app_name, -1);
                char *lower_fname = g_ascii_strdown(fname, -1);
                char *dash = g_strdelimit(g_strdup(lower), " ", '-');
                if (strstr(lower_fname, dash)) {
                    g_free(dash); g_free(lower_fname); g_free(lower);
                    g_free(fpath); g_dir_close(dir);
                    return g_strdup(fname);
                }
                g_free(dash); g_free(lower_fname); g_free(lower);
            }
            g_free(fpath);
        }
        g_dir_close(dir);
    }
    return NULL;
}

/* ── Menu position: below the navbar bar ── */
static void position_menu_below(GtkMenu *menu, gint *x, gint *y,
                                gboolean *push_in, gpointer user_data)
{
    gint offset_x = GPOINTER_TO_INT(user_data);
    GdkWindow *gdk_win = gtk_widget_get_window(main_window);
    if (!gdk_win) return;
    gint wx, wy;
    gdk_window_get_origin(gdk_win, &wx, &wy);

    GtkRequisition req;
    gtk_widget_get_preferred_size(GTK_WIDGET(menu), NULL, &req);

    GdkScreen *screen = gdk_window_get_screen(gdk_win);
    gint mon = gdk_screen_get_monitor_at_point(screen, wx, wy);
    GdkRectangle geo;
    gdk_screen_get_monitor_geometry(screen, mon, &geo);

    gint px = wx + offset_x;
    gint py = wy + 42;

    if (px + req.width > geo.x + geo.width) px = geo.x + geo.width - req.width;
    if (px < geo.x) px = geo.x;

    *x = px;
    *y = py;
    *push_in = TRUE;
}

/* ── Launch helpers ── */
static void launch_terminal(void)   { g_spawn_command_line_async("x-terminal-emulator", NULL); }
static void launch_files(void) {
    const char *home = getenv("HOME");
    char *cmd = g_strdup_printf("pcmanfm %s", home ? home : "/");
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}
static void launch_run(void)   { g_spawn_command_line_async("rofi -show drun", NULL); }
static void launch_power(void) { g_spawn_command_line_async("/usr/essentials/options", NULL); }
static void launch_about(void) { g_spawn_command_line_async("/usr/essentials/about", NULL); }

extern gboolean menu_is_open;

static void on_menu_deactivate(GtkWidget *w, gpointer data) {
    menu_is_open = FALSE;
}

/* ── Main menu popup (from logo button) ── */
static void popup_main_menu(void) {
    menu_is_open = TRUE;
    GtkWidget *menu = gtk_menu_new();

    GtkWidget *run_i   = gtk_menu_item_new_with_label("Run");
    GtkWidget *files_i = gtk_menu_item_new_with_label("File Explorer");
    GtkWidget *term_i  = gtk_menu_item_new_with_label("Shell Prompt");
    GtkWidget *power_i = gtk_menu_item_new_with_label("Power");
    GtkWidget *about_i = gtk_menu_item_new_with_label("About Your Device");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), run_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), files_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), term_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), power_i);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), about_i);

    g_signal_connect(run_i,   "activate", G_CALLBACK(launch_run),      NULL);
    g_signal_connect(files_i, "activate", G_CALLBACK(launch_files),    NULL);
    g_signal_connect(term_i,  "activate", G_CALLBACK(launch_terminal), NULL);
    g_signal_connect(power_i, "activate", G_CALLBACK(launch_power),    NULL);
    g_signal_connect(about_i, "activate", G_CALLBACK(launch_about),    NULL);
    g_signal_connect(menu, "deactivate", G_CALLBACK(on_menu_deactivate), NULL);
    g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);

    gtk_widget_show_all(menu);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                   position_menu_below, GINT_TO_POINTER(10),
                   0, gtk_get_current_event_time());
    G_GNUC_END_IGNORE_DEPRECATIONS
}
