#!/usr/bin/env python3

import gi
import subprocess

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Gdk


class PowerWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="Power Options")
        self.set_default_size(320, 360)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        self.set_resizable(False)
        self.set_type_hint(Gdk.WindowTypeHint.DIALOG)

        header = Gtk.HeaderBar()
        header.set_show_close_button(True)
        header.set_decoration_layout("close:")
        header.props.title = "System"
        self.set_titlebar(header)

        css = b"""
        window { 
            background-color: #1e1e1e;
            color: #ffffff;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
        }
        .header-title {
            font-size: 18px;
            font-weight: 600;
            color: #ffffff;
            margin-bottom: 5px;
        }
        .header-subtitle {
            font-size: 13px;
            color: #98989d;
            margin-bottom: 15px;
        }
        .action-card {
            background-color: rgba(255, 255, 255, 0.06);
            border-radius: 14px;
            padding: 8px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        button.mac-btn {
            background-color: transparent;
            color: #ffffff;
            border-radius: 8px;
            padding: 12px;
            font-weight: 500;
            font-size: 14px;
            border: none;
            box-shadow: none;
        }
        button.mac-btn:hover {
            background-color: rgba(255, 255, 255, 0.1);
        }
        button.btn-danger {
            color: #FF3B30; /* macOS System Red */
            font-weight: 600;
        }
        button.btn-danger:hover {
            background-color: rgba(255, 59, 48, 0.15);
        }
        button.btn-cancel {
            background-color: rgba(255, 255, 255, 0.06);
            color: #0A84FF; /* macOS System Blue */
            font-weight: 600;
            border-radius: 12px;
            margin-top: 10px;
        }
        button.btn-cancel:hover {
            background-color: rgba(255, 255, 255, 0.12);
        }
        """

        provider = Gtk.CssProvider()
        provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        outer.set_margin_top(20)
        outer.set_margin_bottom(20)
        outer.set_margin_start(30)
        outer.set_margin_end(30)
        self.add(outer)

        title_label = Gtk.Label(label="Power Options")
        title_label.get_style_context().add_class("header-title")
        outer.pack_start(title_label, False, False, 0)

        subtitle_label = Gtk.Label(label="What do you want to do?")
        subtitle_label.get_style_context().add_class("header-subtitle")
        outer.pack_start(subtitle_label, False, False, 0)

        action_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        action_card.get_style_context().add_class("action-card")
        outer.pack_start(action_card, False, False, 10)

        shutdown_btn = Gtk.Button(label="Shut Down")
        shutdown_btn.get_style_context().add_class("mac-btn")
        shutdown_btn.get_style_context().add_class("btn-danger")
        
        restart_btn = Gtk.Button(label="Restart")
        restart_btn.get_style_context().add_class("mac-btn")
        
        logout_btn = Gtk.Button(label="Log Out")
        logout_btn.get_style_context().add_class("mac-btn")

        action_card.pack_start(shutdown_btn, True, True, 0)
        
        action_card.pack_start(Gtk.Separator(), False, False, 0)
        action_card.pack_start(restart_btn, True, True, 0)
        
        action_card.pack_start(Gtk.Separator(), False, False, 0)
        action_card.pack_start(logout_btn, True, True, 0)

        cancel_btn = Gtk.Button(label="Cancel")
        cancel_btn.get_style_context().add_class("mac-btn")
        cancel_btn.get_style_context().add_class("btn-cancel")
        outer.pack_end(cancel_btn, False, False, 0)

        # Event Listeners
        shutdown_btn.connect("clicked", self.shutdown)
        restart_btn.connect("clicked", self.restart)
        logout_btn.connect("clicked", self.logout)
        cancel_btn.connect("clicked", self.cancel)

    def confirm(self, text):
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.QUESTION,
            buttons=Gtk.ButtonsType.YES_NO,
            text=text
        )
        response = dialog.run()
        dialog.destroy()
        return response == Gtk.ResponseType.YES

    def shutdown(self, widget):
        if self.confirm("Are you sure you want to shut down your computer now?"):
            subprocess.run(["systemctl", "poweroff"])

    def restart(self, widget):
        if self.confirm("Are you sure you want to restart your computer now?"):
            subprocess.run(["systemctl", "reboot"])

    def logout(self, widget):
        if self.confirm("Are you sure you want to log out from system?"):
            subprocess.run(["openbox", "--exit"])

    def cancel(self, widget):
        Gtk.main_quit()


win = PowerWindow()
win.connect("destroy", Gtk.main_quit)
win.show_all()
Gtk.main()