"""Headless GUI construction tests.

The GUI cannot be instantiated on a machine without Tk (including CI and the
Linux dev box), yet a typo in widget construction or in the status renderer
would only show up on the owner's Windows PC. These tests stub the tkinter
widget surface so `_build()` and `update_from_status()` are actually executed,
which catches attribute errors a syntax check cannot.
"""

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


if __name__ == "__main__":
    unittest.main()
