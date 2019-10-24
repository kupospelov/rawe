# rawe

A lightweight daemon for Wayland compositors to run tasks after some period of uninterrupted activity. It is compatible with any compositor which implements the KDE [idle](https://github.com/kupospelov/rawe/blob/master/protocols/idle.xml) protocol.

## Compiling from source

Dependencies:
* meson
* wayland

```shell
meson build
ninja -C build
```

## Usage

Use the following command to display a notification every 20 minutes and reset the timer after 3 minutes of inactivity:

```rawe -i 180000 timeout 1200000 "notify-send '20 minutes have passed!'"```

See `rawe --help` for other options.
