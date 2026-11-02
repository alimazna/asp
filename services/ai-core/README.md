# AYZR Native AI Core

Our own AI service. It owns the model runtime, the tool layer, the memory and
the HTTP API. It does **not** depend on LLM7 or any paid provider to function.

## Why this exists

The previous design sent prompts to an external provider. That left the whole
ecosystem hostage to one vendor's quota, and the free tier ran out mid-analysis.
This service inverts that: the model runs under our control, and external
providers are an optional adapter that is off by default.

## What it actually is, honestly

- A **model runtime** (llama.cpp `llama-server`) serving an open-weights GGUF
  model on CPU.
- A **provider abstraction** so the model behind it can be swapped by config.
- A **tool layer** of read-only calls into the AYZR backend.
- A **grounding layer** that refuses to answer market questions without data.
- An **HTTP API** owned by the project.

It is **not** a newly trained foundation model, and it is not claimed to match
frontier models. Measured behaviour of the local models is in
`docs/ayzr/AI_MODEL_SELECTION.md`, including the cases where they are wrong.

## Layout

```
services/ai-core/
  ayzr_ai/
    config.py        settings, all from environment
    providers/       base interface, local llama, mock, external adapter
    tools/           read-only AYZR tools with schema + timeout + allowlist
    agents/          specialist agents + a routing general assistant
    memory/          knowledge base, retrieval, session context
    grounding.py     citation + refusal rules
    json_repair.py   defensive structured-output parsing
    security.py      auth, rate limit, prompt-injection guard
    main.py          FastAPI app: /api/v1/ai/*
  tests/             pytest suite, mock provider by default
```

## Run

```bash
# 1. model runtime (separate process, separate failure domain)
llama-server -m models/qwen2.5-1.5b-instruct-q4_k_m.gguf \
  --host 127.0.0.1 --port 8080 -c 4096 -t 4

# 2. the AI service
cd services/ai-core
pip install -r requirements.txt
AYZR_AI_TOKEN=<token> AYZR_BACKEND_URL=http://127.0.0.1:8081 \
  python -m uvicorn ayzr_ai.main:app --host 127.0.0.1 --port 8090
```

The runtime and the service are separate on purpose: if the model dies, the
service reports `unavailable` and the rest of AYZR keeps running.

## Test

```bash
cd services/ai-core && python -m pytest tests/ -v
```

Tests use the mock provider, so they never touch a live model or a paid API.
