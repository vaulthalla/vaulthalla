# Root Cause Prompt

When debugging:

1. State observed behavior and expected behavior.
2. Identify where state diverges (data, transform, render, side-effect).
3. Isolate smallest failing path.
4. Implement fix at source of divergence.
5. Add regression guard (test/check/assertion).
