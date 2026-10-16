#!/usr/bin/env bash
# Deploy astra-frontend/ to Cloudflare Pages project `astra`.
#
# Use this when CI cannot deploy (e.g. the repository secrets are not set).
# Run it from anywhere; it locates the repo root itself.
#
# Required environment (never commit these):
#   CLOUDFLARE_API_TOKEN   token with the "Cloudflare Pages: Edit" permission
#   CLOUDFLARE_ACCOUNT_ID  the account that owns the `astra` Pages project
#
# Optional:
#   BACKEND_BASE           backend base URL baked into the bundle
#                          (default: the production Railway URL)
#   BRANCH                 Pages branch to publish to (default: main)
#
# Example:
#   CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... ./scripts/deploy-pages.sh
set -euo pipefail

if [ -z "${CLOUDFLARE_API_TOKEN:-}" ] || [ -z "${CLOUDFLARE_ACCOUNT_ID:-}" ]; then
  echo "error: CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID must both be set." >&2
  echo "       Create a token at https://dash.cloudflare.com/profile/api-tokens" >&2
  echo "       with the 'Cloudflare Pages: Edit' permission." >&2
  exit 1
fi

cd "$(dirname "$0")/.."
BACKEND_BASE="${BACKEND_BASE:-https://astra-server-production.up.railway.app}"
BRANCH="${BRANCH:-main}"

echo "== unit tests =="
node --test tests/*.test.js

echo "== build (BACKEND_BASE=$BACKEND_BASE) =="
BACKEND_BASE="$BACKEND_BASE" node build.js

echo "== secret gate =="
node scripts/secret-scan.js dist

echo "== deploy to Cloudflare Pages (project: astra, branch: $BRANCH) =="
npx --yes wrangler@3 pages deploy dist --project-name=astra --branch="$BRANCH"

echo "== done: https://astra-dia.pages.dev =="
