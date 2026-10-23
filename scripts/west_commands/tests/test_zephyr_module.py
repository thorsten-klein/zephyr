# Copyright (c) 2026 Thorsten Klein
#
# SPDX-License-Identifier: Apache-2.0
"""
Tests for extra Zephyr modules declared in west manifests
(self: userdata: zephyr-extra-modules:) in scripts/zephyr_module.py
"""

import os
import sys
import textwrap
from pathlib import Path, PurePosixPath
from types import SimpleNamespace

import pytest

ZEPHYR_BASE = Path(__file__).resolve().parents[3]
sys.path.append(os.fspath(ZEPHYR_BASE / "scripts"))

import zephyr_module  # noqa: E402


def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content), encoding='utf-8')


def make_module(path, name):
    write(path / 'zephyr' / 'module.yml', f'name: {name}\n')


def posix(path):
    return PurePosixPath(Path(path).resolve().as_posix())


@pytest.fixture
def workspace(tmp_path):
    '''Workspace with manifest repo 'app' importing projects 'dep' and 'lib'.'''
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          projects:
            - name: dep
              url: https://example.com/dep
              import: true
            - name: lib
              url: https://example.com/lib
          self:
            path: app
            userdata:
              zephyr-extra-modules:
                - modules/app_mod
        ''',
    )
    write(
        tmp_path / 'dep' / 'west.yml',
        '''\
        manifest:
          projects:
            - name: nested
              url: https://example.com/nested
              import: west-nested.yml
          self:
            userdata:
              zephyr-extra-modules:
                - modules/dep_mod
        ''',
    )
    write(
        tmp_path / 'nested' / 'west-nested.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules:
                - nested_mod
        ''',
    )
    return tmp_path


def west_projs(topdir, *names):
    '''Return west_projects() like data, with 'names' as active projects.'''
    projects = [
        SimpleNamespace(
            name='manifest', abspath=str(topdir / 'app'), posixpath=(topdir / 'app').as_posix()
        )
    ]
    projects += [
        SimpleNamespace(name=n, abspath=str(topdir / n), posixpath=(topdir / n).as_posix())
        for n in names
    ]
    return {'manifest_path': str(topdir / 'app' / 'west.yml'), 'projects': projects}


def test_no_west():
    assert zephyr_module.west_extra_modules(None) == []


def test_no_userdata(tmp_path):
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            userdata:
              other-tool: 42
        ''',
    )
    assert zephyr_module.west_extra_modules(west_projs(tmp_path)) == []


def test_nested_imports(workspace):
    result = zephyr_module.west_extra_modules(west_projs(workspace, 'dep', 'lib', 'nested'))
    assert result == [
        posix(workspace / 'app' / 'modules' / 'app_mod'),
        posix(workspace / 'dep' / 'modules' / 'dep_mod'),
        posix(workspace / 'nested' / 'nested_mod'),
    ]


def test_inactive_project_is_skipped(workspace):
    # 'dep' inactive: neither it nor anything it imports is considered.
    result = zephyr_module.west_extra_modules(west_projs(workspace, 'lib'))
    assert result == [posix(workspace / 'app' / 'modules' / 'app_mod')]


def test_first_definition_wins(workspace):
    # 'dep' is first defined without import; the later import is ignored.
    write(
        workspace / 'app' / 'west.yml',
        '''\
        manifest:
          projects:
            - name: dep
              url: https://example.com/dep
            - name: dep
              url: https://example.com/dep
              import: true
        ''',
    )
    assert zephyr_module.west_extra_modules(west_projs(workspace, 'dep', 'nested')) == []


def test_self_import_directory(tmp_path):
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            import: submanifests
        ''',
    )
    write(
        tmp_path / 'app' / 'submanifests' / 'b.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: [mod_b]
        ''',
    )
    write(
        tmp_path / 'app' / 'submanifests' / 'a.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: [mod_a, /abs/mod]
        ''',
    )
    write(tmp_path / 'app' / 'submanifests' / 'ignored.txt', 'not a manifest')

    assert zephyr_module.west_extra_modules(west_projs(tmp_path)) == [
        posix(tmp_path / 'app' / 'mod_a'),
        posix('/abs/mod'),
        posix(tmp_path / 'app' / 'mod_b'),
    ]


def test_map_and_list_import(tmp_path):
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          projects:
            - name: dep
              url: https://example.com/dep
              import:
                - file: one.yml
                - two.yml
        ''',
    )
    for f in ['one', 'two']:
        write(
            tmp_path / 'dep' / f'{f}.yml',
            f'''\
            manifest:
              self:
                userdata:
                  zephyr-extra-modules: [{f}]
            ''',
        )

    assert zephyr_module.west_extra_modules(west_projs(tmp_path, 'dep')) == [
        posix(tmp_path / 'dep' / 'one'),
        posix(tmp_path / 'dep' / 'two'),
    ]


def test_duplicates_and_env_vars(tmp_path, monkeypatch):
    monkeypatch.setenv('MY_MODULES', str(tmp_path / 'shared'))
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules:
                - ${MY_MODULES}/mod
                - ../shared/mod
        ''',
    )
    assert zephyr_module.west_extra_modules(west_projs(tmp_path)) == [
        posix(tmp_path / 'shared' / 'mod'),
    ]


@pytest.mark.parametrize(
    'value',
    ['mod_a;mod_b', '[mod_a, 1]', '[""]'],
    ids=['string', 'non-string entry', 'empty entry'],
)
def test_malformed_entries(tmp_path, value):
    write(
        tmp_path / 'app' / 'west.yml',
        f'''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: {value}
        ''',
    )
    with pytest.raises(SystemExit) as e:
        zephyr_module.west_extra_modules(west_projs(tmp_path))
    assert 'must be a list of paths' in str(e.value)


