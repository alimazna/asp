# Audit Report — T29 Part 2b + T13 evidential re-audit

- **Auditor:** Agent-D
- **Date:** 2026-10-08 10:25 UTC
- **Repo HEAD audited:** `91f3cad`
- **Verdict:** **T13 evidential = 96/96 (PASS) — reproduced in two independent
  checkouts. T27 POC numbers reproduced exactly. Two harness/packaging defects found
  (port leak, corpus mixing). Decision-grade T27 not yet available to audit.**

---

## 1. T13 evidential path — 96/96 (PASS)

At HEAD, with a **clean process table**, `T13_REAL_DATA=1` gives **96/96**. I
reproduced this in my long-lived workspace *and* in a fresh `git clone` of `main` +
fresh cmake build. D1 and D2 are both closed; the RULE C gate stays shut
(`probability=null`, `score_is_probability=false`) while the bridge reaches real gold
and M15 is decision-grade.

**Discrepancy explained.** An earlier run of mine gave 91/96 with 5 timeframes
(M1/M5/M15) reporting `has_closed_bar=false`, `sequence=0`, plus the host DEGRADED.
Root cause was **not a product defect** — it was a **leaked bridge process**.

## 2. DEFECT (harness) — real-data run leaks `bridge_service.py` on fixed port 8791

- `bridge_service.py` binds a **fixed** port (`DEFAULT_PORT = 8791`); the host's
  `StartupCoordinator` also hard-codes `--port 8791` (`src/platform/windows/`).
- `test_e2e_real_host_t13.py::run_real_data_checks` terminates the host `proc` but
  **does not reap the host-spawned bridge** (`build/resources/bridge/mt5_python/
  bridge_service.py`). The default (no-data) run does **not** leak; the real-data run
  leaves exactly one bridge behind.
- **Reproduced:** run the real-data path twice with no cleanup → the second run's
  bridge fails `OSError: [Errno 98] Address already in use`, the host keeps the stale
  DEGRADED bridge, and the harness **false-fails to 91/96** on 5 real-feed checks.
  Kill the stray → back to 96/96.

**Impact.** Any audit (or CI) that runs the real-data suite more than once in a
container will see a spurious 91/96 unless the stray is killed first. This can
**mask a genuine regression** (a real 91/96 looks like the known leak). Recommend the
harness kill the bridge it caused (by port/cmdline) in its `finally`, or the bridge
support an ephemeral port.

## 3. T27 POC (Agent-B) — reproduced exactly

Ran `-m src.models.realdata --partition-mode fraction --l2 0.01 --wf-train 300
--wf-test 100` on the MT5 corpus. Every published number reproduces:

| Quantity | Agent-B claim | My reproduction |
|---|---|---|
| dev/val/oos | 3997 / 1333 / 1339 | **same** |
| OOS calibrated brier / ECE / MCE | 0.2499 / 0.0017 / 0.0017 | **0.249946 / 0.001699 / 0.001699** |
| OOS raw brier / ECE | 0.2915 / 0.1774 | **0.291541 / 0.177353** |
| WF folds / pooled n | 63 / 6300 | **same** (non-overlapping) |
| pooled brier / ECE / MCE / acc | 0.2546 / 0.0489 / 0.36 / 0.4992 | **same** |
| low/high tier coverage | 0 | **0** |

**Honest framing confirmed.** `brier_skill ≈ 0.0002`, accuracy ≈ coin flip, low/high
tiers zero-coverage, pooled `MCE 0.36 ≫ ECE` — Agent-B flagged these as weaknesses,
not hidden. No overclaim. The `--l2 0.01` knob is non-default *only* to make IRLS
converge (default `1e-6` untouched); OOS is touched once, no tuning — defensible.
Split is causal/disjoint (`splits.py::fractional_split` + `assert_causal`), sound.

## 4. DEFECT (packaging, minor) — `load_corpus(dir)` mixes both corpora

`_iter_json_files` now globs `*.json` **and** `*.json.gz` under a directory. The one
committed corpus dir therefore loads **119,753 = 6,670 (MT5) + 113,083 (Dukascopy)**
FeatureSets when passed as a directory. The two corpora are different providers,
price types, and eras. `main` now accepts a **file**, so the correct invocation is
safe — but the **default** (directory) path silently concatenates them. Recommend the
default resolve one explicit corpus, or the dir reject mixed corpora.

## 5. Corpus counts verified

`load_corpus` per file: Dukascopy **113,083** examples, MT5 **6,670** — both match
`corpus/README.md`. (Dukascopy row/checksum integrity: see `AUDIT-T29-dukascopy-corpus.md`.)

## 6. Not yet auditable

**Decision-grade T27** (Dukascopy 2021-2025, year partition, `--partition-mode year`,
113,083 sets) is **in flight** per Agent-B's 09:20 comm; no report at HEAD. The
`FINAL_REPORT` T27 section is still **PENDING**. I cannot audit numbers that have not
landed; queue T29 Part 2c for when they do. The 3.5-month MT5 numbers above are
**POC / cross-check**, not the publication verdict.

## 7. Environment anomalies (not product defects)

Disk was at 100% (107 GB volume; the `.json.gz` corpora + build output filled it),
which killed one repro run with empty output. Cleaned; noted for the record.

## Independence

Agent-D authored none of the corpus, feed, host, or T27 harness. All numbers
re-derived read-only; scratch only under `/tmp`. The port-leak defect was reproduced
twice, in two SEPARATE clones, and confirmed by killing the stray (91/96 → 96/96).

<!-- AI agent (OpenHands/agent-d) on behalf of the operator -->
