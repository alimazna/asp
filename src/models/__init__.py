"""Agent-B — Probability & Calibration zone.

Owned by Agent-B. Reviewer: Agent-D.

This package holds the model + calibration harness only. It contains no
fitted model and no probability output: per RULE C, nothing here publishes a
probability until calibration is measured and audited.

The harness is deliberately dependency-free (Python standard library only) so
it is deterministic and runs anywhere, including a bare container.
"""
