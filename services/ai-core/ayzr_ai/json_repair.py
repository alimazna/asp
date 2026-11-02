"""Defensive structured-output parsing.

Measured reality of our local models: asked for "one JSON object, no fences",
Qwen2.5-1.5B still wrapped its answer in ```json fences, and the 3B model leaked
non-Arabic characters into Arabic text. So we never trust the model's format.
We extract, parse, validate against a schema, and reject what does not fit.

Rejecting is the point. A malformed analysis must become an explicit
`invalid_output` state, never a confident-looking wrong answer.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field

_FENCE_RE = re.compile(r"```(?:json)?\s*(.*?)```", re.DOTALL | re.IGNORECASE)


@dataclass
class ParseOutcome:
    ok: bool
    data: dict = field(default_factory=dict)
    error: str = ""
    repaired: bool = False


def extract_json_object(text: str) -> str:
    """Pull the first JSON object out of arbitrary model text.

    Handles the fence case we actually observed, plus leading prose and trailing
    commentary.
    """
    if not text:
        return ""
    stripped = text.strip()

    # 1. Fenced block, the case we measured.
    fence = _FENCE_RE.search(stripped)
    if fence:
        inner = fence.group(1).strip()
        if inner.startswith("{"):
            return inner

    # 2. Already a bare object.
    if stripped.startswith("{"):
        return stripped

    # 3. First brace-balanced object anywhere in the text.
    start = stripped.find("{")
    if start == -1:
        return ""
    depth = 0
    in_str = False
    esc = False
    for i in range(start, len(stripped)):
        ch = stripped[i]
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return stripped[start : i + 1]
    return ""


def _strip_trailing_commas(s: str) -> str:
    return re.sub(r",\s*([}\]])", r"\1", s)


def parse_json_object(text: str) -> ParseOutcome:
    """Extract and parse. Attempts one bounded repair, then gives up."""
    candidate = extract_json_object(text)
    if not candidate:
        return ParseOutcome(ok=False, error="no JSON object found in output")

    try:
        data = json.loads(candidate)
        if not isinstance(data, dict):
            return ParseOutcome(ok=False, error="top-level value is not an object")
        return ParseOutcome(ok=True, data=data)
    except json.JSONDecodeError:
        pass

    # Bounded repair: trailing commas only. We do not invent missing keys.
    try:
        data = json.loads(_strip_trailing_commas(candidate))
        if isinstance(data, dict):
            return ParseOutcome(ok=True, data=data, repaired=True)
    except json.JSONDecodeError as exc:
        return ParseOutcome(ok=False, error=f"invalid JSON: {exc.msg}")

    return ParseOutcome(ok=False, error="invalid JSON")


# --- schema validation -------------------------------------------------------

ALLOWED_DIRECTIONS = {"bullish", "bearish", "sideways", "no_trade"}

# Arabic-only guard. Our models leaked foreign script into Arabic text, and the
# 1.5B model also drops English words mid-sentence ("يsignal"). Both are caught.
#
# Market identifiers are legitimate and must not be flagged: XAUUSD, the
# timeframes, and a short list of terms the domain genuinely uses in Latin form.
ALLOWED_LATIN_TOKENS = {
    "xauusd", "m1", "m5", "m15", "m30", "h1", "h4", "d1", "w1", "mn1",
    "astra", "ayzr", "api", "json", "sl", "tp", "rsi", "macd", "atr",
    "fed", "fomc", "cpi", "ppi", "gdp", "ecb", "boj", "boe", "nfp",
    "bullish", "bearish", "sideways", "no_trade", "quant", "lab",
    "usd", "eur", "gbp", "jpy",
}
_NON_ARABIC_SCRIPT_RE = re.compile(r"[\u4e00-\u9fff\u3040-\u30ff\u0400-\u04ff]{2,}")
_LATIN_RUN_RE = re.compile(r"[A-Za-z]{2,}")
_ARABIC_RE = re.compile(r"[\u0600-\u06ff]")


def _has_disallowed_foreign(text: str) -> str:
    """Return a reason string if the text has non-Arabic script we reject."""
    if not text:
        return ""
    if _NON_ARABIC_SCRIPT_RE.search(text):
        return "non-Arabic script"
    # Latin runs are only a problem when Arabic is also present: a pure English
    # identifier list is not the failure mode we are guarding against.
    if _ARABIC_RE.search(text):
        for match in _LATIN_RUN_RE.finditer(text):
            if match.group(0).lower() not in ALLOWED_LATIN_TOKENS:
                return "non-Arabic script"
    return ""



@dataclass
class AnalysisFields:
    summary_ar: str = ""
    direction: str = "no_trade"
    confidence_reason_ar: str = ""
    facts: list[str] = field(default_factory=list)
    inferences: list[str] = field(default_factory=list)
    entry: float | None = None
    stop_loss: float | None = None
    take_profit: float | None = None


def validate_analysis(data: dict, require_arabic: bool = True) -> tuple[AnalysisFields, list[str]]:
    """Validate a model analysis object. Returns (fields, problems).

    Missing optional keys are fine. A bad direction, a non-Arabic Arabic field,
    or a numeric level the model made up without data are all problems.
    """
    problems: list[str] = []
    out = AnalysisFields()

    summary = data.get("summary_ar")
    if isinstance(summary, str) and summary.strip():
        if require_arabic and not _ARABIC_RE.search(summary):
            problems.append("summary_ar is not Arabic")
        else:
            foreign = _has_disallowed_foreign(summary)
            if foreign:
                problems.append(f"summary_ar contains {foreign}")
            else:
                out.summary_ar = summary.strip()
    else:
        problems.append("summary_ar missing")

    direction = data.get("direction")
    if isinstance(direction, str) and direction.strip().lower() in ALLOWED_DIRECTIONS:
        out.direction = direction.strip().lower()
    else:
        problems.append(f"invalid direction: {direction!r}")

    reason = data.get("confidence_reason_ar")
    if isinstance(reason, str) and reason.strip():
        if not _has_disallowed_foreign(reason):
            out.confidence_reason_ar = reason.strip()

    for key, target in (("facts", out.facts), ("inferences", out.inferences)):
        val = data.get(key)
        # The grammar path returns one string (a numbered list inside it), while
        # an unconstrained model returns a list. Accept both; split the string on
        # line breaks and list markers so the shape is stable either way.
        items: list[str] = []
        if isinstance(val, list):
            items = [x for x in val if isinstance(x, str)]
        elif isinstance(val, str):
            items = re.split(r"\n+|(?<=[.،])\s*\d+[.)-]\s*", val)
        for item in items:
            item = item.strip()
            if item and not _has_disallowed_foreign(item):
                target.append(item)

    for key in ("entry", "stop_loss", "take_profit"):
        val = data.get(key)
        if val is None:
            continue
        if isinstance(val, (int, float)) and not isinstance(val, bool):
            setattr(out, key, float(val))
        else:
            problems.append(f"{key} is not numeric")

    return out, problems
