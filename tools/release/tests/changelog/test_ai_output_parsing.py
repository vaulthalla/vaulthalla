from __future__ import annotations

import unittest

from tools.release.changelog.ai.providers.parsing import parse_json_object_from_text


class AIOutputParsingTests(unittest.TestCase):
    def test_plain_json_object_parses(self) -> None:
        parsed = parse_json_object_from_text('{"ok":true,"value":1}')
        self.assertEqual(parsed["ok"], True)
        self.assertEqual(parsed["value"], 1)

    def test_json_fence_wrapper_parses(self) -> None:
        parsed = parse_json_object_from_text("Noise\n```json\n{\"ok\":true}\n```\nTail")
        self.assertEqual(parsed["ok"], True)

    def test_json_with_prefix_suffix_parses(self) -> None:
        parsed = parse_json_object_from_text("prefix {\"ok\": true, \"nested\": {\"x\": 1}} suffix")
        self.assertEqual(parsed["nested"]["x"], 1)

    def test_common_single_key_envelope_is_unwrapped(self) -> None:
        parsed = parse_json_object_from_text('{"result":{"title":"x","summary":"y","sections":[]}}')
        self.assertEqual(parsed["title"], "x")
        self.assertIn("sections", parsed)

    def test_non_object_json_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "JSON root must be object"):
            parse_json_object_from_text("[1,2,3]")

    def test_malformed_json_fails_clearly(self) -> None:
        with self.assertRaisesRegex(ValueError, "could not recover valid JSON object"):
            parse_json_object_from_text("not json {broken")


if __name__ == "__main__":
    unittest.main()
