/* AYZR Observatory — application layer.
 *
 * Reads ONLY the backend's documented read surface (same allowlist the proxy
 * enforces). Every value rendered is either a server value or the literal
 * Arabic "—"/"غير متاح"; nothing is invented. When a route fails the affected
 * panel degrades to an inline note instead of blanking the view.
 */
'use strict';

const API = {
  async get(path) {
    // Same-origin only: the proxy in front of this page holds the client
    // token, so the browser must never be pointed at another origin.
    const url = path.startsWith('http') ? path : window.location.origin + path;
    const res = await fetch(url, { headers: { 'Accept': 'application/json' } });
    const text = await res.text();
    let json = null;
    try { json = text ? JSON.parse(text) : null; } catch { json = null; }
    return { ok: res.ok, status: res.status, json };
  },
};

const state = { tab: 'overview', timeframe: 'M15', newsFilter: 'all', cache: {} };

/* ---- formatting helpers ------------------------------------------------ */
const esc = (s) => String(s ?? '').replace(/[&<>"']/g,
  (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const NA = '<span class="muted">—</span>';

function num(v, digits = 2) {
  if (v === null || v === undefined || v === '' || Number.isNaN(Number(v))) return NA;
  return Number(v).toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits });
}
function price(v) { return num(v, 2); }

function tsUtc(sec) {
  if (!sec && sec !== 0) return NA;
  const d = new Date(Number(sec) * 1000);
  if (Number.isNaN(d.getTime())) return NA;
  return d.toISOString().replace('T', ' ').slice(0, 19) + ' UTC';
}
function tsMs(ms) {
  if (ms === null || ms === undefined) return NA;
  return tsUtc(Math.floor(Number(ms) / 1000));
}
function ageLabel(ms) {
  if (ms === null || ms === undefined) return NA;
  const s = Math.floor(Number(ms) / 1000);
  if (s < 60) return `${s} ث`;
  if (s < 3600) return `${Math.floor(s / 60)} د`;
  if (s < 86400) return `${Math.floor(s / 3600)} س`;
  return `${Math.floor(s / 86400)} ي`;
}
const unwrap = (r) => (r && r.json && (r.json.data !== undefined ? r.json.data : r.json)) || null;

function pill(text, kind = '') { return `<span class="pill ${kind}">${esc(text)}</span>`; }
function emptyBox(msg) { return `<div class="empty">${esc(msg)}</div>`; }
function card(title, body) { return `<div class="card">${title ? `<h2>${esc(title)}</h2>` : ''}${body}</div>`; }
function row(k, v) { return `<div class="row"><span class="k">${k}</span><span class="v">${v}</span></div>`; }
function metric(label, value, foot) {
  return `<div class="metric"><div class="label">${esc(label)}</div>
    <div class="value">${value}</div>${foot ? `<div class="foot">${foot}</div>` : ''}</div>`;
}

/* Freshness / quality vocabulary shared by every panel. */
function freshnessPill(fr) {
  if (!fr) return pill('غير متاح');
  const s = fr.state || (fr.is_fresh ? 'FRESH' : 'STALE');
  const kind = s === 'FRESH' ? 'ok' : (s === 'STALE' ? 'warn' : 'bad');
  const ar = { FRESH: 'حديث', STALE: 'متأخر', MISSING: 'مفقود', UNKNOWN: 'غير معروف' }[s] || s;
  return pill(ar, kind);
}
function qualityPill(q) {
  if (!q) return pill('غير متاح');
  const s = q.state || 'UNKNOWN';
  const kind = s === 'VALID' ? 'ok' : (s === 'UNKNOWN' ? '' : 'warn');
  const ar = {
    VALID: 'سليمة', DEGRADED: 'متدهورة', INVALID: 'غير صالحة', UNKNOWN: 'غير معروفة',
    STALE: 'متأخرة', MISSING: 'مفقودة', INCOMPLETE: 'غير مكتملة', OUT_OF_ORDER: 'غير مرتبة',
    DUPLICATE: 'مكرّرة',
  }[s] || s;
  return pill(ar, kind);
}

/* ---- chart: candlesticks, no fabricated values ------------------------- */
function drawCandles(canvas, bars) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const W = rect.width, H = rect.height;
  ctx.clearRect(0, 0, W, H);
  if (!bars || !bars.length) return;

  const padL = 8, padR = 62, padT = 12, padB = 22;
  const plotW = W - padL - padR, plotH = H - padT - padB;
  let lo = Infinity, hi = -Infinity;
  for (const b of bars) { lo = Math.min(lo, b.low); hi = Math.max(hi, b.high); }
  if (!isFinite(lo) || !isFinite(hi) || hi === lo) { hi = lo + 1; }
  const y = (p) => padT + (hi - p) / (hi - lo) * plotH;
  const n = bars.length;
  const step = plotW / n;
  const bw = Math.max(2, Math.min(9, step * 0.62));

  // Horizontal grid + right-side price axis.
  ctx.strokeStyle = 'rgba(39,69,106,.45)';
  ctx.fillStyle = '#5E7699';
  ctx.font = '10px monospace';
  ctx.textAlign = 'left';
  for (let i = 0; i <= 4; i++) {
    const p = lo + (hi - lo) * (i / 4);
    const yy = Math.round(y(p)) + .5;
    ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + plotW, yy); ctx.stroke();
    ctx.fillText(p.toFixed(2), padL + plotW + 6, yy + 3);
  }

  for (let i = 0; i < n; i++) {
    const b = bars[i];
    const cx = padL + i * step + step / 2;
    const up = b.close >= b.open;
    const color = up ? '#2FBF71' : '#E4574F';
    ctx.strokeStyle = color; ctx.fillStyle = color;
    ctx.beginPath(); ctx.moveTo(cx, y(b.high)); ctx.lineTo(cx, y(b.low)); ctx.stroke();
    const yo = y(b.open), yc = y(b.close);
    const top = Math.min(yo, yc), hgt = Math.max(1, Math.abs(yc - yo));
    ctx.fillRect(cx - bw / 2, top, bw, hgt);
  }
  // Time axis: first and last bar open times.
  ctx.fillStyle = '#5E7699'; ctx.font = '10px monospace';
  ctx.textAlign = 'right';
  ctx.fillText(tsUtc(bars[0].time).slice(5, 16), padL + plotW, H - 6);
  ctx.textAlign = 'left';
  ctx.fillText(tsUtc(bars[n - 1].time).slice(5, 16), padL, H - 6);
}

