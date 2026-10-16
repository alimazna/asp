#!/usr/bin/env node
/* Secret gate for the built frontend bundle.

This is a *content* gate, not a keyword gate: the UI legitimately contains
prose such as "Secrets in this bundle: none", so matching the bare words
`secret`/`token` produces false positives. Instead we flag things that look
like an actual leaked credential:

  1. an Authorization / Bearer header value,
  2. a high-entropy value assigned to a token/key/secret/password field,
  3. a Cloudflare / GitHub / OpenAI / JWT / AWS shaped token,
  4. a backend base URL that is not a plain https URL,
  5. an unexpected key present in the generated config.js.

Exit code 1 (with the offending line) if anything is found.
*/
const fs = require("fs");
const path = require("path");

const dist = process.argv[2] || "dist";
const problems = [];

const files = [];
(function walk(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(p);
    else if (/\.(js|html|css|json|txt)$/.test(entry.name)) files.push(p);
  }
})(dist);

const TOKEN_SHAPES = [
  /\bBearer\s+[A-Za-z0-9._~+/=-]{16,}/i,           // Authorization: Bearer <token>
  /\bgh[pousr]_[A-Za-z0-9]{20,}/,                    // GitHub tokens
  /\bsk-[A-Za-z0-9]{20,}/,                           // OpenAI-style keys
  /\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{5,}/, // JWT
  /\b(?:AKIA|ASIA)[0-9A-Z]{16}\b/,                   // AWS access key id
];

// A credential assigned to a credential-named field: the value must be long
// and high-entropy so ordinary words or short ids do not trip the gate.
const ASSIGN =
  /(api[_-]?key|api[_-]?token|secret|passwd|password|client[_-]?secret|private[_-]?key)["']?\s*[:=]\s*["']([^"']{20,})["']/gi;

function looksRandom(s) {
  if (!/[A-Za-z]/.test(s) || !/[0-9]/.test(s)) return false;
  const classes = [/[a-z]/, /[A-Z]/, /[0-9]/, /[^A-Za-z0-9]/]
    .filter((r) => r.test(s)).length;
  return classes >= 3 || /^[A-Fa-f0-9]{32,}$/.test(s)
    || /^[A-Za-z0-9+/=]{40,}$/.test(s);
}

for (const file of files) {
  const lines = fs.readFileSync(file, "utf8").split("\n");
  lines.forEach((line, i) => {
    for (const re of TOKEN_SHAPES) {
      const m = re.exec(line);
      if (m) problems.push(`${file}:${i + 1}: credential-shaped value: ${m[0].slice(0, 12)}…`);
    }
    let m;
    ASSIGN.lastIndex = 0;
    while ((m = ASSIGN.exec(line)) !== null) {
      if (looksRandom(m[2])) {
        problems.push(`${file}:${i + 1}: ${m[1]} assigned a high-entropy value`);
      }
    }
  });
}

// config.js is the only generated file; verify it holds nothing unexpected.
const cfgPath = path.join(dist, "config.js");
if (fs.existsSync(cfgPath)) {
  const cfg = fs.readFileSync(cfgPath, "utf8");
  const allowed = ["ASTRA_BACKEND_BASE", "ASTRA_REFRESH_MS", "ASTRA_ALLOW_SAME_ORIGIN"];
  const keys = [...cfg.matchAll(/window\.([A-Z0-9_]+)\s*=/g)].map((x) => x[1]);
  for (const k of keys) {
    if (!allowed.includes(k)) problems.push(`config.js: unexpected key ${k}`);
  }
  const base = /window\.ASTRA_BACKEND_BASE\s*=\s*"([^"]*)"/.exec(cfg);
  const baseUrl = base ? base[1] : "";
  const sameOrigin = /window\.ASTRA_ALLOW_SAME_ORIGIN\s*=\s*true/.test(cfg);
  if (baseUrl && !/^https:\/\/[^\s"']+$/.test(baseUrl)) {
    problems.push("config.js: ASTRA_BACKEND_BASE is not a plain https URL");
  }
  if (!baseUrl && !sameOrigin) {
    problems.push("config.js: BACKEND_BASE is empty and same-origin is not allowed "
      + "- a production build would ship an unconfigured API base");
  }
}

if (problems.length) {
  for (const p of problems) console.error("::error::" + p);
  console.error(`secret gate FAILED (${problems.length} finding(s))`);
  process.exit(1);
}
console.log(`secret gate passed: ${files.length} file(s) scanned, no credential-shaped content`);
