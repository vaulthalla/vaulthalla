# Feature Checklist

- Requirements are explicit, including non-goals.
- UX path is validated for mobile and desktop.
- Data model/API contracts are confirmed before coding.
- Edge cases and empty/error states are implemented.
- Types are strict; avoid `any`.
- Existing patterns/components are reused when appropriate.
- Tests or manual verification steps are documented.
- `bash .codex/scripts/changed.sh all` passes.
- Run subsystem verification:
- Web scope: `bash .codex/scripts/verify.sh web`
- Release/version scope: `bash .codex/scripts/verify.sh release`
- Cross-cutting scope: `bash .codex/scripts/verify.sh all`
- Handoff includes changed files, behavior summary, and risks.
