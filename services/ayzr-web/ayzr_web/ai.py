"""Local LLM analysis in Arabic, grounded in numbers we computed ourselves.

The model runs locally through llama.cpp, so no article text, price or question
ever leaves this machine, and no external API quota or credential is involved.

Two hard rules:
  1. The prompt carries the real numbers. The model is told to use only those
     numbers and to say so when something is missing.
  2. The decoder is constrained by a GBNF grammar so it cannot emit non-Arabic
     characters. That is only possible because we own the runtime.
"""

from __future__ import annotations

import json
import re

import httpx

from .signals import Proposal

# Arabic-only grammar: string values admit Arabic letters, spaces, digits and
# punctuation, and nothing else. The decoder physically cannot emit a Latin
# letter inside them.
ARABIC_GRAMMAR = r"""
root ::= "{" sp "\"summary_ar\"" sp ":" sp arstr sp "," sp "\"risk_ar\"" sp ":" sp arstr sp "," sp "\"watch_ar\"" sp ":" sp arstr sp "}"
arstr ::= "\"" artext "\""
artext ::= (arw | sp | digit | punct)+
arw ::= [\u0600-\u06FF]
sp ::= [ \t\n]*
digit ::= [0-9]
punct ::= [.،؛:!؟\-٪%()+=/]
"""

_SYSTEM = """أنت محلل أسواق مالية متخصص في الذهب. تكتب بالعربية الفصحى فقط.

قواعد صارمة:
1. استخدم الأرقام المذكورة في المعطيات فقط. ممنوع اختراع أي رقم.
2. ممنوع ذكر أي احتمال رقمي أو نسبة نجاح. لا توجد بيانات نتائج.
3. اذكر في حقل risk_ar المخاطر والمعلومات الناقصة بصراحة.
4. اذكر في حقل watch_ar ما يجب مراقبته لتأكيد أو إبطال السيناريو.
5. لا تكتب أي كلمة بغير الحروف العربية."""


class AiUnavailable(RuntimeError):
    pass


def _norm_number(tok: str) -> str:
    """Normalise a numeric token for comparison.

    Only trailing zeros *after a decimal point* are dropped. Stripping them from
    the integer part would collapse 10, 100 and 1000 into "1", which would let an
    invented "100" pass as grounded whenever a "1" appeared in the input.
    """
    t = tok.replace(",", "")
    if "." in t:
        t = t.rstrip("0").rstrip(".")
    return t or "0"


def _numbers_in(text: str) -> set[str]:
    """Numeric tokens in a string, normalised for comparison."""
    # Thousands separators are matched as part of one token. Splitting
    # "3,954.79" into "3954" and "79" would flag both as invented and suppress a
    # perfectly grounded analysis.
    pattern = r"\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?"
    return {_norm_number(t) for t in re.findall(pattern, text)}


def _grounded_numbers(facts: dict) -> tuple[set[str], list[float]]:
    """Every number the model was shown, from both keys and values.

    Keys count: the fact names carry period lengths ("مؤشر القوة النسبية 14",
    "المتوسط السريع 20"), so a model that names the indicator is quoting its
    input, not inventing a figure.

    Returns the normalised tokens and the same values as floats, so a rounded
    restatement can be recognised as quoting rather than inventing.
    """
    tokens: set[str] = set()
    floats: list[float] = []
    for k, v in facts.items():
        for text in (str(k), str(v)):
            tokens |= _numbers_in(text)
            for tok in re.findall(r"\d+(?:\.\d+)?", text.replace(",", "")):
                try:
                    floats.append(float(tok))
                except ValueError:
                    pass
    return tokens, floats


def _decimals(tok: str) -> int:
    return len(tok.split(".")[1]) if "." in tok else 0


def _ungrounded_numbers(analysis: dict, facts: dict) -> list[str]:
    """Numbers the model wrote that were never in its input.

    The model has no basis for any figure beyond the ones it was handed, so an
    unknown number is an invention — the "10% loss" derived from a 2.0
    risk/reward ratio is the case that motivated this.
    """
    allowed, allowed_floats = _grounded_numbers(facts)
    found: set[str] = set()
    for field in ("summary_ar", "risk_ar", "watch_ar"):
        found |= _numbers_in(analysis.get(field, ""))

    invented = []
    for tok in found:
        if tok in allowed:
            continue
        # The model may restate a supplied figure at lower precision ("55.7"
        # for 55.66). Accept that, but only if some supplied number really does
        # round to what was written — otherwise a figure was invented.
        d = _decimals(tok)
        try:
            value = float(tok)
        except ValueError:
            invented.append(tok)
            continue
        if not any(round(av, d) == round(value, d) for av in allowed_floats):
            invented.append(tok)
    return sorted(invented)


