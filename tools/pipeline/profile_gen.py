# SPDX-License-Identifier: GPL-3.0-or-later
"""Game profiles: the per-game folders the recompiler and the runtime build from.

A profile is recomp/games/<id>/ (game.toml, config.py, CMakeLists.txt, main.sites) plus runtime/games/<id>/ (the game module: title
defaults, play-mode hooks). Built-in profiles are kept by hand (sra: Splatoon Raiders, with the fixes found while porting it). Any
other title gets a generated profile, id "x<title id>" (git ignores those folders), made from its extracted ExeFS:
  - the code modules and their standard Switch 2 load slots, file hashes, the main module's build id;
  - translation-unit counts from the module sizes (more units = more parallel compiles);
  - the frame hook from frame_hook_scan.py (sead titles), or none.
Regenerating overwrites a generated profile; a built-in one is never touched.
"""
from __future__ import annotations

import glob
import hashlib
import json
import math
import os
import re
import struct
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
TEMPLATES = os.path.join(HERE, 'templates')

MAIN_BASE = 0x7100000000
SDK_BASE = 0x7500000000
SUBSDK_BASE = 0x7600000000
SUBSDK_STRIDE = 0x10000000           # subsdk0, subsdk1, ... each get a 256 MiB slot below rtld
RTLD_BASE = 0x7700000000
BYTES_PER_UNIT = 200_000             # Raiders: main 41 MB -> 200 units, sdk 7.8 MB -> 45


class ProfileError(Exception):
    pass


def builtin_profiles() -> dict[str, str]:
    """{title id (upper hex): profile id} of the hand-kept profiles in recomp/games (generated x* ones excluded)."""
    out = {}
    for toml_path in glob.glob(os.path.join(REPO, 'recomp', 'games', '*', 'game.toml')):
        pid = os.path.basename(os.path.dirname(toml_path))
        if pid.startswith('x') or not os.path.isdir(os.path.join(REPO, 'runtime', 'games', pid)):
            continue
        with open(toml_path, 'rb') as f:
            out[tomllib.load(f)['game']['title_id'].upper()] = pid
    return out


def generated_id(title_id: str) -> str:
    return 'x' + title_id.lower()


def title_from_filename(path: str) -> tuple[str, str]:
    """('Mario Kart World', '1.8.0') from 'Mario Kart World + Update 1.8.0.nup'; NUPs carry no control data (name, version)."""
    stem = os.path.splitext(os.path.basename(path))[0]
    m = re.match(r'^(.*?)\s*(?:\+\s*Update\s*|\[v?|\(v?|\s+v)(\d+(?:\.\d+){1,3})', stem, re.I)
    if m:
        return m.group(1).strip(' -_[]()') or stem, m.group(2)
    return stem.strip(), '1.0.0'


def safe_name(title: str) -> str:
    """'Mario Kart World' -> 'MarioKartWorld' (exe and folder names)."""
    s = re.sub(r'[^A-Za-z0-9]+', ' ', title).title().replace(' ', '')
    return s or 'Game'


def _nso_build_id(path: str) -> str:
    with open(path, 'rb') as f:
        head = f.read(0x60)
    if head[:4] != b'NSO0':
        raise ProfileError(f'{os.path.basename(path)} is not an NSO module')
    return head[0x40:0x54].hex()


def _sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def module_layout(exefs: str) -> list[dict]:
    """The code modules of an ExeFS in lifter order (main, sdk, subsdk0.., rtld) with their load slots."""
    names = sorted(n for n in os.listdir(exefs) if os.path.isfile(os.path.join(exefs, n)) and not n.endswith('.npdm'))
    if 'main' not in names or 'rtld' not in names:
        raise ProfileError('the ExeFS has no main or rtld module')
    subs = sorted((n for n in names if re.fullmatch(r'subsdk\d+', n)), key=lambda n: int(n[6:]))
    unknown = set(names) - {'main', 'sdk', 'rtld', *subs}
    if unknown:
        raise ProfileError('unexpected ExeFS files: ' + ', '.join(sorted(unknown)))
    order = ['main'] + (['sdk'] if 'sdk' in names else []) + subs + ['rtld']
    mods = []
    for i, n in enumerate(order):
        size = os.path.getsize(os.path.join(exefs, n))
        if n == 'main':
            base = MAIN_BASE
        elif n == 'sdk':
            base = SDK_BASE
        elif n == 'rtld':
            base = RTLD_BASE
        else:
            k = int(n[6:])
            base = SUBSDK_BASE + k * SUBSDK_STRIDE
            if size > SUBSDK_STRIDE or base + SUBSDK_STRIDE > RTLD_BASE:
                raise ProfileError(f'{n} does not fit the generated load slots')
        mods.append({'name': n, 'file': n, 'base': base, 'index': i, 'size': size,
                     'units': max(1, math.ceil(size / BYTES_PER_UNIT)) if n != 'rtld' else 1})
    return mods


def _fill(text: str, values: dict) -> str:
    for k, v in values.items():
        text = text.replace('@' + k + '@', str(v))
    if re.search(r'@[A-Z_]+@', text):
        raise ProfileError('template placeholder left unfilled: ' + re.search(r'@[A-Z_]+@', text).group(0))
    return text


