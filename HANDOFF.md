# Splatoon Raiders native port handoff

## Workspace layout

- Port source repository: `outputs/splatoon-raiders-native/`
- Local Raiders files, extracted modules, and build output: `work/raiders/`
- Original user NUP: in the user's Downloads folder; it is not part of this repository.
- Progress log: `outputs/splatoon-raiders-native/PORT_STATUS.md`

Keep the game dump and generated build artifacts local. Do not commit or package them with the source repository.

## Current state

**Latest (2026-11-14, multi-game: pipeline + launcher):**
- **Pipeline** (`tools/pipeline`, see its README): `recomp_pipeline.py build <game.nup>` goes from a .nup to a playable folder in one
  command, with JSON progress. Built-in profile for Raiders; any other title gets a generated `x<title id>` profile, frame hook
  found by `frame_hook_scan.py`.
  - Raiders from its .nup: 19 minutes, starts to the styled start menu, 60 fps.
  - Mario Kart World (generated profile, frame hook 0x710069cbb0 "likely"): builds and links completely (191,459 functions in main),
    loads all 5 modules, starts its services and mounts the 23 GB RomFS, then the SDK aborts 2.6 s in:
    `nn::nlibsdk::heap::CentralHeap::Free` -> `nnmem_nlib_debug_break` (heap block check), called from main 0x7100a93580 right
    after reading `System/HeapSize/Default.Nin_Ounce_NVN2.HeapDef.byml.zs`. Not caused by the host libc functions ([hle] libc =
    false gives the same abort). Four MapSharedMemory refusals at random addresses just before are the SDK's normal retries.
    f_7100a93580 is a destructor of a stack object (x19 = 0x7e000ffd28): it calls `member96->vtable[3](member96)`, and that call goes
    through an rtld import stub (f_7700002400) straight into sdk `CentralHeap::Free(0x79c5b00120)`, which rejects the block. A
    virtual slot that lands on an imported SDK function points at load-time symbol binding (MKW is the first title with a second
    subsdk, subsdk1) or at an object made by another allocator. Next: log what member96's vtable slot 3 holds and which relocation
    wrote it (runtime loader, cross-module ABS64/GLOB_DAT binding), and compare the object's constructor (f_7100a935e0 allocates
    0x68 bytes via f_71003e3550).
- **Launcher** (`apps/launcher`, see its README): Tauri app; library from scan folders, Recompile with live stages and log, Play,
  per-game settings, covers, controller navigation. Release exe: `apps/launcher/src-tauri/target/release/recomp-launcher.exe`.

**Latest (2026-11-09, evening, milestones 104–107):**
- **60 fps in play.**
  - Pipeline-key file appends moved off the render thread: no more 0.4–2 s freezes in new areas.
  - Dedicated allocations for images of 4 MB or more: video memory dropped from 6.8 GB over budget to 3.7 GB.
  - Uniform-buffer writes merged into one copy per run.
  - A scripted mission on the user's save holds 60.0 fps; the hideout deck went from 50–56 to 60.
- **Camera.**
  - The game's own stick camera: Motion Controls OFF on the TV/Tabletop page, set in the user's save; `stick_gyro = off`.
  - The user approved it.
- **Settings menu.**
  - Splatoon style: the game's font from romfs, the theme, View + Menu or F1, the 1.4× size.
  - A camera section with sensitivity, curve, dead zone and inverts.
- **Test tools.**
  - `vpad:` selftest steps.
  - `[prof] trace_trigger`.
  - The scripted mission route (`raiders-ms1.ini`: X, down ×4 to Board, A, then A through the dialogue).
- **Open.** Occasional 20–30 ms frames (1% lows ~50).

**Earlier (2026-11-09, afternoon, milestone 103):**
- **Right-stick aim (user reports).**
  - No more drift: the virtual pad's gravity follows its own orientation.
  - Both axes inverted, 120 deg/s, curve 1.6.
  - Xbox buttons by position (`controls.ini label_abxy = 0`).
- Waiting on the user to confirm the feel. A 40-fps report came while my test runs were sharing the GPU; re-measure when nothing else runs.

**Earlier (2026-11-09, late morning, milestone 102):**
- **Beach-sky stripes fixed.** The ink-able ground layers use a 69-byte vertex stride (and an attribute at +65). Vulkan needs aligned attribute fetches, and the RX 5700 XT rounds unaligned ones down, which scattered those meshes' vertices into full-width slivers.
  - `[nvn] vertex_realign` (default on) repacks any vertex binding whose stride or offsets break alignment into an aligned layout. The new offsets go in the pipeline key; the per-draw copy is made on the CPU.
  - The wide beach view and the tutorial top-down view are clean. Fps unchanged or better (wide view 56–57).
