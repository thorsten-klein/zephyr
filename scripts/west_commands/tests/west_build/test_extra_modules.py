# Copyright (c) 2025 Thorsten Klein <thorsten.klein@bshg.com>
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import subprocess
import sys
import textwrap

import pytest

ZEPHYR_MODULE_PY = Path(__file__).parents[3] / 'zephyr_module.py'

DEFAULT_MODULE_YML = textwrap.dedent('''
    build:
      cmake: .
      kconfig: kconfig.{name}
      settings:
        board_root: .
        dts_root: .
        snippet_root: .
        soc_root: .
        arch_root: .
        module_ext_root: .
''')

DEFAULT_KCONFIG = textwrap.dedent('''
    config {config}
	    bool "Some config"
	    help
	      Some help.

    source "Kconfig.zephyr"
''')

def git_init(path: Path):
    subprocess.check_output(['git','init', '-b', 'main'], cwd=path)

def git_add_commit(path: Path):
    subprocess.check_output(['git','add', '.'], cwd=path)
    subprocess.check_output(['git','commit', '-m', 'initial'], cwd=path)

def git_create(path: Path):
    git_init(path)
    git_add_commit(path)

def create_west_project(path: Path, west_yml: str, module_yml: str | None = None, kconfig: str | None = None, remote=False):
    path_module_yml = Path(path) / 'zephyr' / 'module.yml'
    path_west_yml = Path(path) / 'west.yml'
    path_kconfig = Path(path) / f'Kconfig.{path.name}'

    # create directories
    path.mkdir(parents=True)

    # create west.yml
    path_west_yml.write_text(west_yml)

    # create zephyr/module.yml
    if module_yml:
        path_module_yml.parent.mkdir(parents=True)
        path_module_yml.write_text(module_yml)
    
    # create kconfig
    if kconfig:
        path_kconfig.write_text(kconfig)

    if remote:
        git_create(path)

def setup_default_repos():
    # setup a repository structure with multiple subprojects
    repos_dir = Path('repos').absolute()
    repo_picolib = repos_dir / 'picolibc'
    repo_mcuboot = repos_dir / 'mcuboot'
    repo_zephyr = repos_dir / 'zephyr'

    # create remote repositories picolib and mcuboot
    simple_west_yml = textwrap.dedent('''
        manifest:
    ''')
    create_west_project(repo_picolib, west_yml=simple_west_yml, remote=True)
    create_west_project(repo_mcuboot, west_yml=simple_west_yml, remote=True)

    # create remote repository zephyr
    zephyr_west_yml = textwrap.dedent('''
        manifest:
          projects:
          - name: picolibc
            revision: main
            path: modules/lib/picolibc
          - name: mcuboot
            revision: main
            path: mcuboot
    ''')
    zephyr_kconfig = DEFAULT_KCONFIG.format(config='zephyr')

    create_west_project(
        repo_zephyr,
        west_yml=zephyr_west_yml,
        kconfig=zephyr_kconfig,
        remote=True
    )
    
    return repos_dir

def test_extra_modules():
    repos_dir = setup_default_repos()

    # additional repos
    project_dir = repos_dir.parent / 'ws' / 'project'
    repo_base = project_dir / 'base'
    repo_level_1 = project_dir / 'level_1'
    repo_level_2 = project_dir / 'level_2'


    # ------------------------------
    # create project
    # ------------------------------
    project_west_yml = textwrap.dedent('''
        manifest:
          self:
            import:
            - base/west.yml
            - level_1/west.yml
    ''')
    project_kconfig = DEFAULT_KCONFIG.format(config='FROM_PROJECT') + \
        textwrap.dedent('''
            source Kconfig.base
            source Kconfig.level_1
        ''')

    create_west_project(
        project_dir,
        west_yml=project_west_yml,
        kconfig=project_kconfig
    )

    # ------------------------------
    # create subproject 'base'
    # ------------------------------
    project_base_west_yml = textwrap.dedent(f'''
        # this project specifies remote project (zephyr)
        manifest:
          projects:
          - name: zephyr
            revision: main
            url: {repos_dir / 'zephyr'}
    ''')

    create_west_project(
        project_dir / 'base',
        west_yml=project_base_west_yml,
        module_yml=DEFAULT_MODULE_YML.format(name='base'),
        kconfig=DEFAULT_KCONFIG.format(config='FROM_BASE')
    )
    
    # ------------------------------
    # create subproject 'level_1'
    # ------------------------------
    project_level_1_west_yml = textwrap.dedent('''
        # this manifest imports another submanifest
        manifest:
          self:
            import:
            # TODO: This should be relative path ../level_2
            - level_2/west.yml
    ''')

    project_level_1_kconfig = DEFAULT_KCONFIG.format(config='FROM_LEVEL_1') + \
        textwrap.dedent('''
            source Kconfig.level_2
        ''')

    create_west_project(
        project_dir / 'level_1',
        west_yml=project_level_1_west_yml,
        module_yml=DEFAULT_MODULE_YML.format(name='level_1'),
        kconfig=project_level_1_kconfig
    )


    # ------------------------------
    # create subproject 'level_2'
    # ------------------------------
    project_level_2_west_yml = textwrap.dedent('''
        manifest:
    ''')

    create_west_project(
        project_dir / 'level_2',
        west_yml=project_level_2_west_yml,
        module_yml=DEFAULT_MODULE_YML.format(name='level_2'),
        kconfig=DEFAULT_KCONFIG.format(config='FROM_LEVEL_2')
    )

    # init and update west workspace
    workspace_dir = project_dir.parent
    subprocess.run(f'west init -l {project_dir}'.split(), cwd=workspace_dir)
    subprocess.run(f'west update'.split(), cwd=workspace_dir)

    # check if all KConfig are usable
    outfile = workspace_dir / 'actual-kconfig'
    subprocess.run([sys.executable, ZEPHYR_MODULE_PY, '--kconfig-out', outfile], cwd=workspace_dir)
    subprocess.run(['cat', outfile])
