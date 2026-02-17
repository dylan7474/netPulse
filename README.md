# NetPulse Pro

NetPulse Pro is a lightweight, browser-based network health monitor. It lets you track up to five endpoints in real time, observe simulated latency and health trends, and keep a small activity log for quick diagnostics.

Because the app is a static frontend (`index.html`), there is no backend service to install. You can open it directly in a browser or serve it locally with any static web server.

This repository also includes `netpulse.ps1`, a Windows Forms PowerShell edition that performs real ICMP pings and draws a mini latency graph per target.

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

## PowerShell edition (`netpulse.ps1`)

The PowerShell UI is intended for Windows PowerShell 5.1+ (or PowerShell 7 on Windows with WinForms support).

### Run

```powershell
powershell -ExecutionPolicy Bypass -File .\netpulse.ps1
```

### Controls and behavior

- **Add**: Accepts hostname, IP, or URL input. Invalid values are rejected and logged.
- **Duplicate prevention**: Targets are deduplicated by host.
- **Start Monitoring / Stop Monitoring**: Runs ICMP checks every 3 seconds.
- **Save Config**: Persists target URLs to `netpulse_config.json` next to the script.
- **Auto-load on launch**: Restores saved targets when `netpulse_config.json` exists.
- **Health light logic**:
  - Green: normal packet success in the last 30/60 seconds.
  - Amber: more than 3 drops in the last 30 seconds.
  - Red: more than 10 drops in the last 60 seconds.

## Roadmap

- Add optional real endpoint probing through a small backend service.
- Add configurable check intervals and timeout values in the UI.
- Export and import monitoring profiles as JSON.
- Add lightweight automated UI tests for key controls.
