# NetPulse Pro

NetPulse Pro is a lightweight, browser-based network health monitor. It lets you track up to five endpoints in real time, observe simulated latency and health trends, and keep a small activity log for quick diagnostics.

Because the app is a static frontend (`index.html`), there is no backend service to install. You can open it directly in a browser or serve it locally with any static web server.

## Basic controls

- **Add to Monitor**: Add an endpoint to the active watch list (up to 5).
- **Start Monitoring / Stop Monitoring**: Toggle live checks for all configured endpoints.
- **Save**: Persist configured targets and preferences in local browser storage.
- **Auto-Start**: Automatically begin monitoring on page load when targets exist.
- **Remove**: Remove a target from the monitor table.
- **Clear Log**: Clear messages in the activity log panel.

## Build & run

This project ships with a `Makefile` and `configure` script to validate a minimal local toolchain.

1. Validate local dependencies:

   ```bash
   ./configure
   ```

2. Run project checks:

   ```bash
   make verify
   ```

3. Launch a local static server:

   ```bash
   make serve
   ```

4. Open `http://localhost:8080` in your browser.

## Roadmap

- Add optional real endpoint probing through a small backend service.
- Add configurable check intervals and timeout values in the UI.
- Export and import monitoring profiles as JSON.
- Add lightweight automated UI tests for key controls.
