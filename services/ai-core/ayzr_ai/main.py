"""AYZR Native AI Core HTTP API.

Owned by the project. Authenticated, rate limited, size limited, and honest
about failure. If the model runtime is down, every route says so instead of
pretending; the rest of AYZR keeps running because this service is separate.
"""

from __future__ import annotations

import logging
import os
from contextlib import asynccontextmanager

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from .agents.specialists import ALL_AGENTS
from .config import Settings
from .core import AiCore
from .memory.knowledge import default_knowledge_base
from .providers.factory import ProviderConfigError, build_provider
from .security import RateLimiter, constant_time_token_match
from .tools.ayzr_tools import build_registry

logger = logging.getLogger("ayzr_ai")

_settings: Settings | None = None
_core: AiCore | None = None
_limiter: RateLimiter | None = None
_startup_error = ""


def _allow_mock() -> bool:
    return os.environ.get("AYZR_AI_ALLOW_MOCK", "").lower() in {"1", "true", "yes"}


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _settings, _core, _limiter, _startup_error
    _settings = Settings.from_env()
    logging.basicConfig(level=getattr(logging, _settings.log_level.upper(), logging.INFO))
    _limiter = RateLimiter(_settings.rate_limit_per_minute)
    try:
        provider = build_provider(_settings, allow_mock=_allow_mock())
    except ProviderConfigError as exc:
        # A misconfiguration must not crash-loop the container; serve health
        # that says exactly what is wrong and refuse AI calls.
        _startup_error = str(exc)
        logger.error("provider configuration failed: %s", exc)
        provider = None

    tools = build_registry(
        _settings.backend_url, _settings.backend_client_token, _settings.backend_timeout_ms
    )
    kb = default_knowledge_base()
    _core = AiCore(_settings, provider, tools, kb) if provider else None
    if kb.size:
        logger.info("knowledge base loaded with %d chunks", kb.size)
    yield
    _core = None


app = FastAPI(title="AYZR Native AI Core", version="0.1.0", lifespan=lifespan)


# --- auth --------------------------------------------------------------------

async def require_auth(
    authorization: str | None = Header(default=None),
    request: Request = None,
):
    settings = _settings or Settings.from_env()
    presented = ""
    if authorization and authorization.lower().startswith("bearer "):
        presented = authorization[7:].strip()
    if not constant_time_token_match(presented, settings.service_token):
        raise HTTPException(status_code=401, detail="unauthorized")
    if _limiter is not None:
        allowed, retry_after = _limiter.allow(presented)
        if not allowed:
            raise HTTPException(
                status_code=429,
                detail="rate limited",
                headers={"Retry-After": str(retry_after)},
            )
    return True


# --- size guard --------------------------------------------------------------

@app.middleware("http")
async def limit_body_size(request: Request, call_next):
    settings = _settings or Settings.from_env()
    length = request.headers.get("content-length")
    if length and length.isdigit() and int(length) > settings.max_request_bytes:
        return JSONResponse(
            status_code=413, content={"error": "payload_too_large"}
        )
    return await call_next(request)


# --- models ------------------------------------------------------------------

class ChatRequest(BaseModel):
    question: str = Field(min_length=1, max_length=4000)
    session_id: str = Field(default="default", max_length=64)
    json_output: bool | None = None


class AnalyzeNewsRequest(BaseModel):
    headline: str = Field(min_length=1, max_length=600)
    body: str = Field(default="", max_length=8000)
    source: str = Field(default="", max_length=200)
    published_at: int = 0


class ExplainRequest(BaseModel):
    opportunity_id: str = Field(default="", max_length=128)
    question: str = Field(default="", max_length=1000)


class ResearchRequest(BaseModel):
    question: str = Field(min_length=1, max_length=2000)


def _require_core() -> AiCore:
    if _core is None:
        raise HTTPException(
            status_code=503,
            detail=f"ai core unavailable: {_startup_error or 'not initialised'}",
        )
    return _core


# --- routes ------------------------------------------------------------------

@app.get("/api/v1/ai/health")
async def health():
    settings = _settings or Settings.from_env()
    provider_health = {}
    if _core is not None:
        try:
            provider_health = await _core.provider.health()
        except Exception as exc:  # noqa: BLE001
            provider_health = {"error": type(exc).__name__}
    return {
        "service": "ayzr-native-ai-core",
        "ready": _core is not None,
        "startup_error": _startup_error,
        "provider_health": provider_health,
        "config": settings.redacted(),
    }


@app.get("/api/v1/ai/models")
async def models(_: bool = Depends(require_auth)):
    settings = _settings or Settings.from_env()
    return {
        "active_provider": settings.provider,
        "local_model": settings.local_model,
        "external_enabled": settings.external_enabled,
        "agents": [a.to_dict() for a in ALL_AGENTS.values()],
        "note": "Local runtime is the production path; external adapter is opt-in.",
    }


@app.get("/api/v1/ai/metrics")
async def metrics(_: bool = Depends(require_auth)):
    core = _require_core()
    m = dict(core.metrics)
    reqs = max(1, m["requests"])
    m["average_latency_ms"] = m["total_latency_ms"] // reqs
    return m


@app.post("/api/v1/ai/chat")
async def chat(req: ChatRequest, _: bool = Depends(require_auth)):
    core = _require_core()
    answer = await core.answer(
        req.question, session_id=req.session_id, force_json=req.json_output
    )
    return answer.to_dict()


@app.post("/api/v1/ai/analyze/news")
async def analyze_news(req: AnalyzeNewsRequest, _: bool = Depends(require_auth)):
    core = _require_core()
    question = (
        f"حلّل الخبر التالي وأثره المحتمل على الذهب XAUUSD، مع فصل الحقائق عن الاستنتاجات: "
        f"{req.headline}"
    )
    untrusted = f"{req.headline}\n{req.body}".strip()
    # Each article is analysed in its own session. A shared session let the
    # model echo the previous article's answer, which is exactly the kind of
    # cross-contamination that makes a "summary" untrustworthy.
    import hashlib

    session_id = "news-" + hashlib.sha256(
        (req.headline + req.source).encode()
    ).hexdigest()[:12]
    answer = await core.answer(
        question,
        session_id=session_id,
        force_json=True,
        untrusted_text=untrusted,
        untrusted_label=req.source or "news",
    )
    answer.evidence.append(
        {
            "kind": "article_meta",
            "label": "بيانات الخبر",
            "source": req.source or "unknown",
            "timestamp": req.published_at,
            "values": {"headline": req.headline},
        }
    )
    return answer.to_dict()


@app.post("/api/v1/ai/explain/opportunity")
async def explain_opportunity(req: ExplainRequest, _: bool = Depends(require_auth)):
    core = _require_core()
    question = (
        "اشرح الفرصة المنشورة الحالية وأسبابها ومستوياتها كما وردت من المنسّق المركزي، "
        "دون تغيير أي مستوى."
    )
    if req.question:
        question += f" سؤال إضافي: {req.question}"
    answer = await core.answer(question, session_id="opportunity")
    return answer.to_dict()


@app.post("/api/v1/ai/research")
async def research(req: ResearchRequest, _: bool = Depends(require_auth)):
    core = _require_core()
    answer = await core.answer(req.question, session_id="research")
    return answer.to_dict()
