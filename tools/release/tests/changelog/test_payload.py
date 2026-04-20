from __future__ import annotations

from pathlib import Path
import unittest

from tools.release.changelog.models import CategoryContext, CommitInfo, DiffSnippet, FileChange, ReleaseContext
from tools.release.changelog.payload import (
    AI_PAYLOAD_SCHEMA_VERSION,
    PayloadBuildConfig,
    PayloadLimits,
    build_ai_payload,
    render_ai_payload_json,
)


FIXTURES_DIR = Path(__file__).parent / "fixtures"


def _load_fixture(name: str) -> str:
    return (FIXTURES_DIR / name).read_text(encoding="utf-8")


def _multi_category_context() -> ReleaseContext:
    debian = CategoryContext(
        name="debian",
        commit_count=1,
        insertions=2,
        deletions=1,
        commits=[
            CommitInfo(
                sha="aaaaaaaabbbbbbbb1",
                subject="Adjust package install destinations",
                body="",
                files=["debian/install"],
                insertions=2,
                deletions=1,
                categories=["debian"],
            ),
        ],
        files=[
            FileChange(
                path="debian/install",
                category="debian",
                subscopes=("install",),
                insertions=2,
                deletions=1,
                commit_count=1,
                score=6.2,
                flags=("packaging",),
            ),
        ],
        snippets=[],
        detected_themes=["packaging"],
    )

    tools = CategoryContext(
        name="tools",
        commit_count=2,
        insertions=14,
        deletions=4,
        commits=[
            CommitInfo(
                sha="bbbbbbbbcccccccc2",
                subject="Refine release payload determinism",
                body="",
                files=["tools/release/changelog/payload.py"],
                insertions=9,
                deletions=2,
                categories=["tools"],
            ),
            CommitInfo(
                sha="ccccccccdddddddd3",
                subject="Expand release CLI payload command tests",
                body="",
                files=["tools/release/tests/test_cli_changelog.py"],
                insertions=5,
                deletions=2,
                categories=["tools"],
            ),
        ],
        files=[
            FileChange(
                path="tools/release/changelog/payload.py",
                category="tools",
                subscopes=("release", "changelog"),
                insertions=9,
                deletions=2,
                commit_count=1,
                score=14.6,
                flags=("release-tooling",),
            ),
            FileChange(
                path="tools/release/tests/test_cli_changelog.py",
                category="tools",
                subscopes=("release", "tests"),
                insertions=5,
                deletions=2,
                commit_count=1,
                score=10.1,
                flags=("release-tooling",),
            ),
        ],
        snippets=[
            DiffSnippet(
                path="tools/release/changelog/payload.py",
                category="tools",
                subscopes=("release", "changelog"),
                score=8.4,
                reason="Selected from high-scoring tools file; flags: release-tooling",
                patch="@@ -20,6 +20,14 @@\n+def build_ai_payload(...):\n+    return payload",
                flags=("release-tooling",),
            ),
        ],
        detected_themes=["release-automation", "tools"],
    )

    core = CategoryContext(
        name="core",
        commit_count=3,
        insertions=48,
        deletions=16,
        commits=[
            CommitInfo(
                sha="ddddddddeeeeeeee4",
                subject="Harden websocket server handshake flow",
                body="",
                files=["core/protocols/websocket/server.cpp"],
                insertions=22,
                deletions=7,
                categories=["core"],
            ),
            CommitInfo(
                sha="eeeeeeeeffffffff5",
                subject="Improve fuse daemon mount cleanup on shutdown",
                body="",
                files=["core/fuse/mount_service.cpp"],
                insertions=18,
                deletions=6,
                categories=["core"],
            ),
            CommitInfo(
                sha="ffffffff111111116",
                subject="Refine usage parser aliases for vh shell commands",
                body="",
                files=["core/usage/parser.cpp"],
                insertions=8,
                deletions=3,
                categories=["core"],
            ),
        ],
        files=[
            FileChange(
                path="core/protocols/websocket/server.cpp",
                category="core",
                subscopes=("protocols", "websocket"),
                insertions=22,
                deletions=7,
                commit_count=1,
                score=19.2,
                flags=("implementation",),
            ),
            FileChange(
                path="core/fuse/mount_service.cpp",
                category="core",
                subscopes=("fuse", "mount_service.cpp"),
                insertions=18,
                deletions=6,
                commit_count=1,
                score=18.4,
                flags=("filesystem", "implementation"),
            ),
            FileChange(
                path="core/usage/parser.cpp",
                category="core",
                subscopes=("usage", "parser.cpp"),
                insertions=8,
                deletions=3,
                commit_count=1,
                score=11.8,
                flags=("implementation",),
            ),
        ],
        snippets=[
            DiffSnippet(
                path="core/protocols/websocket/server.cpp",
                category="core",
                subscopes=("protocols", "websocket"),
                score=9.8,
                reason="Selected from high-scoring core file; flags: implementation",
                patch=(
                    "@@ -101,6 +101,10 @@\n"
                    "+if (!validate_client_nonce(request)) {\n"
                    "+    return handshake_error(\"invalid nonce\");\n"
                    "+}\n"
                ),
                flags=("implementation",),
            ),
            DiffSnippet(
                path="core/fuse/mount_service.cpp",
                category="core",
                subscopes=("fuse", "mount_service.cpp"),
                score=9.1,
                reason="Selected from high-scoring core file; flags: filesystem, implementation",
                patch=(
                    "@@ -44,7 +44,12 @@\n"
                    "-cleanup_mount_point(path);\n"
                    "+if (is_mount_active(path)) {\n"
                    "+    cleanup_mount_point(path);\n"
                    "+}\n"
                ),
                flags=("filesystem", "implementation"),
            ),
        ],
        detected_themes=["filesystem", "core", "protocols"],
    )

    meta = CategoryContext(
        name="meta",
        commit_count=1,
        insertions=0,
        deletions=0,
        commits=[
            CommitInfo(
                sha="11111111222222227",
                subject="Touch README heading",
                body="",
                files=["README.md"],
                insertions=0,
                deletions=0,
                categories=["meta"],
            ),
        ],
        files=[
            FileChange(
                path="README.md",
                category="meta",
                subscopes=("README.md",),
                insertions=0,
                deletions=0,
                commit_count=1,
                score=1.2,
                flags=(),
            ),
        ],
        snippets=[],
        detected_themes=[],
    )

    return ReleaseContext(
        version="2.4.0",
        previous_tag="v2.3.1",
        head_sha="1234567890abcdef",
        commit_count=7,
        categories={"meta": meta, "core": core, "debian": debian, "tools": tools},
        cross_cutting_notes=[],
    )


