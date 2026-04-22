# Kiln Terminal

A full-featured GTK4 / libadwaita terminal emulator written in C++20, built on
top of [VTE](https://gitlab.gnome.org/GNOME/vte). It looks and behaves like a
native GNOME app (adaptive header bar, tabbed UI, dark/light following the
system preference) and lets you pick which shell to launch for every new tab.

## Features

- **Native GNOME look** via `AdwApplicationWindow` + `AdwHeaderBar` +
  `AdwToolbarView`.
- **Tabbed interface** using `AdwTabView` / `AdwTabBar` with drag-reorder,
  attention indicators on bell, and per-tab titles from the shell.
- **Multi-shell support**: the "new tab" split button has a dropdown listing
  every shell found on your system (parsed from `/etc/shells` plus common
  locations), each with its icon and path.
- **Follows the system color scheme** live — the palette swaps when you toggle
  dark mode in GNOME Settings / Tweaks.
- **Sensible defaults**: 10 000 line scrollback, truecolor, hyperlinks, sixel,
  mouse autohide, scroll-on-keystroke.
- **Keyboard shortcuts**:
  | Action | Shortcut |
  | --- | --- |
  | New tab (default shell) | `Ctrl+Shift+T` |
  | Close tab | `Ctrl+Shift+W` |
  | Copy | `Ctrl+Shift+C` |
  | Paste | `Ctrl+Shift+V` |
  | Zoom in / out / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
  | Quit | `Ctrl+Shift+Q` |

## Build dependencies

On Ubuntu / Debian / Linux Mint:

```bash
sudo apt install \
    build-essential cmake pkg-config \
    libgtk-4-dev libadwaita-1-dev libvte-2.91-gtk4-dev
```

On Fedora:

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config \
    gtk4-devel libadwaita-devel vte291-gtk4-devel
```

On Arch:

```bash
sudo pacman -S base-devel cmake pkgconf gtk4 libadwaita vte4
```

## Build & run

```bash
./run.sh
```

…or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kiln-terminal
```

## Project layout

```
src/
  main.cpp            # entry point
  application.{hpp,cpp}  # AdwApplication + app-level actions (about, quit)
  window.{hpp,cpp}       # main window, tab view, shell menu, shortcuts
  terminal_tab.{hpp,cpp} # VteTerminal wrapper, palette, spawn
  shell_detector.{hpp,cpp}  # /etc/shells parsing + known-shell metadata
  config.hpp.in       # generated build metadata
data/
  dev.kiln.Terminal.desktop
CMakeLists.txt
run.sh
```

## License

MIT.
