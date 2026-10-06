# &#x20;Python MetaTrader5 — دليل كامل للأرشيف

---

## 📌 الجزء 1: شو هي الطريقة؟

**Python MetaTrader5** = مكتبة رسمية من MetaQuotes تسمح لـ Python بقراءة البيانات (شمعات، أسعار لحظية، حساب) من MT5 Terminal المثبت على جهازك.

**المبدأ الأساسي:**

text

```
Valetax (سيرفر الوسيط)
      ↓ إنترنت
MT5 Terminal (على جهازك)
      ↓ IPC محلي (Named Pipes)
Python + MetaTrader5 library
      ↓ كتابة ملفات
ملفات JSON
      ↓ قراءة
AURA / أي تطبيق
```

svgsvg

**الميزة:** نفس سعر Valetax **بالضبط** — لأنه يقرأ من نفس MT5 اللي إنت شايفه.

---

## 📌 الجزء 2: المتطلبات

| المتطلب                 | التفاصيل                          |
| ----------------------- | --------------------------------- |
| **نظام التشغيل**        | Windows 10/11                     |
| **Python**              | 3.8 – 3.11 (⚠️ **ما يدعم 3.12+**) |
| **MT5 Terminal**        | مثبّت ومفتوح + مسجل دخول          |
| **MetaTrader5 library** | `pip install MetaTrader5`         |
| **numpy**               | يُثبّت تلقائياً مع المكتبة        |
| **إنترنت**              | للـMT5 فقط (Python ما يحتاج)      |

**ملاحظة مهمة:** MT5 **لازم يكون مفتوح** وقت تشغيل السكربت. لأنه Python يقرأ من ذاكرته.

---

## 📌 الجزء 3: التثبيت (مرة واحدة)

### الخطوة 1: تأكد من Python

cmd

```
py -3.11 --version
```

svgsvg

**المتوقع:** `Python 3.11.7`

### الخطوة 2: ثبّت pip (إذا مو موجود)

cmd

```
py -3.11 -m ensurepip --upgrade
```

svgsvg

### الخطوة 3: ثبّت المكتبات

cmd

```
py -3.11 -m pip install MetaTrader5
```

svgsvg

**ملاحظة:** إذا فشل بسبب بطء الشبكة:

cmd

```
py -3.11 -m pip install MetaTrader5 --timeout 600 --retries 20
```

svgsvg

### الخطوة 4: تأكد من التثبيت

cmd

```
py -3.11 -c "import MetaTrader5; print(MetaTrader5.__version__)"
```

svgsvg

**المتوقع:** `5.0.6231` (أو أحدث)

---

## 📌 الجزء 4: السكربت الأساسي (نسخة كاملة)

**احفظ هذا الملف باسم `read_candles.py`:**

python

