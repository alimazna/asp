# BACKEND_BUILD_ORDER.md

## Wave order
1. Build + foundation
2. Resilience
3. Python MT5 bridge
4. C++ data ingestion + Windows host/startup
5. Deterministic decision chain
6. Shadow + outcomes + persistence + health
7. Learning/research
8. Evolution
9. Validation
10. Governance
11. Operating window/recovery
12. Telegram backend
13. Backend/frontend API + packaging
14. Full review + acceptance

## Gate rule
Do not begin Alpha frontend implementation until the backend review gate is PASS and `PROJECT_STATE.md` says `BACKEND_APPROVED_FOR_FRONTEND`.
