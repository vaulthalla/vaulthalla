<!--
vl-release staged release notes: the user-facing story of the NEXT release.
Maintained continuously while working; `vlr prepare` promotes it into the release notes
history (and the GitHub release) and resets this file to this template.

Format: the first line is "# <release title>" WITHOUT a version number (vlr adds it).
Everything after the title is the Markdown release body. Describe the resulting behavior
for users and operators; keep it representative of what actually ships.
-->
# Simpler Cloudflare DNS-01 setup for the S3 domain

### `--certbot-dns-cloudflare` takes the credentials file directly

Publishing the S3 gateway on its own hostname used to take two flags. Now the credentials file is the value of
`--certbot-dns-cloudflare`:

```bash
sudo vh setup nginx --domain vault.example.com --s3-domain s3.vault.example.com \
  --certbot-dns-cloudflare /etc/vaulthalla/certbot/cloudflare.ini
```

The older form, `--certbot-dns-cloudflare --cloudflare-credentials <path>`, still works, and
`--cloudflare-credentials <path>` on its own now also selects DNS-01 mode. If both flags are given with different
paths, setup refuses to run instead of guessing.

### The credentials file is documented

`vh setup nginx --help` and the [S3 Gateway Setup](https://vaulthalla.io/docs/s3-gateway/setup) guide now explain
the whole setup:

- the file holds `dns_cloudflare_api_token = <token>`, a Cloudflare API token with **Zone → DNS → Edit** on the
  zones of both hostnames;
- how to create it as a root-only `0600` file;
- why it has to stay in place after setup (certbot reads it again on every renewal);
- why `--s3-domain` needs DNS-01. Vaulthalla renders both HTTPS hosts itself, and DNS-01 is the mode that issues
  one certificate covering both, without opening port 80.

The S3 gateway guide no longer shows `--s3-domain` with `--certbot`, which setup has always refused.

### `vh setup` help no longer needs sudo

`vh setup nginx --help` (and `--help` on `setup db`, `setup remote-db` and the `teardown` commands) asked for sudo,
and under sudo printed the lifecycle helper's terse internal help instead of the documented one. Help now comes
from the same command reference as every other `vh` command, without sudo. If the daemon is not reachable, for
example before `vh setup db`, `sudo vh setup … --help` still prints the helper's own help.
