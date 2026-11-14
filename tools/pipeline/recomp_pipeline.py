# SPDX-License-Identifier: GPL-3.0-or-later
"""recomp_pipeline: turns a Switch 2 .nup into a playable native Windows game folder, in one command.

  recomp_pipeline.py inspect <game.nup>                       what the package holds (JSON), reads only the header and table of contents
  recomp_pipeline.py build <game.nup> [options]               the whole pipeline:
      stage     copy the .nup to the SSD work folder first (when it is on another drive, e.g. an external HDD)
      exefs     unpack the code (tools/raiders/extract-exefs.mjs, every file SHA-256 checked)
      profile   the game's profile: the built-in one for its title id, else a generated one (tools/pipeline/profile_gen.py)
      lift      translate ARM64 to C++ and compile it (nxrecomp + clang-cl, Ninja; minutes to hours, cached by sccache)
      link      build the game exe against the runtime (game-build, release mode)
      romfs     unpack the game's assets straight into the output folder
      package   the runtime files next to the exe (shader translator, fonts, extractor), the settings file, cleanup
  options: --work DIR (SSD scratch; default <workspace>\\work\\pipeline)  --out DIR (library; default <workspace>\\work\\library)
           --stage auto|always|never  --keep-temp  --jobs N  --title NAME  --version X.Y.Z  --only STAGE[,STAGE]  --json

Progress: with --json every line on stdout is one JSON object, for the launcher:
  {"event":"stage","stage":"lift","label":"Translating ARM64","index":4,"count":7}
  {"event":"progress","stage":"lift","done":120,"total":2500}
  {"event":"log","stage":"lift","line":"..."}
  {"event":"done","ok":true,"game_dir":"...","exe":"..."}  /  {"event":"done","ok":false,"stage":"lift","error":"..."}
Without --json the same goes out as plain text. Every line also goes to <work>\\<id>\\pipeline.log.
The user's .nup is only ever read.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
WORKSPACE = os.path.normpath(os.path.join(REPO, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'recomp'))

import nup  # noqa: E402
import profile_gen  # noqa: E402

STAGES = [
    ('stage', 'Copying to SSD'),
    ('exefs', 'Unpacking code'),
    ('profile', 'Creating game profile'),
    ('lift', 'Translating ARM64'),
    ('link', 'Linking native exe'),
    ('romfs', 'Unpacking assets'),
    ('package', 'Finishing'),
]
LABELS = dict(STAGES)

# Shared tools the pipeline needs; the defaults follow this workspace's layout (tools/raiders/*.ps1 use the same places).
TOOLS = {
    'python': os.path.join(WORKSPACE, 'work', 'toolchain', 'python313-embed', 'python.exe'),
    'node': os.path.join(WORKSPACE, 'work', 'release', 'node', 'node.exe'),
    'a2b': os.path.join(WORKSPACE, 'work', 'raiders', 'build', 'astc-textures'),
    'spirv_harness': os.path.join(WORKSPACE, 'work', 'raiders', 'build', 'native-x64-v2', 'tools', 'spirv_harness'),
    'sccache_dir': os.path.join(WORKSPACE, 'work', 'build', 'sccache'),
}


class PipelineError(Exception):
    pass


class Reporter:
    def __init__(self, as_json: bool):
        self.as_json = as_json
        self.stage = None
        self.logf = None
        self._last_progress = 0.0

    def open_log(self, path: str):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.logf = open(path, 'a', encoding='utf-8')
        self.logf.write(f'\n==== {time.strftime("%Y-%m-%d %H:%M:%S")} ====\n')

    def emit(self, obj: dict, text: str | None = None):
        if self.as_json:
            sys.stdout.write(json.dumps(obj) + '\n')
        elif text is not None:
            sys.stdout.write(text + '\n')
        sys.stdout.flush()
        if self.logf and text is not None:
            self.logf.write(text + '\n')
            self.logf.flush()

    def begin(self, stage: str):
        self.stage = stage
        idx = [s for s, _ in STAGES].index(stage)
        self.emit({'event': 'stage', 'stage': stage, 'label': LABELS[stage], 'index': idx, 'count': len(STAGES)},
                  f'== {LABELS[stage]} ({idx + 1}/{len(STAGES)})')

    def log(self, line: str):
        self.emit({'event': 'log', 'stage': self.stage, 'line': line}, line)

    def progress(self, done: int, total: int, force: bool = False):
        now = time.monotonic()
        if not force and now - self._last_progress < 0.25 and done < total:
            return
        self._last_progress = now
        self.emit({'event': 'progress', 'stage': self.stage, 'done': done, 'total': total},
                  None if self.as_json else None)

    def done(self, ok: bool, **kw):
        obj = {'event': 'done', 'ok': ok, **kw}
        self.emit(obj, ('DONE ' if ok else 'FAILED ') + json.dumps(kw))


def run(cmd, rep: Reporter, env=None, cwd=None, on_line=None, shell=False):
    """Runs a command, streaming its output lines to the log (and to on_line); raises on a non-zero exit."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=cwd, shell=shell,
                         text=True, encoding='utf-8', errors='replace', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    tail = []
    for line in p.stdout:
        line = line.rstrip('\r\n')
        if not line:
            continue
        tail = (tail + [line])[-30:]
        if on_line is None or on_line(line) is not False:
            rep.log(line)
    code = p.wait()
    if code != 0:
        raise PipelineError(f'exit code {code}: ' + (tail[-1] if tail else ''))


