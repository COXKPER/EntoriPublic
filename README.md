
![Entori](https://avm.fourvo.id/ENTORI%20(2).png)

**Open Source • Secure • Modern**

Entori is a custom Debian-based Linux live distribution featuring the Openbox window manager, LightDM, and a macOS-style menubar.

## Build

The core desktop utilities (`about`, `menu`, `options`) in `config/includes.chroot/usr/essentials/` are written in C with GTK3 and must be compiled before building the ISO:

```sh
make
```

This compiles `about.c`, `menu.c`, and `options.c` against GTK3 and libwnck.

**Requirements:** `gcc`, `pkg-config`, `libgtk-3-dev`, `libwnck-3-dev`

Special thanks to the **Imphnen Community** for their support and contributions.
