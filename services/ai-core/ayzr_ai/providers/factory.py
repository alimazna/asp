"""Provider factory. The only place that decides which model answers.

The local runtime is the default and the intended production path. The external
adapter is opt-in: it refuses to construct unless it is both enabled in config
and given a token, so an accidental deploy cannot start spending money.
"""

from __future__ import annotations

from ..config import Settings
from .base import ModelProvider
from .local import LocalLlamaProvider
from .mock import MockProvider


class ProviderConfigError(RuntimeError):
    pass


def build_provider(settings: Settings, allow_mock: bool = False) -> ModelProvider:
    name = (settings.provider or "local").lower()

    if name == "local":
        return LocalLlamaProvider(
            base_url=settings.local_base_url,
            model=settings.local_model,
            default_timeout_ms=settings.local_timeout_ms,
        )

    if name == "mock":
        if not allow_mock:
            raise ProviderConfigError(
                "mock provider refused: set AYZR_AI_ALLOW_MOCK=1 for tests only"
            )
        import os

        return MockProvider(mode=os.environ.get("AYZR_AI_MOCK_MODE", "valid"))

    if name == "external":
        if not settings.external_enabled:
            raise ProviderConfigError(
                "external provider is disabled; set AYZR_AI_EXTERNAL_ENABLED=1 "
                "and supply a token explicitly"
            )
        if not settings.external_base_url or not settings.external_token:
            raise ProviderConfigError(
                "external provider needs AYZR_AI_EXTERNAL_URL and "
                "AYZR_AI_EXTERNAL_TOKEN"
            )
        from .external import ExternalOpenAICompatProvider

        return ExternalOpenAICompatProvider(
            base_url=settings.external_base_url,
            model=settings.external_model,
            token=settings.external_token,
            default_timeout_ms=settings.local_timeout_ms,
        )

    raise ProviderConfigError(f"unknown provider: {name!r}")
