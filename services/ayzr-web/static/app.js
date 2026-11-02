/* AYZR frontend. All user-facing text is Arabic; the DOM is RTL throughout.
   Every number on screen comes from an API response. When a request fails the
   view renders an explicit Arabic state — it never falls back to a plausible
   placeholder. */

const TF_LABELS = {
  M1: "دقيقة", M5: "5 دقائق", M15: "15 دقيقة", M30: "30 دقيقة",
  H1: "ساعة", H4: "4 ساعات", D1: "يومي", MN1: "شهري",
};

const state = {
  route: "home",
  timeframe: "M15",
  history: JSON.parse(sessionStorage.getItem("ayzr_history") || "[]"),
};

/* ---------- helpers ---------- */

const nf = (n, d = 2) =>
  n === null || n === undefined ? "—" : Number(n).toLocaleString("en-US", {
    minimumFractionDigits: d, maximumFractionDigits: d,
  });

function arabicTime(ts) {
  if (!ts) return "—";
  const d = typeof ts === "number" ? new Date(ts * 1000) : new Date(ts);
  if (isNaN(d.getTime())) return "—";
  return d.toLocaleString("ar-EG", {
    year: "numeric", month: "long", day: "numeric",
    hour: "2-digit", minute: "2-digit",
  });
}

function relTime(ts) {
  if (!ts) return "—";
  const d = typeof ts === "number" ? new Date(ts * 1000) : new Date(ts);
  const mins = Math.floor((Date.now() - d.getTime()) / 60000);
  if (mins < 1) return "الآن";
  if (mins < 60) return `قبل ${mins} دقيقة`;
  const hrs = Math.floor(mins / 60);
  if (hrs < 24) return `قبل ${hrs} ساعة`;
  return `قبل ${Math.floor(hrs / 24)} يوم`;
}

async function api(path) {
  const res = await fetch(path, { headers: { Accept: "application/json" } });
  if (!res.ok) {
    let detail = `خطأ ${res.status}`;
    try {
      const body = await res.json();
      if (body && body.detail) detail = body.detail;
    } catch (_) { /* non-JSON error body */ }
    const err = new Error(detail);
    err.status = res.status;
    throw err;
  }
  return res.json();
}

