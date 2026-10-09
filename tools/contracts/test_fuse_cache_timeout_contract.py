"""Contract: FUSE replies never let the kernel cache metadata (#183).

The kernel's dentry/attr cache is shared across uids, and the mount doesn't use default_permissions (RBAC lives in the
daemon). A reply with a non-zero entry or attr timeout let any caller stat a path another uid had just resolved,
without the daemon being asked: up to 60 s after a create. Every timeout in core/src/fuse must be the zero constant
`kKernelMetadataTimeout`, and that constant must stay 0.

Run by module: python3 -m unittest tools.contracts.test_fuse_cache_timeout_contract
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
FUSE_SOURCES = REPO_ROOT / "core" / "src" / "fuse"
CONSTANT = "kKernelMetadataTimeout"

# `e.attr_timeout = <value>;`, `entry.entry_timeout = <value>;`
TIMEOUT_ASSIGNMENT_RE = re.compile(r"\b(?:attr|entry)_timeout\s*=\s*([^;]+);")
# `fuse_reply_attr(req, &st, <value>)`
REPLY_ATTR_RE = re.compile(r"\bfuse_reply_attr\s*\(([^;]*)\)\s*;")
CONSTANT_DEFINITION_RE = re.compile(r"constexpr\s+double\s+" + CONSTANT + r"\s*=\s*([^;]+);")


def timeout_offenders(text: str) -> list[tuple[int, str]]:
    """(line, value) for every reply timeout that isn't the named zero constant."""
    found: list[tuple[int, str]] = []
    for match in TIMEOUT_ASSIGNMENT_RE.finditer(text):
        value = match.group(1).strip()
        if value != CONSTANT:
            found.append((text.count("\n", 0, match.start()) + 1, value))
    for match in REPLY_ATTR_RE.finditer(text):
        args = [arg.strip() for arg in match.group(1).split(",")]
        timeout = args[-1] if len(args) >= 3 else ""
        if timeout != CONSTANT and timeout != "timeout":  # Reply::attr forwards its parameter
            found.append((text.count("\n", 0, match.start()) + 1, timeout))
    return found


class FuseCacheTimeoutContractTests(unittest.TestCase):
    def test_detector_catches_literal_timeouts(self) -> None:
        # Guards the guard: the pre-#183 replies are caught ...
        self.assertTrue(timeout_offenders("e.attr_timeout  = 60.0;"))
        self.assertTrue(timeout_offenders("e.entry_timeout = 0.1;"))
        self.assertTrue(timeout_offenders("fuse_reply_attr(req, &st, 0.1); // match lookup"))
        # ... the constant is not.
        self.assertFalse(timeout_offenders(f"e.attr_timeout = {CONSTANT};"))
        self.assertFalse(timeout_offenders(f"fuse_reply_attr(req, &st, {CONSTANT});"))

    def test_constant_is_zero(self) -> None:
        definitions = [
            m.group(1).strip()
            for path in sorted(FUSE_SOURCES.rglob("*.cpp"))
            for m in CONSTANT_DEFINITION_RE.finditer(path.read_text(encoding="utf-8"))
        ]
        self.assertEqual(len(definitions), 1, f"expected one definition of {CONSTANT}, found {definitions}")
        self.assertIn(definitions[0], {"0", "0.0"}, f"{CONSTANT} must stay 0 (#183)")

    def test_every_fuse_reply_timeout_uses_the_zero_constant(self) -> None:
        offenders = [
            f"{path.relative_to(REPO_ROOT)}:{line}: {value}"
            for path in sorted(FUSE_SOURCES.rglob("*.cpp"))
            for line, value in timeout_offenders(path.read_text(encoding="utf-8"))
        ]
        self.assertEqual(offenders, [], "FUSE reply timeouts must be kKernelMetadataTimeout (#183):\n" + "\n".join(offenders))


if __name__ == "__main__":
    unittest.main()