- New diagnostics, off by default: `[nvn] vertex_copy_fs`, `ubo_copy_fs`. The trace prints each vertex binding (address, size, stride).

**Earlier (2026-11-09, morning, milestones 99–101):**
- **Handheld mode is the default** in `raiders-user.ini` and `raiders-play.ini` (`[am] operation_mode = handheld`).
  - Why: docked, the game renders 3072x1728, and its dynamic resolution spiralled into new images and 25–47 MB of uploads a frame, then out of memory at ~184 s. That was the "breaks right as gameplay starts".
  - Handheld runs 300 s with no out-of-memory.
- **Fixed:**
  - the `writeback_gpu_writer` crash (the writer was freed by `finish()`); it now handles reinterpretations only;
  - ink-splat feedback copies (`feedback_mask_aware`);
  - barrier pairs around every `UpdateUniformBuffer` (`write_batch`).
- **Measured (run bc11).** Gameplay runs at a locked 60. The wide beach dialogue view is at 52–58 fps (was 31–45).
- **Still open: beach-sky stripes** (milestone 100, with every ruled-out cause).
  - In the wide view, the ground layer `vs 44dba4ef/fs 55647ed7` (pre-pass) and `vs 810284f1/fs f0fe5663` (colour) mismatch row by row, although their math, constants, mesh and polygon offset are identical.
  - The top-down view is striped for ~30 s, then clean.
- **Test recipe:** `raiders-bc11.ini`: handheld, Return script, screenshots every 10 s from 130 s.

**Earlier (2026-11-09, early morning, milestones 96–98):**
- **Fixed:**
  - a video-memory leak (images made for finalized textures, ~430 MB/s);
  - ~100 `vkCreateImage` a frame (virtual-pool alias lookup);
  - feedback copies rebuilt every 1.2 s (now keyed by image);
  - an upload retried forever when one 89 MB staging buffer failed (now sliced);
  - gyro yaw direction;
  - right-stick-as-gyro option.
- **Measured.** The hideout dialogue runs at a **locked 60 fps** without the profiler (12 fps before the fixes). The profiler's `[prof] trace` itself causes 150 ms stalls every ~1.2 s, so measure stutters without it.
- **Still wrong:**
  - beach stripes: the ground draw `fs f0fe5663` samples float lookup tables that the game draws in one format and reads in another;
  - `writeback_gpu_writer` added, but it never fired yet;
  - a jump to 3,100 live images with 34 shadow maps at ~180 s in one run, then out of memory again.
- **Test recipe.** Start from the user's save (`save-user-backup-20261108`). Use the gpA-style short Return script to stop at the beach, the bc1-style 80-press script to talk through it. Configs: `work/raiders/raiders-bc*.ini`.

**Latest (2026-11-08, evening, milestone 95):**
- **Fixed:**
  - cutscene lighting (lighting probes are now cube-compatible);
  - aborts at 245 s (MapMemory from physical memory) and 351 s (nfp GetDeviceState).
- **Video memory:** out-of-memory now retries after releasing kept images. Images in virtual pools of at most 1 GB share one block per pool.
- **Measured:** a 600 s run had no crash and no failed allocation, with a 4.1 GB peak.
- **Next:**
  - check which pools hold the cutscene images (the peak did not drop);
  - make the scripted input finish character creation, so gameplay can be tested.

**Earlier (2026-11-08, later afternoon, milestone 94):**
- **Skin holes fixed.** The game renders BC4 character masks through an 8-byte colour view. The port now renders into a stand-in image and copies it into the blocks (`tex_bc_target_*`).
- **Character creation at a locked 60 fps** (1% low 54–56). Timestamp reports carried the replay's CPU time, so the game's dynamic resolution changed size every frame and every size meant new images. `[nvn] gpu_timestamp_scale = 0.4` (now in `raiders-user.ini` / `raiders-play.ini`) keeps it at 2560x1440, and the recycler then reuses 99.8% of images.
- **Cutscenes at a steady 30 fps.** The limiter now follows the game's present interval; before, cutscenes played too slow and then too fast.
- **Next:**
  - gameplay (GPU cost at 2560x1440 there, gyro);
  - real GPU timestamps instead of the scale;
  - button labels on the selected menu item.

**Earlier (2026-11-08, afternoon, milestone 93):**
- **Progress:** scripted runs reach character creation, at about 45 fps in the game's 60 fps mode, with no crash in 7 minutes.
- **Fixed:** the VMA image pool caused a device-out-of-memory crash after about 3 minutes, so it is now off by default.
- **Limits:**
  - character creation: render-thread replay (GPU 12.8 ms);
  - cutscenes: target 30 fps and reach about 23.
