# Recompile pipeline

One command that turns a Switch 2 `.nup` into a native Windows game folder. The launcher app (phase 2, `apps/launcher`) drives this
tool; it works on its own too.

```powershell
$py = "..\..\work\toolchain\python313-embed\python.exe"      # the portable Python 3.13 the build already uses
& $py tools\pipeline\recomp_pipeline.py inspect "<game.nup>"
& $py tools\pipeline\recomp_pipeline.py build   "<game.nup>" [--work D:\RecompWork] [--out D:\Games\Recompiled] [--json]
```

## Stages

| Stage | What it does | Tool |
|---|---|---|
| `stage` | Copies the `.nup` to the work folder first when it is on another drive (external HDD); removed at the end. `--stage auto\|always\|never` | built in |
| `exefs` | Unpacks the code, every file SHA-256 checked | `tools/raiders/extract-exefs.mjs` (Node) |
| `profile` | The game's profile: the built-in one for its title ID (`sra` = Splatoon Raiders), else a generated one | `profile_gen.py`, `frame_hook_scan.py` |
| `lift` | Translates ARM64 to C++ and compiles it (cached by sccache) | `recomp/` (nxrecomp + clang-cl, Ninja) |
| `link` | Builds the game exe against the runtime, release mode | `game-build/` |
| `romfs` | Unpacks the assets straight into the output folder | extractor |
| `package` | Runtime files next to the exe (shader translator, fonts, Node, extractor), the settings ini, `game.json`; deletes temporary build files unless `--keep-temp` | built in |

`--only exefs,profile` runs a subset. The user's `.nup` is only ever read.

## Progress protocol (`--json`)

One JSON object per stdout line:

```json
{"event":"stage","stage":"lift","label":"Translating ARM64","index":3,"count":7}
{"event":"progress","stage":"lift","done":120,"total":2500}
{"event":"log","stage":"lift","line":"[120/2500] Building CXX object ..."}
{"event":"profile","profile":{"id":"x0400c3f00006e000","generated":true,"frame_hook":7523715504, ...}}
{"event":"done","ok":true,"game_dir":"...","exe":"..."}
{"event":"done","ok":false,"stage":"lift","error":"..."}
```

The full log is also written to `<work>\<profile id>\pipeline.log`.

## Profiles

- **Built-in** (`recomp/games/<id>` + `runtime/games/<id>`, kept by hand): carry the fixes found while porting a game. Used when the
  title ID matches and the module hashes match the recorded version.
- **Generated** (`x<title id>`, ignored by git): made from the extracted ExeFS. Module load slots follow the framework's Switch 2
  convention (main 0x71…, sdk 0x75…, subsdkN 0x76…+N·256 MiB, rtld 0x77…), translation units scale with module size, the build ID
  comes from the NSO header. The title and version come from the file name (`<Title> + Update <x.y.z>.nup`; NUPs carry no control
  data); `--title` / `--version` override them.

### Frame hook detection

Play mode needs the game's per-frame method (window events, input). `frame_hook_scan.py` finds sead's run loop
(`for (;;) this->vtable[0xE0](this)`), the vtables that hold it, and their slot 0xE0. When several classes share the loop:

1. the method found in the fewest vtables (a class's own override, not the shared base version) - **override**;
2. on a tie, the vtable with the most slots no other vtable shares (the class the game runs implements more of its own) - **likely**.

Checked on two games: Splatoon Raiders gives `0x71031b8c60` (rule 1), the address found by hand during the port; Mario Kart World
gives `0x710069cbb0` (rule 2, "likely": not yet confirmed by running it). Titles not built on sead get no hook.

## Expectations

A generated profile gets a game to **build**. Whether it **plays** depends on how much of that game the shared runtime already covers
(GPU features, system services): a new title usually stops at its first unsupported feature, and each fix there goes into the runtime
and helps every later game. Built-in profiles are the tested ones.
