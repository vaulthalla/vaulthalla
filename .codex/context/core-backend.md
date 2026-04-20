# Core Backend Map (`core/`)

## Build System Snapshot

`core/meson.build` defines a C++23 build with unity mode and many native deps.

```meson
project('vaulthalla', 'cpp',
  version: '0.28.1',
  default_options: [
    'cpp_std=c++23',
    'unity=on',
    'unity_size=9',
  ]
)
```

Key dependencies include: `fuse3`, `libsodium`, `libcurl`, `boost`, `libpqxx`, `pdfium`, `yaml-cpp`, `spdlog`, `openssl >=3.0`, `tss2-esys`, `tss2-tctildr`.

Primary binaries/libraries:

- `vaulthalla-server`
- `vaulthalla-cli`
- `vh_usage` (manpage markdown generator)
- static libs: `vaulthalla`, `vhseed`, `vhusage`

Meson options (`core/meson.options`):

- `manpage` (default `true`)
- `install_data` (default `true`)
- `build_unit_tests` (default `false`)
- `integration_tests` (default `false`)

## Runtime Startup and Service Wiring

`core/main/main.cpp` boot order:

1. Initialize config and logging registries
2. Initialize DB + prepared statements + optional seeding
3. Initialize runtime deps + storage wiring
4. Start runtime manager services
5. Run until SIGINT/SIGTERM

`core/src/runtime/Manager.cpp` owns service lifecycle + watchdog restart logic.

## Protocol Surfaces

Protocol service (`core/src/protocols/ProtocolService.cpp`) starts:

- WebSocket server (`core/src/protocols/ws/Server.cpp`)
- HTTP preview server (`core/src/protocols/http/Server.cpp`)

Config gated via `Registry::get().websocket.enabled` and `Registry::get().http_preview.enabled`.

Shell control protocol:

- Unix socket path in `core/main/cli.cpp`: `/run/vaulthalla/cli.sock`
- Server implementation: `core/src/protocols/shell/Server.cpp`
- Router/command parser: `core/src/protocols/shell/Router.cpp`, `Parser.hpp`

## CLI Names and Usage Module

`core/usage/src/UsageManager.cpp` sets root aliases to `vh`.

`core/usage/include/CommandUsage.hpp` hard-codes usage bin display to `"vh"`.

Usage subsystem (`core/usage/`) is shared by:

- CLI shell command usage/help generation
- manpage markdown generator binary (`vh_usage`, invoked in Meson custom target)
- integration test command modeling surfaces under `core/tests/integrations`

`bin/setup/install_dirs.sh` creates system CLI aliases:

- `vaulthalla`
- `vh`

Both symlink to `vaulthalla-cli`.

## FUSE Daemon

`core/src/fuse/Service.cpp`:

- Ensures mountpoint readiness
- Starts low-level FUSE session with `allow_other` + `auto_unmount`
- Mount path uses generated path config (`/mnt/vaulthalla` outside test mode)
- Uses thread pool dispatch for request handling

## Config Model Highlights

`core/include/config/Config.hpp` defaults:

- Websocket: `0.0.0.0:33369`
- HTTP preview: `0.0.0.0:33370`
- DB: `localhost:5432`, db/user `vaulthalla`
- Auth token TTLs, sync retention, service sweeper intervals, sharing/caching/audit settings

Runtime loads config through `core/src/config/Registry.cpp`.

## Integration Tests

`core/tests/integrations/main.cpp` runs:

- test mode path switching
- DB wipe/init/seed
- selected runtime services (`FUSE`, shell server)
- command/fuse integration runner
