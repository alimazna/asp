"""Headless GUI construction tests.

The GUI cannot be instantiated on a machine without Tk (including CI and the
Linux dev box), yet a typo in widget construction or in the status renderer
would only show up on the owner's Windows PC. These tests stub the tkinter
widget surface so `_build()` and `update_from_status()` are actually executed,
which catches attribute errors a syntax check cannot.
"""

import os
import sys
import types
import unittest


class _Widget:
    def __init__(self, *args, **kwargs):
        self._text = kwargs.get("text", "")
        self._config = dict(kwargs)

    def pack(self, *a, **k):
        pass

    def grid(self, *a, **k):
        pass

    def title(self, *a, **k):
        pass

    def minsize(self, *a, **k):
        pass

    def config(self, *a, **k):
        if "text" in k:
            self._text = k["text"]
        self._config.update(k)

    configure = config

    def cget(self, key):
        return self._text if key == "text" else self._config.get(key, "")

    def insert(self, *a, **k):
        if len(a) >= 2:
            self._text = a[1]

    def delete(self, *a, **k):
        self._text = ""

    def see(self, *a, **k):
        pass

    def destroy(self, *a, **k):
        pass

    def after(self, *a, **k):
        pass

    def mainloop(self, *a, **k):
        pass

    def winfo_exists(self, *a, **k):
        return True


class _Var:
    def __init__(self, *a, **k):
        self._value = ""

    def get(self):
        return self._value

    def set(self, value):
        self._value = value


def _install_fake_tkinter():
    tk = types.ModuleType("tkinter")
    tk.Tk = _Widget
    for name in ("Label", "Text", "Frame", "Button", "Entry", "Toplevel"):
        setattr(tk, name, _Widget)
    for const in ("NORMAL", "DISABLED", "END", "LEFT", "RIGHT", "BOTH", "X", "Y",
                  "WORD", "FLAT", "SUNKEN", "TOP", "BOTTOM", "HORIZONTAL",
                  "VERTICAL", "INSERT"):
        setattr(tk, const, const.lower())
    tk.StringVar = _Var
    tk.BooleanVar = _Var

    ttk = types.ModuleType("tkinter.ttk")
    for name in ("Frame", "Label", "LabelFrame", "Button", "Entry", "Notebook",
                 "Separator"):
        setattr(ttk, name, _Widget)
    tk.ttk = ttk

    st = types.ModuleType("tkinter.scrolledtext")
    st.ScrolledText = _Widget

    sys.modules["tkinter"] = tk
    sys.modules["tkinter.ttk"] = ttk
    sys.modules["tkinter.scrolledtext"] = st
    sys.modules["tkinter.messagebox"] = types.ModuleType("tkinter.messagebox")

    # simpledialog is imported inside _open_settings; the tests drive it by
    # replacing askstring, so it only needs to exist as a module.
    sd = types.ModuleType("tkinter.simpledialog")

    def _no_answer(*a, **k):
        return None

    sd.askstring = _no_answer
    sys.modules["tkinter.simpledialog"] = sd


def _no_answer(*a, **k):
    """Default simpledialog.askstring: the user cancelled."""
    return None


def _answers(*values):
    """askstring stand-in that returns the given values in order."""
    queue = list(values)

    def _next(*a, **k):
        return queue.pop(0) if queue else None

    return _next


class _Config:
    symbol = "XAUUSD"
    timeframes = ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1", "MN1"]


class _Engine:
    def status_snapshot(self):
        return {"running": True}

    def pause(self):
        pass

    def resume(self):
        pass


class GuiConstructionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _install_fake_tkinter()

    def _gui(self):
        for mod in [m for m in sys.modules if m.startswith("astra_sender.gui")]:
            del sys.modules[mod]
        from astra_sender.gui import SenderGui
        return SenderGui(_Engine(), _Config(), "/tmp/astra-test")

    def test_gui_builds_without_a_display(self):
        gui = self._gui()
        self.assertEqual(gui.symbol_label.cget("text"), "XAUUSD")

    def test_mt5_unavailable_shows_reason_and_red(self):
        gui = self._gui()
        gui.update_from_status({
            "mt5_state": "UNAVAILABLE", "server_state": "ONLINE",
            "last_error": "MetaTrader5 is not reachable. Open the MT5 terminal.",
            "sent": 1, "acknowledged": 0, "deduplicated": 0,
            "queued": 0, "failed": 0,
        })
        self.assertIn("MetaTrader5", gui.reason_label.cget("text"))
        self.assertEqual(gui.mt5_label.cget("text"), "UNAVAILABLE")
        self.assertEqual(gui.server_label.cget("text"), "ONLINE")

    def test_server_online_with_mt5_down_is_distinguishable(self):
        """The exact bug the owner hit: server healthy, terminal not."""
        gui = self._gui()
        gui.update_from_status({
            "mt5_state": "UNAVAILABLE", "server_state": "ONLINE",
            "last_error": "", "sent": 0, "acknowledged": 0,
            "deduplicated": 0, "queued": 0, "failed": 0,
        })
        # With no explicit error the GUI still explains what to do.
        self.assertIn("MT5", gui.reason_label.cget("text"))

    def test_state_colours(self):
        gui = self._gui()
        self.assertEqual(gui._state_colour("READY"), "#006400")
        self.assertEqual(gui._state_colour("ONLINE"), "#006400")
        self.assertEqual(gui._state_colour("UNAVAILABLE"), "#b00020")
        self.assertEqual(gui._state_colour("AUTH_FAILED"), "#b00020")
        self.assertEqual(gui._state_colour("STARTING"), "#8a6d00")