def _truncation_context() -> ReleaseContext:
    core = CategoryContext(
        name="core",
        commit_count=5,
        insertions=110,
        deletions=40,
        commits=[
            CommitInfo(
                sha="22222222333333338",
                subject="Very long subject alpha for truncation behavior checks",
                body="",
                files=["core/src/runtime/alpha.cpp"],
                insertions=30,
                deletions=8,
                categories=["core"],
            ),
            CommitInfo(
                sha="33333333444444449",
                subject="Very long subject beta for truncation behavior checks",
                body="",
                files=["core/src/runtime/beta.cpp"],
                insertions=25,
                deletions=7,
                categories=["core"],
            ),
            CommitInfo(
                sha="4444444455555555a",
                subject="Very long subject gamma for truncation behavior checks",
                body="",
                files=["core/src/runtime/gamma.cpp"],
                insertions=18,
                deletions=9,
                categories=["core"],
            ),
            CommitInfo(
                sha="5555555566666666b",
                subject="Very long subject delta for truncation behavior checks",
                body="",
                files=["core/src/runtime/delta.cpp"],
                insertions=21,
                deletions=6,
                categories=["core"],
            ),
            CommitInfo(
                sha="6666666677777777c",
                subject="Very long subject epsilon for truncation behavior checks",
                body="",
                files=["core/src/runtime/epsilon.cpp"],
                insertions=16,
                deletions=10,
                categories=["core"],
            ),
        ],
        files=[
            FileChange(
                path="core/src/runtime/alpha.cpp",
                category="core",
                subscopes=("src", "runtime"),
                insertions=30,
                deletions=8,
                commit_count=2,
                score=21.1,
                flags=("implementation",),
            ),
            FileChange(
                path="core/src/runtime/beta.cpp",
                category="core",
                subscopes=("src", "runtime"),
                insertions=25,
                deletions=7,
                commit_count=2,
                score=20.4,
                flags=("implementation",),
            ),
            FileChange(
                path="core/src/runtime/gamma.cpp",
                category="core",
                subscopes=("src", "runtime"),
                insertions=18,
                deletions=9,
                commit_count=1,
                score=18.8,
                flags=("implementation",),
            ),
            FileChange(
                path="core/src/runtime/delta.cpp",
                category="core",
                subscopes=("src", "runtime"),
                insertions=21,
                deletions=6,
                commit_count=1,
                score=18.1,
                flags=("implementation",),
            ),
        ],
        snippets=[
            DiffSnippet(
                path="core/src/runtime/alpha.cpp",
                category="core",
                subscopes=("src", "runtime"),
                score=10.2,
                reason="Very long snippet reason alpha that should be clipped for payload compactness",
                patch=(
                    "@@ -1,8 +1,12 @@\n"
                    "+line1\n"
                    "+line2\n"
                    "+line3\n"
                    "+line4\n"
                    "+line5\n"
                    "+line6\n"
                ),
                flags=("implementation",),
            ),
            DiffSnippet(
                path="core/src/runtime/beta.cpp",
                category="core",
                subscopes=("src", "runtime"),
                score=9.7,
                reason="Very long snippet reason beta that should be clipped for payload compactness",
                patch="@@ -1,2 +1,2 @@\n+only one line\n",
                flags=("implementation",),
            ),
            DiffSnippet(
                path="core/src/runtime/gamma.cpp",
                category="core",
                subscopes=("src", "runtime"),
                score=9.1,
                reason="Very long snippet reason gamma that should be clipped for payload compactness",
                patch="@@ -1,2 +1,2 @@\n+only one line\n",
                flags=("implementation",),
            ),
        ],
        detected_themes=["core", "runtime"],
    )

    meta = CategoryContext(
        name="meta",
        commit_count=1,
        insertions=0,
        deletions=0,
        commits=[],
        files=[],
        snippets=[],
        detected_themes=[],
    )

    return ReleaseContext(
        version="2.4.1",
        previous_tag="v2.4.0",
        head_sha="fedcba0987654321",
        commit_count=6,
        categories={"meta": meta, "core": core},
        cross_cutting_notes=[],
    )


