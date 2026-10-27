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
    # MT5 probe states. CONNECTED must look healthy; CONNECTING and NO_DATA are
    # in-progress or incomplete, so they read amber rather than green.
    "CONNECTED": "#1a7f37",
    "CONNECTING": "#9a6700",
    "NO_DATA": "#9a6700",
    "STARTING": "#9a6700",
    "PAUSED": "#9a6700",
    "AUTH_FAILED": "#cf222e",
    "ERROR": "#cf222e",
    "FAILED": "#cf222e",
    "STOPPED": "#57606a",
    "DISCONNECTED": "#57606a",
}


def colour(state: str) -> str:
    """A colour for a component state. Unknown states look neutral, not fine."""
    return STATE_COLOURS.get(state, STATE_COLOURS["STOPPED"])


#: Text for each MT5 probe state. Kept here, not in the widget code, so the
#: rule that a failure must not read like "not tried yet" is testable without
#: a display. The failure text carries the reason the reader reported.
PROBE_TEXT = {
    "CONNECTED": "Terminal: connected ({detail})",
    "CONNECTING": "Terminal: connecting...",
    "NO_DATA": "Terminal: reachable, no closed bar",
    "FAILED": "Terminal: failed - {detail}",
    "DISCONNECTED": "Terminal: not connected",
}


def probe_text(state: str, detail: str = "") -> str:
    """The label text for an MT5 probe state.

    An unrecognised state says "not checked" rather than borrowing a healthy
    one, so a state added without text shows up as unknown instead of as
    connected.
    """
    template = PROBE_TEXT.get(state)
    if template is None:
        return "Terminal: not checked"
    return template.format(detail=detail)


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


def resolve_brief_height(needed: int, screen_h: int, current: int,
                         growing: bool) -> int:
    """Pick the window height when the setup-text panel is toggled.

    Kept here, free of tkinter, so the rule can be tested without a display.

    Growing must never shrink the window. On a short screen the screen cap can
    be below the height already in use, and shrinking while the panel grows
    pushes the rows above it past the bottom edge. Packed tkinter content has
    no scrollbar, so a row that goes past the bottom is gone for good.
    """
    target = min(needed + 8, screen_h - 80)
    if growing:
        target = max(target, current)
    # Clamp to the window minimum here rather than relying on minsize() to do
    # it. A caller that trusted a smaller number would clip the bottom row.
    return max(target, 700)