async def analyze(
    proposal: Proposal,
    *,
    base_url: str = "http://127.0.0.1:8080",
    timeout_s: float = 150.0,
    max_tokens: int = 400,
) -> dict:
    """Return Arabic analysis grounded in the proposal's real numbers."""
    ind = proposal.indicators or {}
    facts = {
        "الأصل": "XAUUSD (عقد COMEX الآجل GC=F)",
        "الإطار الزمني": proposal.timeframe,
        "السعر الحالي": ind.get("price"),
        "المتوسط السريع 20": ind.get("ema20"),
        "المتوسط البطيء 50": ind.get("ema50"),
        "مؤشر القوة النسبية 14": ind.get("rsi14"),
        "مدى الحركة الحقيقي 14": ind.get("atr14"),
        "الاتجاه المستنتج": proposal.direction,
        "عدد الإشارات المتوافقة": proposal.confidence,
        "الدخول المقترح": proposal.entry,
        "وقف الخسارة": proposal.stop_loss,
        "الهدف": proposal.take_profit,
        "نسبة المخاطرة للمكافأة": proposal.risk_reward,
        "الدعم": (proposal.levels or {}).get("support"),
        "المقاومة": (proposal.levels or {}).get("resistance"),
    }
    # Round every number before it reaches the prompt: an unrounded float makes
    # the model quote meaningless precision back at the user.
    facts = {
        k: (round(v, 2) if isinstance(v, float) else v)
        for k, v in facts.items()
        if v is not None
    }
    checks = "\n".join(f"- {c}" for c in proposal.checks) or "- لا توجد"

    user = (
        "المعطيات المحسوبة من شموع حقيقية مغلقة:\n"
        f"{json.dumps(facts, ensure_ascii=False, indent=2)}\n\n"
        f"الإشارات المستقلة:\n{checks}\n\n"
        "اكتب تحليلاً موجزاً: ملخص الوضع في summary_ar، والمخاطر والنقص في risk_ar، "
        "وما يجب مراقبته في watch_ar."
    )

    payload = {
        "model": "local",
        "messages": [
            {"role": "system", "content": _SYSTEM},
            {"role": "user", "content": user},
        ],
        "max_tokens": max_tokens,
        "temperature": 0.1,
        "stream": False,
        "grammar": ARABIC_GRAMMAR,
    }

    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            resp = await client.post(
                f"{base_url}/v1/chat/completions", json=payload
            )
    except httpx.HTTPError as exc:
        raise AiUnavailable(f"local model unreachable: {exc}") from exc

    if resp.status_code != 200:
        raise AiUnavailable(f"local model returned HTTP {resp.status_code}")

    try:
        content = resp.json()["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError, ValueError) as exc:
        raise AiUnavailable(f"unexpected model response: {exc}") from exc

    try:
        parsed = json.loads(content)
    except json.JSONDecodeError as exc:
        # The grammar should prevent this; if it still happens we say so rather
        # than showing raw model text as if it were an analysis.
        raise AiUnavailable(f"model output was not valid JSON: {exc}") from exc

    analysis = {
        "summary_ar": str(parsed.get("summary_ar", "")).strip(),
        "risk_ar": str(parsed.get("risk_ar", "")).strip(),
        "watch_ar": str(parsed.get("watch_ar", "")).strip(),
    }

    invented = _ungrounded_numbers(analysis, facts)
    if invented:
        # Withhold the text rather than show a fabricated figure. The technical
        # proposal is unaffected and remains on screen.
        raise AiUnavailable(
            "النموذج ذكر أرقاماً لم تُعطَ له: " + ", ".join(invented[:5])
        )

    return {
        "status": "ok",
        **analysis,
        "model": "qwen2.5-1.5b-instruct-q4_k_m",
        "provider": "local",
        "grounded_on": facts,
    }


async def health(base_url: str = "http://127.0.0.1:8080", timeout_s: float = 4.0) -> dict:
    try:
        async with httpx.AsyncClient(timeout=timeout_s) as client:
            r = await client.get(f"{base_url}/health")
        return {"reachable": r.status_code == 200, "model": "qwen2.5-1.5b-instruct-q4_k_m"}
    except httpx.HTTPError:
        return {"reachable": False, "model": "qwen2.5-1.5b-instruct-q4_k_m"}
