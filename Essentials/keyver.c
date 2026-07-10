/*
 * keyver – global hotkey daemon
 *
 * Reads /etc/creata/config.json, grabs X11 hotkeys listed in the
 * "bindings" array, and spawns the associated commands via fork/exec.
 *
 * Build:
 *   gcc $(pkg-config --cflags json-glib-1.0 glib-2.0) -Wall -Wextra -O2 \
 *       -o keyver keyver.c \
 *       $(pkg-config --libs json-glib-1.0 glib-2.0) -lX11
 */

#include <glib.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define CONFIG_PATH "/etc/creata/config.json"

/* ---- Binding storage ---- */

typedef struct {
    unsigned int mod_mask;   /* X modifier mask          */
    KeyCode      keycode;    /* X keycode                */
    char       **argv;       /* NULL-terminated cmd argv */
} Binding;

static GArray   *bindings;   /* GArray of Binding */
static Display  *dpy;
static GMainLoop *loop;

/* ---- Child reaper (avoid zombies) ---- */

static void sigchld_handler(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

/* ---- Spawn command ---- */

static void spawn_command(char **argv)
{
    pid_t pid = fork();
    if (pid < 0) {
        g_warning("fork failed: %s", g_strerror(errno));
        return;
    }
    if (pid == 0) {
        /* Detach from controlling terminal */
        setsid();

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }

        execvp(argv[0], argv);
        /* If we get here exec failed */
        fprintf(stderr, "Invalid Arguments: %s\n", argv[0]);
        _exit(127);
    }
    /* Parent continues; SIGCHLD handler reaps. */
}

/* ---- Modifier / key parsing ---- */

/*
 * Parse a hotkey string such as "<cmd>+<shift>+t" or "<cmd>+<space>".
 * Returns TRUE on success, filling *mod_mask and *keysym.
 */
static gboolean parse_hotkey(const char *hotkey, unsigned int *mod_mask,
                             KeySym *keysym)
{
    *mod_mask = 0;
    *keysym   = NoSymbol;

    gchar **tokens = g_strsplit(hotkey, "+", -1);
    if (!tokens) return FALSE;

    gboolean ok = FALSE;

    for (int i = 0; tokens[i]; i++) {
        const char *t = tokens[i];

        if (g_ascii_strcasecmp(t, "<cmd>") == 0)
            *mod_mask |= Mod4Mask;
        else if (g_ascii_strcasecmp(t, "<shift>") == 0)
            *mod_mask |= ShiftMask;
        else if (g_ascii_strcasecmp(t, "<ctrl>") == 0)
            *mod_mask |= ControlMask;
        else if (g_ascii_strcasecmp(t, "<alt>") == 0)
            *mod_mask |= Mod1Mask;
        else if (t[0] == '<' && t[strlen(t) - 1] == '>') {
            /* Special key in angle brackets, e.g. <space>, <Return> */
            gchar *name = g_strndup(t + 1, strlen(t) - 2);
            *keysym = XStringToKeysym(name);
            g_free(name);
            if (*keysym == NoSymbol) goto out;
        } else {
            /* Bare key name, e.g. "t", "F1" */
            *keysym = XStringToKeysym(t);
            if (*keysym == NoSymbol) goto out;
        }
    }

    ok = (*keysym != NoSymbol);

out:
    g_strfreev(tokens);
    return ok;
}

/* ---- Config loading ---- */

