#!/bin/bash
# Container entrypoint: run the ASTRA backend and, unless disabled, the
# perpetual biquote candle feeder beside it.
#
# One image, one source of candles. The feeder posts to the backend over
# loopback, so no external collector and no outbound service other than
# biquote itself is required. If either process dies the entrypoint exits and
# the platform's restart policy (Railway ON_FAILURE) brings the container back.
#
# Env:
#   PORT                     - backend listen port (Railway assigns it).
#   ASTRA_SENDER_TOKEN       - required: feeder uses it to POST candles.
#   ASTRA_CLIENT_TOKEN       - read-only token for the AYZR app.
#   ASTRA_ADMIN_TOKEN        - optional admin token.
#   ASTRA_ALLOWED_ORIGINS    - CORS origins (comma-separated).
#   AYZR_FEEDER_ENABLED      - "0" to run the backend without the feeder.
#   AYZR_FEEDER_INTERVAL_S   - poll interval (default 30).
#   AYZR_FEEDER_BACKFILL     - bars per timeframe on the feeder's first run
#                              (default 500).
#   SUPABASE_URL             - optional; enables the Supabase candle archive.
#   SUPABASE_SECRET_KEY      - optional; secret key for the archive.
#   SUPABASE_TABLE           - archive table (default candles).
set -u

PORT="${PORT:-8080}"
cd /opt/astra

echo "[entrypoint] starting astra_server on 0.0.0.0:${PORT}"
./astra_server --host=0.0.0.0 --port "${PORT}" &
SERVER_PID=$!

# Wait for the backend to answer before the feeder starts posting.
for _ in $(seq 1 30); do
    if curl -fsS "http://127.0.0.1:${PORT}/api/v1/livez" >/dev/null 2>&1; then
        echo "[entrypoint] backend ready"
        break
    fi
    sleep 1
done

FEEDER_PID=""
if [ "${AYZR_FEEDER_ENABLED:-1}" != "0" ]; then
    echo "[entrypoint] starting biquote feeder"
    if [ -n "${SUPABASE_URL:-}" ] && [ -n "${SUPABASE_SECRET_KEY:-}" ]; then
        echo "[entrypoint] supabase candle archive enabled"
    fi
    python3 ./biquote_sender.py \
        --base-url "http://127.0.0.1:${PORT}/api/v1" \
        --token "${ASTRA_SENDER_TOKEN:-}" \
        --source-id biquote \
        --backfill "${AYZR_FEEDER_BACKFILL:-500}" \
        --loop --interval-seconds "${AYZR_FEEDER_INTERVAL_S:-30}" \
        --state "${ASTRA_DATA_DIR:-/opt/astra/data}/biquote_state.json" \
        --supabase-url "${SUPABASE_URL:-}" \
        --supabase-key "${SUPABASE_SECRET_KEY:-}" \
        --supabase-table "${SUPABASE_TABLE:-candles}" &
    FEEDER_PID=$!
fi

# Block until either process exits, then propagate for a platform restart.
if [ -n "${FEEDER_PID}" ]; then
    wait -n "${SERVER_PID}" "${FEEDER_PID}"
else
    wait "${SERVER_PID}"
fi
echo "[entrypoint] a child exited; stopping container for restart"
kill "${SERVER_PID}" "${FEEDER_PID:-}" 2>/dev/null || true
exit 1
