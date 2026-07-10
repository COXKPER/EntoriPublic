#ifndef MOLA_WINDOW_MONITOR_H
#define MOLA_WINDOW_MONITOR_H

#include "dock.h"

/* Start periodic scanning for GUI windows.  Adds / removes running
 * (non-pinned) items on the dock automatically.  Marks pinned items
 * as running when their window is found.
 *
 * Polling interval: ~2 seconds. */
void window_monitor_start(Dock *dock);

/* Stop the window monitor and release resources. */
void window_monitor_stop(void);

#endif /* MOLA_WINDOW_MONITOR_H */