def test_parse_modules_merges_with_env(tmp_path, monkeypatch):
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: [../mod_manifest]
        ''',
    )
    make_module(tmp_path / 'app', 'app')
    make_module(tmp_path / 'mod_manifest', 'mod_manifest')
    make_module(tmp_path / 'mod_env', 'mod_env')
    monkeypatch.setenv(
        'ZEPHYR_EXTRA_MODULES', f'{tmp_path / "mod_env"};{tmp_path / "mod_manifest"}'
    )
    monkeypatch.delenv('EXTRA_ZEPHYR_MODULES', raising=False)

    modules = zephyr_module.parse_modules(str(tmp_path / 'zephyr'), west_projs=west_projs(tmp_path))

    assert [m.meta['name'] for m in modules] == ['app', 'mod_manifest', 'mod_env']


def test_parse_modules_with_explicit_modules(tmp_path, monkeypatch):
    # ZEPHYR_MODULES overrides west, so manifest extra modules are ignored.
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: [../mod_manifest]
        ''',
    )
    make_module(tmp_path / 'mod_manifest', 'mod_manifest')
    make_module(tmp_path / 'mod_cli', 'mod_cli')

    modules = zephyr_module.parse_modules(
        str(tmp_path / 'zephyr'),
        west_projs=west_projs(tmp_path),
        modules=[],
        extra_modules=[(tmp_path / 'mod_cli').as_posix()],
    )

    assert [m.meta['name'] for m in modules] == ['mod_cli']


def test_parse_modules_invalid_manifest_module(tmp_path, monkeypatch):
    write(
        tmp_path / 'app' / 'west.yml',
        '''\
        manifest:
          self:
            userdata:
              zephyr-extra-modules: [../no_module]
        ''',
    )
    (tmp_path / 'no_module').mkdir()
    monkeypatch.delenv('ZEPHYR_EXTRA_MODULES', raising=False)
    monkeypatch.delenv('EXTRA_ZEPHYR_MODULES', raising=False)

    with pytest.raises(SystemExit) as e:
        zephyr_module.parse_modules(str(tmp_path / 'zephyr'), west_projs=west_projs(tmp_path))
    assert 'is not a valid zephyr module' in str(e.value)


def run_main(monkeypatch, tmp_path, west_data, *args):
    '''Run zephyr_module.py like zephyr_module.cmake does and return the
    resulting module name to path mapping of zephyr_modules.txt.'''
    cmake_out = tmp_path / 'zephyr_modules.txt'
    monkeypatch.setattr(zephyr_module, 'west_projects', lambda manifest=None: west_data)
    monkeypatch.setattr(
        sys,
        'argv',
        [
            'zephyr_module.py',
            f'--zephyr-base={tmp_path / "zephyr"}',
            *args,
            '--cmake-out',
            os.fspath(cmake_out),
        ],
    )
    monkeypatch.delenv('ZEPHYR_EXTRA_MODULES', raising=False)
    monkeypatch.delenv('EXTRA_ZEPHYR_MODULES', raising=False)

    zephyr_module.main()

    modules = {}
    for line in cmake_out.read_text(encoding='utf-8').splitlines():
        name, path, _ = line[1:-1].split('":"')
        modules[name] = path
    return modules


def test_main_manifest_and_cmake_extra_modules(workspace, monkeypatch):
    make_module(workspace / 'app' / 'modules' / 'app_mod', 'app_mod')
    make_module(workspace / 'dep' / 'modules' / 'dep_mod', 'dep_mod')
    make_module(workspace / 'nested' / 'nested_mod', 'nested_mod')
    make_module(workspace / 'cmake_mod', 'cmake_mod')

    modules = run_main(
        monkeypatch,
        workspace,
        west_projs(workspace, 'dep', 'lib', 'nested'),
        '--extra-modules',
        (workspace / 'cmake_mod').as_posix(),
    )

    assert modules == {
        'app_mod': (workspace / 'app' / 'modules' / 'app_mod').resolve().as_posix(),
        'dep_mod': (workspace / 'dep' / 'modules' / 'dep_mod').resolve().as_posix(),
        'nested_mod': (workspace / 'nested' / 'nested_mod').resolve().as_posix(),
        'cmake_mod': (workspace / 'cmake_mod').as_posix(),
    }


def test_main_cmake_extra_module_takes_precedence(workspace, monkeypatch):
    # A module given with -DEXTRA_ZEPHYR_MODULES overrides a manifest
    # declared module of the same name.
    make_module(workspace / 'app' / 'modules' / 'app_mod', 'app_mod')
    make_module(workspace / 'custom' / 'app_mod', 'app_mod')

    modules = run_main(
        monkeypatch,
        workspace,
        west_projs(workspace, 'lib'),
        '--extra-modules',
        (workspace / 'custom' / 'app_mod').as_posix(),
    )

    assert modules == {'app_mod': (workspace / 'custom' / 'app_mod').as_posix()}


def test_main_zephyr_modules_ignores_manifest(workspace, monkeypatch):
    # -DZEPHYR_MODULES replaces west, including its extra modules.
    make_module(workspace / 'app' / 'modules' / 'app_mod', 'app_mod')
    make_module(workspace / 'lib', 'lib')

    modules = run_main(
        monkeypatch,
        workspace,
        west_projs(workspace, 'lib'),
        '--modules',
        (workspace / 'lib').as_posix(),
    )

    assert modules == {'lib': (workspace / 'lib').as_posix()}
