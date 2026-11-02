"""Read-only tools over the AYZR backend.

Every tool is GET-only by construction. There is no write path here, so the AI
cannot change ASTRA, inject a trade, edit engine rules, or alter Quant Lab
results even if a prompt tells it to. The allowlist is the whole set of
capabilities; anything not listed does not exist.

Each tool: a JSON schema, a timeout, an allowlist entry, and a safe error shape
that never leaks a stack trace or a credential.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Awaitable, Callable

import httpx


@dataclass
class ToolResult:
    ok: bool
    data: Any = None
    error: str = ""
    source: str = ""
    timestamp: int = 0


@dataclass
class ToolSpec:
    name: str
    description_ar: str
    schema: dict
    handler: Callable[..., Awaitable[ToolResult]]
    timeout_ms: int = 10_000
    read_only: bool = True


class ToolRegistry:
    def __init__(self, backend_url: str, client_token: str, timeout_ms: int = 10_000):
        self.backend_url = backend_url.rstrip("/")
        self._token = client_token
        self.default_timeout_ms = timeout_ms
        self._tools: dict[str, ToolSpec] = {}

    # --- registration ---
    def register(self, spec: ToolSpec) -> None:
        if not spec.read_only:
            raise ValueError(f"tool {spec.name} is not read-only; refusing")
        self._tools[spec.name] = spec

    def names(self) -> list[str]:
        return sorted(self._tools)

    def get(self, name: str) -> ToolSpec | None:
        return self._tools.get(name)

    def schemas(self) -> list[dict]:
        return [
            {"name": t.name, "description": t.description_ar, "parameters": t.schema}
            for t in self._tools.values()
        ]

    # --- invocation ---
    async def call(self, name: str, args: dict | None = None) -> ToolResult:
        spec = self._tools.get(name)
        if spec is None:
            # Allowlist miss: a hallucinated tool name is a normal event, not a
            # crash. Report it so the caller can tell the model "no such tool".
            return ToolResult(
                ok=False, error=f"unknown tool: {name}", source="allowlist"
            )
        try:
            return await spec.handler(**(args or {}))
        except TypeError as exc:
            return ToolResult(
                ok=False, error=f"bad arguments: {type(exc).__name__}", source=name
            )
        except Exception as exc:  # noqa: BLE001 - never leak internals to the model
            return ToolResult(
                ok=False, error=f"tool failed: {type(exc).__name__}", source=name
            )

    # --- shared HTTP helper ---
    async def _get(self, path: str, timeout_ms: int | None = None) -> ToolResult:
        url = f"{self.backend_url}{path}"
        headers = {}
        if self._token:
            headers["Authorization"] = f"Bearer {self._token}"
        timeout_s = (timeout_ms or self.default_timeout_ms) / 1000.0
        try:
            async with httpx.AsyncClient(timeout=timeout_s) as client:
                resp = await client.get(url, headers=headers)
        except httpx.TimeoutException:
            return ToolResult(ok=False, error="backend timeout", source=path)
        except httpx.ConnectError:
            return ToolResult(ok=False, error="backend unreachable", source=path)
        except httpx.HTTPError as exc:
            return ToolResult(ok=False, error=f"transport: {type(exc).__name__}", source=path)

        if resp.status_code == 401 or resp.status_code == 403:
            return ToolResult(ok=False, error="backend rejected credential", source=path)
        if resp.status_code == 404:
            return ToolResult(ok=False, error="not found", source=path)
        if resp.status_code == 503:
            # The backend is honest about missing data; pass that through as a
            # normal empty result so the AI can say "no data", not "error".
            return ToolResult(ok=True, data=None, source=path)
        if resp.status_code != 200:
            return ToolResult(ok=False, error=f"status {resp.status_code}", source=path)
        try:
            body = resp.json()
        except ValueError:
            return ToolResult(ok=False, error="non-JSON response", source=path)
        # The AYZR envelope is {"api","schema","data"}; unwrap it.
        data = body.get("data", body) if isinstance(body, dict) else body
        return ToolResult(ok=True, data=data, source=path)
