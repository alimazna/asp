"""Local model provider: our own llama.cpp runtime.

This is the production path. The runtime is a separate process, so a model crash
degrades this service to `unavailable` instead of taking AYZR down with it.
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


class LocalLlamaProvider(ModelProvider):
    name = "local"

    def __init__(self, base_url: str, model: str, default_timeout_ms: int = 120_000):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.default_timeout_ms = default_timeout_ms

    async def generate(self, request: GenerateRequest) -> GenerateResult:
        started = time.monotonic()
        payload = {
            "model": self.model,
            "messages": [
                {"role": m.role, "content": m.content} for m in request.messages
            ],
            "max_tokens": request.max_tokens,
            "temperature": request.temperature,
            "stream": False,
        }
        # llama-server honours this, which is what makes the small model emit
        # parseable JSON. It is a grammar constraint, not a promise from the
        # model, so the caller still validates.
        if request.json_hint:
            payload["response_format"] = {"type": "json_object"}
        # Our own runtime can do something no external API can: constrain the
        # decoder with a GBNF grammar. When supplied, the model cannot emit a
        # character outside it, so an Arabic-only field stays Arabic-only.
        if request.grammar:
            payload["grammar"] = request.grammar
            payload.pop("response_format", None)

        timeout_s = (request.timeout_ms or self.default_timeout_ms) / 1000.0
        try:
            async with httpx.AsyncClient(timeout=timeout_s) as client:
                resp = await client.post(
                    f"{self.base_url}/v1/chat/completions", json=payload
                )
        except httpx.TimeoutException:
            return self._fail(
                ProviderOutcome.TIMEOUT, "model runtime timed out", started
            )
        except httpx.ConnectError:
            return self._fail(
                ProviderOutcome.UNAVAILABLE,
                "model runtime not reachable",
                started,
            )
        except httpx.HTTPError as exc:
            return self._fail(
                ProviderOutcome.SERVER_ERROR,
                f"transport error: {type(exc).__name__}",
                started,
            )

        latency = int((time.monotonic() - started) * 1000)

        if resp.status_code == 401 or resp.status_code == 403:
            return self._fail(
                ProviderOutcome.AUTH_FAILURE, "runtime rejected credential", started
            )
        if resp.status_code == 429:
            return GenerateResult(
                outcome=ProviderOutcome.RATE_LIMITED,
                provider=self.name,
                model=self.model,
                error="runtime rate limited",
                latency_ms=latency,
            )
        if resp.status_code >= 500:
            return self._fail(
                ProviderOutcome.SERVER_ERROR, "runtime error", started
            )
        if resp.status_code != 200:
            return self._fail(
                ProviderOutcome.BAD_REQUEST,
                f"runtime returned {resp.status_code}",
                started,
            )

        try:
            body = resp.json()
            choice = body["choices"][0]
            text = (choice.get("message") or {}).get("content") or ""
            usage = body.get("usage") or {}
            finish = choice.get("finish_reason") or ""
        except (KeyError, IndexError, ValueError, TypeError):
            return self._fail(
                ProviderOutcome.INVALID_OUTPUT,
                "runtime response not in expected shape",
                started,
            )

        if not text.strip():
            # A reasoning model that spent its whole budget returns nothing.
            # Say so instead of pretending the answer was empty on purpose.
            return GenerateResult(
                outcome=ProviderOutcome.INVALID_OUTPUT,
                provider=self.name,
                model=self.model,
                error=f"empty content (finish_reason={finish or 'unknown'})",
                latency_ms=latency,
                finish_reason=finish,
                prompt_tokens=int(usage.get("prompt_tokens") or 0),
                completion_tokens=int(usage.get("completion_tokens") or 0),
            )

        return GenerateResult(
            outcome=ProviderOutcome.OK,
            text=text,
            provider=self.name,
            model=self.model,
            latency_ms=latency,
            finish_reason=finish,
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
        try:
            async with httpx.AsyncClient(timeout=3.0) as client:
                resp = await client.get(f"{self.base_url}/health")
            return {
                "provider": self.name,
                "model": self.model,
                "reachable": resp.status_code == 200,
            }
        except httpx.HTTPError as exc:
            return {
                "provider": self.name,
                "model": self.model,
                "reachable": False,
                "error": type(exc).__name__,
            }
