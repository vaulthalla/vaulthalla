<!--
vl-release staged changelog: package-facing changes for the NEXT release.
Maintained continuously while working; `vlr prepare` renders it into debian/changelog
and resets this file to this template.

Format: one "- " bullet per change (concise, technical). Indent continuation lines.
Optional "## Section" headings group bullets; if used, every bullet must be under one.
One level of nested "  - " detail bullets is allowed. Consolidate; don't paste commit logs.
-->
- auth: the password dictionary check matched any >=3-letter word as a substring,
  refusing most long random passwords (the longer, the likelier). It now refuses
  only a password that is a dictionary word after trimming leading/trailing
  non-letters. One policy (Validator::passwordPolicyViolation) serves
  registration and password change; passwords of 20+ characters no longer need
  a digit; refusals name the violated rule. Drops a leaked curl handle per
  breach lookup. Regression test: test_password_policy.
