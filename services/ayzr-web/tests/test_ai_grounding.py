"""The model must not state a number it was never given.

The prompt forbids invented figures, but the model broke that rule in
production: it converted the 2.0 risk/reward ratio into "a 10% loss". A prompt
instruction is not enforcement, so the output is checked against the input.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import pytest
from ayzr_web.ai import _ungrounded_numbers, _norm_number, _numbers_in

FACTS = {
    "الدخول": 3954.79,
    "وقف الخسارة": 3945.18,
    "الهدف": 3974.02,
    "نسبة المخاطرة للمكافأة": 2.0,
    "مؤشر القوة النسبية 14": 56.9,
    "الاتجاه المستنتج": "bullish",
}


def test_normalisation_keeps_integer_magnitude():
    """10, 100 and 1000 must not collapse to the same token."""
    assert _norm_number("10") == "10"
    assert _norm_number("100") == "100"
    assert _norm_number("1000") == "1000"


def test_normalisation_trims_only_fractional_zeros():
    assert _norm_number("2.0") == "2"
    assert _norm_number("2.50") == "2.5"
    assert _norm_number("3,954.79") == "3954.79"


def test_invented_percentage_is_caught():
    """The exact production defect."""
    bad = {"summary_ar": "الخسارة المحتملة تقدر بنحو 10% من المخزون.",
           "risk_ar": "", "watch_ar": ""}
    assert "10" in _ungrounded_numbers(bad, FACTS)


def test_supplied_levels_are_accepted():
    good = {"summary_ar": "وقف الخسارة عند 3945.18 والهدف 3974.02.",
            "risk_ar": "نسبة المخاطرة للمكافأة 2.0.", "watch_ar": ""}
    assert _ungrounded_numbers(good, FACTS) == []


def test_a_supplied_one_does_not_license_an_invented_hundred():
    facts = {"عدد الإشارات المتوافقة": 1}
    bad = {"summary_ar": "احتمال النجاح 100%.", "risk_ar": "", "watch_ar": ""}
    assert "100" in _ungrounded_numbers(bad, facts)


def test_text_without_numbers_is_clean():
    ok = {"summary_ar": "الاتجاه صاعد والمؤشرات متوافقة.",
          "risk_ar": "المعلومات الناقصة واضحة.", "watch_ar": "راقب المقاومة."}
    assert _ungrounded_numbers(ok, FACTS) == []


def test_numbers_inside_supplied_text_are_allowed():
    facts = {"الإطار الزمني": "M15", "ملاحظة": "المتوسط 20"}
    ok = {"summary_ar": "على الإطار 15 نراقب المتوسط 20.", "risk_ar": "", "watch_ar": ""}
    assert _ungrounded_numbers(ok, facts) == []


def test_numbers_inside_fact_names_are_grounded():
    """Indicator names carry period lengths the model may legitimately cite."""
    facts = {"مؤشر القوة النسبية 14": 55.7, "المتوسط السريع 20": 3950.4}
    ok = {"summary_ar": "مؤشر القوة النسبية 14 عند 55.7 فوق المتوسط 20.",
          "risk_ar": "", "watch_ar": ""}
    assert _ungrounded_numbers(ok, facts) == []


def test_fact_names_do_not_license_unrelated_numbers():
    facts = {"مؤشر القوة النسبية 14": 55.7}
    bad = {"summary_ar": "احتمال الربح 99%.", "risk_ar": "", "watch_ar": ""}
    assert "99" in _ungrounded_numbers(bad, facts)


def test_a_rounded_restatement_is_accepted():
    """The model may quote 55.66 as 55.7; that is precision, not invention."""
    facts = {"مؤشر القوة النسبية 14": 55.66}
    ok = {"summary_ar": "المؤشر عند 55.7.", "risk_ar": "", "watch_ar": ""}
    assert _ungrounded_numbers(ok, facts) == []


def test_rounding_tolerance_does_not_license_any_number():
    """Rounding must not become a loophole for fabricated figures."""
    facts = {"مؤشر القوة النسبية 14": 55.66}
    for claim in ("خسارة 10%", "ربح 99%", "احتمال 75%"):
        bad = {"summary_ar": claim, "risk_ar": "", "watch_ar": ""}
        assert _ungrounded_numbers(bad, facts), claim


def test_rounding_matches_at_the_written_precision_only():
    facts = {"السعر": 3954.79}
    ok = {"summary_ar": "السعر 3955.", "risk_ar": "", "watch_ar": ""}
    assert _ungrounded_numbers(ok, facts) == []
    bad = {"summary_ar": "السعر 3900.", "risk_ar": "", "watch_ar": ""}
    assert _ungrounded_numbers(bad, facts) == ["3900"]


def test_extraction_handles_decimal_and_comma_forms():
    assert _numbers_in("السعر 3,954.79 تقريباً") == {"3954.79"}
    assert _numbers_in("لا أرقام هنا") == set()
