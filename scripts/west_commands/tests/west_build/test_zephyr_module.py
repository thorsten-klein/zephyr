import os
import pytest
import sys
import yaml

from pathlib import Path

pythonpath = os.fspath(Path(__file__).parents[3])
sys.path.append(pythonpath)
import zephyr_module

def save_extra_module_yml(path: Path, paths: list[str], recursive=None):
    data = {'extra-modules':{}}
    data['extra-modules']['paths'] = [p for p in paths]
    if recursive is not None:
        data['extra-modules']['recursive'] = recursive
    
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w') as file:
        yaml.dump(data, file, default_flow_style=False)

def test_parse_modules():
    modules = zephyr_module.parse_modules('zephyr')
