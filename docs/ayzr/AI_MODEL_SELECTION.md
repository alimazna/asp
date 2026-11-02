# AYZR Native AI Core — design and measured behaviour

This document records what the Native AI Core is, what it does, and — most
importantly — what was **measured** rather than assumed. Every quality claim
below is backed by an actual command and its output.

## 1. What it is

Our own AI service, living in `services/ai-core/`. It owns:

- the model runtime (llama.cpp `llama-server`, CPU-only),
- a provider abstraction so the model can be swapped by config,
- a read-only tool layer over the AYZR backend,
- a grounding layer that refuses market questions without data,
- an HTTP API at `/api/v1/ai/*`.

It is **not** a newly trained foundation model. It serves open-weights GGUF
models and constrains them.

## 2. Why we own the runtime

The decisive technical finding of this work: **because we own the runtime, we can
constrain the decoder with a GBNF grammar.** An external OpenAI-compatible
provider cannot do this. That single capability removes the worst defect we
measured.

## 3. Measured model behaviour

### 3.1 Qwen2.5-1.5B-Instruct Q4_K_M (selected)

| Probe | Result |
|---|---|
| Arabic generation, CPU | ~27 tok/s |
| Financial reasoning, "rate cut → gold?" | **correct** (bullish) |
| Guessing a price with no data | **refused** — returned `null`, `no_trade` |
| Wrapping JSON in fences when told not to | yes → handled by `extract_json_object` |
| Mixed-language leak | `"البنك المركزي يsignal خفض الفائدة"` → **caught, then eliminated by grammar** |
| With Arabic-only GBNF grammar | **pure Arabic, zero warnings** |

### 3.2 Qwen2.5-3B-Instruct Q4_K_M (rejected)

| Probe | Result |
|---|---|
| Arabic generation, CPU | ~14.5 tok/s (**half** the 1.5B) |
| Financial reasoning, "rate cut → gold?" | **wrong and self-contradictory** |
| Foreign-script leak | leaked CJK into Arabic: `"الذهب 资产"` |

The 3B model is slower *and* less correct on the reasoning probe. **1.5B is
selected**; 3B is downloaded but not used. This is a measured decision, not a
preference.

## 4. The Arabic-only grammar

`ayzr_ai/grammars.py` defines a GBNF grammar whose string values admit Arabic
letters, spaces, digits and punctuation, and nothing else. The model cannot emit
a Latin letter inside those values because the decoder will not allow it.

Verified live:

```
Before grammar: "البنك المركزي يsignal خفض الفائدة"      status: ok + warning
After  grammar: "البنك المركزي الأمريكي يعلن مخاوف لخفض الفائدة بسبب تقلص التضخم"
                                                          status: ok, warnings: []
```

## 5. Grounding rules (enforced outside the model)

- A `market` question with no candle evidence returns `insufficient_data` and
  the model is **never called**.
- Any numeric level the model returns that is not present in the retrieved data
  is stripped and reported.
- An empty Arabic summary becomes `invalid_output`, never a silent `ok`.

## 6. Provider failure taxonomy

`ProviderOutcome`: `ok`, `unavailable`, `timeout`, `auth_failure`,
`rate_limited`, `quota_exhausted`, `bad_request`, `server_error`,
`invalid_output`. Only `timeout`, `rate_limited` and `server_error` are marked
retryable. The external adapter is **off by default** and refuses to build
without an explicit opt-in plus a token, so an accidental deploy cannot spend
money.

## 7. Honest limitations

- The local model is a 1.5B model. It is adequate for summarisation and script
  compliance; it is **not** a frontier reasoner. Its market direction on news is
  a plausible reading, not a validated edge, and the UI must not present it as
  one.
- No outcome data exists, so nothing here is claimed to be profitable.
- Tests run against the mock provider. The live-model checks above were run
  manually and their outputs are quoted verbatim.

## 8. Test totals

```
services/ai-core:  50 passed
```

Run: `cd services/ai-core && python -m pytest tests/ -q`
