# AYZR — Ecosystem Surface (Phases 6–7) Verification Report

Date: 2026-10-27 (UTC). Repo `/workspace/project/asp`, branch
`feat/ayzr-ecosystem`. This pass extends the AYZR ecosystem to two read-only
clients — the Android app and the private web observatory — and verifies the
whole path against a running backend. It separates **verified facts** (commands
run, output observed) from **remaining work**.

## 1. What this pass changed

| Change | File | Why |
|--------|------|-----|
| Envelope wrapping for the four ecosystem handlers | `src/api/AstraServerMain.cpp` | The four routes emitted a bare object; the frozen contract and both clients expect `{api,schema,data}`. |
| Ecosystem models (missing field ⇒ `null`) | `ayzr-android/lib/data/models/ecosystem.dart` | Sessions, engine registry, one cycle, ledger page. An uncalibrated confidence keeps `probability:null`. |
| Ecosystem API methods + `tradeDetail` | `ayzr-android/lib/data/api_client.dart` | One call per route, plus the single-trade detail route. |
| Ecosystem `Section<>`s + loaders | `ayzr-android/lib/state/app_state.dart` | Each route has its own load phase so one failure does not blank the tab. |
| Tabbed observatory screen | `ayzr-android/lib/ui/screens/ecosystem_screen.dart` | Sessions / engines / opportunities / ledger, Arabic labels, honest empty states. |
| Top-bar entry | `ayzr-android/lib/ui/app_shell.dart` | `Icons.hub_outlined`, tooltip `Ar.ecoTitle`. |
| Arabic strings | `ayzr-android/lib/core/strings_ar.dart` | Ecosystem surface text. |
| Proxy allowlist + tab | `tools/ayzr_observatory/serve.py`, `index.html` | The four read paths are allowlisted; the "المنظومة" tab renders them; write-shaped siblings stay refused. |
| Tests | `ayzr-android/test/ecosystem_test.dart` (+18), `test/test_helpers.dart`, `test/live_integration_test.dart` (+1 live), `tools/ayzr_observatory/tests/test_observatory_security.py` (+3) | Cover the new surface, the envelope, the calibrated gate, and the proxy refusal. |
| Docs + memory | `docs/ayzr/ARCHITECTURE.md`, `docs/ayzr/memory/SHARED_MEMORY.md` | Record the surface and decisions D-07/D-08. |

## 2. Verified test totals (commands actually run)

| Suite | Command | Result |
|-------|---------|--------|
| C++ harness binaries | `cd build-ayzr && for t in *Tests; do ./"$t"; done` | **49/49 binaries, 515 cases, 0 failed, 0 assertion failures** |
| `EcosystemApiTests` | `cd build-ayzr && ./EcosystemApiTests` | **6 cases, 0 failed** (run from the build dir so the server binary resolves) |
| `AyzrSecurityTests` | `./AyzrSecurityTests` | **20 cases, 0 failed** |
| Flutter app | `cd ayzr-android && flutter test` | **153 passed, 4 skipped, 0 failed** |
| Flutter live (real backend) | `AYZR_LIVE_BASE_URL=… flutter test test/live_integration_test.dart --tags live --plain-name 'ecosystem routes…'` | **+1, all tests passed** |
| Observatory security | `python3 tests/test_observatory_security.py` | **22 passed, 0 failed** |
| Python models | `python3 -m unittest discover -s tests/models` | **308 passed, 0 failed** |
| ASTRA CORE gate | `cd research/ayzr_quant_lab && python3 tests/test_astra_protection.py` | **PASS — `aee61fa9225b4fc6…` unchanged** |
| Quant Lab | `python3 -m unittest tests.test_lab_registry` | **16 passed, 0 failed** |

## 3. End-to-end simulation (real server, synthetic data)

Server: `build-ayzr/astra_server` on `127.0.0.1`, `ASTRA_LLM_PROVIDER=mock`,
fresh data dir, client token set. A 40-bar aligned M15 batch was ingested.

```
[ingest] POST /api/v1/candles/ingest → accepted 40/40
[GET] /api/v1/ayzr/ecosystem/sessions      → {"api":"v1","schema":"1.0","data":{…}}
[GET] /api/v1/ayzr/ecosystem/engines       → envelope, 5 engines, healthAr present
[GET] /api/v1/ayzr/ecosystem/opportunities → envelope, direction NO_TRADE when nothing qualifies
[GET] /api/v1/ayzr/ecosystem/ledger?limit=5→ envelope, append-only entries
[GET] without token                        → 401
```

Observatory proxy against the same backend:

```
[GET] /                                        → 200, page carries data-tab="ecosystem"
[GET] /api/v1/ayzr/ecosystem/engines           → 200 (token added server-side; none in browser)
[GET] /api/v1/ayzr/ecosystem/ledger/append     → 403 (write-shaped sibling refused)
```

The Flutter live test drives the real `ApiClient` against the same server and
asserts every route answers with a parseable envelope and that an uncalibrated
confidence is `null` on the wire.

## 4. Failure isolation observed

- A route that fails degrades to an inline note in the observatory tab rather
  than blanking it (each fetch is independent).
- Each ecosystem `Section<>` in the app has its own `LoadPhase`, so one failed
  route shows its own error state while the others render.
- The proxy allowlist is explicit: adding an app route does not widen it.
- ASTRA CORE fingerprint unchanged; the ecosystem layer never edits ASTRA files.

## 5. Build artifact

- Debug APK: `ayzr-android/build/app/outputs/flutter-apk/app-debug.apk`
  (~106 MB, built this pass). Manifest permissions: `INTERNET` only.
- Release build still needs `android/key.properties` + a real keystore.
- No device/emulator run was possible in this environment.

## 6. Remaining work

- No device or emulator run: the APK is built and its manifest verified, not
  installed.
- Live LLM7 remains blocked on the free-tier 429 / token rotation (F-E-6 in
  `SECURITY.md`); tests use `ASTRA_LLM_PROVIDER=mock`.
- The observatory remains a loopback-only private tool; it is not deployed.
- The pasted LLM7 token in the task prompt is **not** in the repository
  (`grep` exit 1) and must be treated as compromised.
