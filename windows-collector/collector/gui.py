"""Desktop window for the collector.

Built on tkinter, which ships with CPython on Windows, so the packaged
executable needs no extra GUI dependency and starts quickly on a low-memory PC.

The window only ever renders values that came from ``SyncEngine.status()``. It
never invents a price, a signal, or a "connected" state: an unknown field shows
as a dash and a broken component shows its real error text.
"""

from __future__ import annotations

import os
import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk
from typing import Optional

from . import __version__
from .config import (
    CollectorConfig,
    default_config_path,
    default_ledger_path,
    default_store_path,
    load_token,
    save_token,
)
from .display import colour as _colour
from .display import fmt_time as _fmt_time
from .engine import SyncEngine
from .identity import TIMEFRAMES
from .ledger import DeliveryLedger
from .mt5_reader import Mt5Reader
from .receiver import ReceiverClient
from .store import CandleStore

REFRESH_MS = 1000


class CollectorApp:
    """Tk window driving a ``SyncEngine`` on a background thread."""

    def __init__(self, root: tk.Tk):
        self.root = root
        self.config_path = default_config_path()
        self.config = CollectorConfig.load(self.config_path)
        self.store: Optional[CandleStore] = None
        self.ledger: Optional[DeliveryLedger] = None
        self.engine: Optional[SyncEngine] = None
        self.events: "queue.Queue[tuple]" = queue.Queue()
        self._closing = False

        root.title(f"ASTRA Collector {__version__}")
        root.geometry("980x680")
        root.minsize(820, 600)
        self._build()
        self._load_into_form()
        self._drain_events()
        root.protocol("WM_DELETE_WINDOW", self.on_close)

    # ---------------------------------------------------------------- layout

    def _build(self) -> None:
        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill="both", expand=True)

        status_row = ttk.Frame(outer)
        status_row.pack(fill="x", pady=(0, 8))

        self.mt5_label = ttk.Label(status_row, text="MT5: —", font=("Segoe UI", 11, "bold"))
        self.mt5_label.grid(row=0, column=0, sticky="w", padx=(0, 20))
        self.server_label = ttk.Label(status_row, text="Server: —",
                                      font=("Segoe UI", 11, "bold"))
        self.server_label.grid(row=0, column=1, sticky="w", padx=(0, 20))
        self.symbol_label = ttk.Label(status_row, text="Symbol: —",
                                      font=("Segoe UI", 11, "bold"))
        self.symbol_label.grid(row=0, column=2, sticky="w", padx=(0, 20))
        self.last_sync_label = ttk.Label(status_row, text="Last sync: —")
        self.last_sync_label.grid(row=0, column=3, sticky="w")

        form = ttk.LabelFrame(outer, text="Destination and identity", padding=8)
        form.pack(fill="x", pady=(0, 8))
        form.columnconfigure(1, weight=1)

        ttk.Label(form, text="Receiver URL").grid(row=0, column=0, sticky="w")
        self.url_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.url_var).grid(
            row=0, column=1, sticky="ew", padx=6)

        ttk.Label(form, text="Symbol").grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.symbol_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.symbol_var, width=18).grid(
            row=1, column=1, sticky="w", padx=6, pady=(6, 0))

        ttk.Label(form, text="Sender id").grid(row=2, column=0, sticky="w", pady=(6, 0))
        self.sender_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.sender_var, width=24).grid(
            row=2, column=1, sticky="w", padx=6, pady=(6, 0))

        ttk.Label(form, text="Token").grid(row=3, column=0, sticky="w", pady=(6, 0))
        self.token_var = tk.StringVar()
        ttk.Entry(form, textvariable=self.token_var, show="•").grid(
            row=3, column=1, sticky="ew", padx=6, pady=(6, 0))

        buttons = ttk.Frame(form)
        buttons.grid(row=4, column=1, sticky="w", pady=(8, 0), padx=6)
        ttk.Button(buttons, text="Save settings",
                   command=self.on_save_settings).pack(side="left")
        ttk.Button(buttons, text="Test connection",
                   command=self.on_test).pack(side="left", padx=6)
        ttk.Button(buttons, text="Reveal token",
                   command=self.on_reveal_token).pack(side="left")

        controls = ttk.Frame(outer)
        controls.pack(fill="x", pady=(0, 8))
        self.start_button = ttk.Button(controls, text="Start", command=self.on_start)
        self.start_button.pack(side="left")
        self.pause_button = ttk.Button(controls, text="Pause", command=self.on_pause,
                                       state="disabled")
        self.pause_button.pack(side="left", padx=6)
        self.stop_button = ttk.Button(controls, text="Stop", command=self.on_stop,
                                      state="disabled")
        self.stop_button.pack(side="left")
        ttk.Button(controls, text="Open data folder",
                   command=self.on_open_folder).pack(side="left", padx=6)

        table_frame = ttk.LabelFrame(outer, text="Timeframes", padding=6)
        table_frame.pack(fill="both", expand=True, pady=(0, 8))
        columns = ("tf", "read", "acked", "stored", "gaps")
        self.tree = ttk.Treeview(table_frame, columns=columns, show="headings",
                                 height=10)
        headings = {
            "tf": "TF", "read": "Last closed read (UTC)",
            "acked": "Last acknowledged (UTC)", "stored": "Stored", "gaps": "Gaps",
        }
        for column, heading in headings.items():
            self.tree.heading(column, text=heading)
            self.tree.column(column, width=90 if column in ("tf", "stored", "gaps")
                             else 230, anchor="w")
        self.tree.pack(fill="both", expand=True)
        for timeframe in TIMEFRAMES:
            self.tree.insert("", "end", iid=timeframe,
                             values=(timeframe, "—", "—", 0, 0))

        counters = ttk.Frame(outer)
        counters.pack(fill="x", pady=(0, 8))
        self.counter_vars = {}
        for index, name in enumerate(
                ("sent", "acknowledged", "deduplicated", "queued", "failed",
                 "rejected")):
            ttk.Label(counters, text=f"{name}:").grid(row=0, column=index * 2,
                                                      sticky="e")
            var = tk.StringVar(value="0")
            self.counter_vars[name] = var
            ttk.Label(counters, textvariable=var, width=8).grid(
                row=0, column=index * 2 + 1, sticky="w", padx=(2, 12))

        error_frame = ttk.LabelFrame(outer, text="Status and errors", padding=6)
        error_frame.pack(fill="both", expand=True)
        self.error_text = tk.Text(error_frame, height=6, wrap="word",
                                  state="disabled")
        self.error_text.pack(fill="both", expand=True)

    # ------------------------------------------------------------- form sync

    def _load_into_form(self) -> None:
        self.url_var.set(self.config.backend_url)
        self.symbol_var.set(self.config.symbol)
        self.sender_var.set(self.config.sender_id)
        self.token_var.set(load_token())

    def on_reveal_token(self) -> None:
        if not self.token_var.get():
            messagebox.showinfo("Token", "No token is stored yet.")
            return
        if messagebox.askyesno(
                "Reveal token",
                "Show the stored token in plain text? Make sure nobody is "
                "looking at your screen."):
            for widget in self.root.winfo_children():
                self._reveal(widget)

    def _reveal(self, widget) -> None:
        if isinstance(widget, ttk.Entry) and widget.cget("show") == "•":
            widget.configure(show="")
        for child in widget.winfo_children():
            self._reveal(child)

    # ------------------------------------------------------------- callbacks

    def on_save_settings(self) -> None:
        url = self.url_var.get().strip()
        if not url:
            messagebox.showerror("Invalid settings", "The receiver URL is required.")
            return
        previous = self.config.backend_url
        previous_id = self.config.destination_id()
        self.config.backend_url = url
        self.config.symbol = self.symbol_var.get().strip() or "XAUUSD"
        self.config.sender_id = self.sender_var.get().strip() or "collector"
        self.config.save(self.config_path)
        if self.token_var.get().strip():
            save_token(self.token_var.get().strip())

        if previous and self.config.destination_id() != previous_id:
            # Changing the destination is the one edit that can hide data: a
            # new receiver has confirmed nothing, so every candle must be
            # re-offered to it. Say so plainly rather than letting it surprise.
            messagebox.showwarning(
                "Destination changed",
                "The receiver URL changed.\n\n"
                "Delivery confirmations are tracked separately for each "
                "destination, so the new receiver will be sent the candles it "
                "is missing, including ones the previous receiver already "
                "had. Nothing is lost and no candle is sent twice to the same "
                "receiver.")
        else:
            messagebox.showinfo("Saved", f"Settings saved to\n{self.config_path}")
        self._log(f"settings saved (destination {self.config.destination_id()})")

    def on_test(self) -> None:
        self._save_quietly()
        problems = self.config.validate()
        if problems:
            messagebox.showerror("Configuration", "\n".join(problems))
            return
        self._log("testing MT5 and receiver connection...")

        def work():
            reader = Mt5Reader(symbol=self.config.symbol)
            try:
                reader.connect()
                self.events.put(("log", f"MT5 ready: {reader.resolved_symbol} "
                                        f"({reader.broker})"))
            except Exception as exc:  # noqa: BLE001
                self.events.put(("log", f"MT5 problem: "
                                        f"{getattr(exc, 'message', exc)}"))
            client = ReceiverClient(
                self.config.backend_url, load_token(),
                timeout_seconds=self.config.request_timeout_seconds)
            try:
                health = client.health()
                state = health.get("data", health).get("status", "ok") \
                    if isinstance(health, dict) else "ok"
                self.events.put(("log", f"receiver health: {state}"))
            except Exception as exc:  # noqa: BLE001
                self.events.put(("log", f"receiver problem: "
                                        f"{getattr(exc, 'message', exc)}"))
            finally:
                reader.disconnect()

        threading.Thread(target=work, daemon=True).start()

    def on_start(self) -> None:
        self._save_quietly()
        problems = self.config.validate()
        if not load_token():
            problems.append("No token is configured.")
        if problems:
            messagebox.showerror("Cannot start", "\n".join(problems))
            return
        if self.engine is not None:
            return
        self.store = CandleStore(default_store_path())
        self.ledger = DeliveryLedger(default_ledger_path())
        client = ReceiverClient(self.config.backend_url, load_token(),
                                timeout_seconds=self.config.request_timeout_seconds)
        self.engine = SyncEngine(self.config, self.store, self.ledger, client,
                                 reader=Mt5Reader(symbol=self.config.symbol))
        self.engine.start()
        self.start_button.configure(state="disabled")
        self.pause_button.configure(state="normal", text="Pause")
        self.stop_button.configure(state="normal")
        self._log("collector started")

    def on_pause(self) -> None:
        if self.engine is None:
            return
        if self.engine.paused:
            self.engine.resume()
            self.pause_button.configure(text="Pause")
            self._log("resumed")
        else:
            self.engine.pause()
            self.pause_button.configure(text="Resume")
            self._log("paused: no new candles will be sent until resumed")

    def on_stop(self) -> None:
        if self.engine is None:
            return
        self._log("stopping: flushing the current pass...")
        self.engine.stop()
        if self.ledger is not None:
            self.ledger.close()
        if self.store is not None:
            self.store.close()
        self.engine = None
        self.store = None
        self.ledger = None
        self.start_button.configure(state="normal")
        self.pause_button.configure(state="disabled", text="Pause")
        self.stop_button.configure(state="disabled")
        self._log("stopped")

    def on_open_folder(self) -> None:
        folder = os.path.dirname(default_store_path())
        try:
            if hasattr(os, "startfile"):
                os.startfile(folder)  # type: ignore[attr-defined]
            else:
                messagebox.showinfo("Data folder", folder)
        except Exception:  # noqa: BLE001
            messagebox.showinfo("Data folder", folder)

    def _save_quietly(self) -> None:
        self.config.backend_url = self.url_var.get().strip()
        self.config.symbol = self.symbol_var.get().strip() or "XAUUSD"
        self.config.sender_id = self.sender_var.get().strip() or "collector"
        try:
            self.config.save(self.config_path)
        except Exception as exc:  # noqa: BLE001
            self._log(f"could not save settings: {exc}")

    # ------------------------------------------------------------- rendering

    def _drain_events(self) -> None:
        if self._closing:
            return
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log":
                    self._log(payload)
        except queue.Empty:
            pass
        self._refresh()
        self.root.after(REFRESH_MS, self._drain_events)

    def _refresh(self) -> None:
        if self.engine is None:
            self.mt5_label.configure(text="MT5: stopped", foreground=_colour("STOPPED"))
            self.server_label.configure(text="Server: stopped",
                                        foreground=_colour("STOPPED"))
            self.symbol_label.configure(text=f"Symbol: {self.config.symbol}")
            return
        status = self.engine.status()
        mt5_state = status.get("mt5_state", "STARTING")
        receiver_state = status.get("receiver_state", "STARTING")
        if status.get("paused"):
            receiver_state = "PAUSED"
        self.mt5_label.configure(text=f"MT5: {mt5_state}",
                                 foreground=_colour(mt5_state))
        self.server_label.configure(text=f"Server: {receiver_state}",
                                    foreground=_colour(receiver_state))
        self.symbol_label.configure(
            text=f"Symbol: {status.get('resolved_symbol') or self.config.symbol}")
        self.last_sync_label.configure(
            text=f"Last sync: {_fmt_time(status.get('last_sync_at'))}")

        reads = status.get("last_read_per_timeframe", {})
        acked = status.get("last_acked_per_timeframe", {})
        stored = status.get("stored_count_per_timeframe", {})
        gaps = status.get("gap_count_per_timeframe", {})
        for timeframe in TIMEFRAMES:
            read = reads.get(timeframe) or {}
            ack = acked.get(timeframe) or {}
            self.tree.item(timeframe, values=(
                timeframe,
                _fmt_time(read.get("open_time_utc")),
                _fmt_time(ack.get("open_time_utc")),
                stored.get(timeframe, 0),
                gaps.get(timeframe, 0),
            ))
        for name, var in self.counter_vars.items():
            var.set(str(status.get(f"{name}_total", 0)))

        lines = []
        if status.get("last_error"):
            lines.append(f"[{status.get('last_error_code')}] "
                         f"{status['last_error']}")
        lines.extend(status.get("notes", [])[:5])
        lines.extend(status.get("local_gaps", [])[:5])
        if lines:
            self._log("\n".join(lines))

    def _log(self, message: str) -> None:
        if not message:
            return
        stamp = time.strftime("%H:%M:%S")
        self.error_text.configure(state="normal")
        self.error_text.insert("end", f"[{stamp}] {message}\n")
        self.error_text.see("end")
        # Keep the buffer small: this runs for days on a low-memory machine.
        line_count = int(self.error_text.index("end-1c").split(".")[0])
        if line_count > 500:
            self.error_text.delete("1.0", f"{line_count - 400}.0")
        self.error_text.configure(state="disabled")

    # ---------------------------------------------------------------- close

    def on_close(self) -> None:
        if self.engine is not None:
            if not messagebox.askokcancel(
                    "Quit", "Stop the collector and close?\n\n"
                            "Delivery state is saved, so it resumes exactly "
                            "where it left off."):
                return
            self.engine.stop()
        self._closing = True
        try:
            if self.ledger is not None:
                self.ledger.close()
            if self.store is not None:
                self.store.close()
        except Exception:  # noqa: BLE001
            pass
        self.root.destroy()


def main() -> int:
    root = tk.Tk()
    CollectorApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