const esc = (s) => String(s ?? "").replace(/[&<>"']/g, (c) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

const loading = (msg) =>
  `<div class="state"><div class="spinner"></div>${esc(msg)}</div>`;

const errorState = (title, detail) =>
  `<div class="state error"><strong>${esc(title)}</strong>${esc(detail || "")}</div>`;

const emptyState = (title, detail) =>
  `<div class="state"><strong>${esc(title)}</strong>${esc(detail || "")}</div>`;

const dirLabel = { bullish: "صاعد", bearish: "هابط", sideways: "عرضي", no_trade: "لا تداول" };

/* ---------- market status ---------- */

async function refreshStatus() {
  const dot = document.getElementById("feedDot");
  const label = document.getElementById("feedLabel");
  try {
    const h = await api("/api/health");
    const ok = h.model && h.model.reachable;
    dot.className = "dot " + (ok ? "live" : "stale");
    label.textContent = ok ? "البيانات حية · التحليل جاهز" : "البيانات حية · التحليل متوقف";
  } catch (e) {
    dot.className = "dot down";
    label.textContent = "تعذّر الاتصال بالخدمة";
  }
}

/* ---------- chart (hand-rolled SVG candlesticks) ---------- */

function candleChart(bars) {
  if (!bars || bars.length < 2) return "";
  const W = 1000, H = 320, pad = { t: 16, b: 26, l: 8, r: 60 };
  const lows = bars.map((b) => b.low), highs = bars.map((b) => b.high);
  const min = Math.min(...lows), max = Math.max(...highs);
  const range = max - min || 1;
  const plotW = W - pad.l - pad.r, plotH = H - pad.t - pad.b;
  const step = plotW / bars.length;
  const bodyW = Math.max(1.5, step * 0.62);

  const y = (v) => pad.t + plotH - ((v - min) / range) * plotH;

  let grid = "";
  for (let i = 0; i <= 4; i++) {
    const val = min + (range * i) / 4;
    const yy = y(val);
    grid += `<line x1="${pad.l}" y1="${yy}" x2="${W - pad.r}" y2="${yy}"
      stroke="#1e2c47" stroke-width="1" stroke-dasharray="3 5"/>`;
    grid += `<text x="${W - pad.r + 8}" y="${yy + 4}" fill="#64728c"
      font-size="11" font-family="sans-serif">${nf(val)}</text>`;
  }

  let candles = "";
  bars.forEach((b, i) => {
    // RTL: oldest bar on the right so time reads right-to-left like the text.
    const x = pad.l + plotW - (i + 0.5) * step;
    const up = b.close >= b.open;
    const color = up ? "#22c55e" : "#ef4444";
    candles += `<line x1="${x}" y1="${y(b.high)}" x2="${x}" y2="${y(b.low)}"
      stroke="${color}" stroke-width="1"/>`;
    const top = y(Math.max(b.open, b.close));
    const h = Math.max(1.5, Math.abs(y(b.open) - y(b.close)));
    candles += `<rect x="${x - bodyW / 2}" y="${top}" width="${bodyW}" height="${h}"
      fill="${color}" rx="1"/>`;
  });

  return `<svg class="chart-svg" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">
    ${grid}${candles}</svg>`;
}

/* ---------- views ---------- */

async function viewHome() {
  const el = document.getElementById("view");
  el.innerHTML = loading("جارٍ تحميل بيانات السوق…");

  let quote = null, quoteErr = null, analysis = null, analysisErr = null, news = null;

  try { quote = await api("/api/quote"); } catch (e) { quoteErr = e; }
  try { analysis = await api(`/api/analysis?tf=M15&with_ai=true`); }
  catch (e) { analysisErr = e; }
  try { news = await api("/api/news?limit=5"); } catch (_) { /* optional */ }

  let html = "";

  if (quote) {
    const pct = quote.change_pct;
    const cls = pct > 0 ? "up" : pct < 0 ? "down" : "flat";
    const badge = pct > 0 ? "badge-up" : pct < 0 ? "badge-down" : "badge-flat";
    const arrow = pct > 0 ? "▲" : pct < 0 ? "▼" : "—";
    html += `<section class="card">
      <div class="card-title">سعر الذهب الآن</div>
      <div class="price-hero">
        <div class="price-row">
          <span class="price-value">${nf(quote.price)}</span>
          <span class="price-unit">دولار أمريكي / أونصة</span>
          ${pct !== null && pct !== undefined
            ? `<span class="price-change ${badge}">${arrow} ${nf(Math.abs(pct))}%</span>`
            : ""}
        </div>
        <div class="meta-row">
          <span>المصدر: <b>${esc(quote.source)}</b></span>
          <span>أعلى اليوم: <b>${nf(quote.day_high)}</b></span>
          <span>أدنى اليوم: <b>${nf(quote.day_low)}</b></span>
          ${quote.spread !== null && quote.spread !== undefined
            ? `<span>فرق السعر: <b>${nf(quote.spread)}</b></span>` : ""}
          <span>آخر تحديث: <b>${esc(relTime(quote.market_time))}</b></span>
        </div>
      </div>
    </section>`;
  } else {
    html += `<section class="card">${errorState(
      "البيانات غير متاحة",
      "تعذّر جلب سعر الذهب من المصدر. " + (quoteErr ? quoteErr.message : "")
    )}</section>`;
  }

  if (analysis && analysis.proposal) {
    const p = analysis.proposal;
    const dcls = `dir-${p.direction}`;
    html += `<section class="card">
      <div class="signal-head">
        <div class="card-title" style="margin:0">المقترح التداولي — الإطار 15 دقيقة</div>
        <div class="direction ${dcls}">${esc(dirLabel[p.direction] || p.direction)}</div>
      </div>`;

    if (p.status === "insufficient_data") {
      html += `<div class="state warn"><strong>بيانات غير كافية</strong>${esc(p.reason_ar)}</div>`;
    } else if (p.direction === "sideways" || p.direction === "no_trade") {
      html += `<div class="state"><strong>لا توجد صفقة موثوقة حالياً</strong>${esc(p.reason_ar)}</div>`;
    } else {
      html += `<div class="levels">
        <div class="level entry"><div class="level-label">الدخول</div>
          <div class="level-value">${nf(p.entry)}</div></div>
        <div class="level stop"><div class="level-label">وقف الخسارة</div>
          <div class="level-value">${nf(p.stop_loss)}</div></div>
        <div class="level target"><div class="level-label">الهدف</div>
          <div class="level-value">${nf(p.take_profit)}</div></div>
        <div class="level"><div class="level-label">المخاطرة / المكافأة</div>
          <div class="level-value">${nf(p.risk_reward, 2)}</div></div>
      </div>
      <div class="reason">${esc(p.reason_ar)}</div>`;
      if (p.checks && p.checks.length) {
        html += `<ul class="checks">${p.checks.map((c) => `<li>${esc(c)}</li>`).join("")}</ul>`;
      }
    }
    html += `<div class="disclaimer">${esc(p.disclaimer_ar || "")}</div></section>`;

    if (analysis.ai && analysis.ai.status === "ok") {
      html += `<section class="card">
        <div class="card-title">التحليل بالذكاء الاصطناعي المحلي</div>
        <div class="ai-block">
          <div class="ai-part"><h4>ملخص الوضع</h4><p>${esc(analysis.ai.summary_ar)}</p></div>
          <div class="ai-part"><h4>المخاطر والنقص</h4><p>${esc(analysis.ai.risk_ar)}</p></div>
          <div class="ai-part"><h4>ما يجب مراقبته</h4><p>${esc(analysis.ai.watch_ar)}</p></div>
        </div>
        <div class="disclaimer">النموذج: ${esc(analysis.ai.model)} · يعمل محلياً على هذا الخادم،
          ولا تُرسل أي بيانات إلى مزوّد خارجي.</div></section>`;
    } else if (analysis.ai && analysis.ai.status === "unavailable") {
      html += `<section class="card"><div class="card-title">التحليل بالذكاء الاصطناعي المحلي</div>
        <div class="state warn"><strong>التحليل غير متاح مؤقتاً</strong>
        ${esc(analysis.ai.reason_ar || "")}<br>المقترح الفني أعلاه محسوب من الشموع ولا يعتمد على النموذج.</div>
        </section>`;
    }
  } else {
    html += `<section class="card"><div class="card-title">المقترح التداولي</div>
      ${errorState("التحليل غير متاح",
        analysisErr ? analysisErr.message : "لم تُرجع الخدمة تحليلاً.")}</section>`;
  }

  if (news && news.events && news.events.length) {
    html += `<section class="card"><div class="card-title">أهم الأحداث الاقتصادية</div>
      ${news.events.slice(0, 4).map(newsRow).join("")}</section>`;
  }

  el.innerHTML = html;
}

function newsRow(e) {
  const imp = e.importance === "high" ? "imp-high"
    : e.importance === "medium" ? "imp-medium" : "imp-low";
  const impLabel = e.importance === "high" ? "أهمية عالية"
    : e.importance === "medium" ? "أهمية متوسطة" : "أهمية منخفضة";
  const hasVals = e.actual !== null || e.forecast !== null;
  return `<div class="news-item" style="margin-bottom:12px">
    <div class="news-top">
      <span class="news-name">${esc(e.name)}</span>
      <span class="imp ${imp}">${impLabel}</span>
      <span class="imp imp-low">${esc(e.currency || "")}</span>
    </div>
    ${hasVals ? `<div class="news-values">
      <span>الفعلي: <b>${e.actual === null ? "لم يُعلن" : esc(e.actual)}</b></span>
      <span>المتوقع: <b>${e.forecast === null ? "—" : esc(e.forecast)}</b></span>
      <span>السابق: <b>${e.previous === null ? "—" : esc(e.previous)}</b></span>
    </div>` : ""}
    <div class="news-time">${esc(arabicTime(e.time))} · المصدر: ${esc(e.source || "")}</div>
  </div>`;
}

async function viewAnalysis() {
  const el = document.getElementById("view");
  el.innerHTML = `
    <section class="card">
      <div class="card-title">الإطار الزمني</div>
      <div class="tf-bar" id="tfBar">
        ${Object.keys(TF_LABELS).map((tf) =>
          `<button class="tf-btn ${tf === state.timeframe ? "active" : ""}" data-tf="${tf}">
            ${TF_LABELS[tf]} <span style="opacity:.6">${tf}</span></button>`).join("")}
      </div>
      <div id="analysisBody">${loading("جارٍ تحميل الشموع…")}</div>
    </section>`;

  document.getElementById("tfBar").addEventListener("click", (ev) => {
    const btn = ev.target.closest(".tf-btn");
    if (!btn) return;
    state.timeframe = btn.dataset.tf;
    document.querySelectorAll(".tf-btn").forEach((b) =>
      b.classList.toggle("active", b.dataset.tf === state.timeframe));
    loadAnalysisBody();
  });

  await loadAnalysisBody();
}

async function loadAnalysisBody() {
  const body = document.getElementById("analysisBody");
  if (!body) return;
  body.innerHTML = loading("جارٍ تحميل الشموع…");
  const tf = state.timeframe;

  let candles, analysis;
  try {
    [candles, analysis] = await Promise.all([
      api(`/api/candles?tf=${tf}&limit=200`),
      api(`/api/analysis?tf=${tf}&with_ai=false`),
    ]);
  } catch (e) {
    body.innerHTML = errorState("تعذّر تحميل البيانات", e.message);
    return;
  }

  const p = analysis.proposal;
  let html = `<div class="chart-wrap">${candleChart(candles.candles)}</div>
    <div class="meta-row">
      <span>عدد الشموع: <b>${candles.count}</b></span>
      <span>المصدر: <b>${esc(candles.source)}</b></span>
      <span>آخر شمعة: <b>${esc(arabicTime(candles.candles[candles.candles.length - 1].open_time))}</b></span>
    </div>`;

  if (p && p.status === "ok" && p.direction !== "sideways" && p.direction !== "no_trade") {
    html += `<div class="levels" style="margin-top:18px">
      <div class="level entry"><div class="level-label">الدخول</div>
        <div class="level-value">${nf(p.entry)}</div></div>
      <div class="level stop"><div class="level-label">وقف الخسارة</div>
        <div class="level-value">${nf(p.stop_loss)}</div></div>
      <div class="level target"><div class="level-label">الهدف</div>
        <div class="level-value">${nf(p.take_profit)}</div></div>
      <div class="level"><div class="level-label">المخاطرة / المكافأة</div>
        <div class="level-value">${nf(p.risk_reward, 2)}</div></div>
    </div>`;
  }

  if (p) {
    html += `<div class="reason">${esc(p.reason_ar)}</div>`;
    if (p.checks && p.checks.length) {
      html += `<ul class="checks">${p.checks.map((c) => `<li>${esc(c)}</li>`).join("")}</ul>`;
    }
    const ind = p.indicators || {};
    html += `<div class="meta-row" style="margin-top:16px">
      <span>المتوسط 20: <b>${nf(ind.ema20)}</b></span>
      <span>المتوسط 50: <b>${nf(ind.ema50)}</b></span>
      <span>القوة النسبية: <b>${nf(ind.rsi14, 1)}</b></span>
      <span>مدى الحركة: <b>${nf(ind.atr14)}</b></span>
      <span>الدعم: <b>${nf((p.levels || {}).support)}</b></span>
      <span>المقاومة: <b>${nf((p.levels || {}).resistance)}</b></span>
    </div>
    <div class="disclaimer">${esc(p.disclaimer_ar || "")}</div>`;
  }

  body.innerHTML = html;
}

async function viewNews() {
  const el = document.getElementById("view");
  el.innerHTML = `<section class="card"><div class="card-title">الأحداث الاقتصادية</div>
    <div id="newsBody">${loading("جارٍ تحميل الأحداث…")}</div></section>`;

  const body = document.getElementById("newsBody");
  let data;
  try {
    data = await api("/api/news?limit=40");
  } catch (e) {
    body.innerHTML = errorState("تعذّر تحميل الأحداث", e.message);
    return;
  }

  if (!data.events || !data.events.length) {
    body.innerHTML = emptyState("لا توجد أحداث اقتصادية حالياً",
      "لم تُرجع الخدمة أي حدث ذي أهمية في هذه الفترة.");
    return;
  }

  body.innerHTML = data.events.map(newsRow).join("") +
    `<div class="disclaimer">المصدر: ${esc(data.source)} · ${data.count} حدث</div>`;
}

async function viewHistory() {
  const el = document.getElementById("view");
  const rows = state.history;

  let html = `<section class="card">
    <div class="card-title">سجل التحليلات</div>`;

  if (!rows.length) {
    html += emptyState("لا يوجد سجل بعد",
      "يُسجَّل كل تحليل تفتحه هنا مع وقته ومستوياته. افتح صفحة التحليل أو الرئيسية لإضافة أول سجل.");
  } else {
    html += rows.map((r) => `<div class="hist-row">
      <div class="hist-main">
        <span class="direction dir-${r.direction}" style="font-size:15px">
          ${esc(dirLabel[r.direction] || r.direction)} · ${esc(r.timeframe)}</span>
        <span class="hist-time">${esc(arabicTime(r.at))}</span>
      </div>
      <div class="hist-main" style="text-align:end">
        <span>الدخول ${nf(r.entry)}</span>
        <span class="hist-time">وقف ${nf(r.stop_loss)} · هدف ${nf(r.take_profit)}</span>
      </div>
    </div>`).join("");
    html += `<div class="disclaimer">هذا سجل للقراءات التي عُرضت في وقتها، وليس سجل نتائج.
      لا توجد بيانات تُثبت ربحية هذه القواعد.</div>`;
  }

  html += `</section>`;
  el.innerHTML = html;
}

/* ---------- history capture ---------- */

function captureHistory() {
  api("/api/analysis?tf=M15&with_ai=false").then((d) => {
    const p = d && d.proposal;
    if (!p || p.status !== "ok" || !p.entry) return;
    const last = state.history[0];
    if (last && last.entry === p.entry && last.direction === p.direction
        && Date.now() - last.at < 300000) return;
    state.history.unshift({
      direction: p.direction, timeframe: p.timeframe, entry: p.entry,
      stop_loss: p.stop_loss, take_profit: p.take_profit,
      at: p.as_of ? p.as_of * 1000 : Date.now(),
    });
    state.history = state.history.slice(0, 40);
    sessionStorage.setItem("ayzr_history", JSON.stringify(state.history));
  }).catch(() => { /* history is best-effort, never blocks the UI */ });
}

/* ---------- router ---------- */

const ROUTES = { home: viewHome, analysis: viewAnalysis, news: viewNews, history: viewHistory };

async function render() {
  document.querySelectorAll(".tab").forEach((t) =>
    t.classList.toggle("active", t.dataset.route === state.route));
  window.scrollTo(0, 0);
  await ROUTES[state.route]();
  captureHistory();
}

document.querySelectorAll(".tab").forEach((tab) =>
  tab.addEventListener("click", () => {
    state.route = tab.dataset.route;
    render();
  }));

refreshStatus();
render();
setInterval(refreshStatus, 60000);
