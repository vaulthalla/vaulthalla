fs = import('fs')

# ╭──────────────────────────────── VERSION HEADER ─────────────────────────────────╮

version_split = meson.project_version().split('.')
if version_split.length() < 3
    error('Project version must be in the format X.Y.Z (e.g., 0.9.2)')
endif

conf = configuration_data()
conf.set_quoted('VH_VERSION', meson.project_version())
conf.set_quoted('VH_VERSION_MAJOR', version_split[0])
conf.set_quoted('VH_VERSION_MINOR', version_split[1])
conf.set_quoted('VH_VERSION_PATCH', version_split[2])

vh_version_header = configure_file(
    output: 'version.h',
    configuration: conf,
    install: true,
    install_dir: get_option('includedir') / 'vaulthalla'
)


# ╭──────────────────────────────── PATH HEADER ─────────────────────────────────╮

repo_root = meson.project_source_root()
core_root = meson.current_source_dir()
config_dir = join_paths(get_option('sysconfdir'), meson.project_name())
log_dir = join_paths(get_option('localstatedir'), 'log', meson.project_name())
state_dir = join_paths(get_option('localstatedir'), 'lib', meson.project_name())
runtime_dir = join_paths('/run', meson.project_name())
data_dir = join_paths(get_option('prefix'), get_option('datadir'), meson.project_name())
psql_dir = join_paths(data_dir, 'psql')

conf = configuration_data()
conf.set_quoted('VH_MOUNT_PATH', join_paths('/mnt', meson.project_name()))
conf.set_quoted('VH_CONFIG_PATH', join_paths('/', config_dir, 'config.yaml'))
conf.set_quoted('VH_LOG_PATH', log_dir)
conf.set_quoted('VH_BACKING_PATH', state_dir)
conf.set_quoted('VH_RUNTIME_PATH', runtime_dir)
conf.set_quoted('VH_CACHE_PATH', '/.cache')
conf.set_quoted('VH_PSQL_SCHEMAS_PATH', psql_dir)
conf.set_quoted('VH_PSQL_TEST_SCHEMAS_PATH', join_paths(repo_root, 'deploy/psql'))
conf.set_quoted('VH_TEST_ENV_PATH', join_paths(repo_root, 'deploy/vaulthalla.env'))

vh_paths_header = configure_file(
    input: 'deploy/path/paths.h.in',
    output: 'paths.h',
    configuration: conf,
    install: true,
    install_dir: get_option('includedir') / 'vaulthalla'
)


# ╭──────────────────────────── DEFINE DEPENDENCIES ────────────────────────────╮

fmt_dep = dependency('fmt', required: true)

deps = [
    dependency('fuse3', required: true),
    dependency('libsodium', required: true),
    dependency('libcurl', required: true),
    dependency('boost', modules: ['filesystem', 'system'], required: true),
    dependency('libpqxx', required: true),
    dependency('pdfium', required: true),
    dependency('libmagic', required: true),
    dependency('yaml-cpp', required: true),
    dependency('libjpeg', required: true),
    dependency('pugixml', required: true),
    dependency('uuid', required: true),
    dependency('threads', required: true),
    dependency('spdlog', required: true),
    dependency('libturbojpeg', required: true),
    dependency('tss2-esys', required: true),
    dependency('tss2-tctildr', required: true),
    dependency('tss2-rc', required: true),
    dependency('openssl', version: '>=3.0', required: true),
    dependency('zlib', required: true),
    fmt_dep
]

test_deps = deps + dependency('gtest', required: true, main: false)


# ╭────────────────────────────────   FILES   ──────────────────────────────────╮

inc         = include_directories('include', 'vendor', 'usage/include')
src         = join_paths(core_root, 'src')
tests_src   = join_paths(core_root, 'tests/unit')
main        = files(join_paths(core_root, 'main/main.cpp'))

sources = []
foreach f : run_command('sh', '-c', 'find "$1" -type f -name "*.cpp" | LC_ALL=C sort', 'find-cpp', src, check: true).stdout().strip().split('\n')
    sources += files(f)
endforeach

usage_inc = include_directories('usage/include')
usage_src = join_paths(core_root, 'usage/src')
main_usage = files(join_paths(core_root, 'main/usage.cpp'))

usage_sources = []
foreach f : run_command('sh', '-c', 'find "$1" -type f -name "*.cpp" | LC_ALL=C sort', 'find-cpp', usage_src, check: true).stdout().strip().split('\n')
    usage_sources += files(f)
endforeach


