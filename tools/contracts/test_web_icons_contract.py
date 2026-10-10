"""Contract: console icons take their color from CSS, cyan by default (#188).

The FontAwesome SVGs carry no fill, so a path paints black unless SVGR puts fill="currentColor" on the root. SVGR has
no `fill` option: passing one is silently ignored, which is how every icon rendered black.
"""

from __future__ import annotations

from pathlib import Path
import re
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]


class WebIconsContractTests(unittest.TestCase):
    config = (REPO_ROOT / "web/next.config.ts").read_text(encoding="utf-8")
    css = (REPO_ROOT / "web/src/app/globals.css").read_text(encoding="utf-8")

    def test_svgr_sets_fill_through_svg_props_for_both_bundlers(self) -> None:
        options = re.search(r"const svgrOptions = (\{.*\})\n", self.config)
        self.assertIsNotNone(options, "one svgrOptions object shared by turbopack and webpack")
        self.assertRegex(options.group(1), r"svgProps: \{[^}]*fill: 'currentColor'")
        self.assertIn("'data-vh-icon'", options.group(1))
        self.assertEqual(self.config.count("options: svgrOptions"), 2)
        self.assertNotRegex(self.config, r"loader: '@svgr/webpack', options: \{")

    def test_icons_default_to_the_accent_in_the_base_layer(self) -> None:
        base = self.css.split("@layer base {", 1)[1]
        self.assertRegex(base, r"svg\[data-vh-icon\] \{\s*color: var\(--accent-text\);")


if __name__ == "__main__":
    unittest.main()
