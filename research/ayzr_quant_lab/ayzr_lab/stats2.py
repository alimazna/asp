"""Advanced statistical validation for strategy research.

The Phase I report used a plain iid bootstrap, which understates uncertainty when
trade outcomes are serially dependent. This module adds the tools an honest
verdict needs:

  stationary_bootstrap_ci  Politis–Romano resampling with geometric block lengths
  deflated_sharpe          Bailey & López de Prado (2014) — Sharpe corrected for
                           how many trials were run and for skew/kurtosis
  pbo_cscv                 Probability of Backtest Overfitting via combinatorially
                           symmetric cross-validation (Bailey et al. 2015)
  holm_bonferroni          multiple-testing correction over a family of trials

All functions take plain Python lists of per-trade returns (R multiples) or a
matrix of trial return series, and use only the standard library so they run
anywhere.
"""

from __future__ import annotations

import math
import random
from typing import Dict, List, Optional, Sequence, Tuple

EULER_MASCHERONI = 0.5772156649015329


def _mean(xs: Sequence[float]) -> float:
    return sum(xs) / len(xs)


def _std(xs: Sequence[float], ddof: int = 1) -> float:
    m = _mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - ddof)) if len(xs) > ddof else 0.0


def _skew(xs: Sequence[float]) -> float:
    n = len(xs)
    if n < 3:
        return 0.0
    m, s = _mean(xs), _std(xs)
    if s == 0:
        return 0.0
    return sum(((x - m) / s) ** 3 for x in xs) * n / ((n - 1) * (n - 2))


def _kurtosis(xs: Sequence[float]) -> float:
    n = len(xs)
    if n < 4:
        return 3.0
    m, s = _mean(xs), _std(xs)
    if s == 0:
        return 3.0
    k = sum(((x - m) / s) ** 4 for x in xs) / n
    return k


def _norm_cdf(z: float) -> float:
    return 0.5 * (1.0 + math.erf(z / math.sqrt(2.0)))


def _norm_ppf(p: float) -> float:
    # Acklam's rational approximation; accurate to ~1e-9.
    if p <= 0.0:
        return -math.inf
    if p >= 1.0:
        return math.inf
    a = [-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
         1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00]
    b = [-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
         6.680131188771972e+01, -1.328068155288572e+01]
    c = [-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
         -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00]
    d = [7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
         3.754408661907416e+00]
    plow, phigh = 0.02425, 1 - 0.02425
    if p < plow:
        q = math.sqrt(-2 * math.log(p))
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1)
    if p > phigh:
        q = math.sqrt(-2 * math.log(1 - p))
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) / ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1)
    q = p - 0.5
    r = q * q
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q / (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1)


# --------------------------------------------------------------------------
def stationary_bootstrap_ci(rs: Sequence[float], iters: int = 5000,
                            seed: int = 7, alpha: float = 0.05,
                            mean_block: float = 10.0) -> Dict[str, float]:
    """Politis–Romano stationary bootstrap CI on the mean.

    Resamples the series in geometric-length blocks, preserving short-range
    dependence that a plain iid bootstrap destroys.
    """
    n = len(rs)
    if n < 10:
        return {}
    rnd = random.Random(seed)
    p = 1.0 / max(2.0, mean_block)
    means = []
    for _ in range(iters):
        s = 0.0
        i = rnd.randrange(n)
        count = 0
        while count < n:
            s += rs[i]
            count += 1
            if rnd.random() < p:
                i = rnd.randrange(n)
            else:
                i = (i + 1) % n
        means.append(s / n)
    means.sort()
    lo = means[int(iters * (alpha / 2))]
    hi = means[int(iters * (1 - alpha / 2))]
    return {"mean": _mean(rs), "ci_low": lo, "ci_high": hi,
            "crosses_zero": lo <= 0.0 <= hi}


