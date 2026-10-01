import argparse
import os
from pathlib import Path

from tools.release.packaging import build_debian_package, validate_release_artifacts, \
    resolve_debian_publication_settings, publish_debian_artifacts
from tools.release.packaging.debian import REFERENCE_DEFAULT_CONFIG_PATH
from tools.release.packaging.publication import DEFAULT_VERIFY_ATTEMPTS, DEFAULT_VERIFY_DELAY_SECONDS


def cmd_build_deb(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    result = build_debian_package(
        repo_root=repo_root,
        output_dir=args.output_dir,
        dry_run=args.dry_run,
    )

    print("Debian build plan")
    print("----------------")
    print(f"Repo root:   {result.repo_root}")
    print(f"Package:     {result.package_name}")
    print(f"Version:     {result.package_version}")
    print(f"Output dir:  {result.output_dir}")
    print(f"Command:     {' '.join(result.command)}")

    if result.dry_run:
        print("\nDry run only. No Debian build command was executed.")
        return 0

    print()
    print("Artifacts")
    print("---------")
    for artifact in result.artifacts:
        print(f"- {artifact}")
    if getattr(result, "checksums", None) is not None:
        print(f"\nChecksums: {result.checksums}")
    if result.build_log is not None:
        print(f"\nBuild log: {result.build_log}")

    return 0


def cmd_validate_release_artifacts(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()

    result = validate_release_artifacts(
        output_dir=output_dir,
        require_changelog=not bool(args.skip_changelog),
        reference_config=repo_root / REFERENCE_DEFAULT_CONFIG_PATH,
    )
    print("Release artifact validation")
    print("---------------------------")
    print(f"Output dir:        {result.output_dir}")
    print(f"Debian artifacts:  {len(result.debian_artifacts)}")
    print(f"Web artifacts:     {len(result.web_artifacts)}")
    if not args.skip_changelog:
        print(f"Changelog files:   {len(result.changelog_artifacts)}")
    checksums = getattr(result, "checksums", None)
    if checksums is not None:
        state = "generated" if getattr(result, "checksums_generated", False) else "verified"
        print(f"Checksums:         {checksums} ({state})")
    print("Status:            OK")
    return 0


def cmd_publish_deb(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = (repo_root / output_dir).resolve()

    settings = resolve_debian_publication_settings(
        mode=args.mode,
        nexus_repo_url=args.nexus_repo_url,
        nexus_user=args.nexus_user,
        nexus_password=args.nexus_pass,
        env=os.environ,
    )
    lab_evidence = None
    if getattr(args, "lab_evidence", None):
        lab_evidence = Path(args.lab_evidence)
        if not lab_evidence.is_absolute():
            lab_evidence = (repo_root / lab_evidence).resolve()
    result = publish_debian_artifacts(
        output_dir=output_dir,
        settings=settings,
        dry_run=bool(args.dry_run),
        require_enabled=bool(args.require_enabled),
        allow_older_version=bool(getattr(args, "allow_older_version", False)),
        lab_evidence=lab_evidence,
        verify_attempts=int(getattr(args, "verify_attempts", DEFAULT_VERIFY_ATTEMPTS)),
        verify_delay_seconds=float(getattr(args, "verify_delay", DEFAULT_VERIFY_DELAY_SECONDS)),
    )

    print("Debian publication")
    print("------------------")
    print(f"Publication required: {'yes' if args.require_enabled else 'no'}")
    print(f"Mode:              {result.mode}")
    print("Upload mode:       post-binary-to-base-url")
    print("Append filename:   no")
    print(f"Output dir:        {result.output_dir}")
    print(f"Debian artifacts:  {len(result.artifacts)}")
    for artifact in result.artifacts:
        print(f"- {artifact}")

    if not result.enabled:
        print("Status:            SKIPPED")
        print(f"Reason:            {result.skipped_reason or 'Publication disabled.'}")
        return 0

    print(f"Target URLs:       {len(result.target_urls)}")
    for target_url in result.target_urls:
        print(f"- {target_url}")
    print("Plan:")
    for plan in result.plans:
        print(f"- {plan.artifact.path.name}: {plan.action} (sha256 {plan.artifact.sha256}; {plan.reason})")
    if lab_evidence is not None:
        print(f"Lab evidence:      {lab_evidence} (sha256 matches)")

    if result.dry_run:
        print("Status:            DRY-RUN (validated against APT index, not uploaded)")
    elif result.plans and not result.uploaded:
        print("Status:            ALREADY PUBLISHED (identical sha256; nothing uploaded)")
    else:
        print("Status:            PUBLISHED (verified by sha256 in APT index)")
    return 0
