// Opt-in integration test: runs the real API client against the real dev
// backend (scripts/mock_api.py). Cross-platform, so the same command works in
// PowerShell, cmd.exe, bash and zsh.
//
//   node scripts/test-live.mjs                 // expects the mock on :8790
//   node scripts/test-live.mjs http://127.0.0.1:8792/api/v1/
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const base = process.argv[2] ?? "http://127.0.0.1:8790/api/v1/";

const child = spawn(
  process.platform === "win32" ? "npx.cmd" : "npx",
  ["vitest", "run", "tests/live-mock.test.ts"],
  {
    cwd: root,
    stdio: "inherit",
    shell: process.platform === "win32",
    env: { ...process.env, ASTRA_LIVE_MOCK: "1", ASTRA_LIVE_BASE: base },
  },
);

child.on("exit", (code) => process.exit(code ?? 1));