- **Next:**
  - skin blotches in character creation;
  - cheap per-frame render-target uploads;
  - a 1080p display mode;
  - gyro.
- **Settings note:** `controls.ini` overrides `[latency]` from the run ini.

**Earlier (2026-11-08, later):** Characters fixed and the GPU about a third faster (PORT_STATUS milestones 88–92).
- **Characters:** fixed by loading `LDC.64` words before writing registers.
- **Shaders:** fp64 rounding and denormal emulation are now opt-in (`NXRT_SHADER_EXACT_ROUNDING` / `NXRT_SHADER_EXACT_DENORMS`).
- **Render targets:** recycled (`[nvn] tex_recycle`) and allocated from VMA image pools (`[gfx] image_pool`).
- **Frame rate:** menus run at a locked 60 fps. Story cutscenes ask for 30 fps (present interval 2) and reach about 23. The limit is the render thread (texture loading about 11 ms, replay about 29 ms per frame).
- **Next:** cut the texture-loading cost on the render thread, then reach gameplay and measure it there.

**Earlier (2026-11-08):** Torn characters, probable root cause found but not fixed yet. It is a **shader translator bug**: the address of a bone matrix read is taken from the result of the previous constant-buffer load instead of from the bone index.
- Where: `work/raiders/dump-skin2/out/7d8062625f8b9bb1.spv` (GLSL text), line 284: `uint(floatBitsToInt(temp_86) + 44)`. It should be `temp_15 + 44`. Likely an `LDC` whose destination register equals its address register, possibly in `tools/shaders/ampere/frontend/AmpereEmit.cs`.
- Next: decode the SASS around it with `sassdecode --dump work/raiders/dump-skin2/code_7d8062625f8b9bb1.bin ctrl_...bin`, fix the emitter, then clear the cached `.spv` files (shader_cache/spirv) for the skinned shaders.
- Ruled out: palette contents (all 139 bones valid, decoded with the trace now split into 96-word lines) and stream-out.
- Also fixed: command-buffer `FenceSync` now gets its serial at submit time on the game thread (`api_device.cpp` `arm_list_fences`). Before, the game could see an old, already signalled serial and rewrite per-frame uniform rings early. This did not visibly change the characters.

**Update 2026-11-07 (Claude Code session):** The RomConfig crash is fixed. The game's NVN device setup required NVN2 API minor ≥ 39 and the runtime reported 30, so the device singleton at `0x7104e143c8` was never created (PORT_STATUS milestone 61). Startup now loads shaders, fonts, messages, UI, effects and sound data, then stops at an unimplemented `IHidServer` command 3150 (`svcBreak`, milestone 62). Still no frame presented, and nothing is playable yet.

**Later the same day:** 3150 is implemented (milestone 63). A recompiler bug that mistranslated pointer-authentication instructions in `sdk` is fixed (milestone 64; requires a module relift, which takes about 1 minute). Boot now reaches audio and stops at an unimplemented `IAudioDevice` command 20 (milestone 65).

**Latest (milestones 82–87, night of 2026-11-07/08):** **The 3D world renders**: the intro's scenes, ocean, clouds, lighting and effects. The fixes:
- buffers in virtual pools;
- NVN2 compute and graphics push constants (c[0] table offset, bank 0x18 shared by all stages);
- separate texture/sampler binds;
- a translator `BRA` predicate bug that caused GPU timeouts;
- the Switch 2 tensor-core upscaler replaced by a native linear upscale.

The SPIR-V cache was rebuilt with the fixed translator (old one: `shader_cache/spirv.bak-prebra-20261107`). Scripted runs get through the menus on their own (`[input] hid_keyboard` off for Raiders; menu sequence A, Right, A, then A).

Open:
- skinned characters are torn into spikes (milestone 87 lists what was ruled out);
- about 22–25 fps at 4K output (switch to 1080p next);
- gyro on the user's Switch Pro Controller 2 is detected and wired, but untested in gameplay;
- button labels on the selected menu item.

**Earlier (milestones 78–81):** The post-loading freeze is fixed (large compute pipelines build in the background). The tensor-core shaders are smaller, and after their first 257 s build they load from the pipeline cache instantly. Runs reached the story intro and character creation, with the 2D UI correct and the 3D view black. Command lists the runtime wrongly dropped are now kept; the effect on the 3D view is not verified yet.

**Current blocker:** since 17:28 the difficulty menu ignores A/B, while the D-pad works and hid shows A pressed. Many causes are ruled out (see milestone 81); check the runtime caches next.

