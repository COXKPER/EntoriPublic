#!/usr/bin/env python3

import gi
import os
import time
import subprocess

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("Wnck", "3.0")
gi.require_version("GdkX11", "3.0")

from gi.repository import Gtk, Gdk, Wnck, GLib, Gio

Gtk.init()

# ------------------------------------------------------------
# CONSTANTS
# ------------------------------------------------------------

DOCK_DIR = os.path.expanduser("~/.config/favorit-anak-sd")
DOCK_FILE = os.path.join(DOCK_DIR, "here.desktop")

APP_DIRS = [
    "/usr/share/applications",
    "/usr/local/share/applications",
    os.path.expanduser("~/.local/share/applications")
]

# ------------------------------------------------------------
# DESKTOP DISCOVERY
# ------------------------------------------------------------

def desktop_key(path, key):
    try:
        with open(path) as f:
            for line in f:
                if line.startswith(key + "="):
                    return line.split("=",1)[1].strip()
    except:
        pass
    return None


def find_desktop_for(wm_class, app_name):

    candidates = []

    for d in APP_DIRS:
        if not os.path.isdir(d):
            continue

        for f in os.listdir(d):
            if f.endswith(".desktop"):
                candidates.append((d,f))

    if wm_class:
        w = wm_class.lower()
        for d,n in candidates:
            val = desktop_key(os.path.join(d,n),"StartupWMClass")
            if val and val.lower()==w:
                return n

    if app_name:
        a = app_name.lower()
        for d,n in candidates:
            val = desktop_key(os.path.join(d,n),"Name")
            if val and val.lower()==a:
                return n

    if app_name:
        a = app_name.lower().replace(" ","-")
        for d,n in candidates:
            if a in n.lower():
                return n

    return None


# ------------------------------------------------------------
# DOCK FILE
# ------------------------------------------------------------

def read_dock():
    entries=set()
    if os.path.exists(DOCK_FILE):
        with open(DOCK_FILE) as f:
            for l in f:
                entries.add(l.strip())
    return entries


def write_dock(entries):
    os.makedirs(DOCK_DIR,exist_ok=True)
    with open(DOCK_FILE,"w") as f:
        for e in entries:
            f.write(e+"\n")


def add_to_dock(name):
    e=read_dock()
    if name in e:
        return False,"already"
    e.add(name)
    write_dock(e)
    return True,"added"


def remove_from_dock(name):
    e=read_dock()
    if name not in e:
        return False,"missing"
    e.remove(name)
    write_dock(e)
    return True,"removed"


# ------------------------------------------------------------
# DBUS SERVICE
# ------------------------------------------------------------

DBUS_XML="""
<node>
 <interface name='com.menubar.Dock'>
  <method name='AddToDock'>
   <arg type='s' direction='in'/>
   <arg type='b' direction='out'/>
   <arg type='s' direction='out'/>
  </method>
  <method name='RemoveFromDock'>
   <arg type='s' direction='in'/>
   <arg type='b' direction='out'/>
   <arg type='s' direction='out'/>
  </method>
 </interface>
</node>
"""

dbus_conn=None

def show_logo_menu(widget, event):

    if event.button != 1:
        return False

    menu = Gtk.Menu()

    run_item = Gtk.MenuItem(label="Run")
    power_item = Gtk.MenuItem(label="Power")
    files_item = Gtk.MenuItem(label="Files")
    terminal_item = Gtk.MenuItem(label="Terminal")

    menu.append(run_item)
    menu.append(files_item)
    menu.append(terminal_item)
    menu.append(Gtk.SeparatorMenuItem())
    menu.append(power_item)
    
    terminal_item.connect("activate", lambda x:
        subprocess.Popen(["x-terminal-emulator"])
    )
    files_item.connect("activate", lambda x:
        subprocess.Popen(["xdg-open", os.path.expanduser("~")])
    )
    run_item.connect("activate", lambda x:
        subprocess.Popen(["rofi","-show","drun"])
    )
    power_item.connect("activate", lambda x:
        subprocess.Popen(["python3", "/usr/essentials/options.py"])
    )

    menu.show_all()
    menu.popup_at_pointer(event)

    return True

def setup_dbus():

    global dbus_conn

    dbus_conn=Gio.bus_get_sync(Gio.BusType.SESSION,None)

    Gio.bus_own_name_on_connection(
        dbus_conn,
        "com.menubar.Dock",
        Gio.BusNameOwnerFlags.NONE,
        None,None
    )


# ------------------------------------------------------------
# SCREEN
# ------------------------------------------------------------

screen=Gdk.Screen.get_default()
monitor=screen.get_primary_monitor()
geo=screen.get_monitor_geometry(monitor)

wnck_screen=Wnck.Screen.get_default()
wnck_screen.force_update()


# ------------------------------------------------------------
# WINDOW
# ------------------------------------------------------------

win=Gtk.Window()

win.set_decorated(False)
win.set_resizable(False)
win.set_skip_taskbar_hint(True)
win.set_skip_pager_hint(True)
win.set_type_hint(Gdk.WindowTypeHint.DOCK)

