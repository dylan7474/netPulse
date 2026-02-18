# NetPulse Pro

NetPulse Pro is a lightweight, browser-based network health monitor. It lets you track up to five endpoints in real time, observe simulated latency and health trends, and keep a small activity log for quick diagnostics.

Because the app is a static frontend (`index.html`), there is no backend service to install. You can open it directly in a browser or serve it locally with any static web server.

This repository also includes `netpulse.ps1`, a Windows Forms PowerShell edition that performs real ICMP pings and draws a mini latency graph per target.

For Linux desktops, the repository also includes `netpulse.py`, a Tkinter edition that runs as a native GUI app on most distributions with Python 3 and Tk installed.

For users who prefer a compiled binary, `netpulse.c` provides a native GTK desktop GUI that mirrors the Python workflow and can be built into a Linux executable with `gcc`.

## Basic controls

- **Add to Monitor**: Add an endpoint to the active watch list (up to 5).
- **Start Monitoring / Stop Monitoring**: Toggle live checks for all configured endpoints.
- **Save**: Persist configured targets and preferences in local browser storage.
- **Auto-Start**: Automatically begin monitoring on page load when targets exist.
- **Remove**: Remove a target from the monitor table.
- **Clear Log**: Clear messages in the activity log panel.

## Run locally

This is a static application, so no build step is required.

1. (Optional) Validate local dependencies:

   ```bash
   ./configure
   ```

2. Launch a local static server:

   ```bash
   python3 -m http.server 8080
   ```

3. Open `http://localhost:8080` in your browser.

## PowerShell edition (`netpulse.ps1`)

The PowerShell UI is intended for Windows PowerShell 5.1+ (or PowerShell 7 on Windows with WinForms support).

### Run

```powershell
powershell -ExecutionPolicy Bypass -File .\netpulse.ps1
```

### Controls and behavior

- **Add**: Accepts hostname, IP, or URL input. Invalid values are rejected and logged.
- **Duplicate prevention**: Targets are deduplicated by host.
- **Start Monitoring / Stop Monitoring**: Runs ICMP checks every 3 seconds (1500ms timeout per ping by default).
- **Save Config**: Persists target URLs to `netpulse_config.json` next to the script.
- **Auto-load on launch**: Restores saved targets when `netpulse_config.json` exists.
- **Quick add shortcut**: Press **Enter** in the input box to add a target.
- **Per-target stats**: Shows current latency, 60-second average latency, and recent uptime percentage.
- **Health light logic**:
  - Green: normal packet success in the last 30/60 seconds.
  - Amber: more than 3 drops in the last 30 seconds.
  - Red: more than 10 drops in the last 60 seconds.

## C edition (`netpulse.c`)

The C edition is now a Linux GTK desktop app with controls comparable to the Python GUI.

### Build

```bash
make build-c
```

The build uses `pkg-config --cflags --libs gtk+-3.0`, so you need GTK 3 development packages installed (for example `libgtk-3-dev` on Debian/Ubuntu).

### Run

```bash
./netpulse-c
```

You can optionally seed targets from CLI arguments or a config file:

```bash
./netpulse-c github.com 1.1.1.1
./netpulse-c -f targets.txt
./netpulse-c -b http://localhost:8787/probe
```

### Controls and behavior

- **Add**: Accepts hostname, IP, or URL.
- **Duplicate prevention**: Targets are deduplicated by normalized host.
- **Start Monitoring / Stop Monitoring**: Runs ICMP checks every 3 seconds (`ping -c 1 -W 1` by default, configurable with `-i`).
- **Probe backend (optional)**: When set to an `http://` or `https://` URL (or provided via `-b`), checks are sent to a backend probe endpoint using `curl` with `target=<url_or_host>` query parameter instead of direct local ICMP ping. Invalid probe values automatically fall back to local ICMP ping.
- **Save**: Persists targets plus Auto-Start preference to `netpulse_c_config.txt`.
- **Auto-Start**: Starts monitoring on launch when saved targets exist.
- **Remove Selected**: Removes one or more selected targets from the table.
- **Quick add shortcut**: Press **Enter** in the input box to add a target.
- **Per-target stats**: Shows current latency, 60-second average latency, and recent uptime percentage.
- **Health status thresholds** match the Python/PowerShell logic:
  - Green: normal packet success in the last 30/60 seconds.
  - Amber: more than 3 drops in the last 30 seconds.
  - Red: more than 10 drops in the last 60 seconds.

## Python GUI edition (`netpulse.py`)

The Python desktop UI is intended for Linux systems with a graphical environment.

### Run

```bash
python3 netpulse.py
```

### Controls and behavior

- **Add**: Accepts hostname, IP, or URL.
- **Duplicate prevention**: Targets are deduplicated by host.
- **Start Monitoring / Stop Monitoring**: Runs ICMP checks every 3 seconds (`ping -c 1 -W 1`).
- **Save**: Persists targets and Auto-Start state to `netpulse_py_config.json`.
- **Auto-Start**: Begins monitoring automatically when saved targets exist.
- **Remove Selected**: Removes one or more selected targets from the table.
- **Quick add shortcut**: Press **Enter** in the input box to add a target.
- **Per-target stats**: Shows current latency, 60-second average latency, and recent uptime percentage.

## Roadmap

- Add configurable check intervals and timeout values in the UI.
- Export and import monitoring profiles as JSON.
- Add lightweight automated UI tests for key controls.