```
"""
AURA MT5 Bridge — Candle Reader
================================
يقرأ شمعات XAUUSD المغلقة من MT5 ويحفظها JSON.

Requirements:
    Python 3.8-3.11 + MetaTrader5 + numpy
    MT5 Terminal مفتوح ومسجل دخول

Usage:
    py -3.11 read_candles.py
"""

import json
import os
import sys
import time
from datetime import datetime, timezone

import MetaTrader5 as mt5

# ============================================================
# الإعدادات
# ============================================================

# الفريمات (Label, MT5 constant)
TIMEFRAMES = [
    ("M1",  mt5.TIMEFRAME_M1),
    ("M5",  mt5.TIMEFRAME_M5),
    ("M15", mt5.TIMEFRAME_M15),
    ("M30", mt5.TIMEFRAME_M30),
    ("H1",  mt5.TIMEFRAME_H1),
    ("H4",  mt5.TIMEFRAME_H4),
    ("D1",  mt5.TIMEFRAME_D1),
    ("W1",  mt5.TIMEFRAME_W1),
    ("MN1", mt5.TIMEFRAME_MN1),
]

CANDLES_PER_TF = 500  # عدد الشمعات المغلقة المطلوبة

OUTPUT_DIR = "out"    # مجلد الحفظ


# ============================================================
# دوال مساعدة
# ============================================================

def resolve_symbol() -> str:
    """
    يبحث عن رمز XAUUSD المتاح على الوسيط.
    يدعم الأسماء مثل: XAUUSD, XAUUSD.vx, XAUUSD.vcn
    """
    symbols = mt5.symbols_get()
    if symbols is None:
        return "XAUUSD"

    xau_names = [s.name for s in symbols if "XAU" in s.name.upper()]
    if not xau_names:
        return "XAUUSD"

    # الأولوية: XAUUSD بالضبط
    for name in xau_names:
        if name.upper() == "XAUUSD":
            return name

    # الثاني: XAUUSD.xxx
    for name in xau_names:
        if name.upper().startswith("XAUUSD"):
            return name

    # الثالث: أول واحد
    return xau_names[0]


def epoch_seconds(dt_tuple) -> int:
    """يحوّل struct_time من MT5 إلى Unix epoch seconds."""
    return int(dt_tuple)  # MT5 يرجع time كـ integer أصلاً


def write_json(path: str, data: dict) -> None:
    """يحفظ JSON مع إنشاء المجلد إذا لم يوجد."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)


# ============================================================
# الدوال الرئيسية
# ============================================================

def init_mt5() -> bool:
    """الاتصال بـMT5. يرجع True إذا نجح."""
    if not mt5.initialize():
        print(f"[ERROR] فشل الاتصال بـMT5: {mt5.last_error()}")
        return False
    return True


def read_timeframe(symbol: str, label: str, tf) -> dict | None:
    """
    يقرأ شمعات فريم واحد.
    start=1 لتخطي الشمعة الحالية (closed bars only).
    """
    rates = mt5.copy_rates_from_pos(symbol, tf, 1, CANDLES_PER_TF)
    if rates is None or len(rates) == 0:
        print(f"[WARN] ما في شمعات لـ {label}")
        return None

    candles = []
    for r in rates:
        candles.append({
            "time": int(r['time']),
            "open": float(r['open']),
            "high": float(r['high']),
            "low": float(r['low']),
            "close": float(r['close']),
            "tick_volume": int(r['tick_volume']),
            "spread": int(r['spread']),
            "real_volume": int(r['real_volume']),
        })

    return {"label": label, "candles": candles}


def main():
    print("=" * 60)
    print("AURA MT5 Bridge — Candle Reader")
    print("=" * 60)

    # 1. الاتصال
    if not init_mt5():
        sys.exit(1)

    print(f"[OK] متصل بـ MT5 — الإصدار {mt5.version()}")

    # 2. معلومات الحساب
    acc = mt5.account_info()
    if acc is None:
        print("[ERROR] ما قدرنا نقرأ معلومات الحساب")
        mt5.shutdown()
        sys.exit(2)

    print(f"[OK] الحساب: {acc.login}")
    print(f"[OK] الوسيط: {acc.company}")
    print(f"[OK] الرصيد: {acc.balance} {acc.currency}")

    # 3. تحديد الرمز
    symbol = resolve_symbol()
    print(f"[OK] الرمز: {symbol}")

    if not mt5.symbol_select(symbol, True):
        print(f"[ERROR] ما قدرنا نضيف {symbol}")
        mt5.shutdown()
        sys.exit(3)

    # 4. قراءة كل الفريمات
    print()
    print(f"{'الفريم':<6} {'العدد':<8} {'آخر شمعة':<20} {'آخر إغلاق':<12}")
    print("-" * 50)

    index_data = {
        "generated_at_utc": int(time.time()),
        "symbol": symbol,
        "broker": acc.company,
        "account_login": acc.login,
        "timeframes": {},
    }

    success_count = 0
    for label, tf in TIMEFRAMES:
        result = read_timeframe(symbol, label, tf)
        if result is None:
            index_data["timeframes"][label] = {
                "count": 0,
                "last_time": None,
                "last_close": None,
            }
            print(f"{label:<6} {'0':<8} {'-':<20} {'-':<12}")
            continue

        candles = result["candles"]
        last = candles[-1]

        # حفظ ملف الفريم
        file_path = os.path.join(OUTPUT_DIR, f"candles_{label}.json")
        write_json(file_path, {
            "symbol": symbol,
            "timeframe": label,
            "broker": acc.company,
            "account_login": acc.login,
            "generated_at_utc": int(time.time()),
            "count": len(candles),
            "candles": candles,
        })

        # إضافة للـindex
        index_data["timeframes"][label] = {
            "count": len(candles),
            "last_time": last["time"],
            "last_close": last["close"],
        }

        last_dt = datetime.fromtimestamp(last["time"], tz=timezone.utc)
        print(f"{label:<6} {len(candles):<8} {last_dt.strftime('%Y-%m-%d %H:%M'):<20} {last['close']:<12}")

        success_count += 1

    # 5. حفظ الـindex
    index_path = os.path.join(OUTPUT_DIR, "index.json")
    write_json(index_path, index_data)

    # 6. قطع الاتصال
    mt5.shutdown()

    print()
    print(f"[DONE] {success_count}/9 فريمات نجحت")
    print(f"[DONE] الملفات في: {os.path.abspath(OUTPUT_DIR)}")

    # exit code: 0 إذا M15 نجح، وإلا 1
    if "M15" in index_data["timeframes"] and index_data["timeframes"]["M15"]["count"] > 0:
        sys.exit(0)
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
```

