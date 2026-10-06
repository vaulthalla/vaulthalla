# Out-of-process preview helpers (derive seam). Minimal wiring from the HELPERS-MEDIA track so the media helper
# builds in isolation; the HELPERS-CAD track owns this file and the common library, and the lead keeps one copy.

helper_dir = get_option('prefix') / 'lib' / meson.project_name() / 'helpers'
helper_inc = include_directories('.', '../vendor')

seccomp_dep = dependency('libseccomp', required: get_option('preview_media'))

helper_common_lib = disabler()
if seccomp_dep.found()
    helper_common_lib = static_library(
        'vh_helper_common',
        files('common/protocol.cpp', 'common/sandbox.cpp'),
        include_directories: helper_inc,
        dependencies: seccomp_dep,
        install: false,
    )
endif

subdir('preview-media')
