# SPDX-License-Identifier: GPL-3.0-or-later
"""Initial game-specific configuration for Splatoon Raiders (Switch 2)."""
import os
import tomllib

from nxrecomp.config import GameConfig, ModuleSpec

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'game.toml'), 'rb') as f:
    MANIFEST = tomllib.load(f)

_GAME = MANIFEST['game']
_BUILD_ID = MANIFEST['dump']['main_build_id'].upper()

BUILDS = {_BUILD_ID: _GAME['version']}


class Config(GameConfig):
    name = _GAME['id']
    title_id = _GAME['title_id']
    version = _GAME['version']
    build_id = _BUILD_ID
    modules = [ModuleSpec(m['name'], m['base'], m['file']) for m in MANIFEST['module']]
    overrides = {}
    # rtld's entry at module offset 0 is not always discovered by ordinary branch scanning.
    extra_entries = {'rtld': [0x0]}
    patches = None