class SettingsWritesTheConfigInUseTest(unittest.TestCase):
    """Settings must write the file the app actually reads.

    Regression: _open_settings rebuilt the path from app_data_dir, so on a
    per-machine install (config under %PROGRAMDATA%, data_dir under
    %LOCALAPPDATA%) the token was saved to a file nothing ever read, and the
    app kept reporting "not configured" no matter what the owner typed.
    """

    @classmethod
    def setUpClass(cls):
        _install_fake_tkinter()

    def _gui(self, config_path, config):
        for mod in [m for m in sys.modules if m.startswith("astra_sender.gui")]:
            del sys.modules[mod]
        from astra_sender.gui import SenderGui
        return SenderGui(_Engine(), config, "/tmp/astra-unused-dir",
                         config_path=config_path)

    def test_token_is_saved_to_the_resolved_config_path(self):
        import tempfile
        from astra_sender.config import SenderConfig
        from tkinter import simpledialog

        with tempfile.TemporaryDirectory() as tmp:
            # A machine-wide config path, unrelated to data_dir.
            cfg_path = os.path.join(tmp, "ProgramData", "AstraSender",
                                    "config.json")
            config = SenderConfig.from_dict({}, tmp)
            config.backend_url = ""
            config.sender_token = ""
            config.sender_id = ""
            gui = self._gui(cfg_path, config)

            simpledialog.askstring = _answers(
                "https://astra.example", "win10-desk-01", "tok-123", "")
            try:
                saved = gui._open_settings()
            finally:
                simpledialog.askstring = _no_answer

            self.assertTrue(saved, "settings should report success")
            self.assertTrue(os.path.exists(cfg_path),
                            "token was not written to the config in use")
            reloaded = SenderConfig.load(cfg_path)
            self.assertEqual(reloaded.sender_token, "tok-123")
            self.assertEqual(reloaded.sender_id, "win10-desk-01")
            # And nothing leaked into the unused data dir.
            self.assertFalse(
                os.path.exists("/tmp/astra-unused-dir/config.json"))

    def test_cancelled_dialog_saves_nothing(self):
        import tempfile
        from astra_sender.config import SenderConfig

        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = os.path.join(tmp, "config.json")
            config = SenderConfig.from_dict({}, tmp)
            gui = self._gui(cfg_path, config)
            self.assertFalse(gui._open_settings())
            self.assertFalse(os.path.exists(cfg_path))

    def test_blank_token_reports_incomplete_not_success(self):
        import tempfile
        from astra_sender.config import SenderConfig
        from tkinter import simpledialog

        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = os.path.join(tmp, "config.json")
            config = SenderConfig.from_dict({}, tmp)
            config.sender_token = ""
            gui = self._gui(cfg_path, config)

            simpledialog.askstring = _answers(
                "https://astra.example", "win10-desk-01", "   ", "")
            try:
                saved = gui._open_settings()
            finally:
                simpledialog.askstring = _no_answer

            self.assertFalse(saved, "an empty token is not a usable config")
            self.assertIn("incomplete", gui.reason_label.cget("text").lower())


class FirstRunOpensSettingsTest(unittest.TestCase):
    """A double-click on an unconfigured app must offer a way to configure it.

    Regression: __main__ showed a message box and exited, so the windowed exe
    closed immediately and the owner had no way to enter the token.
    """

    @classmethod
    def setUpClass(cls):
        _install_fake_tkinter()

    def test_needs_config_schedules_the_setup_prompt(self):
        for mod in [m for m in sys.modules if m.startswith("astra_sender.gui")]:
            del sys.modules[mod]
        from astra_sender.gui import SenderGui
        gui = SenderGui(_Engine(), _Config(), "/tmp/astra-test",
                        config_path="/tmp/astra-test/config.json",
                        needs_config=True)
        self.assertTrue(gui._needs_config)

    def test_prompt_setup_explains_and_opens_settings(self):
        for mod in [m for m in sys.modules if m.startswith("astra_sender.gui")]:
            del sys.modules[mod]
        from astra_sender.gui import SenderGui
        gui = SenderGui(_Engine(), _Config(), "/tmp/astra-test",
                        config_path="/tmp/astra-test/config.json",
                        needs_config=True)
        called = []
        gui._open_settings = lambda: called.append(True)
        gui._prompt_setup()
        self.assertTrue(called, "the setup prompt must open Settings")
        self.assertIn("not configured", gui.reason_label.cget("text"))


if __name__ == "__main__":
    unittest.main()