static gboolean load_config(void)
{
    JsonParser *parser = json_parser_new();
    GError *err = NULL;

    if (!json_parser_load_from_file(parser, CONFIG_PATH, &err)) {
        g_warning("Failed to load %s: %s", CONFIG_PATH, err->message);
        g_error_free(err);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    if (!root_node || !JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_warning("Config root is not a JSON object");
        g_object_unref(parser);
        return FALSE;
    }

    JsonObject *root = json_node_get_object(root_node);
    if (!json_object_has_member(root, "bindings")) {
        g_warning("Config has no \"bindings\" array");
        g_object_unref(parser);
        return FALSE;
    }

    JsonArray *arr = json_object_get_array_member(root, "bindings");
    guint len = json_array_get_length(arr);

    for (guint i = 0; i < len; i++) {
        JsonObject *entry = json_array_get_object_element(arr, i);
        if (!entry) continue;

        const char *hotkey_str = json_object_get_string_member(entry, "hotkey");
        JsonArray *cmd_arr = json_object_get_array_member(entry, "command");
        if (!hotkey_str || !cmd_arr) continue;

        unsigned int mod_mask;
        KeySym keysym;
        if (!parse_hotkey(hotkey_str, &mod_mask, &keysym)) {
            g_warning("Cannot parse hotkey \"%s\" – skipping", hotkey_str);
            continue;
        }

        KeyCode kc = XKeysymToKeycode(dpy, keysym);
        if (!kc) {
            g_warning("No keycode for keysym 0x%lx (\"%s\") – skipping",
                      keysym, hotkey_str);
            continue;
        }

        /* Build argv */
        guint cmd_len = json_array_get_length(cmd_arr);
        char **argv = g_new0(char *, cmd_len + 1);
        for (guint j = 0; j < cmd_len; j++)
            argv[j] = g_strdup(json_array_get_string_element(cmd_arr, j));

        Binding b = { .mod_mask = mod_mask, .keycode = kc, .argv = argv };
        g_array_append_val(bindings, b);
        g_message("Bound %-24s -> %s", hotkey_str, argv[0]);
    }

    g_object_unref(parser);
    return bindings->len > 0;
}

/* ---- X11 grab helpers ---- */

/*
 * We grab with every combination of Caps Lock / Num Lock / Scroll Lock
 * so the hotkey fires regardless of lock-key state.
 */
static void grab_keys(void)
{
    Window root = DefaultRootWindow(dpy);
    static const unsigned int lock_masks[] = {
        0,
        LockMask,                        /* Caps Lock   */
        Mod2Mask,                        /* Num Lock    */
        LockMask | Mod2Mask,
    };

    for (guint i = 0; i < bindings->len; i++) {
        Binding *b = &g_array_index(bindings, Binding, i);
        for (size_t m = 0; m < G_N_ELEMENTS(lock_masks); m++) {
            XGrabKey(dpy, b->keycode, b->mod_mask | lock_masks[m],
                     root, True, GrabModeAsync, GrabModeAsync);
        }
    }
    XFlush(dpy);
}

static void ungrab_keys(void)
{
    XUngrabKey(dpy, AnyKey, AnyModifier, DefaultRootWindow(dpy));
    XFlush(dpy);
}

/* ---- Mask used for matching (strip lock-key bits) ---- */

static unsigned int clean_mask(unsigned int mask)
{
    return mask & ~(LockMask | Mod2Mask | Mod3Mask);
}

/* ---- X11 event dispatch ---- */

static void dispatch_x_events(void)
{
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        if (ev.type != KeyPress) continue;

        unsigned int state = clean_mask(ev.xkey.state);
        KeyCode kc = ev.xkey.keycode;

        for (guint i = 0; i < bindings->len; i++) {
            Binding *b = &g_array_index(bindings, Binding, i);
            if (b->keycode == kc && b->mod_mask == state) {
                spawn_command(b->argv);
                break;
            }
        }
    }
}

/* ---- GLib ↔ X11 integration ---- */

static gboolean on_x11_event(GIOChannel *src, GIOCondition cond, gpointer data)
{
    (void)src; (void)cond; (void)data;
    dispatch_x_events();
    return G_SOURCE_CONTINUE;
}

/* ---- Cleanup ---- */

static void free_bindings(void)
{
    for (guint i = 0; i < bindings->len; i++) {
        Binding *b = &g_array_index(bindings, Binding, i);
        g_strfreev(b->argv);
    }
    g_array_free(bindings, TRUE);
}

/* ---- SIGTERM / SIGINT handler ---- */

static gboolean on_signal(gpointer data)
{
    (void)data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

/* ---- Main ---- */

int main(void)
{
    /* Set up SIGCHLD reaper */
    struct sigaction sa = { .sa_handler = sigchld_handler, .sa_flags = SA_RESTART | SA_NOCLDSTOP };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* Open X display */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        g_critical("Cannot open X display");
        return 1;
    }

    /* Load config */
    bindings = g_array_new(FALSE, TRUE, sizeof(Binding));
    if (!load_config()) {
        g_critical("No valid bindings loaded");
        g_array_free(bindings, TRUE);
        XCloseDisplay(dpy);
        return 1;
    }

    /* Grab keys on root window */
    grab_keys();

    /* Set up GLib main loop with X11 fd watch */
    loop = g_main_loop_new(NULL, FALSE);

    int x_fd = ConnectionNumber(dpy);
    GIOChannel *x_chan = g_io_channel_unix_new(x_fd);
    g_io_add_watch(x_chan, G_IO_IN, on_x11_event, NULL);

    /* Handle SIGTERM / SIGINT for clean shutdown */
    g_unix_signal_add(SIGTERM, on_signal, NULL);
    g_unix_signal_add(SIGINT,  on_signal, NULL);

    g_message("keyver: %u binding(s) active – listening", bindings->len);
    g_main_loop_run(loop);

    /* Cleanup */
    g_io_channel_unref(x_chan);
    ungrab_keys();
    free_bindings();
    g_main_loop_unref(loop);
    XCloseDisplay(dpy);

    return 0;
}
