# Configuring a framebuffer GUI frontend

MiSTer can optionally start a Linux program over the menu core's framebuffer terminal. This
provides an integration point for alternative frontends while keeping the existing menu as the
default when no frontend is configured.

Add a `gui=` entry to the `[MiSTer]` section of `MiSTer.ini`:

```ini
[MiSTer]
gui=path/to/frontend
```

Relative paths are resolved with MiSTer's normal `getFullPath()` rules. The program must be
executable and able to draw through the framebuffer terminal. The setting has no effect when it
is empty, and a missing target is reported to the MiSTer log while the regular menu remains
available. A missing path is reported once per MiSTer process; after correcting the path, restart
MiSTer to try again.

## Running and returning

MiSTer starts the program when the menu core is ready and the framebuffer terminal is enabled.
It keeps the framebuffer visible when the OSD closes. Holding the menu key for about 1.5 seconds
while another core is active reloads `menu.rbf`; the configured frontend starts again when the
menu core returns.

A frontend that exits with status 0 is treated as a clean request to leave the frontend. MiSTer
keeps the stock menu visible until a core is exited. This lets the frontend hand control back to
the normal menu or start a core without being immediately relaunched. If it exits while a game
core is running, the frontend starts again when the user returns to the menu core.

## Errors

A nonzero exit status is treated as a frontend failure. MiSTer writes the status and captured
standard error to `/tmp/mister-gui.log`, prints the diagnostic on the framebuffer terminal, and
pauses relaunches until another core has been exited. This avoids an automatic crash loop while
leaving the stock menu available. The log is temporary and is lost at reboot.
