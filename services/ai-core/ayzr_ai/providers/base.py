"""Provider-neutral model interface.

The rest of AYZR never imports a concrete provider. It asks for a provider by
name and gets this interface, so the model behind the service can change without
touching the chat logic, the tools, or the Android app.
"""

from __future__ import annotations

import enum
from dataclasses import dataclass, field


class ProviderOutcome(str, enum.Enum):
    OK = "ok"
    UNAVAILABLE = "unavailable"        # runtime down / not reachable
    TIMEOUT = "timeout"
    AUTH_FAILURE = "auth_failure"      # bad credential, not retryable
    RATE_LIMITED = "rate_limited"      # retryable after backoff
    QUOTA_EXHAUSTED = "quota_exhausted"  # not retryable; cool down
    BAD_REQUEST = "bad_request"
    SERVER_ERROR = "server_error"      # retryable
    INVALID_OUTPUT = "invalid_output"  # model answered but unusable

    @property
    def retryable(self) -> bool:
        return self in {
            ProviderOutcome.TIMEOUT,
            ProviderOutcome.RATE_LIMITED,
            ProviderOutcome.SERVER_ERROR,
        }


@dataclass
class Message:
    role: str  # "system" | "user" | "assistant"
    content: str


@dataclass
class GenerateRequest:
    messages: list[Message]
    max_tokens: int = 768
    temperature: float = 0.15
    timeout_ms: int = 120_000
    # A hint, never a guarantee: a small local model may ignore it. The caller
    # must still parse defensively.
    json_hint: bool = False
    # A GBNF grammar. Only the local runtime honours it. This is how we make
    # Arabic-only output structurally impossible to violate instead of merely
    # asking the model to behave. Empty means "no grammar constraint".
    grammar: str = ""


@dataclass
class GenerateResult:
    outcome: ProviderOutcome
    text: str = ""
    provider: str = ""
    model: str = ""
    error: str = ""
    prompt_tokens: int = 0
    completion_tokens: int = 0
    latency_ms: int = 0
    finish_reason: str = ""
    retry_after_seconds: int = -1

    @property
    def ok(self) -> bool:
        return self.outcome == ProviderOutcome.OK


class ModelProvider:
    """Every provider implements exactly this."""

    name: str = "base"

    async def generate(self, request: GenerateRequest) -> GenerateResult:
        raise NotImplementedError

    async def health(self) -> dict:
        raise NotImplementedError
