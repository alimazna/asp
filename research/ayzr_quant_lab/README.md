# AYZR Quant Lab

An offline research environment for the AYZR SHORT / MEDIUM / LONG XAUUSD
engines. It runs entirely against the **real** Dukascopy M1 corpus and **never
modifies ASTRA CORE**. Everything here is reproducible from the committed corpus
plus the scripts in this directory.

## Why it exists

The three horizon engines (`src/ayzr/`) wrap ASTRA's `SignalEngine` with
different thresholds, but nothing had ever measured whether that produces an
*edge*. This lab builds the honest measurement rig: a faithful ASTRA port, a
realistic backtester, and a walk-forward / out-of-sample harness — then uses it
to decide which horizon ideas survive contact with 1.7M real bars.

## Layout

```
ayzr_lab/
  astra_port.py   faithful scalar port of ASTRA's feature/structure/regime/
                  eligibility/signal math (the reference implementation)
  data.py         corpus loading, UTC calendar resampling, BID/ASK spread
  pipeline.py     vectorized features + decision extraction (fast path)
  backtest.py     realistic simulator (next-bar fill, spread, slippage,
                  TP/SL ordering, worst-case ambiguous bars, MAE/MFE, R)
  stats.py        metrics, walk-forward splits, bootstrap CI, sensitivity
  engines.py      horizon specs + composable research hypotheses
  runner.py       end-to-end engine runner
experiments/      baseline, hypothesis sweep, geometry grid, alt hypotheses,
                  final validation
reports/          JSON results + RESEARCH_REPORT.md + frozen ASTRA digest
tests/            fidelity + pipeline + backtest + ASTRA-protection gates
tools/            oracle_dump.cpp (the C++ ASTRA oracle) + built binary
```

## Setup

```bash
python3 -m pip install numpy pandas          # only external deps
g++ -std=c++17 -O2 -I ../../../src \
    tools/oracle_dump.cpp ../../../build-ayzr/libaura_core.a -o tools/oracle_dump
```

`build-ayzr/libaura_core.a` is produced by the normal project build
(`cmake -S . -B build-ayzr && cmake --build build-ayzr`).

## Verify the lab is faithful (run these first)

```bash
python3 tests/test_astra_port.py        # Python port == C++ oracle (1050 checks)
python3 tests/test_pipeline.py          # fast path == scalar reference
python3 tests/test_backtest.py          # no-lookahead, TP/SL order, frictions
python3 tests/test_astra_protection.py  # ASTRA CORE unchanged (frozen digest)
```

The first test compiles/links the real ASTRA engines and diffs every numeric
field against the Python port. If it fails, the lab's math has drifted from ASTRA
and must not be used.

## Run the research

```bash
python3 experiments/baseline_astra_core.py        # ASTRA CORE reference
python3 experiments/hypothesis_sweep.py           # single-filter marginal edge
python3 experiments/geometry_grid.py              # stop x target grid
python3 experiments/alt_hypotheses.py             # MTF / mean-revert / regime
python3 experiments/final_validation.py           # confluence + LONG deep-dive
```

Results land in `reports/*.json`. The synthesis is in
`reports/RESEARCH_REPORT.md`.

## Guardrails

- ASTRA CORE is read-only. `tests/test_astra_protection.py` freezes its oracle
  output; any drift fails the gate.
- No lookahead: decisions use only closed bars; fills happen on the next bar.
- Friction is real: spread from the corpus, adverse slippage on market exits.
- Ambiguous bars default to worst-case (stop first) and are flagged.
- Every headline number is train vs out-of-sample; win rate is never the headline.
