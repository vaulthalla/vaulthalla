# Bugfix Checklist

- Root cause is identified (not just symptom patching).
- Reproduction steps are captured.
- Fix includes guardrails for adjacent edge cases.
- Regression surface is evaluated.
- Error handling and user-facing messaging are sane.
- Types are tightened where ambiguity caused the issue.
- Verification includes before/after behavior checks.
- `bash .codex/scripts/changed.sh all` passes.
- Run subsystem verification:
- Web scope: `bash .codex/scripts/verify.sh web`
- Release/version scope: `bash .codex/scripts/verify.sh release`
- Cross-cutting scope: `bash .codex/scripts/verify.sh all`
