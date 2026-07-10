#ifndef MOLA_DBUS_SERVER_H
#define MOLA_DBUS_SERVER_H

#include "dock.h"

#define MOLA_DBUS_NAME      "com.mola.Desktop"
#define MOLA_DBUS_PATH      "/com/mola/Desktop"
#define MOLA_DBUS_INTERFACE "com.mola.Desktop"

bool  dbus_server_init(Dock *dock);
void  dbus_server_stop(void);

#endif
