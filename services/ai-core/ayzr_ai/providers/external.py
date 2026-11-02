"""Optional external OpenAI-compatible adapter.

Off by default. This exists so an operator can point AYZR at an external
endpoint later without touching the chat logic. It is not the production path
and it is never selected unless explicitly enabled with a token.
"""

from __future__ import annotations

import time

import httpx

from .base import (
    GenerateRequest,
    GenerateResult,
    ModelProvider,
    ProviderOutcome,
)


class ExternalOpenAICompatProvider(ModelProvider):
    name = "external"

    def __init__(self, base_url: str, model: str, token: str, default_timeout_ms: int = 120_000):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self._token = token
        self.default_timeout_ms = default_timeout_ms

    async def generate(self, request: GenerateRequest) -> GenerateResult:
        started = time.monotonic()
        payload = {
            "model": self.model,
            "messages": [
                {"role": m.role, "content": m.content} for m in request.messages
            ],
            "temperature": request.temperature,
            "stream": False,
        }
        headers = {"Authorization": f"Bearer {self._token}"}
        timeout_s = (request.timeout_ms or self.default_timeout_ms) / 1000.0
        try:
            async with httpx.AsyncClient(timeout=timeout_s) as client:
                resp = await client.post(
                    f"{self.base_url}/chat/completions", json=payload, headers=headers
                )
        except httpx.TimeoutException:
            return self._fail(ProviderOutcome.TIMEOUT, "provider timed out", started)
        except httpx.HTTPError as exc:
            return self._fail(
                ProviderOutcome.UNAVAILABLE, f"transport: {type(exc).__name__}", started
            )

        latency = int((time.monotonic() - started) * 1000)
        if resp.status_code in (401, 403):
            return self._fail(ProviderOutcome.AUTH_FAILURE, "auth rejected", started)
        if resp.status_code == 429:
            retry_after = -1
            try:
                retry_after = int(resp.headers.get("Retry-After", "-1"))
            except ValueError:
                retry_after = -1
            return GenerateResult(
                outcome=ProviderOutcome.RATE_LIMITED,
                provider=self.name,
                model=self.model,
                error="rate limited",
                latency_ms=latency,
                retry_after_seconds=retry_after,
            )
        if resp.status_code >= 500:
            return self._fail(ProviderOutcome.SERVER_ERROR, "provider 5xx", started)
        if resp.status_code != 200:
            return self._fail(
                ProviderOutcome.BAD_REQUEST, f"status {resp.status_code}", started
            )

        try:
            body = resp.json()
            text = body["choices"][0]["message"]["content"] or ""
            usage = body.get("usage") or {}
        except (KeyError, IndexError, ValueError, TypeError):
            return self._fail(ProviderOutcome.INVALID_OUTPUT, "unexpected shape", started)

        if not text.strip():
            return GenerateResult(
                outcome=ProviderOutcome.INVALID_OUTPUT,
                provider=self.name,
                model=self.model,
                error="empty content",
                latency_ms=latency,
            )
        return GenerateResult(
            outcome=ProviderOutcome.OK,
            text=text,
            provider=self.name,
            model=self.model,
            latency_ms=latency,
            prompt_tokens=int(usage.get("prompt_tokens") or 0),
            completion_tokens=int(usage.get("completion_tokens") or 0),
        )

    def _fail(self, outcome: ProviderOutcome, msg: str, started: float) -> GenerateResult:
        return GenerateResult(
            outcome=outcome,
            provider=self.name,
            model=self.model,
            error=msg,
            latency_ms=int((time.monotonic() - started) * 1000),
        )

    async def health(self) -> dict:
        return {
            "provider": self.name,
            "model": self.model,
            "reachable": None,
            "note": "external adapter; not probed on health checks",
        }
