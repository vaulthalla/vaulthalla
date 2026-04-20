# Code Review Checklist

- Behavior regressions checked first.
- API contract changes are intentional and documented.
- Null/undefined handling is explicit.
- Async flows handle cancellation/races where relevant.
- Performance impact is reasonable for hot paths.
- Security implications considered for auth/data access.
- Packaging/deploy impact considered if touching `deploy/`, `debian/`, `bin/`, or `tools/release/`.
- No broad `any` types introduced.
- Naming and structure improve readability.
