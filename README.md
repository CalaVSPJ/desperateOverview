# desperateOverview

An overlay / workspace browser for Hyprland that renders a GTK layer-shell
window with live thumbnails, drag-and-drop support, and keyboard navigation.

## Requirements

The project is written in C and depends on the following packages:

- `gcc` (or another C11 capable compiler)
- `pkg-config`
- `gtk+-3.0`
- `gtk-layer-shell-0`
- `gdk-pixbuf-2.0`
- `wayland-client`
- `wayland-scanner` (only required when regenerating protocol bindings)
- `curl` (used to fetch protocol XML files and auto-download yyjson during the build)

## Repository layout

- `src/` – application sources
- `include/` – project headers exposed to multiple modules
- `protocols/` – protocol XML plus `protocols/generated/` for wayland-scanner output
- `vendor/yyjson-0.10.0/` – bundled JSON parser
- `data/`, `docs/`, `scripts/` – runtime assets, documentation, and helper scripts

## Building

```sh
make                # builds the desperateOverview binary
sudo make install   # installs to /usr/local/bin by default
sudo make uninstall # removes the installed binary
```

`PREFIX` and `DESTDIR` are honored, so packaging systems can set custom
install roots:

```sh
make PREFIX=/usr DESTDIR="$pkgdir" install
```

`compile.sh` now simply delegates to `make`, so existing scripts or aliases
continue to work.

The build downloads the matching `yyjson` release on demand into `vendor/`.
If you want to keep a shared copy elsewhere, set `YYJSON_DIR=/path/to/yyjson`
when invoking `make`.

## Configuration

A user-specific config file can live at `~/.config/desperateOverview/config.ini`
(or pass `--config /path/to/file`). See `docs/config.example.ini` for the full
set of keys; `make install` also drops it at
`$(PREFIX)/share/desperateOverview/config.example.ini`.

Behavioral keys include:

- `drag_hold_delay_ms` – delay (ms) before a click starts a drag
- `thumbnail_thread_count` – worker threads for thumbnail decoding
- `follow_drop` – when `true`, the overlay switches to the workspace that a
  dragged window was dropped onto (and issues a Hyprland workspace switch).
- `fade_step` – opacity increment applied every 16 ms during overlay fade-in
  (lower values slow the animation, higher values make it snappier).
- `monitor` – which Hyprland monitor the overlay tracks: `cursor` (default,
  follow the cursor), `focused` (Hyprland's focused output), or an explicit
  output name like `DP-1` / `eDP-1`.

Layout keys (defaults match the prior hard-coded 3×3 / nine-workspace grid):

- `rows`, `cols` – grid dimensions. e.g. `rows = 2`, `cols = 5` for a 2×5 grid.
- `workspaces` – comma-separated list of Hyprland workspace IDs to show, in
  row-major order. Defaults to `1..(rows*cols)` if omitted. Useful for skipping
  or reordering workspaces.

## Running

```sh
desperateOverview                 # same as --show: open the overlay, exit when dismissed
desperateOverview --toggle        # dismiss a showing overlay, or open one if none is up
desperateOverview --hide          # dismiss a showing overlay (no-op if none)
desperateOverview --help          # full option list
```

There is no long-running daemon: every invocation either spawns a fresh
one-shot overlay or talks to one that's already showing via a per-user
Unix socket at `$XDG_RUNTIME_DIR/desp_overview.sock`.

Example Hyprland keybind (in `~/.config/hypr/hyprland.conf`):

```conf
bind = SUPER, TAB, exec, desperateOverview --toggle
```

`scripts/toggle-overview.sh` is a thin wrapper around `--toggle` for setups
that prefer to keybind a script rather than the binary directly.

## Vendored Wayland protocols

The project ships generated bindings for the following protocols:

- `wlr-foreign-toplevel-management-unstable-v1`
- `hyprland-toplevel-export-v1`

The canonical XML descriptions live under `protocols/`. If you ever need to
refresh them (e.g. after updating from upstream) run:

```sh
scripts/update_protocols.sh --fetch   # downloads latest XML + regenerates sources
```

To regenerate from the currently checked-in XML without performing network
requests, drop the `--fetch` flag:

```sh
scripts/update_protocols.sh
```

This script requires both `curl` (when fetching) and `wayland-scanner`.

## Development notes

- `protocols/` holds the XML sources for the custom Wayland protocols as well
  as the update script mentioned above.
- `scripts/` contains `toggle-overview.sh` (thin `--toggle` wrapper) and
  `update_protocols.sh` (regenerate Wayland protocol bindings).

Feel free to open issues or PRs for build regressions, packaging changes, or
code cleanups. Contributions are welcome!