def generate(exefs: str, title_id: str, title: str, version: str, nup_version: int, log=print) -> dict:
    """Writes (or rewrites) the generated profile x<title id>; returns its summary (also saved as profile.json in it)."""
    from frame_hook_scan import scan    # local import: needs nxrecomp, which the pipeline puts on sys.path

    pid = generated_id(title_id)
    rdir = os.path.join(REPO, 'recomp', 'games', pid)
    gdir = os.path.join(REPO, 'runtime', 'games', pid)
    mods = module_layout(exefs)
    log(f'profile {pid}: modules ' + ', '.join(f"{m['name']} ({m['size']:,} bytes, {m['units']} units)" for m in mods))

    hook = scan(os.path.join(exefs, 'main'))
    frame = hook['frame_method']
    if frame:
        frame_note = f"sead run loop {hook['candidates'][0]['loop_function']:#x}, vtable slot 0xE0 ({hook['confidence']}; found by frame_hook_scan.py)"
        log(f'profile {pid}: frame hook {frame:#x} ({hook["confidence"]})')
    else:
        frame_note = 'no sead run loop found: play mode is not pumped until a frame hook is added by hand'
        log(f'profile {pid}: no frame hook found ({len(hook["loops"])} run loops, {len(hook["methods"])} candidates)')

    os.makedirs(rdir, exist_ok=True)
    os.makedirs(gdir, exist_ok=True)
    sname = safe_name(title)
    lines = [
        '# SPDX-License-Identifier: GPL-3.0-or-later',
        f'# Generated by tools/pipeline/profile_gen.py from the ExeFS of {title} ({title_id}). Regenerating overwrites it.',
        'schema = 1', '', '[game]',
        f'id = "{pid}"', f'title = "{title}"', f'short_title = "{title}"', f'product = "{title}"',
        f'title_id = "{title_id}"', f'version = "{version}"', 'platform = "switch2"', 'languages = 0x1ffff', '',
        '[files]', f'exe = "{sname}.exe"', f'runtime_dll = "{pid}_rt.dll"', f'ini = "{pid}.ini"', f'log = "{pid}.log"',
        f'setup_exe = "{pid}_setup.exe"', f'launcher_ini = "{pid}_launcher.ini"', f'shader_pack = "shader_cache.{pid}shc"',
        f'save_folder = "{sname}Native"', '', '[dump]',
        f'version = "{version} (NUP package version {nup_version})"', f'display_version = "{version}"',
        f'main_build_id = "{_nso_build_id(os.path.join(exefs, "main"))}"', f'example_folder = "{sname}_v{version}"', '',
    ]
    for n in sorted(os.listdir(exefs)):
        lines += ['[[dump.file]]', f'name = "{n}"', f'sha256 = "{_sha256(os.path.join(exefs, n))}"']
    lines.append('')
    for m in mods:
        lines += ['[[module]]', f'name = "{m["name"]}"', f'file = "{m["file"]}"', f'base = {m["base"]:#x}', f'index = {m["index"]}',
                  f'units = {m["units"]}']
        if m['name'] == 'main' and frame:
            lines.append('sites = "main.sites"')
        if m['name'] == 'rtld':
            lines.append('entry = true')
    with open(os.path.join(rdir, 'game.toml'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    sites = os.path.join(rdir, 'main.sites')
    if frame:
        with open(sites, 'w', newline='\n') as f:
            f.write(f'# Generated: the frame boundary ({frame_note})\nentryhook {frame:#x}\n')
    elif os.path.exists(sites):
        os.remove(sites)

    values = {'ID': pid, 'TITLE': title, 'TITLE_ID': title_id, 'FRAME_UPDATE': f'{(frame - MAIN_BASE) if frame else 0:#x}',
              'FRAME_NOTE': frame_note}
    for sub, dest in (('recomp', rdir), ('runtime', gdir)):
        for name in os.listdir(os.path.join(TEMPLATES, sub)):
            with open(os.path.join(TEMPLATES, sub, name), encoding='utf-8') as f:
                text = f.read()
            with open(os.path.join(dest, name.replace('@ID@', pid)), 'w', encoding='utf-8', newline='\n') as f:
                f.write(_fill(text, values))

    summary = {'id': pid, 'generated': True, 'title': title, 'title_id': title_id, 'version': version, 'exe': f'{sname}.exe',
               'ini': f'{pid}.ini', 'modules': [{k: m[k] for k in ('name', 'base', 'index', 'units', 'size')} for m in mods],
               'frame_hook': frame, 'frame_hook_confidence': hook['confidence'], 'frame_hook_alternatives': hook['methods'][1:]}
    with open(os.path.join(rdir, 'profile.json'), 'w', newline='\n') as f:
        json.dump(summary, f, indent=1)
    return summary


def builtin_summary(pid: str) -> dict:
    with open(os.path.join(REPO, 'recomp', 'games', pid, 'game.toml'), 'rb') as f:
        t = tomllib.load(f)
    return {'id': pid, 'generated': False, 'title': t['game']['title'], 'title_id': t['game']['title_id'], 'version': t['game']['version'],
            'exe': t['files']['exe'], 'ini': t['files']['ini'], 'modules': t['module'], 'expected_files': t['dump'].get('file', [])}


def check_builtin_files(pid: str, exefs: str) -> list[str]:
    """Mismatches between a built-in profile's recorded module hashes and the extracted ExeFS (a different game version)."""
    bad = []
    for e in builtin_summary(pid)['expected_files']:
        p = os.path.join(exefs, e['name'])
        if not os.path.exists(p):
            bad.append(f"{e['name']} missing")
        elif _sha256(p) != e['sha256']:
            bad.append(f"{e['name']} differs (not the version this profile was made for)")
    return bad
