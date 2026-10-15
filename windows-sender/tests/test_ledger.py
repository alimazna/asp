"""Ledger tests: durability, idempotency, uncertain-delivery replay, restart."""

from __future__ import annotations

import os
import tempfile
import unittest

from astra_sender.ledger import DeliveryLedger


def make_ledger():
    directory = tempfile.mkdtemp(prefix="astra_ledger_")
    return DeliveryLedger(os.path.join(directory, "ledger.sqlite")), directory


class LedgerTest(unittest.TestCase):
    def test_identity_is_stable_and_price_independent(self):
        identity_a = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", 1792045800)
        identity_b = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", 1792045800)
        identity_c = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", 1792045801)
        self.assertEqual(identity_a, identity_b)
        self.assertNotEqual(identity_a, identity_c)
        self.assertIn("1792045800", identity_a)

    def test_ack_survives_close_and_reopen(self):
        ledger, directory = make_ledger()
        t = 1792045800
        identity = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", t)
        ledger.record_acknowledged("s1", "XAUUSD", "M15", t, identity, 999)
        ledger.close()

        reopened = DeliveryLedger(os.path.join(directory, "ledger.sqlite"))
        self.assertTrue(reopened.is_acknowledged("s1", "XAUUSD", "M15", t))
        self.assertEqual(reopened.latest_acknowledged("s1", "XAUUSD", "M15"), t)
        self.assertEqual(reopened.latest_acknowledged("s1", "XAUUSD", "H1"), None)
        reopened.close()

    def test_pending_is_retained_and_available_for_replay(self):
        ledger, directory = make_ledger()
        t = 100
        identity = DeliveryLedger.make_identity("s1", "XAUUSD", "M15", t)
        ledger.record_pending("s1", "XAUUSD", "M15", t, identity)
        pending = ledger.pending_identities()
        self.assertEqual(len(pending), 1)
        self.assertEqual(pending[0]["open_time"], t)
        self.assertFalse(ledger.is_acknowledged("s1", "XAUUSD", "M15", t))
        # Filtering: pending is NOT treated as acked (must be retried).
        self.assertEqual(ledger.pending_and_unacked("s1", "XAUUSD", "M15", [t]), [t])
        # Recording an ack clears the pending status.
        ledger.record_acknowledged("s1", "XAUUSD", "M15", t, identity, 200)
        self.assertTrue(ledger.is_acknowledged("s1", "XAUUSD", "M15", t))
        self.assertEqual(ledger.pending_and_unacked("s1", "XAUUSD", "M15", [t]), [])
        ledger.close()

    def test_failed_does_not_become_acked_and_counts(self):
        ledger, directory = make_ledger()
        t = 5
        identity = DeliveryLedger.make_identity("s1", "XAUUSD", "D1", t)
        ledger.record_failed("s1", "XAUUSD", "D1", t, "rejected: bad high")
        self.assertFalse(ledger.is_acknowledged("s1", "XAUUSD", "D1", t))
        counts = ledger.counts()
        self.assertEqual(counts["failed"], 1)
        self.assertEqual(counts["acknowledged"], 0)
        ledger.close()


if __name__ == "__main__":
    unittest.main()