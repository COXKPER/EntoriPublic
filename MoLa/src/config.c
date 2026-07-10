#include "config.h"
#include "dock.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* ── path management ──────────────────────────────────────────────── */

static char *cfg_path = NULL;

static const char *ensure_config_dir(void) {
    if (cfg_path) return cfg_path;

    const char *config_home = g_get_user_config_dir();   /* XDG_CONFIG_HOME */
    char *dir = g_build_filename(config_home, "mola", NULL);
    g_mkdir_with_parents(dir, 0755);
    cfg_path = g_build_filename(dir, "dock.conf", NULL);
    g_free(dir);
    return cfg_path;
}

const char *config_get_path(void) {
    return ensure_config_dir();
}

/* ── load ─────────────────────────────────────────────────────────── */

GList *config_load_pinned(void) {
    const char *path = ensure_config_dir();
    GKeyFile   *kf   = g_key_file_new();
    GError     *err  = NULL;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        /* File doesn't exist yet – not an error on first run */
        g_error_free(err);
        g_key_file_free(kf);
        return NULL;
    }

    gsize   n_groups = 0;
    gchar **groups   = g_key_file_get_groups(kf, &n_groups);
    GList  *list     = NULL;

    for (gsize i = 0; i < n_groups; i++) {
        if (!g_str_has_prefix(groups[i], "App:")) continue;

        PinnedAppInfo *info = g_new0(PinnedAppInfo, 1);
        info->app_id       = g_strdup(groups[i] + 4);   /* skip "App:" */
        info->display_name = g_key_file_get_string(kf, groups[i], "DisplayName", NULL);
        info->icon_name    = g_key_file_get_string(kf, groups[i], "Icon",        NULL);
        info->desktop_file = g_key_file_get_string(kf, groups[i], "Desktop",     NULL);

        if (!info->display_name) info->display_name = g_strdup(info->app_id);
        if (!info->icon_name)    info->icon_name    = g_strdup("application-x-executable");

        list = g_list_append(list, info);
    }

    g_strfreev(groups);
    g_key_file_free(kf);
    return list;
}

/* ── save ─────────────────────────────────────────────────────────── */

void config_save_pinned(GList *items) {
    const char *path = ensure_config_dir();
    GKeyFile   *kf   = g_key_file_new();

    /* items is a GList of DockItem*.  Only write pinned ones. */
    for (GList *l = items; l; l = l->next) {
        DockItem *item = l->data;
        if (!item->pinned) continue;

        char *group = g_strdup_printf("App:%s", item->app_id);
        g_key_file_set_string(kf, group, "DisplayName",
                              item->display_name ? item->display_name : item->app_id);
        g_key_file_set_string(kf, group, "Icon",
                              item->icon_name ? item->icon_name : "application-x-executable");
        if (item->desktop_file)
            g_key_file_set_string(kf, group, "Desktop", item->desktop_file);
        g_free(group);
    }

    GError *err  = NULL;
    gchar  *data = g_key_file_to_data(kf, NULL, NULL);
    if (!g_file_set_contents(path, data, -1, &err)) {
        g_printerr("MoLa config: failed to save %s: %s\n", path, err->message);
        g_error_free(err);
    }
    g_free(data);
    g_key_file_free(kf);
}

/* ── cleanup ──────────────────────────────────────────────────────── */

void config_free_pinned_list(GList *list) {
    for (GList *l = list; l; l = l->next) {
        PinnedAppInfo *info = l->data;
        g_free(info->app_id);
        g_free(info->display_name);
        g_free(info->icon_name);
        g_free(info->desktop_file);
        g_free(info);
    }
    g_list_free(list);
}
