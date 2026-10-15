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


if __name__ == "__main__":
    unittest.main()