class PayloadContractTests(unittest.TestCase):
    maxDiff = None

    def test_schema_and_stable_default_payload_fixture(self) -> None:
        payload = build_ai_payload(_multi_category_context())
        rendered = render_ai_payload_json(payload)
        self.assertEqual(rendered, _load_fixture("payload_multi_category.json"))

        self.assertEqual(payload["schema_version"], AI_PAYLOAD_SCHEMA_VERSION)
        self.assertEqual([item["name"] for item in payload["categories"]], ["debian", "tools", "core", "meta"])
        self.assertEqual(payload["generation"]["truncation"]["any_truncation"], False)

    def test_truncation_rules_fixture(self) -> None:
        config = PayloadBuildConfig(
            limits=PayloadLimits(
                max_categories=1,
                max_commits_per_category=2,
                max_files_per_category=2,
                max_snippets_per_category=1,
                max_snippet_lines=3,
                max_snippet_chars=60,
                max_commit_subject_chars=24,
                max_snippet_reason_chars=28,
            ),
            include_weak_categories=True,
        )
        payload = build_ai_payload(_truncation_context(), config=config)
        rendered = render_ai_payload_json(payload)
        self.assertEqual(rendered, _load_fixture("payload_truncated.json"))

        truncation = payload["generation"]["truncation"]
        self.assertEqual(truncation["categories_truncated_by_limit"], ["meta"])
        self.assertIn("core", truncation["categories_with_evidence_truncated"])
        self.assertTrue(truncation["any_truncation"])

    def test_include_weak_categories_toggle(self) -> None:
        context = _multi_category_context()

        with_weak = build_ai_payload(context, config=PayloadBuildConfig(include_weak_categories=True))
        without_weak = build_ai_payload(context, config=PayloadBuildConfig(include_weak_categories=False))

        self.assertIn("meta", [item["name"] for item in with_weak["categories"]])
        self.assertNotIn("meta", [item["name"] for item in without_weak["categories"]])
        self.assertIn(
            "Weak-signal categories omitted by configuration: `debian`, `meta`",
            without_weak["notes"],
        )

    def test_payload_json_is_byte_stable(self) -> None:
        context = _truncation_context()
        config = PayloadBuildConfig(
            limits=PayloadLimits(
                max_categories=1,
                max_commits_per_category=2,
                max_files_per_category=2,
                max_snippets_per_category=1,
                max_snippet_lines=3,
                max_snippet_chars=60,
                max_commit_subject_chars=24,
                max_snippet_reason_chars=28,
            )
        )
        first = render_ai_payload_json(build_ai_payload(context, config=config))
        second = render_ai_payload_json(build_ai_payload(context, config=config))
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
