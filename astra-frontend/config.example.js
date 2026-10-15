// ASTRA frontend runtime configuration.
//
// This file is a PLACEHOLDER for deployments. Cloudflare Pages injects the
// production value via an environment variable:
//
//   CF_PAGES_ENV_VARS `BACKEND_BASE=https://<railway-service>.up.railway.app`
//
// and the build script (or the wrangler command below) writes the real file
// into the output directory:
//
//   wrangler pages project create astra \
//     --production-branch main
//   BACKEND_BASE=https://... npx wrangler pages deploy astra-frontend \
//     --project-name astra
//
// README: the value here must NOT contain browser-only secrets. Only a public
// HTTPS read base URL belongs here. Sender tokens and admin credentials are
// never placed in the frontend.
window.ASTRA_BACKEND_BASE = "https://REPLACE_WITH_BACKEND_URL";
window.ASTRA_REFRESH_MS = 30000;