def deflated_sharpe(rs: Sequence[float], n_trials: int,
                    sr_benchmark_variance: Optional[float] = None) -> Dict[str, float]:
    """Bailey & López de Prado deflated Sharpe ratio.

    Returns the raw (annualisation-free) Sharpe, the expected maximum Sharpe
    under the null of ``n_trials`` independent trials, and the probability that
    the observed Sharpe is genuinely positive after that adjustment.
    """
    n = len(rs)
    if n < 20:
        return {}
    m = _mean(rs)
    s = _std(rs)
    if s == 0:
        return {}
    sr = m / s  # per-trade Sharpe
    g3, g4 = _skew(rs), _kurtosis(rs)

    # Expected max Sharpe under the null across n_trials (Bailey & LdP 2014).
    if n_trials <= 1:
        sr0 = 0.0
    elif sr_benchmark_variance is None:
        # Use the variance of trial Sharpes' scale proxy = 1/n as a neutral default.
        v = 1.0 / n
        e = math.e
        sr0 = math.sqrt(v) * ((1 - EULER_MASCHERONI) * _norm_ppf(1 - 1.0 / n_trials)
                              + EULER_MASCHERONI * _norm_ppf(1 - 1.0 / (n_trials * e)))
    else:
        e = math.e
        sr0 = math.sqrt(sr_benchmark_variance) * (
            (1 - EULER_MASCHERONI) * _norm_ppf(1 - 1.0 / n_trials)
            + EULER_MASCHERONI * _norm_ppf(1 - 1.0 / (n_trials * e)))
    denom = math.sqrt(max(1e-12, 1 - g3 * sr + (g4 - 1) / 4.0 * sr * sr))
    z = (sr - sr0) * math.sqrt(n - 1) / denom
    return {"sharpe_per_trade": sr, "expected_max_sharpe_null": sr0,
            "deflated_sharpe_prob": _norm_cdf(z), "skew": g3, "kurtosis": g4,
            "n_trials": n_trials}


def pbo_cscv(trial_returns: List[List[float]], n_splits: int = 10,
             max_combos: int = 256, seed: int = 11) -> Dict[str, float]:
    """Probability of Backtest Overfitting (Bailey et al. 2015, CSCV).

    ``trial_returns`` is a list of equal-length per-trade return series (one per
    configuration). We split the (time-ordered) trades into S blocks, form
    combinations of S/2 blocks as in-sample and the rest as out-of-sample, pick
    the IS-best trial each time, and measure how often it lands below the median
    OOS. A high PBO means the "best" configuration is likely an artefact of
    selection.
    """
    T = len(trial_returns)
    if T < 4:
        return {}
    L = min(len(r) for r in trial_returns)
    if L < n_splits * 2:
        return {}
    trimmed = [r[:L] for r in trial_returns]

    def sharpe(seq):
        s = _std(seq)
        return (_mean(seq) / s) if s else 0.0

    block_idxs = []
    size = L // n_splits
    for b in range(n_splits):
        block_idxs.append(list(range(b * size, (b + 1) * size)))

    import itertools
    combos = list(itertools.combinations(range(n_splits), n_splits // 2))
    if len(combos) > max_combos:
        rnd = random.Random(seed)
        combos = rnd.sample(combos, max_combos)

    logits = []
    for is_blocks in combos:
        oos_blocks = [b for b in range(n_splits) if b not in is_blocks]
        is_idx = [i for b in is_blocks for i in block_idxs[b]]
        oos_idx = [i for b in oos_blocks for i in block_idxs[b]]
        is_perf = [sharpe([r[i] for i in is_idx]) for r in trimmed]
        oos_perf = [sharpe([r[i] for i in oos_idx]) for r in trimmed]
        best = max(range(len(trimmed)), key=lambda k: is_perf[k])
        # relative rank of the IS-best among OOS performances
        rank = sum(1 for x in oos_perf if x < oos_perf[best]) / (len(trimmed) - 1)
        rank = min(max(rank, 1e-6), 1 - 1e-6)
        logits.append(math.log(rank / (1 - rank)))
    below = sum(1 for x in logits if x <= 0)
    return {"pbo": below / len(logits), "n_combos": len(logits),
            "median_oos_logit": sorted(logits)[len(logits) // 2]}


def holm_bonferroni(pvalues: Sequence[float], alpha: float = 0.05) -> List[bool]:
    """Holm step-down multiple-testing correction. Returns accept/reject flags."""
    m = len(pvalues)
    order = sorted(range(m), key=lambda i: pvalues[i])
    reject = [False] * m
    for rank, idx in enumerate(order):
        thresh = alpha / (m - rank)
        if pvalues[idx] <= thresh:
            reject[idx] = True
        else:
            break
    return reject


def t_stat_pvalue(rs: Sequence[float]) -> float:
    """One-sided p-value that mean(R) > 0 (iid t approximation)."""
    n = len(rs)
    if n < 3:
        return 1.0
    s = _std(rs)
    if s == 0:
        return 1.0
    t = _mean(rs) / (s / math.sqrt(n))
    return 1.0 - _norm_cdf(t)
