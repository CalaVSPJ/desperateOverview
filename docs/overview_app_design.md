# Overview Overlay Design Notes

This document explains how the integrated overlay (`desperateOverview_app.c`)
is structured today. The goal is to make the intent of the codebase
obvious before making further behavioural changes.

## High-Level Architecture

- **Threads**
  - The GTK/UI thread drives the overlay window.
  - A background core thread (`desperateOverview_core`) listens to Hyprland IPC,
    maintains cached state, and requests UI redraws via `core_redraw_callback`.

- **State Flow**
  - `core_copy_state()` snapshots monitors, workspaces, and windows.
  - `copy_core_state_to_ui()` translates the snapshot into UI friendly
    structures (`g_ws`, `g_active_list`, thumbnails).
  - Drag-and-drop callbacks call `core_move_window()` /
    `core_switch_workspace()` which dispatch Hyprland commands, then the
    core thread refreshes state asynchronously.

## Widget Layout

```
GtkWindow (layer-shell, full-screen)
└── GtkOverlay root_overlay
    ├── GtkDrawingArea background_canvas (dimmed backdrop)
    └── GtkBox root_box (vertical)
        ├── GtkOverlay top_overlay (workspace strip, ~33% height)
        │   └── GtkBox g_overlay_content (one drawing area per workspace)
        └── GtkDrawingArea g_current_preview (active workspace miniature)
```

### Top Overlay (workspace strip)

- Each workspace cell is a `GtkDrawingArea`.
- `draw_cell()` renders:
  - Workspace background (dark grey or teal border depending on active state).
  - Window previews scaled to the cell.
  - Drag highlight if the user is dragging from this workspace.
- Event handlers attached per cell:
  - `button-press` / `button-release` to detect clicks vs. drag initiations.
  - GTK drag-and-drop signals to support moving windows between workspaces.

### Bottom Preview

- `draw_current_workspace()` paints the active workspace at full width,
  including a white border and each window’s texture (no per-window border).
- Purely visual—no input handlers—so only clicks on the background canvas
  or outside the window dismiss the overlay.

## Event Handling

| Input                               | Handler                          | Behaviour |
|------------------------------------|----------------------------------|-----------|
| `GtkWindow` key press (`Esc`)      | `on_key()`                       | Closes overlay |
| `GtkWindow` button press           | `on_overlay_window_button_press()` | Closes overlay **only** when the click lands outside both the top workspace strip and the bottom preview |
| Workspace cell press/release       | `on_cell_button_press/release()` | Distinguish workspace click vs. drag |
| Workspace drag signals             | `on_cell_drag_*()`               | Move windows between workspaces |
| Drag drop target                   | `on_cell_drag_data_received()`   | Calls `core_move_window()` when a payload is dropped |

**Background click rule:** The window-level handler compares the click
coordinates against the bounds of `g_overlay_content` (top strip) and
`g_current_preview` (bottom preview). If the click is outside both, the
overlay is closed. No other widgets consume background clicks.

## Performance Touchpoints

- Hyprland events that trigger refreshes are filtered in
  `event_requires_refresh()` to structural changes (open/close/move window,
  workspace changes, changefloatingmode). Focus/title changes no longer
  cause full-state rebuilds.
- Live thumbnails for the active workspace are captured on demand via
  `desperateOverview_ui_build_live_previews()`. The actual capture happens on a
  worker thread and the decoded pixbufs are applied back on the GTK main loop to
  avoid blocking redraws.

## Styling

- A GTK CSS provider is loaded during `desperateOverview_ui_init()`. The loader
  checks `~/.config/desperateOverview/style.css`, `./desperateOverview.css`, and
  `./data/desperateOverview.css` (in that order). If none exist a tiny built‑in
  theme is used.
- Widgets expose targeted style classes:
  - `desperateOverview-status` – hover/status label above the bottom preview.
  - `desperateOverview-ghost` – the “new workspace” drop target on the top strip.
  - Additional classes can be added as needed; they will be picked up
    automatically once the CSS file is reloaded on the next launch.

## Next Steps

1. Keep this document updated whenever the UI layout or event logic changes.
2. Consider splitting `desperateOverview_app.c` into smaller translation units
   (layout, events, rendering) now that the overall structure is clear.
3. Revisit the background-click behaviour once the current regression
   is fixed—having the precise design documented should make targeted
   fixes safer.