/* ---- OVERVIEW ---------------------------------------------------------- */
async function renderOverview(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل النظرة العامة…</div>`;

  const [healthR, tfR, candlesR, oppR, newsR, sysR] = await Promise.all([
    API.get('/api/v1/health/v1'),
    API.get('/api/v1/timeframes'),
    API.get(`/api/v1/candles?symbol=XAUUSD&tf=${state.timeframe}&limit=180`),
    API.get('/api/v1/ayzr/ecosystem/opportunities'),
    API.get('/api/v1/news?limit=6'),
    API.get('/api/v1/system/state'),
  ]);

  const health = unwrap(healthR) || {};
  const tfs = unwrap(tfR) || [];
  const candles = unwrap(candlesR) || {};
  const opp = unwrap(oppR) || {};
  const sys = unwrap(sysR) || {};
  const bars = candles.bars || [];

  let html = '';
  if (health.bridge === 'offline') {
    html += `<div class="banner info">وضع الظل فقط — لا تنفيذ لعمليات تداول. جسر MT5 غير متصل؛ تُعرض البيانات المتاحة فقط ولا يُختلق غيرها.</div>`;
  } else {
    html += `<div class="banner info">وضع الظل فقط — لا تنفيذ لعمليات تداول.</div>`;
  }

  // Price + change.
  const newest = bars.length ? bars[bars.length - 1] : null;
  const prev = bars.length > 1 ? bars[bars.length - 2] : null;
  let changeHtml = NA, pctHtml = NA;
  if (newest && prev && prev.close) {
    const ch = newest.close - prev.close;
    const pc = ch / prev.close * 100;
    const cls = ch >= 0 ? 'up' : 'down';
    const sign = ch >= 0 ? '+' : '';
    changeHtml = `<span class="${cls}">${sign}${num(ch, 2)}</span>`;
    pctHtml = `<span class="${cls}">${sign}${num(pc, 2)}%</span>`;
  }
  const m15 = tfs.find((t) => t.timeframe === state.timeframe) || {};

  html += `<div class="grid cols-4">
    ${card('', metric('سعر XAUUSD', newest ? price(newest.close) : NA,
        newest ? `المصدر: ${esc(candles.symbol || 'XAUUSD')} · ${esc(state.timeframe)}` : 'بانتظار البيانات'))}
    ${card('', metric('التغيّر', changeHtml, `مقارنة بالشمعة السابقة (${esc(state.timeframe)})`))}
    ${card('', metric('نسبة التغيّر', pctHtml, 'آخر شمعتين مغلقتين'))}
    ${card('', metric('آخر تحديث', tsUtc(m15.last_successful_update), m15.freshness ? `العمر: ${ageLabel(m15.freshness.age_millis)}` : ''))}
  </div>`;

  // Chart + market status.
  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('حركة السعر — ' + state.timeframe,
      `<div class="chart-wrap"><canvas id="ov-chart"></canvas></div>
       ${!bars.length ? emptyBox('لا تتوفر شموع لهذا الإطار الزمني') : ''}`)}
    ${card('حالة السوق والبيانات',
      row('حالة الاتصال', health.status === 'ok' ? pill('متصل', 'ok') : pill(esc(health.status || 'غير متاح'), 'warn')) +
      row('جسر MT5', health.bridge === 'online' ? pill('متصل', 'ok') : pill('غير متصل', 'warn')) +
      row('جودة البيانات', qualityPill(m15.quality)) +
      row('حداثة البيانات', freshnessPill(m15.freshness)) +
      row('شمعات مغلقة فقط', candles.closed_only ? pill('نعم', 'ok') : pill('غير مؤكد', 'warn')) +
      row('عدد الشموع المتاحة', num(candles.count, 0)) +
      row('آخر شمعة مغلقة', tsUtc(candles.newest_closed_time)))}
  </div>`;

  // ASTRA opportunities + news.
  const opps = opp.opportunities || [];
  let oppBody = '';
  if ((opp.dataQualityWarnings || []).length)
    oppBody += `<div class="banner warn">تحذيرات جودة البيانات: ${opp.dataQualityWarnings.map(esc).join(' · ')}</div>`;
  if (!opps.length) {
    oppBody += emptyBox('لا توجد فرص موثوقة حالياً — لا يتم اختلاق فرص عند غياب البيانات.');
  } else {
    for (const o of opps) {
      const kind = o.state === 'PUBLISH' ? 'ok' : (o.state === 'WATCH' ? 'warn' : '');
      oppBody += row(`${esc(o.symbol)} · ${esc(o.timeframe)} · ${esc(o.directionAr)}`,
        pill(esc(o.stateAr), kind));
      if (o.rationaleAr) oppBody += row('', `<span class="muted">${esc(o.rationaleAr)}</span>`);
      if (o.entry) oppBody += row('الدخول', price(o.entry.price));
      if (o.stopLoss) oppBody += row('وقف الخسارة', price(o.stopLoss.price));
      for (const t of (o.takeProfits || [])) oppBody += row('هدف', price(t.price));
    }
  }
  const newsItems = (newsR.json && (newsR.json.news || newsR.json.items || newsR.json.data)) || [];
  let newsBody = '';
  if (!newsItems.length) newsBody = emptyBox('لا توجد أخبار');
  else for (const n of newsItems.slice(0, 5))
    newsBody += row(esc(n.title || n.headline || 'بدون عنوان'),
      `<span class="mono">${esc(n.source || '—')}</span>`);

  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('تحليل ASTRA والفرص', oppBody)}
    ${card('أهم الأخبار', newsBody)}
  </div>`;

  main.innerHTML = html;
  const c = document.getElementById('ov-chart');
  if (c && bars.length) drawCandles(c, bars);
}

/* ---- ANALYSIS ---------------------------------------------------------- */
async function renderAnalysis(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل التحليل…</div>`;
  const TFS = ['M1', 'M5', 'M15', 'M30', 'H1', 'H4', 'D1', 'W1', 'MN1'];
  const [candlesR, tfR, analysisR, contextR, sigR, riskR] = await Promise.all([
    API.get(`/api/v1/candles?symbol=XAUUSD&tf=${state.timeframe}&limit=220`),
    API.get('/api/v1/timeframes'),
    API.get('/api/v1/analysis/latest'),
    API.get('/api/v1/context/latest'),
    API.get('/api/v1/signals/latest'),
    API.get('/api/v1/risk/latest'),
  ]);
  const candles = unwrap(candlesR) || {};
  const bars = candles.bars || [];
  const tfs = unwrap(tfR) || [];
  const cur = tfs.find((t) => t.timeframe === state.timeframe) || {};
  const analysis = unwrap(analysisR) || {};
  const context = unwrap(contextR) || {};
  const signals = unwrap(sigR) || {};
  const risk = unwrap(riskR) || {};

  let html = `<div class="seg" id="tf-seg">${TFS.map((t) =>
    `<button data-tf="${t}" class="${t === state.timeframe ? 'active' : ''}">${t}</button>`).join('')}</div>`;

  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('الشموع — ' + state.timeframe,
      `<div class="chart-wrap" style="height:320px"><canvas id="an-chart"></canvas></div>
       ${!bars.length ? emptyBox('لا تتوفر شموع لهذا الإطار الزمني') : ''}`)}
    ${card('جودة البيانات', 
      row('الإطار', esc(state.timeframe)) +
      row('جودة البيانات', qualityPill(cur.quality)) +
      row('حداثة البيانات', freshnessPill(cur.freshness)) +
      row('شمعات مغلقة فقط', candles.closed_only ? pill('نعم', 'ok') : pill('غير مؤكد', 'warn')) +
      row('عدد الشموع', num(candles.count, 0)) +
      row('آخر شمعة مغلقة', tsUtc(candles.newest_closed_time)))}
  </div>`;

  const unwrapLoose = (o) => (o && typeof o === 'object') ? o : {};

  // ASTRA analysis is a structured object; render its known fields with Arabic
  // labels rather than dumping raw keys. Unknown/absent fields show "—".
  const ctx = analysis.context || {};
  const sig = analysis.signal || {};
  const dec = analysis.decision || {};
  const lv = analysis.levels || {};
  const REG = { TREND_UP: 'اتجاه صاعد', TREND_DOWN: 'اتجاه هابط', RANGE: 'عرضي', UNKNOWN: 'غير معروف' };
  const DIR = { UP: 'صاعد', DOWN: 'هابط', LONG: 'شراء', SHORT: 'بيع', NONE: 'محايد' };
  let astraBody = '';
  if (!analysis.timestamp) {
    astraBody = emptyBox('تحليل ASTRA غير متاح لهذا الوقت');
  } else {
    astraBody =
      row('وقت التحليل', `<span class="mono">${esc(String(analysis.timestamp).replace('T', ' ').replace('Z', ' UTC'))}</span>`) +
      row('النظام', pill(esc(REG[ctx.regime] || ctx.regime || '—'))) +
      row('انحياز H4', esc(DIR[ctx.h4_bias] || ctx.h4_bias || '—')) +
      row('إشارة M15', esc(DIR[ctx.m15_trigger] || ctx.m15_trigger || '—')) +
      row('توافق الأطر', num(ctx.mtf_agreement, 0)) +
      row('التقلّب', esc(ctx.volatility_state || '—')) +
      row('الاتجاه', pill(esc(DIR[sig.direction] || sig.direction || '—'),
          sig.direction === 'UP' ? 'ok' : (sig.direction === 'DOWN' ? 'bad' : ''))) +
      row('الدرجة', sig.score !== null && sig.score !== undefined ? num(sig.score, 1) : NA) +
      row('احتمال مُعاير', sig.probability_calibrated && sig.probability !== null
          ? num(sig.probability, 2) : pill('غير مُعاير', 'warn')) +
      row('الدخول', price(lv.entry)) +
      row('وقف الخسارة', price(lv.stop_loss)) +
      row('الهدف', price(lv.take_profit)) +
      row('المخاطرة/المكافأة', lv.reward_risk ? num(lv.reward_risk, 2) : NA);
  }
  let decBody = '';
  if (!Object.keys(dec).length) decBody = emptyBox('قرار غير متاح');
  else {
    decBody = row('متاح', dec.available ? pill('نعم', 'ok') : pill('لا', 'warn')) +
      row('تعدّد الأطر متاح', dec.mtf_available ? pill('نعم', 'ok') : pill('لا', 'warn')) +
      row('بوابة النواة', dec.mtf_core_gate_passed ? pill('مجتازة', 'ok') : pill('غير مجتازة', 'warn')) +
      row('أسباب الحجب', (dec.block_reasons || []).length
        ? dec.block_reasons.map((r) => pill(esc(r), 'warn')).join(' ') : pill('لا يوجد', 'ok'));
  }

  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('تحليل ASTRA', astraBody)}
    ${card('القرار والجودة', decBody)}
  </div>`;

  main.innerHTML = html;
  const c = document.getElementById('an-chart');
  if (c && bars.length) drawCandles(c, bars);
  document.querySelectorAll('#tf-seg button').forEach((b) => {
    b.onclick = () => { state.timeframe = b.dataset.tf; renderAnalysis(main); };
  });
}

