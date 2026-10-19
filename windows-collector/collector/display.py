"""Pure presentation helpers.

Kept free of any toolkit import so they can be tested on a machine with no
display, and so the rule they encode is visible on its own: a value that is not
known is rendered as a dash. It is never rendered as zero, as a date, or as any
other value that could be mistaken for real data.
"""

from __future__ import annotations

import time
from typing import Optional

UNKNOWN = "—"

STATE_COLOURS = {
    "READY": "#1a7f37",
    "ONLINE": "#1a7f37",
    "STARTING": "#9a6700",
    "PAUSED": "#9a6700",
    "AUTH_FAILED": "#cf222e",
    "ERROR": "#cf222e",
    "STOPPED": "#57606a",
}


def colour(state: str) -> str:
    """A colour for a component state. Unknown states look neutral, not fine."""
    return STATE_COLOURS.get(state, STATE_COLOURS["STOPPED"])


def fmt_time(epoch: Optional[int]) -> str:
    """Format an epoch second as UTC, or a dash when there is no value."""
    if not epoch:
        return UNKNOWN
    try:
        return time.strftime("%Y-%m-%d %H:%M:%SZ", time.gmtime(int(epoch)))
    except (TypeError, ValueError, OSError):
        return UNKNOWN


def fmt_count(value: Optional[int]) -> str:
    """Format a counter, showing a dash rather than a fake zero when unknown."""
    if value is None:
        return UNKNOWN
    return str(int(value))
