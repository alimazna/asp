"""ASTRA Collector - independent closed-candle collector and sender.

Reads closed candles from a MetaTrader 5 terminal on nine timeframes, gives each
candle a deterministic unique id, stores them locally in one durable file, and
delivers only what the receiving endpoint has not confirmed - in strict
chronological order, never twice.

This package is standalone: it does not import from the ASTRA backend, and the
receiving endpoint is a setting rather than a compiled-in constant.
"""

__version__ = "1.0.0"

__all__ = ["__version__"]
