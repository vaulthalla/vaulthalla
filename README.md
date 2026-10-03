[![build](https://img.shields.io/github/actions/workflow/status/vaulthalla/vaulthalla/build_and_test.yml?label=build)](https://github.com/vaulthalla/vaulthalla/actions)
[![release](https://img.shields.io/github/v/release/vaulthalla/vaulthalla?display_name=tag&sort=semver)](https://github.com/vaulthalla/vaulthalla/releases)
[![license](https://img.shields.io/github/license/vaulthalla/vaulthalla?v=2)](https://github.com/vaulthalla/vaulthalla/blob/main/LICENSE)
![platform](https://img.shields.io/badge/Debian-Linux-A81D33?logo=debian&logoColor=white)
[![AES-256-GCM/NI](https://img.shields.io/badge/AES--256--GCM%2FNI-encrypted-cyan)](https://github.com/vaulthalla/vaulthalla)

# Vaulthalla

**Stop renting back your own data.**

Vaulthalla is a self-hosted, encrypted cloud for your own hardware: a rapidly maturing alternative to tools like
Nextcloud and MinIO. It is a compiled C++ daemon rather than a web app: files are encrypted before they reach disk or
a bucket, mounted as a real filesystem, served over an S3-compatible API, and managed from a CLI or a web console.
It installs as a single Debian package.

> 📖 **Documentation: [vaulthalla.io/docs](https://vaulthalla.io/docs)** covers installation, administration,
> storage, sharing, the S3 gateway, and the CLI.

## Install

On Debian or Ubuntu (amd64; Ubuntu 24.04 is the tested target):

```bash
curl -fsSL https://apt.vaulthalla.sh/install.sh | bash
```

Add `-s -- --interactive` after `bash` to choose an install profile and the Linux user who administers Vaulthalla.
The installer adds the Vaulthalla APT repository, installs the `vaulthalla` package with PostgreSQL and nginx, and
starts the services. Upgrades arrive through `apt upgrade`.

Then:

1. Open `http://<host>/` and sign in as `admin`. Each install generates its own password:
   `sudo cat /var/lib/vaulthalla/super_admin_initial_password`.
2. Change it with `vh setup set-super-admin-password`, or keep it and delete that file.
3. For HTTPS, run `sudo vh setup nginx --domain <domain> --certbot`.

Manual APT setup, install profiles and removal are covered in the
[installation guide](https://vaulthalla.io/docs/getting-started/installation).

## Highlights

- **Encrypted vaults**: AES-256-GCM, with keys sealed by the host TPM2 (or a managed software TPM on VPS hosts).
- **A real filesystem**: vaults mount at `/mnt/vaulthalla` through a native FUSE driver, governed by the same
  permissions as everything else.
- **Local or S3-backed storage**: keep vaults on local disks or sync them to S3-compatible providers such as
  Cloudflare R2, with request budgets and cost guardrails so a sync can't run up your bill.
- **S3-compatible gateway**: serve vaults as buckets to existing S3 tools and SDKs.
- **Access control and sharing**: role-based permissions for users, groups and vaults, plus public and
  email-validated share links.
- **CLI and web console**: `vh` for operators and automation, and a web console for everyone, both enforcing the
  same rules.

## Status

Vaulthalla moves quickly and releases often. Check the [release notes](RELEASE_NOTES.md) before upgrading. Builds from
source are for development only; production installs should use the APT package.

## Support

Bug reports and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md). For install or runtime issues,
include the output of `vh status` and `sudo journalctl -u vaulthalla -n 150 --no-pager`.

Licensed under the terms in [LICENSE](LICENSE).