/* ---- NEWS -------------------------------------------------------------- */
const NEWS_FILTERS = [
  ['all', 'الكل'], ['gold', 'الذهب'], ['monetary', 'السياسة النقدية'],
  ['inflation', 'التضخم'], ['data', 'البيانات الاقتصادية'],
  ['central-bank', 'البنوك المركزية'], ['geopolitics', 'الجيوسياسية'],
];
async function renderNews(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل الأخبار…</div>`;
  const r = await API.get('/api/v1/news?limit=60');
  const items = (r.json && (r.json.news || r.json.items || r.json.data)) || [];

  let html = `<div class="seg" id="news-seg">${NEWS_FILTERS.map(([id, ar]) =>
    `<button data-f="${id}" class="${id === state.newsFilter ? 'active' : ''}">${ar}</button>`).join('')}</div>`;

  if (!items.length) {
    html += `<div style="margin-top:16px">${emptyBox('لا توجد أخبار متاحة حالياً. لا يتم اختلاق عناوين أو مصادر.')}</div>`;
    main.innerHTML = html;
    return;
  }
  const filtered = state.newsFilter === 'all' ? items : items.filter((n) =>
    (n.category || n.category_ar || '').toLowerCase().includes(state.newsFilter) ||
    (n.tags || []).some((t) => String(t).toLowerCase().includes(state.newsFilter)));

  html += `<div class="grid cols-2" style="margin-top:16px">`;
  for (const n of filtered) {
    const pub = n.published_at || n.publishedAt;
    const collected = n.collected_at || n.collectedAt;
    html += `<div class="card">
      <h2 style="display:block">${esc(n.title || n.headline || 'بدون عنوان')}</h2>
      <div class="row"><span class="k">المصدر</span><span class="v">${n.url
        ? `<a href="${esc(n.url)}" target="_blank" rel="noopener" style="color:var(--blue-400)">${esc(n.source || '—')}</a>`
        : esc(n.source || '—')}</span></div>
      <div class="row"><span class="k">وقت النشر</span><span class="v mono">${tsUtc(pub)}</span></div>
      <div class="row"><span class="k">وقت الجمع</span><span class="v mono">${tsUtc(collected)}</span></div>
      ${n.category_ar || n.category ? `<div class="row"><span class="k">التصنيف</span><span class="v">${pill(esc(n.category_ar || n.category))}</span></div>` : ''}
      ${n.summary_ar ? `<p style="margin-top:10px;color:var(--text-mid);font-size:13px">${esc(n.summary_ar)}</p>`
        : `<p style="margin-top:10px;color:var(--text-low);font-size:12.5px">لا يتوفر ملخص عربي لهذا الخبر بعد.</p>`}
    </div>`;
  }
  html += `</div>`;
  main.innerHTML = html;
  document.querySelectorAll('#news-seg button').forEach((b) => {
    b.onclick = () => { state.newsFilter = b.dataset.f; renderNews(main); };
  });
}

/* ---- HISTORY ----------------------------------------------------------- */
async function renderHistory(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل السجل…</div>`;
  const [ledR, actR, newsR] = await Promise.all([
    API.get('/api/v1/ayzr/ecosystem/ledger?limit=100'),
    API.get('/api/v1/activity?limit=60'),
    API.get('/api/v1/news/results?limit=40'),
  ]);
  const led = unwrap(ledR) || {};
  const act = unwrap(actR) || {};
  const results = unwrap(newsR) || {};
  const entries = led.entries || [];
  const acts = act.entries || act.activity || act.items || [];
  const analyses = results.results || results.items || (Array.isArray(results) ? results : []);

  let html = `<div class="banner info">السجل مقروء من قاعدة البيانات الدائمة — لا يختفي عند إعادة التشغيل.</div>`;

  // Decision ledger.
  let ledBody = '';
  if (!entries.length) ledBody = emptyBox('لا توجد قرارات مسجّلة بعد');
  else {
    ledBody = `<table><tr><th>الوقت</th><th>النوع</th><th>المحرك</th><th>التفصيل</th></tr>`;
    for (const e of entries.slice(0, 40))
      ledBody += `<tr><td class="mono">${tsUtc(e.atUtc || e.at)}</td>
        <td>${pill(esc(e.typeAr || e.type || '—'))}</td>
        <td class="mono">${esc(e.engineId || '—')}</td>
        <td class="muted">${esc(e.detailAr || e.reasonCode || '—')}</td></tr>`;
    ledBody += `</table>`;
  }

  // AI news analyses.
  let aiBody = '';
  if (!analyses.length) aiBody = emptyBox('لا توجد تحليلات أخبار محفوظة بعد');
  else {
    aiBody = `<table><tr><th>الوقت</th><th>العنوان</th><th>الحالة</th></tr>`;
    for (const a of analyses.slice(0, 30))
      aiBody += `<tr><td class="mono">${tsUtc(a.at || a.analyzed_at)}</td>
        <td>${esc(a.title || a.headline || '—')}</td>
        <td>${pill(esc(a.status_ar || a.status || '—'))}</td></tr>`;
    aiBody += `</table>`;
  }

  // Activity log.
  let actBody = '';
  if (!acts.length) actBody = emptyBox('سجل النشاط فارغ');
  else {
    actBody = `<table><tr><th>الوقت</th><th>النوع</th><th>النتيجة</th><th>التفصيل</th></tr>`;
    for (const a of acts.slice(0, 40)) {
      const kind = a.outcome === 'ok' ? 'ok' : (a.outcome === 'error' ? 'bad' : 'warn');
      actBody += `<tr><td class="mono">${tsUtc(a.at)}</td><td>${esc(a.kind || '')}</td>
        <td>${pill(esc(a.outcome || ''), kind)}</td>
        <td class="muted">${esc(a.detail || a.reason || '')}</td></tr>`;
    }
    actBody += `</table>`;
  }

  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('سجل القرارات', ledBody)}
    ${card('تحليلات الأخبار بالذكاء الاصطناعي', aiBody)}
  </div>
  <div style="margin-top:16px">${card('سجل النشاط', actBody)}</div>`;
  main.innerHTML = html;
}

/* ---- ECOSYSTEM --------------------------------------------------------- */
async function renderEcosystem(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل المنظومة…</div>`;
  const [sessR, engR, oppR, ledR] = await Promise.all([
    API.get('/api/v1/ayzr/ecosystem/sessions'),
    API.get('/api/v1/ayzr/ecosystem/engines'),
    API.get('/api/v1/ayzr/ecosystem/opportunities'),
    API.get('/api/v1/ayzr/ecosystem/ledger?limit=100'),
  ]);
  const sess = unwrap(sessR) || {}, eng = unwrap(engR) || {};
  const opp = unwrap(oppR) || {}, led = unwrap(ledR) || {};

  let html = `<div class="banner info">وضع الظل فقط — لا تنفيذ لعمليات تداول. كل قيمة معروضة مصدرها الخادم.</div>`;

  let sessBody = '';
  const sessions = sess.sessions || [];
  if (!sessions.length) sessBody = emptyBox('لا تتوفر بيانات الجلسات');
  else {
    if (sess.londonNewYorkOverlap) sessBody += row('تداخل لندن ونيويورك', pill('نشط', 'ok'));
    for (const s of sessions) {
      const kind = s.error ? 'warn' : (s.phase === 'OPEN' ? 'ok' : '');
      sessBody += row(`${esc(s.nameAr || s.id)} <span class="mono">${esc(s.zoneId)}</span>`,
        pill(esc(s.phaseAr), kind) + (s.localTime ? ` <span class="mono">${esc(s.localTime)}</span>` : '') +
        (s.inDst ? ' ' + pill('توقيت صيفي', 'warn') : ''));
    }
  }

  let engBody = row('إصدار العقد', `<span class="mono">${esc(eng.contractSchemaVersion)}</span>`);
  const engines = eng.engines || [];
  if (!engines.length) engBody += emptyBox('لا توجد محرّكات مسجّلة');
  else for (const e of engines) {
    const kind = e.health === 'HEALTHY' ? 'ok' : (e.health === 'UNAVAILABLE' ? 'warn' : '');
    engBody += row(`${esc(e.strategyFamilyAr)} <span class="mono">${esc(e.engineId)}</span>`,
      pill(esc(e.healthAr), kind));
    if (e.noteAr) engBody += row('', `<span class="muted">${esc(e.noteAr)}</span>`);
  }

  let oppBody = '';
  if ((opp.dataQualityWarnings || []).length)
    oppBody += `<div class="banner warn">تحذيرات جودة البيانات: ${opp.dataQualityWarnings.map(esc).join(' · ')}</div>`;
  if ((opp.engineFailures || []).length)
    oppBody += `<div class="banner warn">أعطال المحرّكات: ${opp.engineFailures.map(esc).join(' · ')}</div>`;
  const opps = opp.opportunities || [];
  if (!opps.length) oppBody += emptyBox('لا توجد فرص موثوقة حالياً');
  else for (const o of opps) {
    const kind = o.state === 'PUBLISH' ? 'ok' : (o.state === 'WATCH' ? 'warn' : '');
    oppBody += row(`${esc(o.symbol)} · ${esc(o.timeframe)} · ${esc(o.directionAr)}`, pill(esc(o.stateAr), kind));
    if (o.rationaleAr) oppBody += row('', `<span class="muted">${esc(o.rationaleAr)}</span>`);
    if (o.entry) oppBody += row('الدخول', price(o.entry.price));
    if (o.stopLoss) oppBody += row('وقف الخسارة', price(o.stopLoss.price));
    for (const t of (o.takeProfits || [])) oppBody += row('هدف', price(t.price));
  }

  let ledBody = '';
  const entries = led.entries || [];
  if (!entries.length) ledBody = emptyBox('لا توجد قرارات مسجّلة');
  else {
    ledBody = `<table><tr><th>الوقت</th><th>النوع</th><th>التفصيل</th></tr>`;
    for (const e of entries.slice(0, 25))
      ledBody += `<tr><td class="mono">${tsUtc(e.atUtc || e.at)}</td>
        <td>${pill(esc(e.typeAr || e.type || '—'))}</td>
        <td class="muted">${esc(e.detailAr || e.reasonCode || '—')}</td></tr>`;
    ledBody += `</table>`;
  }

  html += `<div class="grid cols-2" style="margin-top:16px">
    ${card('الجلسات', sessBody)}
    ${card('المحركات المتخصصة', engBody)}
    ${card('الفرص', oppBody)}
    ${card('سجل القرارات', ledBody)}
  </div>`;
  main.innerHTML = html;
}