win.set_size_request(geo.width,28)
win.set_keep_above(True)

visual=screen.get_rgba_visual()
if visual:
    win.set_visual(visual)

# ------------------------------------------------------------
# LAYOUT
# ------------------------------------------------------------

main=Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
win.add(main)

left=Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL,spacing=10)
main.pack_start(left,False,False,12)

logo = Gtk.Label(label="☰")

logo_event = Gtk.EventBox()
logo_event.add(logo)
logo_event.set_visible_window(False)

left.pack_start(logo_event, False, False, 0)

logo_event.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)
logo_event.connect("button-press-event", show_logo_menu)
app_label=Gtk.Label(label="Desktop")

left.pack_start(logo,False,False,0)
left.pack_start(app_label,False,False,0)

spacer=Gtk.Box()
spacer.set_hexpand(True)
main.pack_start(spacer,True,True,0)

right=Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL,spacing=14)
main.pack_end(right,False,False,12)

media_label=Gtk.Label()
right.pack_start(media_label,False,False,0)

clock_label=Gtk.Label()
right.pack_end(clock_label,False,False,0)




# ------------------------------------------------------------
# CLOCK
# ------------------------------------------------------------

def update_clock():
    clock_label.set_text(time.strftime("%a %b %d  %H:%M"))
    return True

GLib.timeout_add(1000,update_clock)
update_clock()

# ------------------------------------------------------------
# ACTIVE WINDOW
# ------------------------------------------------------------

current_wm_class=None
current_app_name=None


def update_active(*a):

    global current_wm_class,current_app_name

    active=wnck_screen.get_active_window()

    if active:

        app=active.get_application()

        current_wm_class=active.get_class_instance_name()

        if app:
            current_app_name=app.get_name()
        else:
            current_app_name=active.get_name()

        app_label.set_text(current_app_name)

    else:
        app_label.set_text("Desktop")

wnck_screen.connect("active-window-changed",update_active)
update_active()

# ------------------------------------------------------------
# MEDIA (MPRIS)
# ------------------------------------------------------------

def poll_media():

    try:

        bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)

        res=bus.call_sync(
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "ListNames",
            None,None,0,1000,None
        )

        for name in res.unpack()[0]:

            if name.startswith("org.mpris.MediaPlayer2."):

                md=bus.call_sync(
                    name,
                    "/org/mpris/MediaPlayer2",
                    "org.freedesktop.DBus.Properties",
                    "Get",
                    GLib.Variant("(ss)",("org.mpris.MediaPlayer2.Player","Metadata")),
                    None,0,1000,None
                )

                meta=md.unpack()[0]

                title=meta.get("xesam:title","")
                artist=meta.get("xesam:artist",[""])

                if title:
                    media_label.set_text(f"♫ {artist[0]} — {title}")

                return True

    except:
        pass

    media_label.set_text("")
    return True

GLib.timeout_add(3000,poll_media)

# ------------------------------------------------------------
# RIGHT CLICK MENU
# ------------------------------------------------------------

def show_menu(widget,event):

    if event.button!=3:
        return False

    menu=Gtk.Menu()

    desktop=find_desktop_for(current_wm_class,current_app_name)

    if desktop:

        entries=read_dock()

        if desktop in entries:
            label="Remove from Dock"
        else:
            label="Add to Dock"

        item=Gtk.MenuItem(label=label)

        def action(x):

            if desktop in entries:
                remove_from_dock(desktop)
            else:
                add_to_dock(desktop)

        item.connect("activate",action)

        menu.append(item)

    q=Gtk.MenuItem(label="Quit Menubar")
    q.connect("activate",lambda x:Gtk.main_quit())

    menu.append(q)

    menu.show_all()
    menu.popup_at_pointer(event)

    return True


app_label.add_events(Gdk.EventMask.BUTTON_PRESS_MASK)
app_label.connect("button-press-event",show_menu)

# ------------------------------------------------------------
# POSITION + XPROP STRUT
# ------------------------------------------------------------

def on_map(win):

    gdk_win=win.get_window()

    xid=gdk_win.get_xid()

    h=28
    sx=geo.x
    ex=geo.x+geo.width-1

    partial=f"0,0,{h},0, 0,0,0,0, {sx},{ex}, 0,0"
    full=f"0,0,{h},0"

    subprocess.Popen([
        "xprop","-id",str(xid),
        "-f","_NET_WM_STRUT_PARTIAL","32c",
        "-set","_NET_WM_STRUT_PARTIAL",partial
    ])

    subprocess.Popen([
        "xprop","-id",str(xid),
        "-f","_NET_WM_STRUT","32c",
        "-set","_NET_WM_STRUT",full
    ])


def on_realize(win):
    win.move(geo.x,geo.y)


win.connect("realize",on_realize)
win.connect("map",on_map)

# ------------------------------------------------------------

setup_dbus()

win.connect("destroy",Gtk.main_quit)
win.show_all()

Gtk.main()