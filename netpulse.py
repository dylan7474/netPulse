#!/usr/bin/env python3
"""NetPulse Pro (Python Edition)

A zero-dependency Tkinter desktop monitor for Linux desktops.
Tracks up to five endpoints and performs periodic ICMP checks via `ping`.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import BooleanVar, END, LEFT, RIGHT, StringVar, Tk
from tkinter import messagebox, ttk
from urllib.parse import urlparse

PING_INTERVAL_MS = 3000
PING_TIMEOUT_S = 1
MAX_TARGETS = 5
CONFIG_PATH = Path(__file__).with_name("netpulse_py_config.json")


@dataclass
class HistoryPoint:
    timestamp: float
    success: bool
    latency_ms: float | None


@dataclass
class Target:
    display: str
    host: str
    history: deque[HistoryPoint] = field(default_factory=lambda: deque(maxlen=120))
    last_status: str = "off"
    last_latency_ms: float | None = None


class NetPulseApp:
    def __init__(self, root: Tk) -> None:
        self.root = root
        self.root.title("NetPulse Pro | Python Edition")
        self.root.geometry("980x600")

        self.targets: list[Target] = []
        self.is_monitoring = False
        self.auto_start = BooleanVar(value=False)
        self.input_value = StringVar()

        self._build_ui()
        self._load_config()
        self._render_table()

        if self.auto_start.get() and self.targets:
            self.toggle_monitoring()

    def _build_ui(self) -> None:
        root_frame = ttk.Frame(self.root, padding=12)
        root_frame.pack(fill="both", expand=True)

        controls = ttk.Frame(root_frame)
        controls.pack(fill="x", pady=(0, 8))

        ttk.Label(controls, text="Target").pack(side=LEFT, padx=(0, 6))
        entry = ttk.Entry(controls, textvariable=self.input_value)
        entry.pack(side=LEFT, fill="x", expand=True)
        entry.bind("<Return>", lambda _evt: self.add_target())

        ttk.Button(controls, text="Add", command=self.add_target).pack(side=LEFT, padx=6)
        ttk.Button(controls, text="Remove Selected", command=self.remove_selected).pack(side=LEFT, padx=6)
        self.toggle_button = ttk.Button(controls, text="Start Monitoring", command=self.toggle_monitoring)
        self.toggle_button.pack(side=LEFT, padx=6)
        ttk.Button(controls, text="Save", command=self._save_config).pack(side=LEFT, padx=6)

        ttk.Checkbutton(controls, text="Auto-Start", variable=self.auto_start).pack(side=RIGHT)

        columns = ("target", "status", "latency", "avg", "uptime")
        self.table = ttk.Treeview(root_frame, columns=columns, show="headings", height=12)
        self.table.heading("target", text="Target")
        self.table.heading("status", text="Status")
        self.table.heading("latency", text="Latency")
        self.table.heading("avg", text="Avg 60s")
        self.table.heading("uptime", text="Uptime 60s")
        self.table.column("target", width=340, anchor="w")
        self.table.column("status", width=110, anchor="center")
        self.table.column("latency", width=120, anchor="center")
        self.table.column("avg", width=120, anchor="center")
        self.table.column("uptime", width=120, anchor="center")
        self.table.pack(fill="x")

        stats = ttk.Frame(root_frame)
        stats.pack(fill="x", pady=(8, 8))
        self.stats_label = ttk.Label(stats, text="Targets: 0 | Healthy: 0 | Critical: 0")
        self.stats_label.pack(side=LEFT)

        log_header = ttk.Frame(root_frame)
        log_header.pack(fill="x")
        ttk.Label(log_header, text="Activity Log").pack(side=LEFT)
        ttk.Button(log_header, text="Clear Log", command=self._clear_log).pack(side=RIGHT)

        self.log = ttk.Treeview(root_frame, columns=("msg",), show="headings", height=11)
        self.log.heading("msg", text="Message")
        self.log.column("msg", anchor="w")
        self.log.pack(fill="both", expand=True, pady=(4, 0))

    def _log(self, msg: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.log.insert("", END, values=(f"[{timestamp}] {msg}",))
        self.log.yview_moveto(1.0)

    def _clear_log(self) -> None:
        for row in self.log.get_children():
            self.log.delete(row)

    def _normalize_target(self, raw: str) -> tuple[str, str] | None:
        text = raw.strip()
        if not text:
            return None

        parsed = urlparse(text if "://" in text else f"https://{text}")
        host = parsed.hostname or text
        host = host.strip("[]")
        if not host:
            return None

        display = text if "://" in text else f"https://{text}"
        return display, host

    def add_target(self) -> None:
        normalized = self._normalize_target(self.input_value.get())
        if not normalized:
            messagebox.showwarning("Invalid target", "Enter a valid hostname, IP, or URL.")
            return

        display, host = normalized
        if len(self.targets) >= MAX_TARGETS:
            messagebox.showwarning("Limit reached", f"You can monitor up to {MAX_TARGETS} targets.")
            return

        if any(t.host.lower() == host.lower() for t in self.targets):
            messagebox.showinfo("Duplicate", "That host is already in the watch list.")
            return

        self.targets.append(Target(display=display, host=host))
        self.input_value.set("")
        self._log(f"Target added: {display}")
        self._render_table()

    def remove_selected(self) -> None:
        selected = self.table.selection()
        if not selected:
            return

        indexes = sorted((self.table.index(item) for item in selected), reverse=True)
        for idx in indexes:
            target = self.targets[idx]
            self._log(f"Target removed: {target.display}")
            del self.targets[idx]

        self._render_table()

    def toggle_monitoring(self) -> None:
        self.is_monitoring = not self.is_monitoring
        self.toggle_button.config(text="Stop Monitoring" if self.is_monitoring else "Start Monitoring")
        self._log("Monitoring started." if self.is_monitoring else "Monitoring stopped.")
        if self.is_monitoring:
            self._run_monitor_cycle()

    def _run_monitor_cycle(self) -> None:
        if not self.is_monitoring:
            return

        for target in self.targets:
            success, latency = self._ping(target.host)
            target.history.append(HistoryPoint(timestamp=time.time(), success=success, latency_ms=latency))
            target.last_latency_ms = latency
            target.last_status = self._compute_health(target)

        self._render_table()
        self.root.after(PING_INTERVAL_MS, self._run_monitor_cycle)

    def _compute_health(self, target: Target) -> str:
        now = time.time()
        recent30 = [p for p in target.history if now - p.timestamp <= 30]
        recent60 = [p for p in target.history if now - p.timestamp <= 60]
        drops30 = sum(1 for p in recent30 if not p.success)
        drops60 = sum(1 for p in recent60 if not p.success)

        if drops60 > 10:
            return "red"
        if drops30 > 3:
            return "amber"
        return "green"

    def _stats_for(self, target: Target) -> tuple[str, str, str]:
        now = time.time()
        last60 = [p for p in target.history if now - p.timestamp <= 60]
        successes = [p for p in last60 if p.success]

        latency_text = f"{target.last_latency_ms:.0f} ms" if target.last_latency_ms is not None else "--"
        avg_text = f"{(sum(p.latency_ms for p in successes if p.latency_ms is not None) / len(successes)):.0f} ms" if successes else "--"
        uptime_text = f"{(len(successes) / len(last60) * 100):.0f}%" if last60 else "--"
        return latency_text, avg_text, uptime_text

    def _render_table(self) -> None:
        for row in self.table.get_children():
            self.table.delete(row)

        healthy = 0
        critical = 0

        for target in self.targets:
            status = target.last_status
            if status == "green":
                healthy += 1
            if status in {"amber", "red"}:
                critical += 1

            latency_text, avg_text, uptime_text = self._stats_for(target)
            self.table.insert(
                "",
                END,
                values=(target.display, status.upper(), latency_text, avg_text, uptime_text),
            )

        self.stats_label.config(
            text=f"Targets: {len(self.targets)} | Healthy: {healthy} | Critical: {critical}"
        )

    def _ping(self, host: str) -> tuple[bool, float | None]:
        if shutil.which("ping") is None:
            self._log("`ping` command not found; mark checks as failed.")
            return False, None

        cmd = ["ping", "-c", "1", "-W", str(PING_TIMEOUT_S), host]
        try:
            completed = subprocess.run(cmd, capture_output=True, text=True, timeout=PING_TIMEOUT_S + 1)
        except subprocess.TimeoutExpired:
            return False, None

        output = f"{completed.stdout}\n{completed.stderr}"
        latency_match = re.search(r"time[=<]([0-9.]+)\s*ms", output)
        latency = float(latency_match.group(1)) if latency_match else None
        return completed.returncode == 0, latency

    def _save_config(self) -> None:
        payload = {
            "auto_start": self.auto_start.get(),
            "targets": [target.display for target in self.targets],
        }
        CONFIG_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        self._log(f"Configuration saved: {CONFIG_PATH.name}")

    def _load_config(self) -> None:
        if not CONFIG_PATH.exists():
            return

        try:
            payload = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            self._log("Could not parse saved configuration; starting clean.")
            return

        self.auto_start.set(bool(payload.get("auto_start", False)))

        loaded = 0
        for raw in payload.get("targets", []):
            normalized = self._normalize_target(str(raw))
            if not normalized or len(self.targets) >= MAX_TARGETS:
                continue
            display, host = normalized
            if any(t.host.lower() == host.lower() for t in self.targets):
                continue
            self.targets.append(Target(display=display, host=host))
            loaded += 1

        if loaded:
            self._log(f"Loaded {loaded} targets from configuration.")


def main() -> None:
    root = Tk()
    ttk.Style().theme_use("clam")
    app = NetPulseApp(root)
    root.protocol("WM_DELETE_WINDOW", lambda: (app._save_config(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()
