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
from .brief import build_receiver_brief
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


def add_clipboard_menu(widget: tk.Misc) -> None:
    """Give every entry under ``widget`` a right-click Cut/Copy/Paste menu.

    tkinter ships without one on Windows, and the token has to be pasted from a
    message the owner received, so a user who does not know Ctrl+V has no way to
    enter it. Bound recursively so it covers the whole window.
    """
    menu = tk.Menu(widget, tearoff=0)
    menu.add_command(label="Cut",
                     command=lambda: _clip_apply(widget, "cut"))
    menu.add_command(label="Copy",
                     command=lambda: _clip_apply(widget, "copy"))
    menu.add_command(label="Paste",
                     command=lambda: _clip_apply(widget, "paste"))
    menu.add_separator()
    menu.add_command(label="Select all",
                     command=lambda: _clip_apply(widget, "select"))

    def attach(node: tk.Misc) -> None:
        if isinstance(node, (ttk.Entry, tk.Entry)):
            node.bind("<Button-3>",
                      lambda event, m=menu: m.tk_popup(event.x_root,
                                                       event.y_root))
        for child in node.winfo_children():
            attach(child)

    attach(widget)


def _clip_apply(widget: tk.Misc, action: str) -> None:
    """Apply one clipboard action to the focused entry, if any."""
    target = widget.focus_get()
    if not isinstance(target, (ttk.Entry, tk.Entry)):
        return
    try:
        if action == "cut":
            target.event_generate("<<Cut>>")
        elif action == "copy":
            target.event_generate("<<Copy>>")
        elif action == "paste":
            target.event_generate("<<Paste>>")
        else:
            target.select_range(0, "end")
            target.icursor("end")
    except tk.TclError:
        pass


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
        #: Standalone MT5 probe. Held open between clicks so the connection can
        #: be tested without starting a delivery run.
        self.probe: Optional[Mt5Reader] = None
        self.probe_state = "DISCONNECTED"
        self.probe_detail = ""
        self._probing = False

        root.title(f"ASTRA Collector {__version__}")
        # Tall enough for the full layout at rest; the brief panel grows the
        # window rather than being clipped out of reach.
        root.geometry("980x820")
        root.minsize(880, 700)
        self._build()
        self._load_into_form()
        self._drain_events()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        # First run: no destination and no token means nothing can be sent yet,
        # so open the setup dialog rather than leaving a window that looks
        # ready but silently cannot deliver.
        if not self._is_configured():
            self.root.after(200, self.on_setup_wizard)

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
        self.connect_button = ttk.Button(controls, text="Connect to MT5",
                                         command=self.on_connect_mt5)
        self.connect_button.pack(side="left")
        self.start_button = ttk.Button(controls, text="Start", command=self.on_start)
        self.start_button.pack(side="left", padx=6)
        self.pause_button = ttk.Button(controls, text="Pause", command=self.on_pause,
                                       state="disabled")
        self.pause_button.pack(side="left")
        self.stop_button = ttk.Button(controls, text="Stop", command=self.on_stop,
                                      state="disabled")
        self.stop_button.pack(side="left", padx=6)
        ttk.Button(controls, text="Open data folder",
                   command=self.on_open_folder).pack(side="left", padx=6)

        # Connection state for the standalone MT5 probe, separate from the
        # running engine's own MT5 state.
        self.probe_label = ttk.Label(controls, text="Terminal: not checked")
        self.probe_label.pack(side="right")

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

        # A ready-to-send description of the receiving side. The owner can hand
        # it to a developer or another agent, who can build a compatible
        # receiver from it without knowing the protocol. Collapsed by default so
        # it does not crowd the window during normal use.
        brief_frame = ttk.LabelFrame(outer, text="Receiver setup text", padding=6)
        brief_frame.pack(fill="x", pady=(8, 0))
        brief_bar = ttk.Frame(brief_frame)
        brief_bar.pack(fill="x")
        self.brief_button = ttk.Button(brief_bar, text="Show setup text",
                                       command=self.on_toggle_brief)
        self.brief_button.pack(side="left")
        ttk.Button(brief_bar, text="Copy setup text",
                   command=self.on_copy_brief).pack(side="left", padx=6)
        ttk.Button(brief_bar, text="Save as file...",
                   command=self.on_save_brief).pack(side="left")
        self.brief_hint = ttk.Label(
            brief_bar,
            text="Send this to whoever builds your receiving service.")
        self.brief_hint.pack(side="right")

        self.brief_holder = ttk.Frame(brief_frame)
        self.brief_text = tk.Text(self.brief_holder, height=14, wrap="word",
                                  state="disabled")
        brief_scroll = ttk.Scrollbar(self.brief_holder, orient="vertical",
                                     command=self.brief_text.yview)
        self.brief_text.configure(yscrollcommand=brief_scroll.set)
        self.brief_text.pack(side="left", fill="both", expand=True)
        brief_scroll.pack(side="right", fill="y")
        self._brief_shown = False

        # Re-runnable setup, for changing the destination or the token later.
        setup_row = ttk.Frame(outer)
        setup_row.pack(fill="x", pady=(8, 0))
        ttk.Button(setup_row, text="Run setup again",
                   command=self.on_setup_wizard).pack(side="left")
        ttk.Label(setup_row, text="Use this to change the receiver URL, "
                                  "symbol, or token.").pack(side="left", padx=8)
        add_clipboard_menu(self.root)

    # ---------------------------------------------------------------- setup

    def _is_configured(self) -> bool:
        """True when a destination and a token both exist.

        Either one missing means nothing can be delivered, so the app is not
        configured. Both are checked because a saved URL with no token still
        fails on the first send.
        """
        return bool(self.config.backend_url.strip()) and bool(load_token())

    def on_setup_wizard(self) -> None:
        """First-run dialog. Verifies before it accepts, so a wrong token is
        caught here rather than discovered later as silence."""
        SetupDialog(self, self.root)

    def on_connect_mt5(self) -> None:
        """Connect to the running terminal on demand.

        The probe is kept open on success so a second click re-checks the
        connection instead of reconnecting blindly. Failure is reported with
        the reader's own message, never a generic one.
        """
        if self._probing:
            return
        if self.probe is not None:
            # Already connected: re-read to confirm the terminal is still there.
            self._check_probe()
            return
        self._probing = True
        self.connect_button.configure(state="disabled", text="Connecting...")
        self.probe_state = "CONNECTING"
        self.probe_detail = ""
        self._render_probe()
        symbol = self.symbol_var.get().strip() or self.config.symbol

        def work():
            reader = Mt5Reader(symbol=symbol)
            try:
                reader.connect()
                # Read one bar as well, so "connected" means the terminal
                # actually served data, not merely that a handle opened.
                candle = reader.read_latest_closed(TIMEFRAMES[0])
                self.events.put(("probe_ok", (reader, candle)))
            except Exception as exc:  # noqa: BLE001
                self.events.put(("probe_fail",
                                 str(getattr(exc, "message", exc))))

        threading.Thread(target=work, daemon=True).start()

    def _check_probe(self) -> None:
        """Re-read one bar from the open probe to confirm it is still live.

        A raised error means the terminal is gone. ``None`` without an error
        means the terminal answered but had no closed bar to give, which is a
        different state and is reported as such rather than as a failure.
        """
        reader = self.probe
        if reader is None:
            return
        self._probing = True
        self.connect_button.configure(state="disabled", text="Checking...")

        def work():
            try:
                candle = reader.read_latest_closed(TIMEFRAMES[0])
                self.events.put(("probe_ok", (reader, candle)))
            except Exception as exc:  # noqa: BLE001
                self.events.put(("probe_fail",
                                 str(getattr(exc, "message", exc))))

        threading.Thread(target=work, daemon=True).start()

    def _on_probe_ok(self, payload) -> None:
        reader, candle = payload
        self.probe = reader
        self._probing = False
        self.connect_button.configure(state="normal", text="Check MT5")
        if candle is None:
            # The terminal answered but returned no closed bar. Reported as its
            # own state: claiming "connected" here would overstate what is known.
            self.probe_state = "NO_DATA"
            self.probe_detail = "terminal reachable, no closed bar returned"
            self._render_probe()
            self._log("MT5 reachable but returned no closed M1 bar. Check that "
                      "the symbol is selected in Market Watch and that history "
                      "has been downloaded.")
            return
        self.probe_state = "CONNECTED"
        self.probe_detail = (f"{reader.resolved_symbol} @ "
                             f"{reader.broker or 'unknown broker'}")
        self._render_probe()
        self._log(
            f"MT5 connected: {self.probe_detail}; broker UTC offset "
            f"{reader.clock_offset_seconds()}s; last closed M1 "
            f"{_fmt_time(candle.open_time_utc)} "
            f"O {candle.open} H {candle.high} L {candle.low} C {candle.close}")

    def _on_probe_fail(self, message: str) -> None:
        self._probing = False
        # A failed re-check invalidates the old handle: keeping it would let the
        # window claim a connection the terminal no longer honours.
        self.probe = None
        self.probe_state = "DISCONNECTED"
        self.probe_detail = message
        self.connect_button.configure(state="normal", text="Connect to MT5")
        self._render_probe()
        self._log(f"MT5 not connected: {message}")

    def _render_probe(self) -> None:
        state = self.probe_state
        text = {
            "CONNECTED": f"Terminal: connected ({self.probe_detail})",
            "CONNECTING": "Terminal: connecting...",
            "NO_DATA": f"Terminal: reachable, no closed bar",
            "DISCONNECTED": "Terminal: not connected",
        }.get(state, "Terminal: not checked")
        colour = {
            "CONNECTED": _colour("CONNECTED"),
            "CONNECTING": _colour("STARTING"),
            "NO_DATA": _colour("STARTING"),
            "DISCONNECTED": _colour("STOPPED"),
        }.get(state, _colour("STOPPED"))
        self.probe_label.configure(text=text, foreground=colour)

    # ---------------------------------------------------------------- brief

    def _current_brief(self) -> str:
        # Use the offset the terminal actually reported when a probe has run,
        # so the brief tells a receiver author the real number for this broker
        # rather than a placeholder they would have to guess.
        offset = 0
        if self.probe is not None:
            try:
                offset = self.probe.clock_offset_seconds()
            except Exception:  # noqa: BLE001
                offset = 0
        return build_receiver_brief(
            self.url_var.get().strip() or self.config.backend_url,
            self.sender_var.get().strip() or self.config.sender_id,
            self.symbol_var.get().strip() or self.config.symbol,
            clock_offset_seconds=offset,
        )

    def on_toggle_brief(self) -> None:
        if self._brief_shown:
            self.brief_holder.pack_forget()
            self.brief_button.configure(text="Show setup text")
            self._brief_shown = False
            self._resize_for_brief(False)
            return
        text = self._current_brief()
        self.brief_text.configure(state="normal")
        self.brief_text.delete("1.0", "end")
        self.brief_text.insert("1.0", text)
        self.brief_text.configure(state="disabled")
        self.brief_holder.pack(fill="both", expand=True, pady=(6, 0))
        self.brief_button.configure(text="Hide setup text")
        self._brief_shown = True
        self._resize_for_brief(True)

    def _resize_for_brief(self, growing: bool) -> None:
        """Grow the window so the panel is not clipped out of reach.

        Packed tkinter content has no scrollbar, so a panel taller than the
        window is simply unreachable. The window is enlarged by the panel's own
        requested height, capped to the screen so it can never be pushed off the
        bottom on a small display.
        """
        self.root.update_idletasks()
        needed = self.root.winfo_reqheight()
        screen_h = self.root.winfo_screenheight()
        target = min(needed + 8, screen_h - 80)
        if growing:
            self.root.geometry(f"{self.root.winfo_width()}x{target}")
        else:
            self.root.geometry(f"{self.root.winfo_width()}x{max(target, 700)}")

    def on_copy_brief(self) -> None:
        text = self._current_brief()
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self.root.update_idletasks()
        self._log(f"receiver setup text copied ({len(text)} characters)")
        messagebox.showinfo(
            "Copied",
            "The receiver setup text is on the clipboard.\n\n"
            "Paste it into a message to whoever builds your receiving "
            "service. It contains no token and no secret.")

    def on_save_brief(self) -> None:
        from tkinter import filedialog
        path = filedialog.asksaveasfilename(
            title="Save receiver setup text",
            defaultextension=".md",
            initialfile="astra-receiver-setup.md",
            filetypes=[("Markdown", "*.md"), ("Text", "*.txt")])
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(self._current_brief())
        except OSError as exc:
            messagebox.showerror("Could not save", str(exc))
            return
        self._log(f"receiver setup text saved to {path}")

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
            else:
                # /health is unauthenticated, so follow it with a route that
                # does use the token. Otherwise "Test connection" would report
                # success for a token the receiver will reject on first send.
                try:
                    client.verify_token()
                    self.events.put(("log", "receiver token: accepted"))
                except Exception as exc:  # noqa: BLE001
                    self.events.put(("log", f"receiver token: "
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
                elif kind == "probe_ok":
                    self._on_probe_ok(payload)
                elif kind == "probe_fail":
                    self._on_probe_fail(payload)
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
            if self.probe is not None:
                self.probe.disconnect()
            if self.ledger is not None:
                self.ledger.close()
            if self.store is not None:
                self.store.close()
        except Exception:  # noqa: BLE001
            pass
        self.root.destroy()


class SetupDialog:
    """First-run configuration, and re-runnable from the form.

    The point of the dialog is that it *verifies before it accepts*. A saved
    URL and token that have never been tried produce a window that looks ready
    and delivers nothing, which is the exact failure this project already hit
    once. So the primary button tests the receiver and the token first, and
    only then writes the files.
    """

    def __init__(self, app: CollectorApp, parent: tk.Misc):
        self.app = app
        self.done = False
        self._closed = False
        # Worker threads must not touch Tk widgets directly. Results are posted
        # here and applied on the main thread by ``_drain``, the same pattern the
        # main window uses. Calling ``after`` from a worker thread is not
        # thread-safe and raises "main thread is not in main loop".
        self.events: "queue.Queue[tuple]" = queue.Queue()
        self.window = tk.Toplevel(parent)
        self.window.title("ASTRA Collector setup")
        self.window.geometry("620x460")
        self.window.minsize(560, 420)
        self.window.transient(parent)
        self.window.grab_set()
        self.window.protocol("WM_DELETE_WINDOW", self._close)

        outer = ttk.Frame(self.window, padding=14)
        outer.pack(fill="both", expand=True)

        ttk.Label(outer, text="Connect this PC to your ASTRA receiver",
                  font=("Segoe UI", 13, "bold")).pack(anchor="w")
        ttk.Label(
            outer,
            text=("These four values come from whoever runs the receiving "
                  "service. Nothing is saved until the connection test "
                  "succeeds."),
            wraplength=580, justify="left").pack(anchor="w", pady=(4, 12))

        form = ttk.Frame(outer)
        form.pack(fill="x")
        form.columnconfigure(1, weight=1)

        self.url_var = tk.StringVar(value=app.config.backend_url)
        self.symbol_var = tk.StringVar(value=app.config.symbol or "XAUUSD")
        self.sender_var = tk.StringVar(value=app.config.sender_id
                                       or self._suggest_sender_id())
        self.token_var = tk.StringVar(value=load_token())

        self._row(form, 0, "Receiver URL", self.url_var,
                  hint="Ends with /api/v1")
        self._row(form, 1, "Symbol", self.symbol_var,
                  hint="XAUUSD, or GOLD if that is your broker's name")
        self._row(form, 2, "Sender id", self.sender_var,
                  hint="A stable name for this PC, e.g. home-pc")
        self._row(form, 3, "Token", self.token_var, secret=True,
                  hint="Supplied by the receiver operator")

        self.status = tk.Text(outer, height=7, wrap="word", state="disabled")
        self.status.pack(fill="both", expand=True, pady=(12, 10))

        buttons = ttk.Frame(outer)
        buttons.pack(fill="x")
        self.test_button = ttk.Button(buttons, text="Test and save",
                                      command=self.on_test_and_save)
        self.test_button.pack(side="right")
        ttk.Button(buttons, text="Cancel",
                   command=self.on_cancel).pack(side="right", padx=6)

        self._note("Enter the values and press Test and save.")
        self.url_var.trace_add("write", lambda *_: self._on_url_changed())
        add_clipboard_menu(self.window)
        self._drain()

    # ------------------------------------------------------------------ queue

    def _drain(self) -> None:
        """Apply worker results on the main thread."""
        if self._closed:
            return
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "fail":
                    self._fail(payload)
                elif kind == "fail_token":
                    code, message = payload
                    self._fail_token(code, message)
                elif kind == "save":
                    self._save(*payload)
        except queue.Empty:
            pass
        try:
            self.window.after(120, self._drain)
        except tk.TclError:
            pass

    # ------------------------------------------------------------------ form

    @staticmethod
    def _suggest_sender_id() -> str:
        """A default that is stable per PC and unlikely to collide."""
        try:
            import socket
            name = socket.gethostname().strip().lower()
        except Exception:  # noqa: BLE001
            name = ""
        cleaned = "".join(c if c.isalnum() or c == "-" else "-" for c in name)
        cleaned = cleaned.strip("-")
        return cleaned or "collector"

    def _row(self, form, row: int, label: str, var: tk.StringVar,
             secret: bool = False, hint: str = "") -> None:
        ttk.Label(form, text=label).grid(row=row * 2, column=0, sticky="w",
                                         pady=(8 if row else 0, 0))
        entry = ttk.Entry(form, textvariable=var,
                          show="•" if secret else "")
        entry.grid(row=row * 2, column=1, sticky="ew", padx=6,
                   pady=(8 if row else 0, 0))
        if hint:
            ttk.Label(form, text=hint, foreground="#5a6b7b").grid(
                row=row * 2 + 1, column=1, sticky="w", padx=6)

    def _on_url_changed(self) -> None:
        """Warn early about the most common paste mistake.

        A URL without the /api/v1 suffix is accepted by the entry field but
        every request lands on the wrong path and fails with an unhelpful
        error, so the missing suffix is called out as it is typed.
        """
        url = self.url_var.get().strip()
        if url and not url.rstrip("/").endswith("/api/v1"):
            self._note("Note: the URL usually ends with /api/v1. Add it if the "
                       "receiver operator gave you a URL that already includes "
                       "the version.")

    # ----------------------------------------------------------------- state

    def _note(self, message: str) -> None:
        self.status.configure(state="normal")
        self.status.delete("1.0", "end")
        self.status.insert("1.0", message)
        self.status.configure(state="disabled")

    def _append(self, message: str) -> None:
        self.status.configure(state="normal")
        self.status.insert("end", message + "\n")
        self.status.see("end")
        self.status.configure(state="disabled")

    # --------------------------------------------------------------- actions

    def on_test_and_save(self) -> None:
        url = self.url_var.get().strip()
        symbol = self.symbol_var.get().strip()
        sender = self.sender_var.get().strip()
        token = self.token_var.get().strip()

        # Stage the values so config.validate() checks the real thing, not the
        # previously saved file.
        probe_config = CollectorConfig.from_dict(self.app.config.to_dict())
        probe_config.backend_url = url
        probe_config.symbol = symbol or "XAUUSD"
        probe_config.sender_id = sender
        problems = probe_config.validate()
        if not token:
            problems.append("Token is empty.")
        if problems:
            self._note("Fix these first:\n- " + "\n- ".join(problems))
            return

        self.test_button.configure(state="disabled", text="Testing...")
        self._note("Testing the receiver...\n")

        def work():
            client = ReceiverClient(
                url, token,
                timeout_seconds=probe_config.request_timeout_seconds)
            # 1. Liveness. Proves the URL and the path are right.
            try:
                health = client.health()
            except Exception as exc:  # noqa: BLE001
                self.events.put(("fail",
                                 f"Could not reach the receiver:\n"
                                 f"{getattr(exc, 'message', exc)}"))
                return
            state = "ok"
            if isinstance(health, dict):
                data = health.get("data", health)
                if isinstance(data, dict):
                    state = str(data.get("status", "ok"))

            # 2. Credential. /health does not use the token, so this is the step
            # that actually proves the token works. Without it the dialog would
            # save a token that fails on the first real send.
            try:
                client.verify_token()
            except Exception as exc:  # noqa: BLE001
                self.events.put(("fail_token",
                                 (getattr(exc, "code", ""),
                                  str(getattr(exc, "message", exc)))))
                return

            self.events.put(("save", (url, symbol, sender, token, state)))

        threading.Thread(target=work, daemon=True).start()

    def _fail(self, message: str) -> None:
        self.test_button.configure(state="normal", text="Test and save")
        self._note(message + "\n\nNothing was saved. Correct the value and try "
                             "again.")

    def _fail_token(self, code: str, message: str) -> None:
        self.test_button.configure(state="normal", text="Test and save")
        if code == "AUTH_REJECTED":
            self._note(
                "The receiver is reachable, but it rejected the token.\n\n"
                "Check that the token matches the one the receiver operator "
                "gave you, with no leading or trailing spaces and no line "
                "break. Tokens are case-sensitive.\n\n"
                "Nothing was saved.")
        else:
            self._note(
                f"The receiver answered the liveness check but the token could "
                f"not be verified:\n{message}\n\nNothing was saved.")

    def _save(self, url: str, symbol: str, sender: str, token: str,
              health_state: str) -> None:
        app = self.app
        app.config.backend_url = url
        app.config.symbol = symbol or "XAUUSD"
        app.config.sender_id = sender
        try:
            app.config.save(app.config_path)
            save_token(token)
        except OSError as exc:
            self._fail(f"Could not write the settings files:\n{exc}")
            return

        # Both checks passed: the URL is right and the token is accepted.
        self._append(f"Receiver reachable (status: {health_state}).")
        self._append("Token accepted by the delivery endpoint.")
        self._append("Settings saved. You can close this window.")
        self.test_button.configure(state="normal", text="Test and save")
        self.done = True

        app._load_into_form()
        app._log(f"setup saved (destination {app.config.destination_id()})")
        # Leave the window up briefly so the outcome can be read, then close it
        # from the main thread (this method already runs there).
        self.window.after(1200, self._close)

    def _close(self) -> None:
        self._closed = True
        try:
            self.window.grab_release()
            self.window.destroy()
        except tk.TclError:
            pass

    def on_cancel(self) -> None:
        self._close()


def main() -> int:
    root = tk.Tk()
    CollectorApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
