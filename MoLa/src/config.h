#ifndef MOLA_CONFIG_H
#define MOLA_CONFIG_H

#include <glib.h>
#include <stdbool.h>

/* Information about a pinned application, as stored on disk. */
typedef struct PinnedAppInfo {
    char *app_id;
    char *display_name;
    char *icon_name;
    char *desktop_file;
} PinnedAppInfo;

/* Load the list of pinned apps from ~/.config/mola/dock.conf.
 * Returns a GList* of PinnedAppInfo*.  Caller must free with
 * config_free_pinned_list(). Returns NULL on first run / missing file. */
GList       *config_load_pinned(void);

/* Save the current set of pinned apps.  |items| is a GList* of DockItem*
 * (only pinned items are written). */
void         config_save_pinned(GList *items);

/* Free a list returned by config_load_pinned(). */
void         config_free_pinned_list(GList *list);

/* Returns the absolute path to the config file (owned by the module). */
const char  *config_get_path(void);

#endif /* MOLA_CONFIG_H */