# ╭──────────────────────────────── LIBRARIES ───────────────────────────────────╮

lib_core = static_library(
    'vaulthalla',
    sources,
    include_directories: inc,
    dependencies: deps,
    install: true
)

lib_seed = static_library('vhseed', files(join_paths(core_root, 'seed/src/seed_db.cpp')),
                          include_directories: include_directories('seed/include', 'include', 'vendor'),
                          link_with: lib_core
)

lib_usage = static_library('vhusage',
                           usage_sources,
                           include_directories: usage_inc,
                           dependencies: fmt_dep,
                           install: true
)

lib_usage_native = static_library('vhusage-native',
                                  usage_sources,
                                  include_directories: usage_inc,
                                  dependencies: fmt_dep,
                                  native: true,
                                  install: false,
                                  override_options: ['unity=on', 'b_pch=true']
)


# ╭──────────────────────────────── DEPENDENCIES ────────────────────────────────╮

dep = declare_dependency(
    include_directories: inc,
    link_with: [lib_core, lib_seed, lib_usage],
    dependencies: deps
)


# ╭───────────────────────────────── EXECUTABLES ─────────────────────────────────╮

executable('vaulthalla-server', main,
           include_directories: inc,
           dependencies: dep,
           install: true
)

executable('vaulthalla-cli', files(join_paths(core_root, 'main/cli.cpp')),
           include_directories: include_directories('vendor'),
           dependencies: fmt_dep,
           install: true
)

vh_usage = executable('vh_usage', main_usage,
                      include_directories: usage_inc,
                      link_with: lib_usage_native,
                      native: true,
                      install: false
)


# ╭──────────────────────────────── MANPAGE GENERATION ───────────────────────────────╮

pandoc = find_program('pandoc', required: get_option('manpage'))

vh_md = custom_target('vh-md',
                      output: 'vh.md',
                      command: [vh_usage, '--out', '@OUTPUT@'],
                      build_by_default: true
)

if get_option('manpage')
    man1 = custom_target('vh-man',
                         input: vh_md,
                         output: 'vh.1',
                         command: [pandoc, '-s', '-t', 'man', '@INPUT@', '-o', '@OUTPUT@'],
                         install: true,
                         install_dir: get_option('mandir') / 'man1'
    )
endif


# ╭────────────────────────────────────── TESTS ────────────────────────────────────────╮

if get_option('build_unit_tests')
    configure_file(input: 'test-assets/sample.jpg', output: 'sample.jpg', copy: true)
    configure_file(input: 'test-assets/sample.pdf', output: 'sample.pdf', copy: true)
    configure_file(
        input: 'test-assets/Screenshot 2025-06-26 at 3.29.35\u202FPM.png',
        output: 'Screenshot 2025-06-26 at 3.29.35\u202FPM.png',
        copy: true
    )
endif

if get_option('build_unit_tests')
    unit_test_env = environment()
    unit_test_env.set('VH_PATH_TO_CONFIG', join_paths(repo_root, 'deploy/config/config.yaml'))

    all_test_sources = sources
    foreach f : run_command('sh', '-c', 'find "$1" -type f -name "*.cpp" | LC_ALL=C sort', 'find-cpp', tests_src, check: true).stdout().strip().split('\n')
        all_test_sources += files(f)
    endforeach

    vh_unit_tests = executable(
        'vh_unit_tests',
        all_test_sources,
        link_with: [lib_core, lib_seed, lib_usage],
        include_directories: inc,
        dependencies: test_deps,
        install: false,
    )

    test('vh_unit_tests', vh_unit_tests, env: unit_test_env, workdir: meson.current_build_dir())
endif

if get_option('integration_tests')
    cli_test_inc = include_directories(
        'tests/integrations/include',
        'usage/include',
        'include',
        'vendor'
    )

    cli_test_src = join_paths(core_root, 'tests/integrations/src')
    cli_test_sources = []

    foreach f : run_command('sh', '-c', 'find "$1" -type f -name "*.cpp" | LC_ALL=C sort', 'find-cpp', cli_test_src, check: true).stdout().strip().split('\n')
        cli_test_sources += files(f)
    endforeach

    vh_integration_tests = executable(
        'vh_integration_tests',
        cli_test_sources + join_paths(core_root, 'tests/integrations/main.cpp'),
        include_directories: cli_test_inc,
        link_with: [lib_core, lib_seed, lib_usage],
        dependencies: deps,
        install: false,
    )

    test('vh_integration_tests', vh_integration_tests, workdir: meson.current_build_dir())
endif