/* ---- SYSTEM ------------------------------------------------------------ */
async function renderSystem(main) {
  main.innerHTML = `<div class="loading"><span class="spin"></span>جارٍ تحميل حالة النظام…</div>`;
  const [healthR, readyR, bridgeR, govR, resR, provR, tfR] = await Promise.all([
    API.get('/api/v1/health/v1'), API.get('/api/v1/readyz'), API.get('/api/v1/bridge/status'),
    API.get('/api/v1/governance/status'), API.get('/api/v1/research/status'),
    API.get('/api/v1/providers'), API.get('/api/v1/timeframes'),
  ]);
  const health = unwrap(healthR) || {}, ready = unwrap(readyR) || {};
  const bridge = unwrap(bridgeR) || {}, gov = unwrap(govR) || {};
  const res = unwrap(resR) || {}, prov = unwrap(provR) || {};
  const tfs = unwrap(tfR) || [];

  const kv = (o) => Object.keys(o).length
    ? Object.entries(o).slice(0, 10).map(([k, v]) => row(esc(k),
        typeof v === 'object' ? `<span class="mono">${esc(JSON.stringify(v)).slice(0, 60)}</span>` : esc(String(v)))).join('')
    : emptyBox('غير متاح');

  let tfBody = '';
  if (!tfs.length) tfBody = emptyBox('لا توجد إطارات زمنية');
  else {
    tfBody = `<table><tr><th>الإطار</th><th>الجودة</th><th>الحداثة</th><th>مغلقة</th><th>العمر</th></tr>`;
    for (const t of tfs)
      tfBody += `<tr><td>${esc(t.timeframe)}</td><td>${qualityPill(t.quality)}</td>
        <td>${freshnessPill(t.freshness)}</td>
        <td>${t.has_closed_bar ? pill('نعم', 'ok') : pill('لا', 'warn')}</td>
        <td class="mono">${ageLabel(t.freshness && t.freshness.age_millis)}</td></tr>`;
    tfBody += `</table>`;
  }

  let html = `<div class="banner info">وضع الظل فقط — النظام لا ينفّذ أي عملية تداول.</div>
  <div class="grid cols-4">
    ${card('', metric('الحالة', esc(health.status || '—'),
        ready.ready !== undefined ? (ready.ready ? 'جاهز' : 'غير جاهز') : ''))}
    ${card('', metric('الجسر', esc(bridge.status || health.bridge || '—'), ''))}
    ${card('', metric('الإصدار', esc(health.version || '—'), `العمل: ${num(health.uptime_sec, 0)} ث`))}
    ${card('', metric('المزوّد', esc(prov.provider || prov.active || '—'), ''))}
  </div>
  <div style="margin-top:16px">${card('حالة الإطارات الزمنية', tfBody)}</div>
  <div class="grid cols-3" style="margin-top:16px">
    ${card('الحكامة', kv(gov))}
    ${card('البحث', kv(res))}
    ${card('المزوّدون', kv(prov))}
  </div>`;
  main.innerHTML = html;
}