svgsvg

---

## 📌 الجزء 5: سكربت الاختبار (connect_test.py)

python

```
"""اختبار سريع للاتصال بـMT5."""

import MetaTrader5 as mt5
from datetime import datetime, timezone

# 1. الاتصال
if not mt5.initialize():
    print(f"[ERROR] فشل الاتصال: {mt5.last_error()}")
    quit(1)

print("[OK] متصل")
print(f"     الإصدار: {mt5.version()}")

# 2. الحساب
acc = mt5.account_info()
print(f"     الحساب: {acc.login}")
print(f"     الوسيط: {acc.company}")
print(f"     الرصيد: {acc.balance} {acc.currency}")

# 3. رموز XAU
symbols = mt5.symbols_get()
xau = [s.name for s in symbols if "XAU" in s.name.upper()]
print(f"     رموز XAU: {xau[:8]}")

# 4. آخر 3 شمعات M15
symbol = "XAUUSD"
rates = mt5.copy_rates_from_pos(symbol, mt5.TIMEFRAME_M15, 1, 3)
if rates is not None and len(rates) > 0:
    print(f"\nآخر 3 شمعات M15 لـ {symbol}:")
    for r in rates:
        t = datetime.fromtimestamp(r['time'], tz=timezone.utc)
        print(f"  {t.strftime('%Y-%m-%d %H:%M')} | "
              f"O:{r['open']} H:{r['high']} L:{r['low']} C:{r['close']}")

# 5. السعر اللحظي
tick = mt5.symbol_info_tick(symbol)
if tick:
    print(f"\nالسعر الآن:")
    print(f"  Bid: {tick.bid}")
    print(f"  Ask: {tick.ask}")
    print(f"  Spread: {tick.ask - tick.bid:.2f}")

# 6. قطع الاتصال
mt5.shutdown()
print("\n[DONE]")
```

svgsvg

**الاستخدام:**

cmd

```
py -3.11 connect_test.py
```

svgsvg

---

## 📌 الجزء 6: هيكل الملفات

text

```
AURA/bridge/mt5_python/
├── connect_test.py      ← اختبار الاتصال
├── read_candles.py      ← السكربت الرئيسي (9 فريمات)
├── requirements.txt     ← MetaTrader5>=5.0.6231
├── README.md            ← شرح مختصر
└── out/                 ← المخرجات
    ├── candles_M1.json
    ├── candles_M5.json
    ├── candles_M15.json
    ├── candles_M30.json
    ├── candles_H1.json
    ├── candles_H4.json
    ├── candles_D1.json
    ├── candles_W1.json
    ├── candles_MN1.json
    └── index.json
```

svgsvg

---

## 📌 الجزء 7: شكل ملف JSON

**مثال `candles_M15.json`:**

json

```
{
  "symbol": "XAUUSD",
  "timeframe": "M15",
  "broker": "Valetax Global Limited",
  "account_login": 10012695259,
  "generated_at_utc": 1759756800,
  "count": 500,
  "candles": [
    {
      "time": 1759755600,
      "open": 4154.09,
      "high": 4160.67,
      "low": 4154.09,
      "close": 4160.12,
      "tick_volume": 2106,
      "spread": 8,
      "real_volume": 0
    }
  ]
}
```

svgsvg

**مثال `index.json`:**

json

```
{
  "generated_at_utc": 1759756800,
  "symbol": "XAUUSD",
  "broker": "Valetax Global Limited",
  "account_login": 10012695259,
  "timeframes": {
    "M1":  {"count": 500, "last_time": 1759755600, "last_close": 4160.12},
    "M5":  {"count": 500, "last_time": 1759755300, "last_close": 4160.05},
    "M15": {"count": 500, "last_time": 1759755600, "last_close": 4160.12},
    "...": "..."
  }
}
```

svgsvg

---

## 📌 الجزء 8: كيفية التشغيل

### يدوياً (كل مرة):

**1. افتح MT5** أولاً

**2. في CMD:**

cmd

```
cd C:\AURA\bridge\mt5_python
py -3.11 read_candles.py
```