**Earlier (milestones 73–77):** Most text renders. The game's font shaders needed fp16 texture results, and a set of shaders needed constant banks above c[17]. Raiders now has a frame hook (`entryhook 0x71031b8c60` in `recomp/games/sra/main.sites`), so keyboard, mouse and controller input reach the game. An Enter keypress advances "Select a difficulty" to "Start on this difficulty?". Menus are paced to the display (they ran 5–7× fast before). Still open:
- the highlighted/selected button labels are missing;
- a physical controller is untested;
- the `[test]` tick tooling freezes at about 13 s;
- gameplay is not reached yet.

**Earlier:** The game renders its own frames on the RX 5700 XT (milestone 72; screenshots in `work/raiders/shot_*.png`), but text is missing and the screen waits, likely for input. The shader translator is built (.NET 10 portable in `work/toolchain/dotnet`) and published beside `sra.exe`. Rebuild it with `work\toolchain\dotnet\dotnet.exe publish tools\shaders\harness\SpirvHarness.csproj -c Release -r win-x64 --self-contained true -o <build>\tools\spirv_harness -p:PatchPython=<work\toolchain\python313-embed\python.exe>`.

**Earlier (2026-11-07):** No crash in a 90 s boot. The game's frame loop runs (52K presents) and audio is live, but every draw is skipped because no shaders are translated yet (milestone 70). Next is building `tools/shaders` (.NET 10 + Ryubing sources) so the runtime can translate shaders on demand (milestone 71). The save-data size is an unverified placeholder (milestone 69). Quick loop: `tools\raiders\boot-headless.ps1 -Tag <name> [-Seconds 90]`.

Build note: `build-native.ps1` needs `vswhere.exe` on PATH. Prepend `C:\Program Files (x86)\Microsoft Visual Studio\Installer` in a fresh shell. Analysis scripts used for the xref scan are described in milestone 61.

The older notes below describe the state before that fix and are kept for history.

The native Windows x64 executable and Vulkan/NVN startup path build, and the complete base/update RomFS overlay is present. Startup still crashes before presenting a frame while parsing `NormalProduct.engine__system__RomConfig.bgyml`. The parser dereferences a zero-filled 40-byte registry at guest address `0x7104e143c8`. There is no playable gameplay yet, and neither 60 fps nor 30 fps has been measured.

See the latest entries in `PORT_STATUS.md` for prior probes and logs. The last clean baseline and previous scalar-store trace logs are in `work/raiders/`.

## Clean rebuilt baseline

The module libraries were rebuilt from restored sources with `build-modules.ps1 -Jobs 12`, and the Windows executable was relinked with `build-native.ps1 -Jobs 12`. A fresh bounded headless boot still crashes in `f_71031bb800` reading `0x20`; registers show `x9=0`, and no frame was presented. Logs: `work/raiders/headless-clean-rebuild-20261107.log` and `work/raiders/headless-clean-rebuild-stderr-20261107.log`. Historical diagnostic trace logs and their coverage are listed in milestones 53–56 of `PORT_STATUS.md`.

Static NSO analysis matched all 5,403 main `DT_INIT_ARRAY` entries to lifted bodies; none directly accesses the converter-global field at `main + 0x4b648`. A fresh dynamic-table parse found 95 SDK init-array entries and zero entries for `subsdk0` and `rtld`; the SDK array was already replayed after SDK setup without changing the registry. At the crash, the input YAML node is valid but the registry head resolves to a null list entry. The next investigation is the converter's registration call path, not another missing-module constructor table. Details are in milestone 59.

Temporary source instrumentation has been removed, `CMAKE_CXX_FLAGS` is restored, and the latest module libraries and `sra.exe` have been cleanly rebuilt. The baseline still fails before the first frame. Rebuild commands are:

```powershell
& '.\outputs\splatoon-raiders-native\tools\raiders\build-modules.ps1' -Jobs 12
```

Then build the native executable with:

```powershell
& '.\outputs\splatoon-raiders-native\tools\raiders\build-native.ps1' -Jobs 12
```

Then run a headless boot with the user's local files (adjust the log name to avoid overwriting evidence):

```powershell
& '.\work\raiders\build\native-x64-v2\sra.exe' `
  --game '.\work\raiders' `
  --exefs '.\work\raiders\exefs-1.1.0' `
  --ini '.\work\raiders\raiders-init.ini' `
  --log '.\work\raiders\headless-next.log' `
  --headless --max-seconds 30
```

Capture stderr separately. For the next step, identify non-libc host service/runtime writes that can affect main BSS, then trace the expected registration path through startup and constructor callbacks.

## Build configuration caution

`CMAKE_CXX_FLAGS` in `work/raiders/build/modules-x64/CMakeCache.txt` is `/DWIN32 /D_WINDOWS /EHsc`. Latest clean build completed successfully on 2026-11-07; the diagnostic probes are preserved only in source-control-external historical logs.