/* ---- shell / routing --------------------------------------------------- */
const TABS = {
  overview:  { ar: 'النظرة العامة', render: renderOverview },
  analysis:  { ar: 'التحليل', render: renderAnalysis },
  news:      { ar: 'الأخبار', render: renderNews },
  history:   { ar: 'السجل', render: renderHistory },
  ecosystem: { ar: 'المنظومة', render: renderEcosystem },
  system:    { ar: 'حالة النظام', render: renderSystem },
};

async function setStatus() {
  const r = await API.get('/api/v1/health/v1');
  const el = document.getElementById('conn-status');
  if (!el) return;
  const h = unwrap(r) || {};
  const ok = r.ok && h.status !== 'down';
  el.innerHTML = `<span class="dot ${ok ? 'ok' : 'bad'}"></span> ${ok ? 'متصل' : 'غير متصل'}`;
}

function route(tab) {
  if (!TABS[tab]) tab = 'overview';
  state.tab = tab;
  document.querySelectorAll('.nav-item').forEach((b) =>
    b.classList.toggle('active', b.dataset.tab === tab));
  const main = document.getElementById('main');
  TABS[tab].render(main).catch((e) => {
    main.innerHTML = `<div class="banner bad">تعذّر تحميل هذا القسم: ${esc(e.message)}</div>`;
  });
}

function boot() {
  document.querySelectorAll('.nav-item').forEach((b) => {
    b.onclick = () => { route(b.dataset.tab); document.querySelector('.sidebar').classList.remove('open'); };
  });
  const mt = document.getElementById('menu-toggle');
  if (mt) mt.onclick = () => document.querySelector('.sidebar').classList.toggle('open');
  setStatus();
  setInterval(setStatus, 30000);
  route('overview');
}

document.addEventListener('DOMContentLoaded', boot);