svgsvg

**3. AURA يقرأ الملفات** من `out/`

---

### تلقائياً (Task Scheduler):

**1. اعمل ملف `update_data.bat`:**

bat

```
@echo off
cd /d C:\AURA\bridge\mt5_python
py -3.11 read_candles.py
echo Done.
```

svgsvg

**2. في Task Scheduler:**

- افتح **Task Scheduler**
- **Create Basic Task**
- **Trigger:** Daily / كل 5 دقائق
- **Action:** شغّل `update_data.bat`

---

## 📌 الجزء 9: قواعد مهمة

| القاعدة                         | السبب                                   |
| ------------------------------- | --------------------------------------- |
| **MT5 لازم يشتغل**              | Python يقرأ من ذاكرة MT5                |
| **start_pos=1 (مو 0)**          | لتخطي الشمعة الحالية (closed bars only) |
| **Python 3.8-3.11 فقط**         | 3.12+ ما يدعم MetaTrader5               |
| **آخر 500 شمعة**                | حد معقول — يمكن زيادته                  |
| **لا تلفق بيانات**              | إذا MT5 ما رجّع، السكربت يقولو صراحة    |
| **`mt5.shutdown()` في النهاية** | لتحرير القناة                           |

---

## 📌 الجزء 10: استكشاف الأخطاء

| الخطأ                         | السبب                    | الحل                                  |
| ----------------------------- | ------------------------ | ------------------------------------- |
| `initialize() failed`         | MT5 مو مفتوح             | افتح MT5                              |
| `symbol not found`            | الرمز مختلف              | جرّب `XAUUSD.vx`                      |
| `No rates`                    | الرمز مو في Market Watch | أضفه                                  |
| `No module named MetaTrader5` | المكتبة مو مثبتة         | `py -3.11 -m pip install MetaTrader5` |
| `Wrong Python version`        | Python 3.12+             | استخدم 3.11                           |
| `Timeout` أثناء pip           | إنترنت بطيء              | `--timeout 600 --retries 20`          |

---

## 📌 الجزء 11: الفرق عن الطرق الأخرى

| الطريقة         | المصدر            | يحتاج MT5؟ | يحتاج EA؟ | الدقة |
| --------------- | ----------------- | ---------- | --------- | ----- |
| **Python MT5**  | Valetax (عبر MT5) | ✅          | ❌         | 100%  |
| **EA (MQL5)**   | Valetax (عبر MT5) | ✅          | ✅         | 100%  |
| **Biquote**     | MT5 broker آخر    | ❌          | ❌         | \~95% |
| **Twelve Data** | مصادر مجمعة       | ❌          | ❌         | \~90% |

**Python MT5 = الأدق بدون EA.**

---

## 📌 الجزء 12: معلومات بيئية (من جلستنا)

من تجربتنا الفعلية:

text

```
✅ Python 3.11.7 مثبت
✅ MetaTrader5==5.0.6231
✅ numpy 2.4.6
✅ MT5 متصل (حساب MetaQuotes Demo)
✅ 7 رموز XAU ظهرت: XAUUSD, XAUEUR, XAUAUD, XAUCHF, XAUGBP, XAUG, XAUUSDs
✅ 3 شمعات M15 قُرِئت بنجاح
✅ السعر اللحظي: bid=4165.28, ask=4165.62
```

svgsvg

**ملاحظة:** عند التبديل لحساب Valetax، نفس الكود يشتغل بدون تعديل.

---

## 📌 الجزء 13: مراجع سريعة

| الرابط                                                                                                         | الوصف          |
| -------------------------------------------------------------------------------------------------------------- | -------------- |
| [https://www.mql5.com/en/docs/python_metatrader5](https://www.mql5.com/en/docs/python_metatrader5)             | التوثيق الرسمي |
| [https://pypi.org/project/MetaTrader5/](https://pypi.org/project/MetaTrader5/)                                 | صفحة المكتبة   |
| [https://www.python.org/downloads/release/python-3117/](https://www.python.org/downloads/release/python-3117/) | Python 3.11.7  |

---

## 📌 الجزء 14: الخلاصة بجملة وحدة

> **Python MetaTrader5 يفتح قناة محلية مع MT5 Terminal، يقرأ الشمعات المغلقة من ذاكرته، ويحفظها JSON — بنفس سعر وسيطك بالضبط، بدون EA، وبدون تعديل الكود عند تغيير الحساب.**

---

**نهاية الدليل — احفظه في أرشيفك.**