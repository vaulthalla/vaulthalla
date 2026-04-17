# Feature Checklist

- Requirements are explicit, including non-goals.
- UX path is validated for mobile and desktop.
- Data model/API contracts are confirmed before coding.
- Edge cases and empty/error states are implemented.
- Types are strict; avoid `any`.
- Existing patterns/components are reused when appropriate.
- Tests or manual verification steps are documented.
- `bash .codex/scripts/changed.sh all` passes.
- `bash .codex/scripts/verify.sh` passes before handoff.
- Handoff includes changed files, behavior summary, and risks.