def build_env(jobs: int) -> dict:
    env = dict(os.environ)
    vs_installer = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer')
    env['PATH'] = vs_installer + os.pathsep + env.get('PATH', '')
    env['NX_PYTHON'] = TOOLS['python']
    env['NX_JOBS'] = str(jobs)
    env.setdefault('SCCACHE_DIR', TOOLS['sccache_dir'])
    env.setdefault('SCCACHE_SERVER_PORT', '24339')
    env.setdefault('SCCACHE_CACHE_SIZE', '40G')
    return env


def check_tools(need_build: bool) -> list[str]:
    missing = []
    if not os.path.isfile(TOOLS['node']) and not shutil.which('node'):
        missing.append(f"Node.js ({TOOLS['node']} or on PATH)")
    if need_build:
        for k in ('python', 'a2b', 'spirv_harness'):
            p = TOOLS[k] if k != 'a2b' else os.path.join(TOOLS['a2b'], 'astc2bc7.lib')
            if not os.path.exists(p):
                missing.append(f'{k} ({p})')
        vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
        if not os.path.isfile(vswhere):
            missing.append('Visual Studio 2022 Build Tools with the C++ and LLVM/Clang components')
    return missing


def node_exe() -> str:
    return TOOLS['node'] if os.path.isfile(TOOLS['node']) else (shutil.which('node') or 'node')


def extract(nup_path: str, out_dir: str, section: str, total: int, rep: Reporter):
    """Unpacks one section with the checked extractor; resume mode keeps files already extracted and verified."""
    script = os.path.join(REPO, 'tools', 'raiders', 'extract-exefs.mjs')
    count = [0]
    file_line = re.compile(r'^\S.*  [\d,]+ bytes$')

    def on_line(line):
        if file_line.match(line):
            count[0] += 1
            rep.progress(count[0], total)
            return count[0] <= 20 or count[0] % 500 == 0      # the log keeps the first files and a sample, not 200,000 lines
        m = re.match(r'Resume progress: checked (\d+)', line)
        if m:
            rep.progress(int(m.group(1)), total)
        return True

    run([node_exe(), script, nup_path, out_dir, 'update', section, 'resume'], rep, on_line=on_line)
    rep.progress(total, total, force=True)


def ninja_progress(rep: Reporter, label_switch=None):
    pat = re.compile(r'^\[(\d+)/(\d+)\]\s*(.*)$')

    def on_line(line):
        m = pat.match(line)
        if not m:
            return True
        done, total = int(m.group(1)), int(m.group(2))
        rep.progress(done, total)
        return done % 50 == 0 or done == total or 'error' in line.lower()
    return on_line


def copy_with_progress(src: str, dst: str, rep: Reporter):
    total = os.path.getsize(src)
    if os.path.exists(dst) and os.path.getsize(dst) == total:
        rep.log(f'already staged: {dst}')
        return
    tmp = dst + '.part'
    done = 0
    with open(src, 'rb') as fi, open(tmp, 'wb') as fo:
        while True:
            b = fi.read(16 << 20)
            if not b:
                break
            fo.write(b)
            done += len(b)
            rep.progress(done, total)
    os.replace(tmp, dst)
    rep.progress(total, total, force=True)


