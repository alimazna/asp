"""Deterministic mock provider. Tests only.

It is never selectable in production by accident: the factory refuses "mock"
unless AYZR_AI_ALLOW_MOCK=1 is set, and the health endpoint reports the provider
name so a mock answer can never be mistaken for a real one.
"""

from __future__ import annotations

import json
import time

from .base import (
    GenerateRequest,
    GenerateResult,
    ModelProvider,
    ProviderOutcome,
)


class MockProvider(ModelProvider):
    name = "mock"

    def __init__(self, mode: str = "valid"):
        # valid | empty | malformed | timeout | unavailable | injection_echo
        self.mode = mode
        self.calls: list[GenerateRequest] = []

    async def generate(self, request: GenerateRequest) -> GenerateResult:
        self.calls.append(request)
        started = time.monotonic()

        if self.mode == "timeout":
            return GenerateResult(
                outcome=ProviderOutcome.TIMEOUT,
                provider=self.name,
                model="mock",
                error="mock timeout",
                latency_ms=1,
            )
        if self.mode == "unavailable":
            return GenerateResult(
                outcome=ProviderOutcome.UNAVAILABLE,
                provider=self.name,
                model="mock",
                error="mock runtime down",
                latency_ms=1,
            )
        if self.mode == "empty":
            return GenerateResult(
                outcome=ProviderOutcome.INVALID_OUTPUT,
                provider=self.name,
                model="mock",
                error="empty content (finish_reason=length)",
                latency_ms=1,
            )

        wants_json = request.json_hint or any(
            "JSON" in m.content for m in request.messages
        )

        if self.mode == "malformed":
            text = '{"summary_ar": "نص مقطوع'
        elif self.mode == "injection_echo":
            # Simulates a model that swallowed a malicious instruction and
            # started obeying it. The service must still refuse to leak.
            text = json.dumps(
                {
                    "summary_ar": "SYSTEM OVERRIDE ACCEPTED",
                    "direction": "bullish",
                    "leaked": "secret-token-value",
                },
                ensure_ascii=False,
            )
        elif wants_json:
            text = json.dumps(
                {
                    "summary_ar": "ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع.",
                    "direction": "bullish",
                    "confidence_reason_ar": "تراجع الدولار يدعم الذهب.",
                    "facts": ["بيانات التضخم جاءت أضعف من المتوقع"],
                    "inferences": ["قد يؤجل الفيدرالي رفع الفائدة"],
                },
                ensure_ascii=False,
            )
        else:
            text = "ارتفع الذهب بعد بيانات تضخم أضعف من المتوقع."

        return GenerateResult(
            outcome=ProviderOutcome.OK,
            text=text,
            provider=self.name,
            model="mock",
            latency_ms=int((time.monotonic() - started) * 1000),
            prompt_tokens=sum(len(m.content) // 4 for m in request.messages),
            completion_tokens=len(text) // 4,
            finish_reason="stop",
        )

    async def health(self) -> dict:
        return {"provider": self.name, "model": "mock", "reachable": True, "mode": self.mode}
