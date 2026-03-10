#!/usr/bin/env python3

import gi
import platform
import os

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk

def get_cpu():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    return line.split(":")[1].strip()
    except:
        return "Unknown"


def get_ram():
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if "MemTotal" in line:
                    kb = int(line.split()[1])
                    gb = kb / 1024 / 1024
                    return f"{gb:.1f} GB"
    except:
        return "Unknown"


def get_os():
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("PRETTY_NAME"):
                    return line.split("=")[1].replace('"',"").strip()
    except:
        return "Linux"


class About(Gtk.Window):

    def __init__(self):
        Gtk.Window.__init__(self)

        self.set_title("About This Computer")
        self.set_default_size(520, 320)
        self.set_border_width(15)

        # main box
        box = Gtk.Box(spacing=20)
        self.add(box)

        # icon kiri
        icon = Gtk.Image.new_from_icon_name(
            "computer",
            Gtk.IconSize.DIALOG
        )

        box.pack_start(icon, False, False, 10)

        # kanan
        right = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL,
            spacing=10
        )

        box.pack_start(right, True, True, 0)

        # title
        title = Gtk.Label()
        title.set_markup("<span size='xx-large' weight='bold'>Linux Desktop</span>")
        title.set_xalign(0)

        right.pack_start(title, False, False, 0)

        # subtitle
        subtitle = Gtk.Label(label=get_os())
        subtitle.set_xalign(0)

        right.pack_start(subtitle, False, False, 0)

        sep = Gtk.Separator()
        right.pack_start(sep, False, False, 5)

        # info
        info = f"""
CPU      : {get_cpu()}
Memory   : {get_ram()}
Kernel   : {platform.release()}
Hostname : {platform.node()}
Python   : {platform.python_version()}
"""

        label = Gtk.Label(label=info)
        label.set_xalign(0)

        right.pack_start(label, False, False, 0)

        # button close
        btn = Gtk.Button(label="Close")
        btn.connect("clicked", lambda x: Gtk.main_quit())

        right.pack_end(btn, False, False, 0)


win = About()
win.connect("destroy", Gtk.main_quit)
win.show_all()

Gtk.main()
