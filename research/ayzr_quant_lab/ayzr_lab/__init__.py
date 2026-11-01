"""AYZR Quant Lab — research environment for the multi-horizon XAUUSD engines.

This package never imports or mutates ASTRA CORE. It *replicates* ASTRA's
documented math in Python so the research team can compute features, run
backtests and validate hypotheses over the real Dukascopy corpus. The port is
proven faithful against a compiled C++ oracle (see ``tools/oracle_dump.cpp``
and ``tests/test_astra_port.py``).

Layout
------
astra_port.py   faithful scalar port of ASTRA's feature/structure/regime/
                eligibility/signal math (the reference implementation)
data.py         corpus loading, multi-timeframe resampling, alignment
pipeline.py     vectorized feature computation + decision extraction
backtest.py     realistic trade simulator (spread, slippage, TP/SL ordering)
stats.py        performance metrics + walk-forward / out-of-sample validation
engines.py      AYZR horizon logic + research hypotheses
"""

__version__ = "0.1.0"
