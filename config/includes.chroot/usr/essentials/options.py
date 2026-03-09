import gi
import subprocess

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk


class PowerWindow(Gtk.Window):
    def __init__(self):
        Gtk.Window.__init__(self, title="Power Options")
        self.set_border_width(20)
        self.set_default_size(250, 150)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.add(box)

        shutdown_btn = Gtk.Button(label="Shutdown")
        restart_btn = Gtk.Button(label="Restart")
        logout_btn = Gtk.Button(label="Logout")
        cancel_btn = Gtk.Button(label="Cancel")

        shutdown_btn.connect("clicked", self.shutdown)
        restart_btn.connect("clicked", self.restart)
        logout_btn.connect("clicked", self.logout)
        cancel_btn.connect("clicked", self.cancel)

        box.pack_start(shutdown_btn, True, True, 0)
        box.pack_start(restart_btn, True, True, 0)
        box.pack_start(logout_btn, True, True, 0)
        box.pack_start(cancel_btn, True, True, 0)

    def confirm(self, text):
        dialog = Gtk.MessageDialog(
            self,
            0,
            Gtk.MessageType.QUESTION,
            Gtk.ButtonsType.YES_NO,
            text
        )
        response = dialog.run()
        dialog.destroy()
        return response == Gtk.ResponseType.YES

    def shutdown(self, widget):
        if self.confirm("Shutdown computer?"):
            subprocess.run(["loginctl", "poweroff"])

    def restart(self, widget):
        if self.confirm("Restart computer?"):
            subprocess.run(["loginctl", "reboot"])

    def logout(self, widget):
        if self.confirm("Logout from Openbox?"):
            subprocess.run(["openbox", "--exit"])

    def cancel(self, widget):
        Gtk.main_quit()


win = PowerWindow()
win.connect("destroy", Gtk.main_quit)
win.show_all()
Gtk.main()