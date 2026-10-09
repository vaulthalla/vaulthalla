"""Contract: core log calls use fmt placeholders (`{}`), never printf ones (`%s`, `%d`, ...).

spdlog formats with fmt, so a printf placeholder is printed literally and the argument meant for it is dropped
(#136: "Invalid refresh token: %s" logged the `%s` and lost the reason). This scans every logger call in the core
sources, including calls whose format string sits on the following line(s).

Run by module: python3 -m unittest tools.contracts.test_log_format_contract
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCANNED_DIRS = ("core/src", "core/include", "core/main")
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".ipp", ".tpp"}

# `log::Registry::ws()->warn(`, `logger->error(`, `spdlog::info(` ...
LOG_CALL_RE = re.compile(r"(?:->|\bspdlog::)(?:trace|debug|info|warn|error|critical)\s*\(")
# The format argument: one or more adjacent string literals (possibly split over lines).
FORMAT_LITERALS_RE = re.compile(r'\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)')
# A printf conversion: %s, %d, %5.2f, %lu, %zu, %-10s, %x, %p ... (`%%` is not one).
PRINTF_PLACEHOLDER_RE = re.compile(
    r"(?<!%)%[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L)?[sdiuxXofFeEgGaAcp]"
)


def printf_placeholders(text: str) -> list[tuple[int, str]]:
    """(line, format string) for every logger call whose format string holds a printf placeholder."""
    found: list[tuple[int, str]] = []
    for call in LOG_CALL_RE.finditer(text):
        literals = FORMAT_LITERALS_RE.match(text, call.end())
        if not literals:
            continue
        fmt = literals.group(1)
        if PRINTF_PLACEHOLDER_RE.search(fmt):
            found.append((text.count("\n", 0, call.start()) + 1, " ".join(fmt.split())))
    return found


class LogFormatContractTests(unittest.TestCase):
    def test_detector_catches_printf_placeholders(self) -> None:
        # Guards the guard: the #136 call, a multi-line call and common conversions are caught ...
        self.assertTrue(printf_placeholders('log::Registry::http()->warn("[Router]: Invalid refresh token: %s", e.what());'))
        self.assertTrue(printf_placeholders('logger->error(\n    "[X] failed for %d "\n    "items", n);'))
        self.assertTrue(printf_placeholders('spdlog::info("took %.2f ms (%zu rows)", ms, rows);'))
        # ... while fmt placeholders, literal percentages and non-log strings are not.
        self.assertFalse(printf_placeholders('logger->info("[X] {} of {} ({}% done)", a, b, pct);'))
        self.assertFalse(printf_placeholders('logger->debug("[X] escaped %% sign and {:.2f}", v);'))
        self.assertFalse(printf_placeholders('std::snprintf(buf, sizeof buf, "%s", s);'))

    def test_core_log_calls_have_no_printf_placeholders(self) -> None:
        offenders: list[str] = []
        for directory in SCANNED_DIRS:
            root = REPO_ROOT / directory
            if not root.is_dir():
                continue
            for path in sorted(root.rglob("*")):
                if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                    continue
                text = path.read_text(encoding="utf-8", errors="replace")
                for line, fmt in printf_placeholders(text):
                    offenders.append(f"{path.relative_to(REPO_ROOT)}:{line}: {fmt}")
        self.assertEqual(
            offenders,
            [],
            "printf-style placeholders in fmt/spdlog log calls print literally; use {} instead:\n"
            + "\n".join(offenders),
        )


if __name__ == "__main__":
    unittest.main()
