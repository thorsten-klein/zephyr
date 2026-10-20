# Copyright (c) 2026 BSH Hausgeraete GmbH
#
# SPDX-License-Identifier: Apache-2.0
"""
--diff-base: only build test instances affected by the changes since the
merge-base with a git ref. An instance is affected if a changed file matches a
--diff-force-all pattern, is below the test suite directory, is an input of the
Ninja build graph or a CMake configure dependency, is listed in a
--diff-extra-inputs file, or is included by one of its compilation units (as
reported by the compiler). If a check fails, the instance is treated as affected.
"""

from __future__ import annotations

import json
import logging
import os
import re
import shlex
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

logger = logging.getLogger('twister')

DEFAULT_FORCE_ALL_PATTERNS = [r'(^|/)Kconfig[^/]*$', r'(^|/)CMakeLists\.txt$', r'\.cmake$']

INCLUDABLE_SUFFIXES = {
    '.h', '.hh', '.hpp', '.hxx', '.inc', '.inl', '.ipp', '.tpp', '.tcc', '.def',
    '.c', '.cc', '.cpp', '.cxx', '.s', '.S',
}


@dataclass
class DiffChanges:
    base: str
    files: set[str] = field(default_factory=set)
    force_all: list[str] = field(default_factory=list)


def _git(repo: Path, *args: str) -> str:
    return subprocess.run(['git', '--no-optional-locks', '-C', str(repo), *args],
                          capture_output=True, text=True, check=True).stdout


def get_diff_changes(diff_base: str, force_all_patterns: list[str],
                     repo: Path | None = None) -> DiffChanges | None:
    """Files changed since the merge-base with diff_base (committed, uncommitted,
    untracked or deleted), or None if they cannot be determined."""
    try:
        top = Path(_git(repo or Path.cwd(), 'rev-parse', '--show-toplevel').strip())
        merge_base = _git(top, 'merge-base', diff_base, 'HEAD').strip()
        names = _git(top, 'diff', '--name-only', merge_base).splitlines()
        names += _git(top, 'ls-files', '--others', '--exclude-standard').splitlines()
    except (OSError, subprocess.CalledProcessError) as e:
        logger.warning(f"--diff-base: cannot determine changes vs '{diff_base}' ({e}). "
                       "Building all test instances.")
        return None

    names = sorted({n.strip() for n in names if n.strip()})
    regexes = [re.compile(p) for p in force_all_patterns]
    changes = DiffChanges(diff_base, {os.path.realpath(top / n) for n in names},
                          [n for n in names if any(r.search(n) for r in regexes)])
    logger.info(f"--diff-base: {len(names)} file(s) changed vs '{diff_base}'.")
    if changes.force_all:
        logger.info(f"--diff-base: building all, changed: {', '.join(changes.force_all)}")
    return changes


def _ninja(build_dir: str, *args: str) -> list[str]:
    return subprocess.run(['ninja', '-C', build_dir, '-t', *args],
                          capture_output=True, text=True, check=True).stdout.splitlines()


def _build_inputs(build_dir: str, extra_input_files: list[str]) -> set[str]:
    """Inputs of the build graph and of the RERUN_CMAKE edge (configure deps),
    plus the files listed in extra_input_files."""
    paths = [shlex.split(line)[0] for line in _ninja(build_dir, 'inputs', 'all') if line.strip()]
    in_inputs = False
    for line in _ninja(build_dir, 'query', 'build.ninja'):
        stripped = line.strip()
        if not line[:1].isspace() or stripped.startswith(('input:', 'outputs:')):
            in_inputs = stripped.startswith('input:')
        elif in_inputs and stripped:
            paths.append(stripped.removeprefix('|| '))
    for name in extra_input_files:
        extra = Path(build_dir) / name
        if extra.is_file():
            paths += [p.strip() for p in extra.read_text().splitlines() if p.strip()]
    return {os.path.realpath(os.path.join(build_dir, p)) for p in paths}


def _dependency_command(entry: dict) -> list[str]:
    """Compile command which prints the dependencies instead of compiling."""
    args = entry.get('arguments') or shlex.split(entry['command'])
    if os.path.basename(args[0]) in ('ccache', 'sccache', 'distcc'):
        args = args[1:]
    cmd = []
    skip_next = False
    for arg in args:
        if skip_next or arg in ('-c', '-MD', '-MMD', '-MP'):
            skip_next = False
        elif arg in ('-o', '-MF', '-MT', '-MQ'):
            skip_next = True
        else:
            cmd.append(arg)
    # -MG: headers generated later during the build count as existing
    return [*cmd, '-M', '-MG']


def _included_candidate(build_dir: str, candidates: set[str]) -> str | None:
    with open(os.path.join(build_dir, 'compile_commands.json')) as f:
        entries = json.load(f)
    for entry in entries:
        result = subprocess.run(_dependency_command(entry), cwd=entry['directory'],
                                capture_output=True, text=True, timeout=300)
        if result.returncode != 0:
            raise RuntimeError(f"dependency scan of {entry['file']} failed")
        deps = result.stdout.replace('\\\n', ' ').partition(': ')[2]
        for dep in re.split(r'(?<!\\)\s+', deps):
            path = os.path.realpath(os.path.join(entry['directory'], dep.replace('\\ ', ' ')))
            if dep and path in candidates:
                return path
    return None


def find_affecting_change(changes: DiffChanges, source_dir: str, build_dir: str,
                          extra_input_files: list[str]) -> str | None:
    """Reason why the instance in build_dir is affected by changes, or None."""
    if changes.force_all:
        return f"{changes.force_all[0]} forces building all"
    source_dir = os.path.realpath(source_dir) + os.sep
    for path in sorted(changes.files):
        if path.startswith(source_dir):
            return f"{path} is part of the test suite"
    try:
        touched = changes.files & _build_inputs(build_dir, extra_input_files)
        if touched:
            return f"{min(touched)} is an input of the build"
        candidates = {p for p in changes.files if Path(p).suffix in INCLUDABLE_SUFFIXES}
        deleted = [p for p in sorted(candidates) if not os.path.exists(p)]
        if deleted:
            # -MG reports missing headers as spelled in the #include
            return f"{deleted[0]} was deleted and might be included"
        if candidates:
            included = _included_candidate(build_dir, candidates)
            if included:
                return f"{included} is included by a compilation unit"
    except Exception as e:
        return f"unable to check changes against the build ({e})"
    return None
