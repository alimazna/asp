#!/usr/bin/env bash
# تشغيل منظومة AYZR الكاملة (باك اند + مركز المراقبة) للعرض
set -u
cd "$(dirname "$0")/../.." 2>/dev/null || cd /workspace/project/asp
DATA="${AYZR_DATA_DIR:-/tmp/ayzr_web_data}"
PORT_PUB="${PORT:-12000}"
PORT_BE="${AYZR_BACKEND_PORT:-8081}"

pkill -f "build-ayzr/astra_server" 2>/dev/null
pkill -f "ayzr_observatory/serve_public.py" 2>/dev/null
sleep 1
rm -rf "$DATA"; mkdir -p "$DATA"

ASTRA_LLM_PROVIDER=mock ASTRA_CLIENT_TOKEN="${AYZR_CLIENT_TOKEN:-client-tok}" \
  build-ayzr/astra_server --host 127.0.0.1 --port "$PORT_BE" --data-dir "$DATA" \
  --sender-token "${AYZR_SENDER_TOKEN:-sender-tok}" \
  --admin-token "${AYZR_ADMIN_TOKEN:-admin-tok}" > /tmp/web_backend.log 2>&1 &
for i in $(seq 1 80); do curl -sf "http://127.0.0.1:$PORT_BE/api/v1/livez" >/dev/null 2>&1 && break; sleep 0.25; done

python3 - "$PORT_BE" "${AYZR_SENDER_TOKEN:-sender-tok}" <<'PY'
import json,sys,urllib.request
port,tok=sys.argv[1],sys.argv[2]
step=900; end=1793102400; n=60
bars=[{"time":end-(n-1-i)*step,"open":1900.0+i*1.5,"high":1900.0+i*1.5+1.0,
       "low":1900.0+i*1.5-1.0,"close":1900.0+i*1.5+0.5,"tick_volume":100,"spread":10,"real_volume":0}
      for i in range(n)]
body={"source_id":"web-demo","symbol":"XAUUSD","timeframe":"M15","candles":bars}
req=urllib.request.Request(f"http://127.0.0.1:{port}/api/v1/candles/ingest",
    data=json.dumps(body).encode(),
    headers={"Content-Type":"application/json","Authorization":f"Bearer {tok}"})
print("ingest:", len(json.load(urllib.request.urlopen(req))["data"]["accepted"]))
PY

AYZR_BACKEND="http://127.0.0.1:$PORT_BE" AYZR_CLIENT_TOKEN="${AYZR_CLIENT_TOKEN:-client-tok}" \
  PORT="$PORT_PUB" AYZR_ALLOW_PUBLIC=1 AYZR_HOST=0.0.0.0 \
  python3 tools/ayzr_observatory/serve_public.py > /tmp/web_public.log 2>&1 &
echo "observatory started on 0.0.0.0:$PORT_PUB"
