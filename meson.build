project('vaulthalla', 'cpp',
        version: '1.0.4',
        license: 'AGPL-3.0-or-later',
        meson_version: '>=1.3.0',
        default_options: [
            'warning_level=3',
            'werror=false',
            'cpp_std=c++23',
            'unity=on',
            'unity_size=9',
            'prefix=/usr',
            'cpp_args=-D_GLIBCXX_USE_CXX11_ABI=1'
        ]
)

subdir('core')

if get_option('install_data')
    project_name = meson.project_name()
    bindir = get_option('bindir')
    sysconf_dir = join_paths('/', get_option('sysconfdir'), project_name)
    state_dir = join_paths('/', get_option('localstatedir'), 'lib', project_name)
    log_dir = join_paths('/', get_option('localstatedir'), 'log', project_name)
    data_dir = get_option('datadir') / project_name
    doc_dir = get_option('datadir') / 'doc' / project_name
    libexec_dir = 'lib' / project_name

    install_emptydir(state_dir)
    install_emptydir(log_dir)

    install_data(
        'deploy/config/config.yaml',
        'deploy/config/config_template.yaml.in',
        install_dir: sysconf_dir,
    )

    install_subdir(
        'deploy/psql',
        install_dir: data_dir,
    )

    install_data(
        'deploy/nginx/vaulthalla.conf',
        install_dir: data_dir / 'nginx',
        rename: 'vaulthalla',
    )

    install_data(
        'deploy/lifecycle/main.py',
        install_dir: libexec_dir,
        rename: 'lifecycle',
        install_mode: 'rwxr-xr-x',
    )

    unit_conf = configuration_data()
    unit_conf.set('BINDIR', get_option('prefix') / bindir)

    foreach unit_template : [
        'vaulthalla.service',
        'vaulthalla-cli.service',
        'vaulthalla-web.service',
        'vaulthalla-swtpm.service',
    ]
        unit_input = 'deploy/systemd' / (unit_template + '.in')
        configure_file(
            input: unit_input,
            output: unit_template,
            configuration: unit_conf,
            install: true,
            install_dir: get_option('systemd_system_unit_dir'),
        )
    endforeach

    install_data(
        'deploy/systemd/vaulthalla-cli.socket',
        install_dir: get_option('systemd_system_unit_dir'),
    )

    install_data(
        'LICENSE',
        'debian/copyright',
        install_dir: doc_dir,
    )

    install_data(
        'debian/vaulthalla.udev',
        install_dir: get_option('udev_rules_dir'),
        rename: '60-vaulthalla-tpm.rules',
    )

    install_data(
        'debian/tmpfiles.d/vaulthalla.conf',
        install_dir: get_option('tmpfiles_dir'),
    )

    install_symlink(
        'vaulthalla',
        install_dir: bindir,
        pointing_to: 'vaulthalla-cli',
    )

    install_symlink(
        'vh',
        install_dir: bindir,
        pointing_to: 'vaulthalla-cli',
    )
endif
