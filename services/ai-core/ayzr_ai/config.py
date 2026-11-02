"""Configuration for AYZR Native AI Core.

Everything comes from the environment. No secret has a default, and no secret is
ever logged. The external provider is opt-in and off by default so the service
cannot spend money or hit a vendor quota unless an operator turns it on.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _env_int(name: str, default: int) -> int:
    raw = _env(name)
    if not raw:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _env_bool(name: str, default: bool = False) -> bool:
    raw = _env(name).lower()
    if not raw:
        return default
    return raw in {"1", "true", "yes", "on"}


@dataclass(frozen=True)
class Settings:
    # --- service ---
    host: str = "127.0.0.1"
    port: int = 8090
    # Bearer token clients must present. Empty means "refuse every request":
    # an unauthenticated AI service must never be reachable.
    service_token: str = ""

    # --- provider selection ---
    # "local"  -> our llama.cpp runtime (the intended production path)
    # "mock"   -> deterministic, for tests only
    # "external" -> opt-in adapter, off unless explicitly enabled
    provider: str = "local"

    # local runtime
    local_base_url: str = "http://127.0.0.1:8080"
    local_model: str = "qwen2.5-1.5b-instruct-q4_k_m"
    local_timeout_ms: int = 120_000

    # external provider (disabled by default; requires explicit opt-in)
    external_enabled: bool = False
    external_base_url: str = ""
    external_model: str = ""
    external_token: str = ""

    # --- AYZR backend (read-only tool access) ---
    backend_url: str = "http://127.0.0.1:8081"
    backend_client_token: str = ""
    backend_timeout_ms: int = 10_000

    # --- generation bounds ---
    max_output_tokens: int = 768
    temperature: float = 0.15
    # Reasoning models on CPU are slow; keep a hard wall so a request cannot
    # pin a worker forever.
    request_deadline_ms: int = 180_000

    # --- safety ---
    max_request_bytes: int = 64 * 1024
    rate_limit_per_minute: int = 30
    max_history_turns: int = 8
    max_context_chars: int = 12_000
    # Constrain the local decoder to Arabic-only output with a GBNF grammar.
    # Measured to eliminate the mixed-language defect entirely. Only the local
    # runtime can honour it, so it is ignored for other providers.
    arabic_grammar_enabled: bool = True

    # --- observability ---
    log_level: str = "INFO"

    @classmethod
    def from_env(cls) -> "Settings":
        return cls(
            host=_env("AYZR_AI_HOST", "127.0.0.1"),
            port=_env_int("AYZR_AI_PORT", 8090),
            service_token=_env("AYZR_AI_TOKEN"),
            provider=_env("AYZR_AI_PROVIDER", "local").lower(),
            local_base_url=_env("AYZR_AI_LOCAL_URL", "http://127.0.0.1:8080"),
            local_model=_env("AYZR_AI_LOCAL_MODEL", "qwen2.5-1.5b-instruct-q4_k_m"),
            local_timeout_ms=_env_int("AYZR_AI_LOCAL_TIMEOUT_MS", 120_000),
            external_enabled=_env_bool("AYZR_AI_EXTERNAL_ENABLED", False),
            external_base_url=_env("AYZR_AI_EXTERNAL_URL"),
            external_model=_env("AYZR_AI_EXTERNAL_MODEL"),
            external_token=_env("AYZR_AI_EXTERNAL_TOKEN"),
            backend_url=_env("AYZR_BACKEND_URL", "http://127.0.0.1:8081"),
            backend_client_token=_env("AYZR_BACKEND_CLIENT_TOKEN"),
            backend_timeout_ms=_env_int("AYZR_BACKEND_TIMEOUT_MS", 10_000),
            max_output_tokens=_env_int("AYZR_AI_MAX_OUTPUT_TOKENS", 768),
            temperature=float(_env("AYZR_AI_TEMPERATURE", "0.15") or 0.15),
            request_deadline_ms=_env_int("AYZR_AI_REQUEST_DEADLINE_MS", 180_000),
            max_request_bytes=_env_int("AYZR_AI_MAX_REQUEST_BYTES", 64 * 1024),
            rate_limit_per_minute=_env_int("AYZR_AI_RATE_LIMIT", 30),
            max_history_turns=_env_int("AYZR_AI_MAX_HISTORY_TURNS", 8),
            max_context_chars=_env_int("AYZR_AI_MAX_CONTEXT_CHARS", 12_000),
            arabic_grammar_enabled=_env_bool("AYZR_AI_ARABIC_GRAMMAR", True),
            log_level=_env("AYZR_AI_LOG_LEVEL", "INFO"),
        )

    def redacted(self) -> dict:
        """Safe view for /health. Never returns a secret value."""
        return {
            "provider": self.provider,
            "local_model": self.local_model,
            "local_base_url": self.local_base_url,
            "external_enabled": self.external_enabled,
            "backend_url": self.backend_url,
            "max_output_tokens": self.max_output_tokens,
            "rate_limit_per_minute": self.rate_limit_per_minute,
            "service_token_set": bool(self.service_token),
            "backend_token_set": bool(self.backend_client_token),
        }