def same_drive(a: str, b: str) -> bool:
    return os.path.splitdrive(os.path.abspath(a))[0].lower() == os.path.splitdrive(os.path.abspath(b))[0].lower()


GAME_INI = """# {title} - start-up options for the recompiled game (the in-game settings menu, F1, writes controls.ini).
# Generated by tools/pipeline/recomp_pipeline.py. exefs\\ and romfs\\ next to the exe are the game's files.

[boot]
run_module_init = true

[memory]
limit_gb = 40

[nvn]
gpu_timestamp_scale = 0.4

[pad]
stick_gyro = off

[am]
# handheld renders at the game's handheld resolution (lighter); docked = the TV resolution
operation_mode = handheld
"""


def cmd_build(a) -> int:
    rep = Reporter(a.json)
    only = set(a.only.split(',')) if a.only else None
    want = (lambda s: only is None or s in only)
    stage_name = None
    try:
        nup_path = os.path.abspath(a.nup)
        info = nup.read(nup_path)
        pkg = info.pick('update')
        title_id = pkg.title_id_hex
        guess_title, guess_version = profile_gen.title_from_filename(nup_path)
        title, version = a.title or guess_title, a.version or guess_version
        builtin = profile_gen.builtin_profiles().get(title_id)
        pid = builtin or profile_gen.generated_id(title_id)
        work = os.path.join(os.path.abspath(a.work), pid)
        rep.open_log(os.path.join(work, 'pipeline.log'))
        rep.log(f'{title} {version}: title {title_id}, NUP package {pkg.index} ({pkg.kind_name} v{pkg.version}), '
                f'profile {pid} ({"built-in" if builtin else "generated"})')
        missing = check_tools(need_build=any(want(s) for s in ('lift', 'link')))
        if missing:
            raise PipelineError('missing tools: ' + '; '.join(missing))

        game_dir_name = (profile_gen.builtin_summary(builtin)['exe'][:-4] if builtin else profile_gen.safe_name(title))
        game_dir = os.path.join(os.path.abspath(a.out), game_dir_name)
        exefs = os.path.join(work, 'exefs')
        env = build_env(a.jobs)

        # stage: a .nup on another drive (external HDD) is copied to the SSD work folder first, and removed again at the end
        src = nup_path
        stage_copy = a.stage == 'always' or (a.stage == 'auto' and not same_drive(nup_path, work))
        if want('stage'):
            stage_name = 'stage'
            rep.begin('stage')
            if stage_copy:
                staged = os.path.join(work, 'source.nup')
                os.makedirs(work, exist_ok=True)
                need = info.size + 2 * (pkg.exefs.bytes if pkg.exefs else 0)
                free = shutil.disk_usage(work).free
                if free < need:
                    raise PipelineError(f'not enough space on the work drive to stage the .nup ({need >> 30} GB needed, {free >> 30} GB free)')
                rep.log(f'copying {nup_path} -> {staged}')
                copy_with_progress(nup_path, staged, rep)
                src = staged
            else:
                rep.log('the .nup is on the work drive: no copy needed')
        elif stage_copy and os.path.exists(os.path.join(work, 'source.nup')):
            src = os.path.join(work, 'source.nup')

        if want('exefs'):
            stage_name = 'exefs'
            rep.begin('exefs')
            extract(src, exefs, 'exefs', pkg.exefs.files if pkg.exefs else 0, rep)

        if want('profile'):
            stage_name = 'profile'
            rep.begin('profile')
            if builtin:
                bad = profile_gen.check_builtin_files(builtin, exefs)
                if bad:
                    raise PipelineError(f'the built-in profile {builtin} was made for another version of this game: ' + '; '.join(bad))
                rep.log(f'built-in profile {builtin}: module hashes match')
                summary = profile_gen.builtin_summary(builtin)
            else:
                summary = profile_gen.generate(exefs, title_id, title, version, pkg.version, log=rep.log)
            rep.emit({'event': 'profile', 'profile': summary}, None)
        if any(want(s) for s in ('lift', 'link', 'package')):
            pj = os.path.join(REPO, 'recomp', 'games', pid, 'profile.json')
            if not builtin and not os.path.isfile(pj):
                raise PipelineError(f'no profile for {pid} yet: run the profile stage first')
            if not builtin:
                with open(pj, encoding='utf-8') as f:
                    summary = json.load(f)
            else:
                summary = profile_gen.builtin_summary(pid)

        mod_dir = os.path.join(work, 'build', 'modules')
        exe_dir = os.path.join(work, 'build', 'exe')
        if want('lift'):
            stage_name = 'lift'
            rep.begin('lift')
            upper = pid.upper()
            nx_env = os.path.join(REPO, 'recomp', 'scripts', 'nx_env.cmd')
            src_dir = os.path.join(REPO, 'recomp', 'games', pid)
            tc = os.path.join(REPO, 'recomp', 'cmake', 'clang-cl.cmake')
            cfg = (f'call "{nx_env}" cmake -S "{src_dir}" -B "{mod_dir}" -G Ninja -DCMAKE_TOOLCHAIN_FILE="{tc}" -DCMAKE_BUILD_TYPE=Release '
                   f'-DNX_ROOT="{REPO}" -D{upper}_EXEFS="{exefs}" -D{upper}_GEN="{os.path.join(work, "lift")}"')
            run(f'cmd /d /c "{cfg}"', rep, env=env)
            bld = f'call "{nx_env}" cmake --build "{mod_dir}" --target {pid}_modules -- -j {a.jobs}'
            run(f'cmd /d /c "{bld}"', rep, env=env, on_line=ninja_progress(rep))

        if want('link'):
            stage_name = 'link'
            rep.begin('link')
            env_cmd = os.path.join(REPO, 'runtime', 'scripts', 'env.cmd')
            tc = os.path.join(REPO, 'recomp', 'cmake', 'clang-cl.cmake')
            manifest = os.path.join(REPO, 'recomp', 'games', pid, 'game.toml')
            cfg = (f'cmake -S "{os.path.join(REPO, "game-build")}" -B "{exe_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release '
                   f'-DCMAKE_TOOLCHAIN_FILE:FILEPATH="{tc}" -DGAME_MANIFEST="{manifest}" -DGAME_RUNTIME_DIR="{os.path.join(REPO, "runtime")}" '
                   f'-DGAME_LIB_DIR="{mod_dir}" -DGAME_EXEFS_DEFAULT=exefs -DNXRT_A2B_DIR="{TOOLS["a2b"]}" -DNXRT_BUILD_TESTS=OFF '
                   f'-DNXRT_BUILD_LIFT_TESTS=OFF -DNXRT_BUILD_LAUNCHER=OFF -DGAME_RELEASE=ON -DGAME_VERSION={version}')
            bld = f'cmake --build "{exe_dir}" --target game_exe --parallel {a.jobs}'
            run(f'cmd /d /c "call "{env_cmd}" && {cfg} && {bld}"', rep, env=env, on_line=ninja_progress(rep))

        if want('romfs'):
            stage_name = 'romfs'
            rep.begin('romfs')
            os.makedirs(game_dir, exist_ok=True)
            need = pkg.romfs.bytes if pkg.romfs else 0
            have = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(os.path.join(game_dir, 'romfs')) for f in fs) \
                if os.path.isdir(os.path.join(game_dir, 'romfs')) else 0
            free = shutil.disk_usage(game_dir).free
            if free < need - have:
                raise PipelineError(f'not enough space in the output folder for the assets ({(need - have) >> 30} GB needed, {free >> 30} GB free)')
            extract(src, os.path.join(game_dir, 'romfs'), 'romfs', pkg.romfs.files if pkg.romfs else 0, rep)

        if want('package'):
            stage_name = 'package'
            rep.begin('package')
            os.makedirs(game_dir, exist_ok=True)
            exe_name = summary['exe']
            built = os.path.join(exe_dir, exe_name)
            if not os.path.isfile(built):
                raise PipelineError(f'the linked exe is missing: {built}')
            shutil.copy2(built, game_dir)
            for f in os.listdir(exe_dir):
                if re.fullmatch(r'astcdec-.*-shared\.dll', f):
                    shutil.copy2(os.path.join(exe_dir, f), game_dir)
            shutil.copytree(os.path.join(exe_dir, 'fonts'), os.path.join(game_dir, 'fonts'), dirs_exist_ok=True)
            tools_dir = os.path.join(game_dir, 'tools')
            os.makedirs(tools_dir, exist_ok=True)
            shutil.copy2(os.path.join(REPO, 'tools', 'raiders', 'extract-exefs.mjs'), tools_dir)
            shutil.copytree(TOOLS['spirv_harness'], os.path.join(tools_dir, 'spirv_harness'), dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns('*.pdb'))
            if os.path.isfile(TOOLS['node']):
                os.makedirs(os.path.join(tools_dir, 'node'), exist_ok=True)
                shutil.copy2(TOOLS['node'], os.path.join(tools_dir, 'node'))
            # the code next to the exe (installed layout: game_main finds exefs\main and starts from the game folder)
            shutil.copytree(exefs, os.path.join(game_dir, 'exefs'), dirs_exist_ok=True)
            ini = os.path.join(game_dir, summary['ini'])
            if builtin == 'sra' and os.path.isfile(os.path.join(REPO, 'game-build', 'res', 'release', 'sra.ini')):
                if not os.path.exists(ini):
                    shutil.copy2(os.path.join(REPO, 'game-build', 'res', 'release', 'sra.ini'), ini)
            elif not os.path.exists(ini):
                with open(ini, 'w', encoding='utf-8') as f:
                    f.write(GAME_INI.format(title=title))
            import tomllib
            with open(os.path.join(REPO, 'recomp', 'games', pid, 'game.toml'), 'rb') as f:
                save_folder = tomllib.load(f)['files'].get('save_folder', pid)
            with open(os.path.join(game_dir, 'game.json'), 'w', encoding='utf-8') as f:
                json.dump({'title': title, 'title_id': title_id, 'version': version, 'profile': pid, 'generated': not builtin,
                           'save_folder': save_folder,
                           'exe': exe_name, 'frame_hook': summary.get('frame_hook'), 'source_nup': nup_path,
                           'built': time.strftime('%Y-%m-%dT%H:%M:%S')}, f, indent=1)
            # cleanup: the staged copy always; the build tree (generated C++, objects, libraries) unless --keep-temp
            staged = os.path.join(work, 'source.nup')
            if os.path.exists(staged):
                os.remove(staged)
                rep.log('removed the staged .nup copy')
            if not a.keep_temp:
                for d in (os.path.join(work, 'lift'), os.path.join(work, 'build')):
                    if os.path.isdir(d):
                        shutil.rmtree(d, ignore_errors=True)
                rep.log('removed the temporary build files (sccache keeps the compiled objects for fast rebuilds)')
            rep.log(f'game folder: {game_dir}')

        finished = want('package')      # a partial run (--only) has no playable folder yet
        rep.done(True, profile=pid, generated=not builtin, complete=finished,
                 **({'game_dir': game_dir, 'exe': os.path.join(game_dir, summary['exe'])} if finished else {}))
        return 0
    except (PipelineError, nup.NupError, profile_gen.ProfileError, OSError) as e:
        rep.done(False, stage=stage_name, error=str(e))
        return 1


