#!/usr/bin/env python3

import gi
import platform

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk, Pango


def get_cpu_info():
    model = "Unknown"
    vendor = "Unknown"
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    model = line.split(":")[1].strip()
                if "vendor_id" in line:
                    vendor = line.split(":")[1].strip()
    except Exception:
        pass
    return model, vendor


def get_cpu_ascii(vendor):
    vendor = vendor.lower()
    if "amd" in vendor:
        return (
""" █████╗ ███╗   ███╗██████╗ 
██╔══██╗████╗ ████║██╔══██╗
███████║██╔████╔██║██║  ██║
██╔══██║██║╚██╔╝██║██║  ██║
██║  ██║██║ ╚═╝ ██║██████╔╝
╚═╝  ╚═╝╚═╝     ╚═╝╚═════╝"""
        )
    elif "intel" in vendor:
        return (
"""██╗███╗   ██╗████████╗███████╗██╗     
██║████╗  ██║╚══██╔══╝██╔════╝██║     
██║██╔██╗ ██║   ██║   █████╗  ██║     
██║██║╚██╗██║   ██║   ██╔══╝  ██║     
██║██║ ╚████║   ██║   ███████╗███████╗
╚═╝╚═╝  ╚═══╝   ╚═╝   ╚══════╝╚══════╝"""
        )
    else:
        return "Unknown CPU"


def get_ram():
    try:
        with open("/proc/meminfo") as f:
            for line in f:
                if "MemTotal" in line:
                    kb = int(line.split()[1])
                    gb = kb / 1024 / 1024
                    return f"{gb:.1f} GB"
    except Exception:
        return "Unknown"
    return "Unknown"


def get_os():
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if line.startswith("PRETTY_NAME"):
                    return line.split("=")[1].replace('"', "").strip()
    except Exception:
        return "Linux"


class About(Gtk.Window):
    def __init__(self):
        super().__init__(title="About This System")
        self.set_default_size(480, 520)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        header = Gtk.HeaderBar()
        header.set_show_close_button(True)
        header.props.title = "System Info"
        self.set_titlebar(header)

        self.set_resizable(False)
        self.set_type_hint(Gdk.WindowTypeHint.DIALOG)
        header.set_decoration_layout("close:")

        css = b"""
        window { 
            background-color: #1e1e1e;
            color: #ffffff;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
        }
        .ascii-logo {
            font-family: monospace;
            color: #0A84FF; /* macOS System Blue */
            font-weight: bold;
            font-size: 14px;
            text-shadow: 0px 2px 4px rgba(0, 0, 0, 0.5);
        }
        .os-title {
            font-size: 28px;
            font-weight: 700;
            letter-spacing: -0.5px;
            color: #ffffff;
        }
        .card {
            background-color: rgba(255, 255, 255, 0.06);
            border-radius: 12px;
            padding: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        .spec-label { 
            color: #98989d; 
            font-weight: 600;
            font-size: 13px;
        }
        .spec-value { 
            color: #ffffff; 
            font-size: 13px;
        }
        button.mac-btn {
            background-color: #0A84FF;
            color: white;
            border-radius: 6px;
            padding: 8px 24px;
            font-weight: 600;
            font-size: 13px;
            border: none;
            box-shadow: 0 1px 2px rgba(0, 0, 0, 0.2);
        }
        button.mac-btn:hover {
            background-color: #0070ea;
        }
        """

        provider = Gtk.CssProvider()
        provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        outer.set_margin_top(30)
        outer.set_margin_bottom(30)
        outer.set_margin_start(40)
        outer.set_margin_end(40)
        self.add(outer)

        cpu_model, cpu_vendor = get_cpu_info()

        ascii_label = Gtk.Label(label=get_cpu_ascii(cpu_vendor))
        ascii_label.get_style_context().add_class("ascii-logo")
        ascii_label.set_halign(Gtk.Align.CENTER)
        ascii_label.set_justify(Gtk.Justification.CENTER)
        outer.pack_start(ascii_label, False, False, 0)

        os_label = Gtk.Label(label=get_os())
        os_label.get_style_context().add_class("os-title")
        os_label.set_halign(Gtk.Align.CENTER)
        outer.pack_start(os_label, False, False, 10)

        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        card.get_style_context().add_class("card")
        outer.pack_start(card, False, False, 0)

        grid = Gtk.Grid(row_spacing=12, column_spacing=16)
        grid.set_halign(Gtk.Align.CENTER)
        card.pack_start(grid, True, True, 0)

        specs = [
            ("Processor", cpu_model),
            ("Memory", get_ram()),
            ("Kernel", platform.release()),
            ("Computer Name", platform.node()),
            ("Core Version", platform.python_version())
        ]

        for i, (key, value) in enumerate(specs):
            lbl_key = Gtk.Label(label=key)
            lbl_key.get_style_context().add_class("spec-label")
            lbl_key.set_xalign(1.0) 
            
            lbl_val = Gtk.Label(label=value)
            lbl_val.get_style_context().add_class("spec-value")
            lbl_val.set_xalign(0.0) 
            lbl_val.set_ellipsize(Pango.EllipsizeMode.END)
            lbl_val.set_max_width_chars(30) 
            
            grid.attach(lbl_key, 0, i, 1, 1)
            grid.attach(lbl_val, 1, i, 1, 1)

        btn_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        btn_box.set_halign(Gtk.Align.CENTER)
        
        btn = Gtk.Button(label="OK")
        btn.get_style_context().add_class("mac-btn")
        btn.connect("clicked", lambda _: Gtk.main_quit())
        
        btn_box.pack_start(btn, False, False, 0)
        outer.pack_end(btn_box, False, False, 10)


win = About()
win.connect("destroy", Gtk.main_quit)
win.show_all()
Gtk.main()