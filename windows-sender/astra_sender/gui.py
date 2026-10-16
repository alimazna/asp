"""Lightweight ttk GUI for the ASTRA sender.

Deliberately minimal: a status grid, counters, a local event log, and
pause/resume controls. Runs on Windows 10 with only the standard library
(tkinter ships with CPython on Windows). No browser, no webview, no heavy
toolkit. The GUI never holds secrets: the config is edited in the config
file, and the token is never printed.
"""

from __future__ import annotations

import os
import sys
import tkinter as tk
import tkinter.scrolledtext as st
from tkinter import ttk
from typing import Any, Dict, List, Optional


class SenderGui:
    def __init__(self, engine, config, app_data_dir: str,
                 config_path: Optional[str] = None,
                 needs_config: bool = False):
        self._engine = engine
        self._config = config
        self._data_dir = app_data_dir
        # The exact file Settings must write to. Deriving it from _data_dir
        # wrote to %LOCALAPPDATA% even when the app was reading the
        # installer-provisioned %PROGRAMDATA% config, so saved edits were
        # silently ignored. None keeps the old behaviour for the tests.
        self._config_path = config_path
        self._needs_config = needs_config
        self._root = tk.Tk()
        self._root.title("ASTRA Sender")
        self._root.minsize(560, 480)
        self._build()
        self._last_log_count = 0
        if needs_config:
            # A windowed exe shows no console, so a double-click on a
            # not-yet-configured app used to raise a message box that exited
            # without offering any way to enter the token. Open the window,
            # explain, and jump straight into Settings instead.
            self._root.after(300, self._prompt_setup)

    # -- construction -------------------------------------------------------

    def _build(self) -> None:
        frame = ttk.Frame(self._root, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        # Connection status row.
        row = ttk.Frame(frame)
        row.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(row, text="MT5 terminal:").pack(side=tk.LEFT)
        self.mt5_label = tk.Label(row, text="...", width=20, anchor="w",
                                  bg="#fff", relief=tk.SUNKEN, bd=1)
        self.mt5_label.pack(side=tk.LEFT, padx=(6, 24))
        ttk.Label(row, text="Server:").pack(side=tk.LEFT)
        self.server_label = tk.Label(row, text="...", width=20, anchor="w",
                                     bg="#fff", relief=tk.SUNKEN, bd=1)
        self.server_label.pack(side=tk.LEFT, padx=(6, 0))

        # Symbol + timeframes.
        info = ttk.Frame(frame)
        info.pack(fill=tk.X, pady=(0, 8))
        ttk.Label(info, text="Symbol:").pack(side=tk.LEFT)
        self.symbol_label = ttk.Label(info, text=self._config.symbol, width=16,
                                      anchor="w")
        self.symbol_label.pack(side=tk.LEFT, padx=(6, 24))
        ttk.Label(info, text="Timeframes:").pack(side=tk.LEFT)
        tf_text = ", ".join(self._config.timeframes)
        ttk.Label(info, text=tf_text).pack(side=tk.LEFT, padx=(6, 0))

        # Last successful sync time.
        self.sync_label = ttk.Label(frame, text="Last sync: -", foreground="#555")
        self.sync_label.pack(fill=tk.X, pady=(0, 4))

        # Last candle per timeframe (two-line grid).
        candles_frame = ttk.LabelFrame(frame, text="Candles (last read / last acked, per timeframe)",
                                       padding=8)
        candles_frame.pack(fill=tk.X, pady=6)
        self.candle_labels: Dict[str, tk.Label] = {}
        grid = ttk.Frame(candles_frame)
        grid.pack(fill=tk.X)
        for idx, tf in enumerate(self._config.timeframes):
            r, c = divmod(idx, 3)
            label = ttk.Label(grid, text=f"{tf}: -/ -", width=28, anchor="w")
            label.grid(row=r, column=c, sticky="w", padx=(0, 12), pady=1)
            self.candle_labels[tf] = label

        # Counters.
        counters = ttk.Frame(frame)
        counters.pack(fill=tk.X, pady=6)
        self.sent_label = self._counter_frame(counters, "Sent", 0, 0)
        self.acked_label = self._counter_frame(counters, "Acked", 1)
        self.dedup_label = self._counter_frame(counters, "Dedupe", 2)
        self.queued_label = self._counter_frame(counters, "Queued", 3)
        self.failed_label = self._counter_frame(counters, "Failed", 4)

        # Status/error line.
        self.status_label = ttk.Label(frame, text="starting...", wraplength=520,
                                      foreground="#333")
        self.status_label.pack(fill=tk.X, pady=(4, 0))
        # Second, muted line: the actionable reason (e.g. MT5 closed/not logged
        # in), kept separate so the main status stays scannable. Selectable so
        # the owner can copy the exact text when asking for help.
        self.reason_label = tk.Text(frame, height=3, wrap=tk.WORD,
                                    relief=tk.FLAT, background="#fdf6e3",
                                    foreground="#8a4b00", font=("Segoe UI", 9),
                                    cursor="arrow")
        self.reason_label.pack(fill=tk.X, pady=(0, 6))
        self._set_reason("")

        # Local event log.
        ttk.Label(frame, text="Local event log:").pack(anchor="w")
        self.log_box = st.ScrolledText(frame, height=10, state=tk.DISABLED,
                                       wrap=tk.WORD, font=("Consolas", 9))
        self.log_box.pack(fill=tk.BOTH, expand=True, pady=(2, 6))

        # Controls.
        controls = ttk.Frame(frame)
        controls.pack(fill=tk.X)
        self.pause_btn = ttk.Button(controls, text="Pause", command=self._toggle_pause)
        self.pause_btn.pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(controls, text="Reconnect now", command=self._reconnect).pack(side=tk.LEFT)
        ttk.Button(controls, text="Settings", command=self._open_settings).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(controls, text="Open config folder", command=self._open_config).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(controls, text="MT5 diagnostic", command=self._run_diagnostic).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Label(controls, text=f"config: {self._config_file_summary()}").pack(side=tk.RIGHT)

    def _set_reason(self, text: str) -> None:
        """Replace the reason text, keeping the widget read-only but copyable."""
        self.reason_label.configure(state=tk.NORMAL)
        self.reason_label.delete("1.0", tk.END)
        if text:
            self.reason_label.insert("1.0", text)
        self.reason_label.configure(state=tk.DISABLED)

    def _counter_frame(self, parent: ttk.Frame, label: str, col: int,
                       value: int = 0) -> tk.Label:
        wrapper = ttk.Frame(parent)
        wrapper.grid(row=0, column=col, sticky="w", padx=(0, 18))
        ttk.Label(wrapper, text=label).pack(side=tk.LEFT)
        lbl = tk.Label(wrapper, text=str(value), width=6, anchor="w", bg="#fff",
                       relief=tk.SUNKEN, bd=1)
        lbl.pack(side=tk.LEFT, padx=(4, 0))
        return lbl

    def _config_file_summary(self) -> str:
        # Show the file actually in use, so the label never points the owner at
        # a different config.json than the one Settings writes.
        return self._settings_path() or "-"

    # -- control handlers ---------------------------------------------------

    def _toggle_pause(self) -> None:
        # The engine's paused flag is managed by the engine thread.
        self.log("Pause/resume toggled")
        self._engine.pause() if self._engine.status_snapshot().get("running") and \
            self._paused_state() else self._engine.resume()
        self._paused = not getattr(self, "_paused", False)
        self.pause_btn.configure(text="Resume" if self._paused else "Pause")

    def _paused_state(self) -> bool:
        return getattr(self, "_paused", False)

    def _reconnect(self) -> None:
        self.log("Manual reconnect requested")
        self._engine.resume()

    def _open_config(self) -> None:
        try:
            if sys.platform == "win32":
                os.startfile(self._data_dir)
            else:  # pragma: no cover
                os.system(f'xdg-open "{self._data_dir}" &')
        except Exception as exc:
            self.log(f"cannot open config folder: {exc}")

    def _run_diagnostic(self) -> None:
        """Run the MT5 diagnostic in-process and show the findings in the log.

        Runs on a worker thread because a failed initialize can block for
        seconds; the Tk main loop must stay responsive.
        """
        import threading

        def worker() -> None:
            try:
                from diag_mt5 import main as diag_main
            except Exception as exc:
                self.log(f"diagnostic unavailable: {exc}")
                return
            try:
                code = diag_main()
            except Exception as exc:
                self.log(f"diagnostic failed: {type(exc).__name__}: {exc}")
                return
            self.log(f"MT5 diagnostic finished (exit {code})")

        self.log("running MT5 diagnostic...")
        threading.Thread(target=worker, daemon=True).start()

    def _settings_path(self) -> str:
        """The file Settings writes. Falls back to data_dir/config.json."""
        return self._config_path or os.path.join(self._data_dir, "config.json")

    def _prompt_setup(self) -> None:
        """First-run guidance: explain, then open Settings in one click."""
        from tkinter import messagebox
        self._set_reason(
            "ASTRA Sender is not configured yet.\n"
            "Click OK to enter the backend URL, sender ID and ingestion "
            "token. Nothing is sent anywhere until you do.")
        try:
            messagebox.showinfo(
                "ASTRA Sender - setup required",
                "ASTRA Sender is not configured yet.\n\n"
                f"Config file:\n{self._settings_path()}\n\n"
                "Click OK to fill in the backend URL, sender ID and ingestion "
                "token now. If you already have a config.json, copy it to the "
                "folder above and restart.")
        except Exception:
            pass
        self._open_settings()

    def _open_settings(self) -> bool:
        """Edit the connection settings in the GUI instead of hand-editing JSON.

        Writes through SenderConfig.save() so the on-disk format stays the
        single source of truth; the token is masked on screen and never logged.
        Returns True when the config was saved and is now usable.
        """
        from tkinter import simpledialog
        from .config import SenderConfig

        backend = simpledialog.askstring(
            "ASTRA Sender - backend URL", "Backend HTTPS URL:",
            initialvalue=self._config.backend_url, parent=self._root)
        if backend is None:
            return False
        sender_id = simpledialog.askstring(
            "ASTRA Sender - sender ID", "Sender ID (stable instance name):",
            initialvalue=self._config.sender_id, parent=self._root)
        if sender_id is None:
            return False
        token = simpledialog.askstring(
            "ASTRA Sender - ingestion token",
            "Sender token (paste ASTRA_SENDER_TOKEN):", show="*",
            parent=self._root)
        if token is None:
            return False

        self._config.backend_url = backend.strip()
        self._config.sender_id = sender_id.strip()
        if token.strip():
            self._config.sender_token = token.strip()
        path = self._settings_path()
        try:
            parent = os.path.dirname(path)
            if parent:
                os.makedirs(parent, exist_ok=True)
            SenderConfig.save(self._config, path)
        except OSError as exc:
            self.log(f"cannot save config: {exc}")
            self._set_reason(f"Could not write {path}: {exc}")
            return False

        problems = self._config.validate()
        if problems:
            # Saved, but still unusable (e.g. the token was left blank).
            for problem in problems:
                self.log(f"still not configured: {problem}")
            self._set_reason("Config saved but incomplete:\n  - "
                             + "\n  - ".join(problems))
            return False

        self._needs_config = False
        self.log(f"settings saved to {path}")
        # The engine holds its own client built from the old config, so the
        # new values only take effect after a restart. Say so plainly rather
        # than silently keeping the stale connection.
        self._set_reason("Settings saved. Restart the app to connect with the "
                         "new settings.")
        return True

    # -- engine integration -------------------------------------------------

    @staticmethod
    def _state_colour(state: str) -> str:
        if state in ("READY", "ONLINE"):
            return "#006400"   # green
        if state in ("STARTING", "OFFLINE", "..."):
            return "#8a6d00"   # amber
        return "#b00020"       # red (UNAVAILABLE, ERROR, AUTH_FAILED)

    def update_from_status(self, snapshot: Dict[str, Any]) -> None:
        mt5_state = snapshot.get("mt5_state", "...")
        server_state = snapshot.get("server_state", "...")
        self.mt5_label.config(text=mt5_state, fg=self._state_colour(mt5_state))
        self.server_label.config(text=server_state,
                                 fg=self._state_colour(server_state))
        self.symbol_label.config(text=snapshot.get("resolved_symbol") or self._config.symbol)
        sent = snapshot.get("sent", 0)
        acked = snapshot.get("acknowledged", 0)
        dedup = snapshot.get("deduplicated", 0)
        queued = snapshot.get("queued", 0)
        failed = snapshot.get("failed", 0)
        self.sent_label.config(text=str(sent))
        self.acked_label.config(text=str(acked))
        self.dedup_label.config(text=str(dedup))
        self.queued_label.config(text=str(queued))
        self.failed_label.config(text=str(failed))

        last_error = snapshot.get("last_error", "")
        last_sync = snapshot.get("last_sync_at")
        mt5_state = snapshot.get("mt5_state", "")
        server_state = snapshot.get("server_state", "")

        if mt5_state == "UNAVAILABLE":
            self.status_label.config(text="Stopped: MT5 terminal not available",
                                     fg="#b00020")
        elif server_state in ("ERROR", "AUTH_FAILED", "OFFLINE"):
            self.status_label.config(text=f"Stopped: server {server_state.lower()}",
                                     fg="#b00020")
        elif server_state == "ONLINE" and mt5_state == "READY":
            self.status_label.config(text="Running (shadow data, read-only)",
                                     fg="#006400")
        elif snapshot.get("running"):
            self.status_label.config(text="Starting...", fg="#333")

        # Show the actionable reason. Prefer the explicit error, and fall back
        # to a plain description of the current blocked state.
        reason = last_error
        if not reason:
            if mt5_state == "UNAVAILABLE":
                reason = "open the MT5 terminal and log in; retrying automatically"
            elif server_state == "AUTH_FAILED":
                reason = "server rejected the credential; check Settings"
            elif server_state == "ERROR":
                reason = "cannot reach the server; retrying with backoff"
        self._set_reason(reason)

        if last_sync:
            self.sync_label.config(text=f"Last sync: {last_sync}")

        for tf, value in (snapshot.get("last_candle_per_timeframe") or {}).items():
            if isinstance(value, dict):
                label = self.candle_labels.get(tf)
                if label:
                    t = value.get("time", "-")
                    close = value.get("close", "?")
                    label.config(text=f"{tf}: last {t} close={close}")
        for tf, value in (snapshot.get("acked_candle_per_timeframe") or {}).items():
            label = self.candle_labels.get(tf)
            if label:
                t = value.get("open_time", "-")
                label.config(text=(label.cget("text").split(" ack")[0] +
                                   f" ack={t}"))

    def log(self, message: str) -> None:
        try:
            self.log_box.config(state=tk.NORMAL)
            self.log_box.insert(tk.END, f"{message}\n")
            self.log_box.see(tk.END)
            self.log_box.config(state=tk.DISABLED)
        except Exception:
            pass

    # -- lifecycle ----------------------------------------------------------

    def run(self, on_status) -> None:
        self._engine.start()
        self._poll_status(on_status)
        self._root.mainloop()

    def _poll_status(self, on_status) -> None:
        if on_status is not None:
            try:
                on_status()
            except Exception:
                pass
        self._root.after(1000, lambda: self._poll_status(on_status))

    def close(self) -> None:
        self._engine.stop()
        self._root.destroy()