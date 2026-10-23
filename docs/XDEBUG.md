# Xdebug: CLI Debugging and Profiling

Forge can start a local Xdebug 3 session for the current saved PHP file. The debugger
listener binds only to `127.0.0.1:9003`; Xdebug traffic is not exposed on external
interfaces. Forge passes the debug/profile mode to the launched PHP process and does
not rewrite the system `php.ini`.

## Requirements

- PHP CLI (`php.exe`) is available on `PATH`.
- Xdebug 3 is installed and enabled for that same PHP CLI interpreter.
- Port `9003` is available on the local machine.

If Xdebug is not loaded, run the Xdebug installer or follow
[the official Xdebug installation guide](https://xdebug.org/docs/install), then
restart Forge. The IDE does not download or install PHP extensions.

## Debug the current file

1. Open and save a `.php` file.
2. Put the caret on the line where execution should pause.
3. Select **Run → Debug Current PHP File** or use the toolbar's Debug action.
4. Forge starts a local listener, launches PHP with `XDEBUG_MODE=debug`, sets a line
   breakpoint at the caret, then starts execution.
5. Use **Run → Step Over**, **Step Into**, or **Step Out**. The Terminal tool window
   reports the DBGp session and status.

The current implementation is an early CLI debugger: it does not yet display stack
frames, variables, watches, conditional breakpoints, path mappings, or browser/web
request sessions. The configured PHP interpreter must have Xdebug enabled already.

## Profile the current file

Select **Run → Profile Current PHP File**. Forge launches PHP with
`XDEBUG_MODE=profile` and stores Cachegrind output under the Forge application data
directory's `profiles` subfolder. On process exit it reports the functions with the
largest accumulated self-cost for the first event in the profile.

These values are Cachegrind event counts (normally Xdebug's `Time` event), not
percentages or calibrated wall-clock durations. Call-graph aggregation, comparisons,
web-request profiling, and a graphical profile explorer are not implemented yet.
Generated profiles are kept locally and are not uploaded.