def cmd_inspect(a) -> int:
    info = nup.read(os.path.abspath(a.nup))
    d = nup.describe(info)
    title, version = profile_gen.title_from_filename(a.nup)
    d['title_guess'], d['version_guess'] = title, version
    sel = d.get('selected')
    if sel:
        b = profile_gen.builtin_profiles().get(sel['title_id'])
        d['profile'] = {'id': b or profile_gen.generated_id(sel['title_id']), 'builtin': bool(b)}
    print(json.dumps(d, indent=None if a.json else 1))
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog='recomp_pipeline', description='Switch 2 .nup -> native Windows game folder')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('inspect')
    p.add_argument('nup')
    p.add_argument('--json', action='store_true')
    b = sub.add_parser('build')
    b.add_argument('nup')
    b.add_argument('--work', default=os.path.join(WORKSPACE, 'work', 'pipeline'))
    b.add_argument('--out', default=os.path.join(WORKSPACE, 'work', 'library'))
    b.add_argument('--stage', choices=('auto', 'always', 'never'), default='auto')
    b.add_argument('--keep-temp', action='store_true')
    b.add_argument('--jobs', type=int, default=max(1, min(12, os.cpu_count() or 4)))
    b.add_argument('--title')
    b.add_argument('--version')
    b.add_argument('--only', help='comma-separated stages: ' + ','.join(s for s, _ in STAGES))
    b.add_argument('--json', action='store_true')
    a = ap.parse_args(argv)
    return cmd_inspect(a) if a.cmd == 'inspect' else cmd_build(a)


if __name__ == '__main__':
    sys.exit(main())
