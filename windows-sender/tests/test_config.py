"""Configuration tests: validation, load/save round-trip, defaults."""

from __future__ import annotations

import os
import tempfile
import unittest

from astra_sender.config import (SenderConfig, TIMEFRAMES, default_config_path)

import astra_sender.config as config_module


class ConfigTest(unittest.TestCase):
    def test_nine_timeframes(self):
        self.assertEqual(TIMEFRAMES,
                         ["M1", "M5", "M15", "M30", "H1", "H4", "D1", "W1",
                          "MN1"])

    def test_validation_flags_missing_fields(self):
        cfg = SenderConfig()
        problems = cfg.validate()
        self.assertTrue(any("Backend URL" in p for p in problems))
        self.assertTrue(any("Sender token" in p for p in problems))
        self.assertTrue(any("Sender ID" in p for p in problems))

    def test_round_trip_preserves_secrets_and_all_fields(self):
        cfg = SenderConfig()
        cfg.backend_url = "https://example.invalid/api"
        cfg.sender_token = "tok-secret"
        cfg.sender_id = "win-01"
        cfg.symbol = "GOLD"
        cfg.timeframes = ["M5", "H1"]
        cfg.poll_interval_seconds = 15
        cfg.catchup_max_bars = 50
        cfg.request_timeout_seconds = 20

        directory = tempfile.mkdtemp(prefix="astra_cfg_")
        path = os.path.join(directory, "config.json")
        SenderConfig.save(cfg, path)
        loaded = SenderConfig.load(path)
        self.assertEqual(loaded.backend_url, cfg.backend_url)
        self.assertEqual(loaded.sender_token, cfg.sender_token)
        self.assertEqual(loaded.sender_id, cfg.sender_id)
        self.assertEqual(loaded.symbol, cfg.symbol)
        self.assertEqual(loaded.timeframes, cfg.timeframes)
        self.assertEqual(loaded.poll_interval_seconds, 15)
        self.assertEqual(loaded.catchup_max_bars, 50)
        self.assertEqual(loaded.request_timeout_seconds, 20)

    def test_unreadable_config_is_honest(self):
        directory = tempfile.mkdtemp(prefix="astra_cfg_")
        path = os.path.join(directory, "config.json")
        with open(path, "w") as handle:
            handle.write("not json at all")
        cfg = SenderConfig.load(path)
        # No crash; fields empty so the startup check reports them clearly.
        self.assertEqual(cfg.backend_url, "")
        self.assertEqual(cfg.sender_token, "")


class Mt5PathConfigTest(unittest.TestCase):
    """`mt5_path` must survive a save/load round-trip.

    A previously reported bug was a key the user hand-added to config.json that
    the loader silently discarded. Anything the GUI can now write has to come
    back, or the same confusion returns.
    """

    def test_defaults_to_empty_auto_discovery(self):
        cfg = SenderConfig()
        self.assertEqual(cfg.mt5_path, "")
        self.assertGreater(cfg.mt5_timeout_ms, 0)

    def test_round_trips_through_save_and_load(self):
        directory = tempfile.mkdtemp(prefix="astra_cfg_")
        path = os.path.join(directory, "config.json")
        cfg = SenderConfig()
        cfg.backend_url = "https://astra.example"
        cfg.sender_token = "tok"
        cfg.sender_id = "win-01"
        cfg.mt5_path = r"C:\Program Files\MetaTrader 5 IC Markets (SC)\terminal64.exe"
        cfg.mt5_timeout_ms = 8000
        SenderConfig.save(cfg, path)
        loaded = SenderConfig.load(path)
        self.assertEqual(loaded.mt5_path, cfg.mt5_path)
        self.assertEqual(loaded.mt5_timeout_ms, 8000)

    def test_blank_path_is_valid(self):
        cfg = SenderConfig()
        cfg.backend_url = "https://astra.example"
        cfg.sender_token = "tok"
        cfg.sender_id = "win-01"
        self.assertEqual(cfg.validate(), [])

    def test_non_exe_path_is_rejected(self):
        cfg = SenderConfig()
        cfg.backend_url = "https://astra.example"
        cfg.sender_token = "tok"
        cfg.sender_id = "win-01"
        cfg.mt5_path = r"C:\Program Files\MetaTrader 5"
        problems = cfg.validate()
        self.assertTrue(any("terminal64.exe" in p for p in problems))

    def test_bad_timeout_falls_back_to_default(self):
        cfg = SenderConfig.from_dict({"mt5_timeout_ms": "not-a-number"})
        self.assertEqual(cfg.mt5_timeout_ms, 15000)


if __name__ == "__main__":
    unittest.main()
