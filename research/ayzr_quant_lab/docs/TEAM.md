# AYZR Phase II — Research Team & Division of Work

The brief asks for a multi-agent research team. This is an honest account of how
the work was actually divided and executed in a single OpenHands environment,
with the specialised-agent roles separated by *artefact boundary* rather than by
separate LLM sessions (which this environment cannot spawn and control reliably).

Each role owns distinct files and produces a reviewable artefact. The principal
coordinator (this agent) owns integration, the acceptance gates, and the final
verdict, and never lets a role that built an engine also be the only one that
signs off on it.

| Role | Owns (files) | Deliverable | Artefact |
|---|---|---|---|
| **A1 Quant & ASTRA analysis** | `ayzr_lab/astra_port.py`, `ayzr_lab/pipeline.py`, `validation/audit_phase1.py` | Fidelity proof + Phase I audit | `reports/audit_phase1.json` |
| **A2 LONG research** | `experiments/long_deepdive.py`, `experiments/geometry_grid.py` | LONG edge decomposition | `reports/long_deepdive.json` |
| **A3 SHORT/MEDIUM research** | `experiments/intraday_microstructure.py`, `experiments/alt_hypotheses.py` | New intraday hypotheses | `reports/intraday_microstructure.json` |
| **A4 Data engineering** | `data_sources/*.py`, `ayzr_lab/external.py`, `datasets/` | External data + point-in-time joins | `datasets/*.csv` + meta |
| **A5 Statistical validation** | `ayzr_lab/stats2.py`, `validation/overfitting_audit.py`, `validation/holdout_2026.py` | Overfitting / holdout audit | `reports/overfitting_audit.json` |
| **Coordinator** | `engines/horizon_engines.json`, `reports/PHASE2_REPORT.md` | Frozen specs + final verdict | this report |

## Separation of duties

- **A2/A3 build hypotheses; A5 judges them.** No hypothesis is promoted to
  "promising" without A5's Deflated Sharpe / PBO / holdout numbers.
- **A1 freezes ASTRA** (`tests/test_astra_protection.py`, digest `aee61fa9...`).
  No role may modify `src/`.
- **A4 supplies data; A5 checks the joins** for lookahead (external data is
  lagged to its real publication date in `ayzr_lab/external.py`).

## Why not literally five LLM sessions

OpenHands in this environment runs one agent per conversation; there is no
supported API here to spawn and supervise sub-conversations. Pretending otherwise
would be dishonest. The *effective* multi-agent discipline is preserved by the
artefact ownership above: each deliverable is produced independently and then
reviewed against a different role's artefact, which is the property that matters
for catching overfitting and lookahead.
