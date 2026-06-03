from __future__ import annotations

import argparse
import os
from pathlib import Path

from tools.release.shipped_release import (
    record_release_success,
    render_release_base_resolution_json,
    render_release_base_summary,
    resolve_release_notes_base,
)
from tools.release.version.adapters.version_file import read_version_file


def cmd_resolve_release_notes_base(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    version = args.version or str(read_version_file(repo_root / "VERSION"))
    resolution = resolve_release_notes_base(
        repo_root=repo_root,
        version=version,
        override_tag=args.release_notes_base,
        env=os.environ,
    )
    rendered_json = render_release_base_resolution_json(resolution)
    if args.output:
        target = Path(args.output)
        if not target.is_absolute():
            target = (repo_root / target).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(rendered_json, encoding="utf-8")
        print(f"Wrote release notes base resolution to {target}")
    print(render_release_base_summary(resolution), end="")
    return 0


def cmd_record_release_success(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    version = args.version or str(read_version_file(repo_root / "VERSION"))
    tag = args.tag or f"v{version}"
    target = record_release_success(
        repo_root=repo_root,
        output_dir=args.output_dir,
        tag=tag,
        version=version,
        publication_mode=args.publication_mode or os.environ.get("RELEASE_PUBLISH_MODE", "disabled"),
        target_url=args.target_url or os.environ.get("RELEASE_APT_REPOSITORY_URL") or os.environ.get("NEXUS_REPO_URL"),
        workflow_run_id=args.workflow_run_id or os.environ.get("GITHUB_RUN_ID"),
        output=args.output,
    )
    print(f"Wrote release success ledger to {target}")
    return 0

