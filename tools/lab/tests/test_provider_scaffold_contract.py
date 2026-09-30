"""Contract: the TEST-ONLY provider credential scaffold stays test-only and secret-safe."""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXAMPLE = ROOT / "tools/lab/providers.env.example"
SCRIPT = ROOT / "tools/lab/test_providers.sh"
TARGET = "/etc/vaulthalla/testing"


class ProviderScaffoldContract(unittest.TestCase):
    def test_example_contains_only_placeholders(self):
        secretish = re.compile(r"^(VAULTHALLA_TEST_\w*(ACCESS_KEY|SECRET_ACCESS_KEY|BUCKET|ENDPOINT))=\"([^\"]*)\"", re.M)
        found = secretish.findall(EXAMPLE.read_text())
        self.assertEqual(len(found), 8, found)
        for name, _, value in found:
            self.assertEqual(value, "CHANGE_ME", f"{name} must stay a placeholder in the tracked example")

    def test_example_var_names_match_consumers(self):
        names = set(re.findall(r"^(VAULTHALLA_TEST_(?:S3|R2)_\w+)=", EXAMPLE.read_text(), re.M))
        consumed = set()
        for path in list((ROOT / "core").rglob("*.cpp")) + list((ROOT / "tools/smoke").glob("*.sh")):
            consumed |= set(re.findall(r"VAULTHALLA_TEST_(?:S3|R2)_[A-Z_]+", path.read_text(errors="ignore")))
        self.assertTrue(consumed, "expected existing consumers of VAULTHALLA_TEST_* vars")
        self.assertTrue(consumed <= names, f"consumers read vars missing from example: {sorted(consumed - names)}")

    def test_runtime_and_diagnostics_never_read_the_file(self):
        offenders = []
        roots = [ROOT / "core/src", ROOT / "core/include", ROOT / "core/seed", ROOT / "deploy", ROOT / "debian", ROOT / "web/src"]
        for base in roots:
            for path in base.rglob("*"):
                if path.is_file() and path.suffix not in {".png", ".ico", ".woff2"}:
                    if TARGET in path.read_text(errors="ignore") and path.name not in {"postrm"}:
                        offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [], "only harness tooling may reference the test provider file")

    def test_maintainer_scripts_never_recursively_delete_etc_vaulthalla(self):
        for name in ("preinst", "postinst", "prerm", "postrm"):
            path = ROOT / "debian" / name
            if not path.exists():
                continue
            text = path.read_text()
            self.assertNotRegex(text, r"rm\s+-[a-zA-Z]*r[a-zA-Z]*\s+[\"']?/etc/vaulthalla(/testing)?[\"']?(\s|$)", name)

    def test_filled_copies_are_gitignored(self):
        res = subprocess.run(["git", "check-ignore", "-q", "tools/lab/providers.env"], cwd=ROOT)
        self.assertEqual(res.returncode, 0, "tools/lab/providers.env must be gitignored")
        res = subprocess.run(["git", "check-ignore", "-q", "tools/lab/providers.env.example"], cwd=ROOT)
        self.assertNotEqual(res.returncode, 0, "the example must stay tracked")

    def test_check_mode_never_prints_values(self):
        text = SCRIPT.read_text()
        self.assertIn("never printed", text)
        self.assertNotRegex(text, r"echo\s+\"?\\?\$\{?!", "check must not echo indirect variable values")


if __name__ == "__main__":
    unittest.main()
