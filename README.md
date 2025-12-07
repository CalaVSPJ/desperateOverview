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
make            # builds the desperateOverview binary
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

- `docs/overview_app_design.md` contains a high-level description of the UI
  architecture, shared state, and event handling.
- `protocols/` holds the XML sources for the custom Wayland protocols as well
  as the update script mentioned above.
- `scripts/` contains maintenance helpers (currently only the protocol updater).

Feel free to open issues or PRs for build regressions, packaging changes, or
code cleanups. Contributions are welcome!

