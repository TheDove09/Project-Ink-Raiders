# Port status

## Goal

Build a functional native Windows port of *Splatoon Raiders*, with stable 60 fps as the target. The port is generated on this PC from the locally supplied NUP package. “Native” here means a host Windows executable produced by ahead-of-time code recompilation plus original implementations of the Switch services the game uses; it does not mean an official Windows build.

## Verified milestones

1. Located `F:\Switch 2 Roms\Splatoon Raiders + Update 1.1.0.nup` (about 12.94 GiB).
2. Validated its NUP v1 header and table-of-contents SHA-256.
3. Identified base package and update package 1.1.0, title ID `0400380020056000`.
4. Extracted the update's five ExeFS files to the workspace's `work/raiders/exefs-1.1.0/` and validated their per-file SHA-256 values.
5. Provisioned Visual Studio Build Tools with MSVC, LLVM/Clang, CMake, and Ninja; added an isolated Python toolchain and pinned recompiler dependencies under `work/toolchain/`.
6. Added the Raiders 1.1.0 module manifest and a minimal runtime game identity layer. The main NSO build ID is `663c2e087a083f05307f229dfc4042d5bcc906b6`; the manifest includes verified hashes for all five extracted files.
7. Copied the five updated ExeFS files into `work/raiders/game/dump/exefs/`, the input layout expected by the recompiler.
8. Configured the project for the host's Windows x86-64 target and began native module generation.
9. Lifted `rtld` (44 functions / 2,405 instructions), `subsdk0` (1,533 / 131,216), `sdk` (45,922 / 2,120,714), and `main` (212,223 / 16,200,974); each reports zero untranslated instructions.
10. Fixed FP16 vector unary lowering and zero-length function handling discovered while translating Raiders' SDK and main modules.
11. Compiled and linked all four generated Windows x64 module libraries (`sra_rtld.lib`, `sra_subsdk0.lib`, `sra_sdk.lib`, `sra_main.lib`).
12. Built a Windows x64 `sra.exe` against the runtime and Radeon RX 5700 XT Vulkan backend; built the required ASTC-to-BC7 texture library and decoder DLLs.
13. Extracted the update RomFS: 46,354 files, 104 directories, 13.69 GB of file data, with each NUP extent decoded and SHA-256 verified.
14. Compared the base and update RomFS indexes in the NUP. The update has 86 paths found only in the base, 101 update-only paths, and 104 same-path hash changes; mounting update alone is not a complete overlay.
15. Confirmed the guest `rtld` calls `main`'s 5,403-entry `.init_array` before the RomConfig crash, ruling out an omitted main constructor pass as the current cause.
16. Set the guest alias region to 16 GiB, matching the Raiders profile used by NxNative; this did not change the early crash.
17. Added a two-root RomFS overlay and direct filesystem fallback so update files override the base while base-only files remain visible.
18. Built the Windows executable and booted it with the partial base extraction. The merged image contained 46,359 files, but startup still hit the same RomConfig null dereference before the first frame; the partial base is insufficient to draw a conclusion.
19. Inspected the Windows Switch 2 Ocarina recompilation as a reference. It uses the same broad ARM64-to-C and NVN-to-Vulkan approach, and documents an NVN2 mesh draw implementation; this is useful for runtime behavior, but its game modules and title-specific patches cannot be reused as Raiders code.
20. The Raiders boot log shows the guest requesting NVN2 mesh-task draw functions that this runtime currently exposes as stubs. `vulkaninfo` on this PC's AMD Radeon RX 5700 XT does not list `VK_EXT_mesh_shader`, which the Ocarina reference needs for its native mesh path. A Vulkan mesh-shader fallback or a different GPU path will be needed for mesh-rendered Raiders content on this GPU.
21. Hardened NUP reads to use bounded chunks and report the failed NUP offset and path. Added resume progress logging; this updated extractor has not yet been retried against the USB disk after its timeouts.
22. Confirmed the updated extractor parses, refreshed the README's toolchain status, and removed the failed local-copy log and superseded boot-configuration files. User game data and the verified partial base tree were kept.
23. Added an installed-build default that enables `base-romfs` when that folder exists, without making it mandatory for development runs. Incrementally rebuilt and linked `sra.exe` successfully.
24. Rebooted the rebuilt executable with the partial overlay; it again crashed while reading the product RomConfig at `0x71031bb800`, before presenting a frame. This confirms the packaging change builds, but the incomplete base tree does not resolve startup.
25. The user placed the NUP on the internal Downloads volume. Resumed base RomFS extraction completed successfully with SHA-256 verification for all 46,339 base files. Booted again with the complete base plus update overlay; the crash is unchanged at `0x71031bb800` while processing `NormalProduct.engine__system__RomConfig.bgyml`, before any frame. This rules out missing base-only RomFS files as the cause of this specific null dereference.
26. Added temporary logging around the crash and rebuilt the main module. At the fault, `x22` points to `0x7104e143c8`, whose first five 64-bit words are all zero; the nearby object at `x23=0x7104e14378` begins with a valid guest heap pointer (`0x7802000240`). Restored the generated source, rebuilt the normal main library, relinked `sra.exe`, and confirmed the rebuilt executable still fails at the same guest instruction with no debug marker in its output.

## Current state

The native executable initializes the Windows runtime, Vulkan device, NVN backend, loader, services, and complete base/update RomFS overlay. A TLS layout fix populates the Raiders SDK's `TPIDRRO + 0x1F8` slot alongside the existing `+0x1F0` slot. With the original generated RomConfig code restored, startup still crashes in `f_71031bb800` while reading `0x20` from the empty registry object at `0x7104e143c8`. A new per-constructor snapshot confirms none of the 5,403 main init-array calls changes the registry's 40 bytes by the time that call returns. The deeper `f_71034dd1d0` crash occurred only after temporary null guards skipped this registry failure, so it is not the current baseline. No frame has been presented, so the port is not playable and 60 fps is not measured. The full base extraction is complete (46,339 files, every output SHA-256 checked) from the local Downloads copy. A separate address-space probe found some guest `MapSharedMemory` requests collide with Windows-reserved ranges (Win32 error 487); narrowing the advertised ASLR range caused an earlier SDK assertion and was reverted.

NxNative's current Raiders notes report reaching character creation and the first level on Apple Silicon. Its last session averaged 16.6–16.9 ms/frame, with heavier frames up to 22 ms, so 60 fps was not held everywhere. Its game profile specifies a 16 GiB alias region and 32 MiB system resource size. This PC requires a Windows x86-64 host. [Raiders notes](https://github.com/seedds/nxnative/blob/main/dm/games/0400380020056000/README.md) · [game profile](https://github.com/seedds/nxnative/blob/main/dm/games/0400380020056000/game.tsv).

The Windows [Ocarina Recomp reference](https://github.com/sostenesfreitas/ocarina-recomp) confirms the same broad recompilation/runtime approach for a Switch 2 game. Its README reports reaching the Deku Tree at about 59 fps standing and 51–55 fps walking on one RTX 5090; AMD/Intel GPUs are untested there. Its renderer has NVN2 mesh draw code, but gates it on `VK_EXT_mesh_shader`. This PC's RX 5700 XT does not advertise that Vulkan extension, so code from that renderer is a behavioral reference rather than a drop-in solution for this target GPU. The checkout is kept outside the port repository at `work/raiders/reference-ocarina/` for source comparison.

## Next work

1. ~~Empty RomConfig registry~~: fixed in milestone 61 (NVN2 API minor version).
2. ~~`IHidServer` 3150, `IAudioDevice` 20, `time:u` 200, save data, texel upload stall~~: done (milestones 63–69). The frame loop runs (70). Shaders translate on demand and the first frames render (72). Next: missing text (shared font / text path), input to get past the current screen, `spirv-val` validation, the 2 failed shader blocks, `GPU-side WaitEvent`, and the mesh-shader path for the RX 5700 XT.
3. Implement and validate Raiders' NVN2 mesh-task draws, including a path usable without `VK_EXT_mesh_shader` on this RX 5700 XT.
4. Continue booting through character creation and the first playable level, resolving service and graphics gaps as they appear.
5. Verify a visible Windows window, input, and audio in gameplay; profile real gameplay and iterate toward a measured stable 60 fps.

Game files stay in the local workspace's `work/` directory and are not committed or packaged.

28. Probed startup initialization with temporary logging, then removed the probe and rebuilt the normal executable. In host-link mode, the runtime reads 5,403 distinct main-module init-array addresses; the observed call to 0x71031b80b0 is not one of those entries and does not populate the zeroed object at 0x7104e143c8. The RomConfig crash remains, so the next trace must follow the actual guest startup callback / init-array execution and writes to this object.

29. A temporary dispatch-boundary probe counted more than 5,000 calls into the main guest module before the RomConfig fault. Across those calls, the 40-byte static object at 0x7104e143c8 never changed from zero. Removed the probe and rebuilt the normal executable. This shifts the investigation toward whether the RomConfig parser is supposed to populate this object and whether its input/lookup path succeeds.

30. Dumped the converter arguments immediately before the fault, then restored the generated source, removed the temporary backup, and rebuilt the clean main library and executable. The source object is valid and populated (vtable 0x7104b5eda8 plus pointers into loaded guest data), while the separate 40-byte object at 0x7104e143c8 is entirely zero. This points away from a missing/empty RomConfig file and toward an unpopulated registry/container used by the converter. Diagnostic stderr is saved at work/raiders/romcfg-dump-stderr.log.

31. Matched guest dispatch targets against the host-parsed main init array during a host-link boot. All 5,403 distinct main init-array entries were dispatched before the RomConfig fault. A cross-module dispatch-boundary trace observed more than 35,000 guest function calls and no changes to the static object at 0x7104e143c8. Removed the diagnostic code and rebuilt the normal executable. This rules out a skipped main constructor pass; the empty container is likely left unpopulated by a later startup or data-registration path.

32. Inserted temporary null checks in the RomConfig converter for its zero-filled registry head and cleanup path, rebuilt, and booted with the complete base/update overlay. Startup advanced past RomConfig and opened the RSDB tables plus the AGL/GSYS resource archives, then failed in `f_71034dd1d0` with a host access violation reading `0x170` through a null vtable. Crash registers and static image bytes confirm the pointer slot at `0x7104c4b648` points to BSS object `0x7104e143c8`, whose first qword is still zero. The new diagnostic log is `work/raiders/headless-empty-guard2.log`; the clean source backup for the temporary guards is `work/raiders/work/lift/main/c_156.cpp.empty-guard-backup`. No frame was presented. The guards remain diagnostic only pending validation of the global object's expected initialization and should not be treated as a port fix.
33. Compared the generated Raiders SDK code with thread setup: constructor `f_7500171ef0` loads the static TLS pointer from `TPIDRRO + 0x1F8`, while the runtime only populated `+0x1F0`. Populated both adjacent slots and rebuilt the Windows executable. The controlled SDK-constructor experiment passed the earlier null-pointer fault; normal RTLD startup then loaded all RSDB tables and AGL/GSYS archives before reaching `f_71034dd1d0+0x31c`. The generated code loads a null pointer from `0x7104beb648`, then tries to read its field at `+0x170`, which matches the crash address. Log: `work/raiders/headless-sdk-tls-normal.log`. The TLS change is retained; the RomConfig guards remain diagnostic, and no frame has been presented.
34. Restored the original generated `f_71031bb800` source from the clean pre-guard backup, rebuilt `sra_main.lib` and `sra.exe`, and booted with the TLS fix retained. The baseline still faults at `f_71031bb800+0x1c2`, reading `0x20` from the zero-filled registry at `0x7104e143c8`; log: `work/raiders/headless-sdk-tls-clean-baseline.log`. This confirms the later `f_71034dd1d0` null-vtable crash was downstream of the temporary bypass. The generated-source guards are gone.
35. Tested SDK initialization at the `nn::init::Start` import boundary with a temporary opt-in runtime probe, then removed the probe and rebuilt the normal executable. Under the guest-RTLD boot path, the host-parsed SDK init array is empty at that point because host linking is skipped. Under host-link mode it contains 95 entries, but dispatching those constructors before `Start` reaches entry 1 and immediately triggers `svcBreak` in an SDK failure path. The init array therefore cannot be run wholesale at the import boundary; initialization must follow the callback ordering expected by `Start`. Logs: `work/raiders/headless-sdk-start-probe.log`, `work/raiders/headless-sdk-start-probe-host2.log`.
36. Traced the `nn::init::Start` arguments and confirmed RTLD passes its three callbacks at `0x7700001c64`, `0x7700001460`, and `0x77000014e0`. The startup path calls `__nnmusl_init_dso` for `subsdk0` and `main`, and 274 observed `__cxa_guard_acquire` calls all returned 1 before the RomConfig fault. This rules out a missing DSO-init import and a guard-acquire call that reports an already-initialized singleton for the observed constructors; the zeroed RomConfig converter registry remains unexplained. Diagnostic logging was removed and the normal executable rebuilt. Logs: `work/raiders/headless-start-args.log`, `work/raiders/headless-dso-trace.log`, `work/raiders/headless-guard-trace.log`.
37. Tested the 95-entry SDK init array immediately after the lifted `nninitInitializeSdkModule` call (the earlier probe ran it too early, before SDK initialization). All entries completed without an SDK break, but startup still faulted at the same empty RomConfig registry in `f_71031bb800+0x1c2`; log: `work/raiders/headless-sdk-init-after-setup.log`. Also reordered the manifest's non-entry modules to `main, subsdk0, sdk`, matching the profile for this exact main build ID (`663c2e087a083f05307f229dfc4042d5bcc906b6`). The generated runtime load order is now `rtld, main, subsdk0, sdk`; a clean RTLD boot reached the same crash, so order alone is not the cause. The temporary runtime and generated SDK probes were removed and the executable rebuilt. Log: `work/raiders/headless-module-order-fix.log`.

38. Installed a temporary Windows page write watch on the 40-byte RomConfig registry object at 0x7104e143c8. It reported only the initial RTLD BSS zeroing event before the unchanged access violation. This observation alone did not prove that no later writes occurred; a later constructor probe found the watch failed to report subsequent writes elsewhere on the same BSS page. Treat this watch as inconclusive for the object's write history. Log: work/raiders/headless-registry-write-watch.log.

39. Repeated the write watch across the whole main BSS page 0x7104e14000. It caught only the initial zeroing write at the page base from RTLD's f_77000021e0. Its claim that no later guest function wrote the page was disproved by milestone 44, so this PAGE_GUARD-based trace is not valid evidence about subsequent writes. A clean uninstrumented headless boot still fails at the same RomConfig read, with no frame. Logs: work/raiders/headless-main-bss-page-watch.log and work/raiders/headless-main-bss-page-clean.log.

40. Traced calls to 0x71031b80b0 with register arguments and before/after BSS snapshots. The target is an allocation thunk called from SDK startup (sizes include 0x408, 0x428, 0x148, and 0x498, alignment 0x10), returning heap addresses; it is not the RomConfig registry constructor. Removed the temporary dispatch logging and rebuilt the normal executable. Log: work/raiders/headless-global-init-args.log. The registry remains zero; no frame is presented.

41. Counted guest dispatches through the main init trampoline 0x71000001b0 during RTLD startup. All 5,403 main init-array targets were called before f_71031bb800 processed RomConfig; the first three targets were 0x7100003690, 0x7100003770, and 0x7100003870. The parser still faults at the same zero object, so a skipped main init-array pass is not the cause. Removed the dispatch counter and rebuilt the normal executable. Log: work/raiders/headless-main-init-trampoline-final-count.log.
42. Independently disassembled the original main NSO text with LLVM and compared the native ARM64 instructions at `f_71031bb800` against the lifted C. The instructions agree: load the registry pointer from the global at `0x7104c4b648`, then read its callback at offset `0x20`. The target `0x7104e143c8` falls within the main NSO's declared BSS range (`0x7104d27b00`–`0x7104e2b488`). This confirms the observed null is in guest state, not a bad fault address introduced by lifting. Temporary raw/ELF disassembly files were removed after inspection.
43. Rebooted the current clean executable with the complete base/update overlay and confirmed the same RomConfig crash (`f_71031bb800+0x1c2`, read at `0x20`, no frame). A one-run debug log showed the only live guest worker at the fault was SDK thread-manager worker 81 (`sdk+0x170b20`); its saved LR is inside the SDK's internal synchronization helper. It is not a registry initializer. Log: `work/raiders/headless-resume-20261107.log`; detailed thread log: `work/raiders/headless-thread-debug-20261107.log`.
44. Instrumented main init-array function `f_71031cb070`, verified in the raw `.init_array` list, and observed its entry execute. It changed other fields on BSS page `0x7104e14000` (offset `+0xe0` became `0x7104bd9548`), but did not initialize the registry. This demonstrates why the earlier page watch's single reported write cannot support its no-later-writes conclusion. The temporary instrumentation was removed and `sra_main.lib`/`sra.exe` rebuilt.
45. Temporarily instrumented the main init trampoline to snapshot all 40 bytes at `0x7104e143c8` immediately before and after each init-array function. None of the 5,403 calls changed those bytes at return; startup still failed at the same RomConfig read. Restored the source and rebuilt the clean main library and executable. Diagnostic output: `work/raiders/headless-registryinit-trace-stderr-20261107.log`; boot log: `work/raiders/headless-registryinit-trace-20261107.log`.
46. Rebooted the clean post-probe `sra.exe` with the complete base/update overlay. The expected baseline remains: access violation reading `0x20` in `f_71031bb800+0x1c2`, no frame presented. Confirmed neither generated-source probe string nor its backup remains. Log: `work/raiders/headless-clean-after-probes-20261107.log`.

47. Followed the clean crash through the lifted `nn::init::Start` function `f_75001374a0`: the RomConfig parser is entered from its last callback (`x19`, call site `0x75001374fc`) after `nninitInitializeSdkModule` has returned. The active guest frame chain passes through the main startup routine `f_7100fd8be0`; the registry is still empty at the parser. This narrows the next trace to callback-driven registration during main startup, rather than replaying the already-executed main init array. Source: `work/raiders/work/lift/sdk/c_006.cpp`, `work/raiders/work/lift/main/c_049.cpp`, `work/raiders/headless-sdk-init-after-setup.log`.

48. Re-ran the SDK's exact 95-entry `DT_INIT_ARRAY` from its relocated table immediately after `nninitInitializeSdkModule`, snapshotting the registry's five qwords around each constructor. No entry changed any of the 40 bytes; the clean crash remains `f_71031bb800+0x1c2`. The first attempt read zero entries from the runtime `Module` object in RTLD-link mode, so that result was discarded; the corrected probe used SDK `DT_INIT_ARRAY` at `sdk+0xe3b000` (the first relocated entry was `0x75000b5290`). Probe removed and clean `sra_sdk`/`sra.exe` rebuilt. Log: `work/raiders/headless-sdk-registry-snapshot2-20261107.log`.

49. Parsed the current main NSO dynamic table: it has no `DT_PREINIT_ARRAY`, has `DT_INIT=0x40`, and has the same `0xa8d8`-byte (`5,403`-entry) `DT_INIT_ARRAY` previously traced. The existing DSO trace shows `__nnmusl_init_dso` called for main, and the init trace records target `0x7100000040` as main dispatch #6, before the array entries. Thus the main `DT_INIT` path also runs; the empty registry is not explained by a skipped preinit/DT_INIT/DT_INIT_ARRAY pass. Evidence: NSO dynamic tags from the local `main` image; `work/raiders/headless-dso-trace.log`; `work/raiders/headless-dispatch-init-trace.log`.

50. Added a temporary before/after snapshot to the main `DT_INIT` function `f_7100000040`. The 40-byte registry is all zero on entry and remains all zero on return. Removed the probe and rebuilt clean `sra_main.lib` and `sra.exe`. Log: `work/raiders/headless-main-dtinit-registry-stderr-20261107.log`; boot log: `work/raiders/headless-main-dtinit-registry-20261107.log`.

51. Instrumented SDK `DT_INIT` (`f_7500000060`) on a normal RTLD boot. It ran twice; the 40-byte registry was all zero on both entry and return. This closes the gap left by checking only the SDK's 95-entry init array. Removed the marker and rebuilt clean `sra_sdk.lib` and `sra.exe`. Log: `work/raiders/headless-sdk-dtinit-entrytrace-stderr-20261107.log`; boot log: `work/raiders/headless-sdk-dtinit-entrytrace-20261107.log`.

52. Temporarily instrumented the scalar guest-memory helpers `wr8/wr16/wr32/wr64` to log any store overlapping the 40-byte registry at `0x7104e143c8`. The only observed stores were the loader's five initial 64-bit zeroing writes; no later scalar helper store hit the registry before the unchanged RomConfig crash. This probe does not cover `nx::st<T>`, release/atomic stores, vector stores, or host HLE memory routines. Removed the instrumentation from `ctx.h`; the currently linked diagnostic libraries/executable still contain the probe and need a rebuild after the next source change. Log: `work/raiders/headless-registry-direct-write-stderr-20261107.log`; boot log: `work/raiders/headless-registry-direct-write-20261107.log`.

53. Extended the temporary write trace to scalar stores, C++ `nx::st<T>`/release/exclusive stores, vector stores, and 128-bit exclusives; rebuilt all game modules and the Windows executable, then repeated a clean bounded boot with the complete overlay. The trace again reported only the five initial 64-bit loader zero writes to `[0x7104e143c8, 0x7104e143f0)` and no subsequent writes through the instrumented guest helpers before the unchanged access violation in `f_71031bb800+0x39c` reading `0x20`. No frame was presented. Logs: `work/raiders/headless-registry-fulltrace-stderr-20261107.log` and `work/raiders/headless-registry-fulltrace-20261107.log`. Removed all temporary helper instrumentation from headers and restored the module build flags in CMakeCache; the linked module libraries and `sra.exe` remain diagnostic builds until rebuilt from restored sources. This trace does not cover every direct inline/generated `memcpy`/`memset` or host runtime memory service.

54. Added a separate temporary trace to the host libc HLE's `memcpy`, `memmove`, `memset`, and `strcpy` handlers, rebuilt the native executable, and repeated the bounded boot. No host HLE memory operation wrote to the registry range; stderr contains no `REGMEM` record. The same RomConfig access violation occurs before any frame. Removed the runtime trace source edits. Logs: `work/raiders/headless-registry-memtrace-stderr-20261107.log` and `work/raiders/headless-registry-memtrace-20261107.log`. Remaining unobserved paths include direct inline/generated memory intrinsics and non-libc host service writes.

55. Scanned every generated main-module `.cpp` file for direct `std::memcpy`, `std::memmove`, or `std::memset` operations; no such inline/generated memory intrinsic is present in the lifted main sources. Its guest stores use the traced scalar, vector, atomic, or C++ helpers, while libc HLE memory writes are covered by milestone 54. This narrows the remaining write-path gap to non-libc host services/runtime code and clarifies that registration itself still has not occurred by the RomConfig callback. No source instrumentation remains. Current runtime baseline evidence remains the same crash with no frame.

56. Wrapped the generic `host_import` and SVC dispatchers with a temporary 40-byte before/after snapshot whenever `main` was registered. A one-time marker confirms the watcher was armed with `main` mapped at `0x7100000000`; no import or SVC changed the registry before the RomConfig fault. The same access violation occurs, no frame is presented. Logs: `work/raiders/headless-registry-importtrace2-stderr-20261107.log` and `work/raiders/headless-registry-importtrace2-20261107.log`. Removed the dispatcher instrumentation from source. This rules out stable writes made inside individual import/SVC calls, but not a transient write-and-restore within one call or a write from an unrelated host thread.

57. Decompressed the current main NSO and matched every entry in its 5,403-element `DT_INIT_ARRAY` to lifted functions. All 5,403 targets have generated source; none of those init-array function bodies references the converter-global field at `main + 0x4b648` (lifted as base `0x7104c4b000` plus `1608`). Candidate converter/registry functions therefore are not directly initialized by this array. This narrows the missing path to explicit startup/module registration or a different indirect global alias; it does not establish that every unrelated global constructor is correct. The registry remains zero at the startup callback and the crash is unchanged.

58. Rechecked the update ExeFS against the game manifest to rule out an omitted fifth guest module: it contains four NSOs (`main`, `sdk`, `subsdk0`, `rtld`) plus `main.npdm`, and all four NSOs are listed in the manifest and confirmed registered in the boot log. There is no hidden `subsdk1`/fifth NSO whose constructor table was omitted. This rules out a missing-module explanation for the zero registry.

59. Re-examined the crash registers and parsed `DT_INIT`, `DT_INIT_ARRAY`, and array sizes from all four current NSOs. At the fault, the RomConfig YAML node in `x21` is a valid heap address, while the registry head at `0x7104e143c8` yields a null list entry (`x9=0`); the faulting read at `0x20` is a dereference of that missing registry node. Main has 5,403 init-array entries and SDK has 95; `subsdk0` and `rtld` each have zero. Main and SDK init paths have already been exercised (including a manual SDK-array replay after SDK setup), so an omitted `subsdk0`/RTLD constructor array does not explain this empty registry. The next investigation is to locate the converter registration path and establish why it is not called before RomConfig parsing. Evidence: `work/raiders/headless-registry-importtrace2-stderr-20261107.log`, current NSO dynamic tables, prior milestones 45–57.

60. Rebuilt all four generated module libraries from the restored source tree (`263/263` Ninja steps succeeded; all regenerated modules reported zero untranslated instructions), then rebuilt and relinked `sra.exe` successfully. A fresh bounded headless boot with the complete base/update overlay exited with code 4 at the same pre-frame access violation in `f_71031bb800`, reading `0x20`, with `x9=0`, `x21=0x780500f028`, and `x22=0x7104e143c8`. No temporary registry instrumentation marker was present in the new logs. The clean executable therefore confirms the baseline issue independently of the earlier diagnostic binaries; the game is still not playable. Logs: `work/raiders/headless-clean-rebuild-20261107.log` and `work/raiders/headless-clean-rebuild-stderr-20261107.log`.

61. **Root cause of the RomConfig crash found and fixed.** The 40-byte "registry" at `0x7104e143c8` is not a registry. Its first qword is a pointer to a singleton. A static ARM64 xref scan of the whole main text (adrp/add/ldr/str/stp tracking, including loads through the RELR slot `0x7104c4b648`) found exactly one store to it: `0x71031b87a4` in `f_71031b83f0`, `sead::GameFrameworkOunce`'s NVN device setup (it resolves `nvnDeviceInitialize`/`nvnDeviceGetProcAddress`). Before that store, the function calls `nvnDeviceGetInteger(API_MAJOR_VERSION)` and `(API_MINOR_VERSION)` and returns early unless major == 231 (`0x71031b86a4`) and minor >= 39 (`0x71031b86b0`, `b.lt`). The runtime's NVN2 path reported minor 30 (SDK 20.5), so the singleton was never created and the RomConfig parser read through null. This also explains why no constructor, import, SVC or memory-helper trace ever saw a write (milestones 28–60). Fix: `game::Info::nvn_api_minor` (0 keeps 30 for other titles); Raiders sets 39 in `runtime/games/sra/sra_core.cpp`. Rebuilt `sra.exe`. The headless boot now passes RomConfig and loads RSDB, AGL/GSYS, shader archives (`Hoian_UBER` bfsha), EventFlow, fonts, messages (USen), UI layout, effects, sound group/bus/BGM and SLink2 data. Logs: `work/raiders/headless-nvnminor39-20261107.log` and `-stderr-`.
62. **New first failure:** `nn::hid::IHidServer` command 3150 is not implemented. The runtime returns `0xbcbd5` (module 469, description 1509), and the SDK calls `svcBreak` from `f_75001a4540`. The call chain is game startup (`f_71031bb800` → `f_71031bf160` → `f_7103e033e0` → `f_7103da2f80`) into the SDK hid client (`sdk+0x6d65f0`). Neither this runtime nor the Ocarina reference handles 3150; it looks like a Switch 2 (SDK 21) hid command. Its request/response layout must be determined from the SDK client code before implementing it. Still no frame presented. Also visible now: many NVN2 programs log "no translated shader for block …", which means Raiders' shaders are not yet in the translated shader cache, and the NVN shell reports 542 of 544 entry points as logging stubs before device bind.
63. **Implemented `IHidServer` 3150.** It is vtable slot `0x5F8` (proxy thunk `sdk 0x75006fbf70` → CMIF writer `0x75001a2be0`). Its only SDK caller is `nn::hid::InitializeMouse` (`sdk 0x75006d65f0` → `0x75006c8600`, run once), which replaces Switch 1's `ActivateMouse` (21). Request: u32 at 0 (always 1 from InitializeMouse; meaning not established), aruid at 8, PID attached. Reply: result only. The runtime now does what 21 does (classic mouse active) and logs the u32 (`runtime/src/services/hid.cpp`). Confirmed at runtime: `hid: cmd 3150 (InitializeMouse activation, u32 1)`. Log: `work/raiders/headless-hid3150-20261107.log`.
64. **Fixed a recompiler bug: pointer-authentication instructions were mistranslated.** The next stop was a `GUEST TRAP: undefined instruction` in the SDK's `setjmp` (`0x75000aeea0`), reached from the SDK's TrueType font engine (`FSS_get_glyphmap` → `fnt_Execute`). The tier-1 decoder (`recomp/nxrecomp/lift/a64.py`, data-processing 1-source) ignored the `opcode2` field. As a result, `PACIB x30, sp` was lifted as `x30 = rev16(0)` and `AUTIB` as `cls(0)`, which zeroed the return address and failed the `setjmp` LR check. Tier 1 now raises `Unimpl` when opcode2 or S is nonzero, so tier 2's existing PAC handlers (identity operations) take over. Raw scan: 7 affected instructions, all in `sdk` (2 PACIA, 2 PACIB, 3 AUTIB); main, subsdk0 and rtld have none. Relifted all modules (main regenerated byte-identical; one sdk file changed), rebuilt, and booted. Font rendering now passes and the audio renderer (`audren:u`) is created. Note that the "not translated 0" stats count only untranslated instructions, not mistranslated ones.
65. **New first failure:** `nn::audio::detail::IAudioDevice` command 20 is not implemented (8 bytes in) and returns `0xbcbd5`; the SDK then calls `svcBreak` (call chain main `0x7103556ef8` → `sdk 0x75001f0ed0`). Still no frame presented. Log: `work/raiders/headless-pacfix-20261107.log`. Separately, `pl:u` shared fonts are served from a DejaVuSans fallback and three `MapSharedMemory` attempts return `0xD401` just before; both need follow-up.
66. **`IAudioDevice` 19/20 (output-volume auto-tune).** The SDK embeds the server-side names (`AudioDeviceImpl::{Is,Set}AudioDeviceOutputVolumeAutoTuneEnabled`). Cmd 20 is the getter: aruid in, bool out; its client `nn::audio::IsDeviceOutputVolumeAutoTuneModeEnabled` (`sdk 0x75001f0d80`, vtable +184) aborts on error. Cmd 19 is the setter (vtable +176; the ID was inferred from the adjacent slot and then confirmed at runtime: the game calls it with `1`, 12 bytes in). The state is stored, defaults to off, and has no audible effect on PC (`runtime/src/services/audio.cpp`).
67. **Texel uploads no longer re-tile whole mip levels.** After 66 the boot did not crash but stalled: no frame within 60 s, with the main thread at 100% CPU inside `nvnTextureWriteTexels` → `tegra::tile_level` while building the startup glyph cache. Each write untiled the whole level, copied the region in, and re-tiled the whole level. Added `tegra::tile_region`/`untile_region` (`tools/tiling`), which address only the region's bytes using the same GOB piece mapping (both Switch 1 and Switch 2 orders), and switched `api_texture.cpp` read/write to them. New test `tiling_test region`: 4,000 random cases (all bytes-per-block, 3D, mips, both GOB orders, odd strides) are byte-identical to the old path. The existing `roundtrip` suite (5,944 textures) still passes. The stall disappeared (the upload phase now takes under a second).
68. **`time:u` `IStaticService` 200 (IsStandardNetworkSystemClockAccuracySufficient).** A documented Switch 1 command; the client is `nn::time::StandardNetworkSystemClock::IsAccuracySufficient`, which aborts on error. It returns true, consistent with the runtime's network clock (host UTC, synchronized by Windows).
69. **Account save data.** The game calls `nn::fs::MountSaveData(name, uid)` with no prior `EnsureSaveData` and aborts on "path not found" (`0x7D402`). The runtime already creates the save on first open, but only when the title profile has an account save size, and Raiders had none. The NUP was checked directly: its two packages contain only ExeFS and RomFS (no control/NACP section), and NxNative's public profile lists no save sizes either. Set 64 MiB save / 64 MiB journal (the framework's Pokopia values) as a **documented, unverified placeholder** that only caps the save volume's quota (`runtime/games/sra/sra_core.cpp`; ini `[title]` overrides).
70. **The game's frame loop runs.** With 66–69, a 90 s headless boot ran to the deadline with no crash: 260,150 submits, 52,029 presents, steady frame timing, and the game's audio thread live through XAudio2. **The frames are empty, though: 0 draws, 2,887,952 skipped, 0 pipelines**, because 2,540 shader programs have "no translated shader". The ~615 fps headless figure is an empty loop without vsync, not a performance number. Also seen: `GPU-side WaitEvent` unimplemented (156K skips, non-fatal) and `prepo` `IPrepoService` 10107 unimplemented (play reports, non-fatal). Log: `work/raiders/headless-savedata-20261107.log`.
71. **Next: shader translation.** The framework already includes an Ampere SASS decoder plus a patched Ryujinx shader translator (`tools/shaders`, C#), and the runtime starts it on demand when `tools/spirv_harness/spirv_harness.exe` sits beside `sra.exe`. None of it is built on this PC. It needs the .NET 10 SDK and the Ryubing source checkout (`refs/ryubing`, Codeberg archive `a82350bb77`, MIT); for validation it also uses the Vulkan SDK (`spirv-val`). New helper `tools/raiders/boot-headless.ps1` rebuilds, boots and summarises the first failure.
72. **First rendered frames.** Built the shader translator and turned on on-demand translation:
    - Installed the .NET 10 SDK (10.0.403) portably in `work/toolchain/dotnet` (Microsoft-signed `dotnet-install.ps1`, no PATH or system change).
    - The pinned Ryubing commit could not be fetched: Codeberg returned 404, `git.ryujinx.app` served a bot-check page (not bypassed), and the GitHub mirror returned 404. Instead the sources were staged from the **Ryujinx 1.3.2** subset already vendored, unmodified and MIT-licensed, in `work/raiders/reference-dkb/tools/vendor/ryujinx` into `tools/shaders/third_party/ryubing` (gitignored). All 10 `ShaderPatch` files applied exactly, and the build had 0 errors.
    - Published `spirv_harness` self-contained to `<build>/tools/spirv_harness/`, where the runtime starts it automatically. `spirv-val` (Vulkan SDK) is not installed, so translated modules are not validated yet.
    - Headless 120 s: 1,521,452 draws executed (11,395 skipped), 344 pipelines, 2 blocks failed translation (`7f55d1b5389e14d6`, `4fbad69e51fa58c0`).
    - Windowed 90 s on the RX 5700 XT: 291,778 draws, 0 skipped, 2,914 pipelines, 8,883 presents.
    - Screenshots at 40/60/80 s (`work/raiders/shot_0..2.png`, 1280x720) show the game's own rendered screen: three ink-textured panels on a dark patterned background and an animated squid icon. The screen is unchanged between 40 s and 80 s (likely waiting for input). **Text is missing**: the panels and the band below them render without visible text. Suspects are the `pl:u` DejaVuSans fallback for the system shared font, or a text shader or texture-format gap; not yet investigated.
    - Screenshot runs need a BOM-free ini (PowerShell 5.1's `Set-Content -Encoding utf8` writes a BOM, which hides the first section). PNGs land in the `--game` folder.
73. **Text renders: the two failed blocks were the distance-field font shaders.** Both are fragment shaders that use `TEX.SCR.F16.RN` (a texture sample whose results are fp16, packed two per register). The decoder already read the modifier, but the emitter rejected it. Disassembly (`tools/shaders/ampere/decoder/tool`, `sassdecode --dump`) fixed the layout: the k-th enabled component goes to `k < 2 ? Rd : Rd2`, half `k & 1`. That is the same rule as Ryujinx's Maxwell `TEXS.F16`. Evidence: in `7f55d1b5` the results are read from `R4.H0`/`R4.H1`/`R6.H0`, and the alpha test uses `R6.H1`. In `4fbad69e`, with mask 0x8 and Rd2 = RZ, the single alpha component is read from `R4.H0` and thresholded at 0.54 after `2a-1`, which is a signed-distance-field font test. `AmpereEmit.EmitTexAmpere` now samples into locals and packs them with `PackHalf2x16`. `.RZ` is packed with round-to-nearest, at most 1 fp16 ulp off. The runtime keeps each block's failure in `shader_cache/spirv/results.tsv` and never retries it, so the two stale rows were removed (backup `results.tsv.bak-20261107`). A windowed run then shows the game's **"Select a difficulty"** screen with all text: title, Tourist/Survivalist labels, description, and the orange notes. The highlighted middle panel still has no label (9 draws skipped; to investigate). Shot: `work/raiders/shots2-20261107/font_shot_1.png`.
74. **Input reaches the game, but a test-tooling freeze was found along the way.**
    - Scripted A presses through `[test] replay` with `frame_source = tick` do reach hid. Tick frames are not wall time: there are 2 ticks per present, and presents are unpaced during loading, so tick 1800 is about 9 s in.
    - With any `[test]` tick source active, the game froze at ticks 5524–5614 (about 13 s), **with or without a press**: a scripted run that only slept froze at the same point. Runs without `[test]` reach the difficulty screen and run 70–120 s with no freeze. So this is a tooling interaction, not a game bug. The main thread waits in `nn::os::WaitLightEvent` under the job system, and the leaf waiter (thread 143) polls `TimedWaitLightEvent` in `f_7103593e80`. Not chased further.
    - `tools/raiders/nso_sym.py` names SDK addresses from `.dynsym`.
75. **Live keyboard and controller input: Raiders had no frame hook.** The log said `play: the game (sra) names no frame update: play mode is not pumped`. Without the hook, SDL events are never pumped and pads, keyboard and mouse are never sampled into hid. The Xbox Series X controller was detected ("gamepad 3 added") but could not reach the game. Every `[bindings.*]` section in `controls.ini` was also empty: the framework's actions were the Metroid title's, and Raiders had no factory layout.
    - Frame boundary: sead's run loop `f_71031b8c20` is `for (;;) this->vtable[0xE0](this)`, and that method, `0x71031b8c60`, runs once per main-loop iteration on the main thread. It is now an `entryhook` in `recomp/games/sra/main.sites` (wired through `sites = "main.sites"` in `game.toml`). `games/sra/sra_game.cpp` registers a `GamePlay` with `frame_update = 0x31b8c60`.
    - Factory keyboard bindings, revision 1 (`sra_game.cpp`). The same bindings are also listed as revision-1 additions, so the existing revision-0 `controls.ini` picks them up. With no in-game input hook, play mode treats every frame as a menu frame, so all bindings sit in `[bindings.all]`:

      | Key | Switch input |
      | --- | --- |
      | WASD | left stick |
      | arrows, 1–4 | D-pad |
      | IJKL | right stick |
      | E / Enter | A |
      | Space / Backspace | B |
      | Mouse1 | ZR |
      | Left Shift | ZL |
      | Mouse2 | R |
      | Q | right stick click |
      | Tab | X |
      | F | Y |
      | R | L |
      | Esc | + |
      | C | − |

      The button roles follow the Splatoon series' console layout and are **not yet verified against Raiders**. Mouse aiming needs a camera hook. Pads keep the console layout.
    - Verified: after the relift the log shows `frame hook 1`, and the old `controls.ini` was upgraded to revision 1 with 30 bindings. An Enter key event pushed through SDL (`[play] selftest`, the same path a real key takes) moved the game from "Select a difficulty" to its **"Start on this difficulty?"** screen. That screen shows the chosen difficulty, "Raider", with its label rendered. The controller path (SDL → `poll_pads` → hid) is now pumped as well, but it has not been tested with a physical pad.
    - The main loop runs about 330 iterations per second while loading, so self-test waits are counted in frames, not seconds.
76. **Menus ran 5–7× too fast.** The user saw this in the window. `[display] unpaced_loading` (on by default; it was written for Pokopia's loading screens) leaves frames with fewer than 16 draws uncapped for up to 20 s. Raiders' menus draw fewer than that, so they ran at 300–460 fps, and the game advances its animation once per frame. The default is now per game (`game::Info::unpaced_loading`), and Raiders sets it to false. The ini key still overrides it. After the change, a 111 s run averaged about 52 presents/s including loading.
77. **Constant banks above c[17].** 8 vertex/fragment blocks (`4cd833221d53dd96`, `1bd29c473dcc6e18`, `70273cf017761b71`, `a9f1cf1a0b5e292d`, `bd0f25bfcf8c55d7`, `0de20fdbb8ea01fa`, `312454a27ef836a5`, `843f08d13b29ba13`) read `c[0x18]`, and the translator threw `IndexOutOfRangeException` in `ResourceManager.GetConstantBufferBinding`. The cause is Ryujinx's 18-slot table (Maxwell's limit); Ampere encodes a 5-bit bank.
    - New `tools/shaders/harness/translator/ampere-cbuf-banks.patch` widens the table to 32 and throws a clear error for a bank outside it.
    - `StaticGpuAccessor` gives banks 18–31 their own binding range (109 and up, 14 per stage), so they never collide with another stage's range.
    - The runtime's replay keeps 32 uniform slots per stage instead of 16 (`GState::ubo`, the bind recorder, and the lookups).
    - Note: a new `ShaderPatch` must also be added to the explicit `Compile` exclude/include lists in `translator/Ryujinx.Graphics.Shader.csproj`. Otherwise the original file is compiled and the patch silently does nothing; this happened on the first try.
    - All 8 blocks translate now. The 23 compute blocks with the `TEX…F16.RN` failure recorded also translate after the milestone 73 fix. Their stale `results.tsv` rows were dropped (backup `results.tsv.bak2-20261107`).
    - **Still missing text (and see 78):** the highlighted button's label on the difficulty screen ("Raider") and the two choice labels on the confirm screen. These 8 blocks were not the cause. Next suspects: the 71 draws still skipped ("draw without a usable render target skipped"), and the texture views that fall back to dummies ("attachment format is not compatible", the cube-array view).
78. **User play test (controller): menus work, then a freeze after the loading screen.** The user confirmed the Xbox controller drives the menus.
    - **Button layout:** they asked for the position layout (right face button = A). `controls.ini` `[pad] label_abxy` is now set to 0 (backup `controls.ini.bak-20261107`).
    - **Freeze:** at 44 s, after confirming the difficulty and the loading screen, presenting stopped. Log: `work/raiders/win-play.log`, freeze report `sra_freeze.txt`.
      - The main thread is in `nvnSyncWait` (called from per-frame `f_71031b9ac0`) for serial 7525. The last submitted serial is 7524.
      - The render thread holds 6 queued jobs and is inside `amdvlk64.dll` under `gpu::compute_pipeline` ← `do_dispatch`, i.e. the AMD driver is compiling a compute pipeline.
      - Cause: the compute blocks that started translating after the milestone 73 fp16 fix produce **6–9 MB SPIR-V** each from 32–45 KB of SASS. That is about 200× the input; the median module is 58 KB. Examples: `3894e097d242af87` 8.9 MB, `bab4f31d55819a34`, `2768fa9b1270672b`, `db11aed557d61021`, `e50bf6968b4ff826`, `003cff046bb9990b`. The driver needs minutes for one of these, and pipelines are created synchronously on the render thread, so the game's fence wait never returns.
      - Ruled out: Ryujinx's dynamic-bank `EmitLoadConstant` select chain. The harness's `QueryConstantBufferUse` is the default 0, so that chain is empty.
      - Next: find the expansion in the translated IR of `3894e097` (loop unrolling, local-memory arrays, or the bindless texture path); then also create compute pipelines in the background and skip the dispatch until ready, as graphics pipelines already do.
    - The earlier "test tooling" freezes at about 13 s (milestone 74) are most likely the same render-thread stall, not a tooling problem.
79. **Permanent freeze fixed: big compute pipelines build in the background.**
    - What the blocks are: the oversized compute blocks are **tensor-core programs**. `3894e097` has 2,848 instructions, including 384 `HMMA` (fp16 matrix multiply-accumulate), 192 `MOVM` and 24 `LDSM`, and no loops: a small neural network, most likely the Switch 2 upscaler/denoiser. `AmpereEmit.Tensor.cs` emulates each `HMMA` with 32 lane shuffles (`ctx.Shuffle` with clamp), 64 fp16 unpacks and 64 FMAs, and each shuffle expands to several SPIR-V instructions. That gives 6–9 MB modules.
    - Runtime change: `nvn_pipeline.cpp` `compute_pipeline` now builds any compute shader with at least `[nvn] async_compute_kb` (default 512) of SPIR-V on a `std::async` worker. The pipeline cache is internally synchronized. Its dispatches are skipped until the build is ready, and each step is logged. Shutdown waits for outstanding builds.
    - Result: the game no longer deadlocks. Selecting a difficulty leads into the **story intro**: a dialogue line typed at the bottom of the screen and a "One Month Later" title card, with text rendering. The 180 s run ended at the deadline with about 970K draws and 46,813 textures.
    - Still open:
      - The background build of `2768fa9b1270672b` (8.4 MB) did not finish within about 147 s, so its dispatches stayed skipped. If these blocks produce the 3D image (the upscaler), the scene stays black until they run.
      - Two 10 s stalls (watchdog at 69 s and 143 s) still occur; the user saw them as stutter.
      - 74 textures failed and 18,797 draws were skipped.
    - Next: shrink the HMMA emulation, e.g. a raw subgroup shuffle instead of the clamped `Shuffle`, and fetch each A/B fragment once per instruction, so these modules compile in seconds. Then find what the 10 s stalls are.
80. **Tensor shaders: smaller, and cached after the first build.**
    - Change: `AmpereEmit.ReadLane` now emits a direct `OpGroupNonUniformShuffle`. Every caller passes a lane in 0..31, so the SHFL helper's clamp and validity check always passed and its index mapping was the identity for the 32-wide subgroup the harness declares.
    - Why the modules were large: an opcode histogram of `3894e097` (`tools`: scratch `spv_hist.py`) showed 12,856 helper calls wrapped in 74K `OpVariable` / 92K `OpLoad` / 62K `OpStore`. After the change: 40 calls, 22K variables.
    - Size: 8.9 MB → 8.0 MB. The rest is real per-element work: about 24K FMAs and 64 fp16 unpacks per `HMMA` × 384 (77K `ExtInst`, 80K `Bitcast`). 20 of 35 large shaders were retranslated into the cache (old modules in `work/raiders/spv-bak-20261107`); 15 have no code dump and keep their older versions.
    - Driver compile time on the RX 5700 XT: **257 s** for `2768fa9b` (7.6 MB) on a background thread. The next run loaded it from `pipeline_cache.bin` in **0.0 s**, so the cost is paid once.
    - Gyro: the Switch Pro Controller is detected with `gyro=1 accel=1` (labels 1032). `poll_pads` → `map_sensors` → `NpadInput.six_axis` → hid six-axis state is wired, but not yet tested in gameplay.
81. **Menus to character creation; 2D works, 3D is black; then the confirm press stopped working.**
    - Progress: with the freeze fixed, runs reached the story intro (dialogue, "One Month Later") and **character creation** ("Which hairstyle ya feeling?", the complexion picker). The 2D UI renders fully; the 3D area (the character preview the "Rotate" prompt refers to) is black.
    - The window target is **4K** (3840×2160 `RGBA8_SRGB` window texture): the game behaves as if docked to a 4K TV. For 60 fps on the RX 5700 XT, the reported display mode should become 1080p (not done).
    - Command handles (black-screen suspect): the runtime dropped command lists the game still called, about 500 per second (`CallCommands of unknown handle … overlapped by a newer recording`). The overlap rule rests on an *estimate* of command memory per list (a Switch 1 model). Changes:
      - each list now records the lists it calls when the call is recorded (`CmdList::called`);
      - lists dropped only by an estimated overlap stay resolvable in a bounded table (16,384, `[nvn] keep_overlapped`, default on). Handles are never reused, so this is safe. Finalize and pool release still end a list at once;
      - the drop reason is logged for the first 20 misses, then at powers of two.
      After this, no unknown-handle calls remain. The effect on the 3D view is **not verified yet**, because of the next point.
    - **Open blocker: since 17:28 the "Select a difficulty" menu ignores A (and B), while the D-pad works.**
      - Evidence: a `hid pad 1` log line (play.cpp) shows `buttons 0x1` (A) for 150–500 ms at each press, with nothing stuck. The D-pad moves the cursor (Right → Survivalist, description changes).
      - Earlier runs at 16:41–17:25 confirmed with the same Enter key. `win-trace` at 17:28 used the very same binary and failed, so it is not a code change.
      - Ruled out: account save (fresh save, and a copy of the good-run save), Nintendo controller drivers (SDL HIDAPI Switch/Switch2 off), Joy-Con 2 mouse (`[input] jc2_mouse = off`), the classic mouse feed (new `[input] classic_mouse = false`), the PC cursor position, the tensor shader (new `[nvn] skip_compute`), overlay state, and audio device.
      - Also ruled out: the pipeline prewarm keys (`pipeline_keys.bin`, last written 17:25:14 at the end of the last good run; renamed away for one test, then restored).
      - Still to check: `pipeline_cache.bin`, `shader_cache/spirv` (translations added by the good runs) and `bc7_cache`, moved aside one at a time. Also the game-side menu code: find what the difficulty menu's decide handler checks besides the A trigger.
    - Also found: the missing label is always on the **highlighted** button. The middle button shows "Raider" once the cursor moves away, and Survivalist's label disappears when it is selected.
    - Diagnostics added (cheap; kept): `[nvn] skip_compute`, `[input] classic_mouse`, `[nvn] keep_overlapped`, the `hid pad 1` change log (first 60 changes), and unknown-handle reasons.
    - **Resolved later (82):** the menu ignored scripted Enter because the key also reached the game's USB-keyboard device; a real pad's A always worked. Only the test harness was affected.
    - Housekeeping:
      - the save from the good runs was moved to `work/raiders/save-bak-20261107-1800` (an empty `save/` replaces it);
      - test saves are in `work/raiders/save-test-*`;
      - `controls.ini` has `label_abxy = 0` at the user's request (backup `controls.ini.bak-20261107`);
      - the mouse cursor was moved to the screen corner during one test.
82. **Input: keyboard-device feed off for Raiders.** A key bound to a pad button (Enter = A) also reached the game's USB-keyboard lifo, and with both the difficulty menu ignored the A press (the D-pad worked; a physical pad's A always worked). `[input] hid_keyboard` defaults to `game::Info::hid_keyboard`, which Raiders sets to false. Scripted runs now go through the menus on their own. Menu sequence (from the user): A, one step right on "Start on this difficulty?", A; then A for the autosave notice and to advance the intro. Diagnostics added: the `hid pad 1` change log, `[input] classic_mouse`.
83. **Black 3D, part 1: three runtime gaps fixed. All are real NVN2 features that were missing.**
    - **Buffers in virtual (sparse) pools:** `resolve_in_pool` now translates through `nvnMemoryPoolMapVirtual`'s tile maps, as textures already did. 540 distinct pages had been bound to the dummy buffer.
    - **NVN2 compute push constants:**
      - control bytes +0xa8 = 1 / +0xa9 = word count mean the program reads that many push words from c[0]+0x10;
      - the c[0] resource table starts after them (16-byte aligned). This is checked against the table size at +0xba before it is used;
      - compute pushes are kept in their own slot (`kComputePushSlot` = 8; the game passes stage 0, the vertex slot);
      - a dispatch of such a program gets its own 64 KiB c[0] with the pushed words at +0x10.

      For the tensor upscaler `2768fa9b` the pushed words are input size−1, output size−1, texel sizes and jitter (for example 1601×900 → 2402×1350). Its storage buffer, texture and image now resolve.
    - **Separate texture/sampler binds** (`nvnCommandBufferBindSeparateTextures/Samplers` and their `Deferred` forms) were empty functions. They are now recorded as partial binds (texture id bits 0..19 / sampler id from bit 20) and merged into the slot's combined handle at replay. Before this, the upscaler sampled the font atlas left in its slot, so the screen showed tiled glyphs.
84. **Tensor-core upscaler replaced by a native upscale (`[nvn] native_upscale`, default on).** A compute program with push constants and a translation of 1 MiB or more becomes a linear-filtered `vkCmdBlitImage` from the texture it samples to the image it writes, at the pushed sizes (scaled by the render scale and clamped to the images). This avoids:
    - the 4-minute driver compile;
    - the HMMA emulation, which still produced structured garbage after the bindings were fixed, cause not found;
    - about 0.5 s of GPU time per frame on a GPU without tensor cores.

    The game's input and output images are unchanged; only the filter differs.
85. **Black 3D, part 2: where the picture was lost.**
    - The trace dump now also reads back plain render-target formats (`snapshot_record_raw`: R11G11B10F, RGBA8, RGB10A2, RGBA16F, R32F at level 0). Scratch `dump2png.py` converts the dumps to images.
    - The **3D scene renders correctly on the GPU**: the intro's newspaper-and-map shot, lit and textured, in the R11G11B10F scene target and in the tone-mapped RGB10A2 target.
    - The flat colour came from the post-process pass that builds the upscaler input (vertex `70273cf0` / fragment `bd0f25bf`). It reads the target size and texel size from constant **bank 0x18**, which is where NVN2 **graphics push constants** land: the trace shows the game pushing 1/1602, 1/901, 1602.0, 901.0. Unbound, the vertex shader divided by 0, so the fragment shaders never ran and one texel was smeared over the screen.
    - Bank 0x18 is now bound per draw from the stage's push area (64 KiB, zero-filled; `[nvn] push_bank_stages` mask). Only 8 shaders read it, all post-process: 3 vertex, 5 fragment.
86. **GPU timeout once the post-process ran; a translator branch bug.**
    - Symptom: with vertex push data the full-screen passes finally ran, and at about frame 2204 the device was lost (timeline value UINT64_MAX after a 2.3 s frame). Fragment-only push data: no crash, still flat.
    - Cause: `AmpereEmit` dropped the predicate operand of `BRA`. `@P0 BRA P1, target` branched whenever P0 held, so loops that exit through P1 never ended. `312454a2` has 4 such loops. Fixed: the predicate operand is ANDed with the guard (PT skipped).
    - Every shader translated before the fix may carry the bug, so the SPIR-V cache was moved to `shader_cache/spirv.bak-prebra-20261107` and is being rebuilt on demand.
    - Also added: `[nvn] depth_equal_relax` (an EQUAL depth test without depth writes compares with LEQUAL; it was not the flat-colour cause, kept as a precision guard) and `[nvn] skip_compute`.
87. **The 3D world renders.**
    - The last cause of the GPU timeout: the game pushes graphics constants only with stage 0, and its fragment shaders read the same words. A stage without its own push reads area 0 (`SmallState::push_written`). With zeros, `312454a2`'s step size was 0 and its loop never advanced.
    - Result (scripted run with all SPIR-V retranslated):
      - no freeze in 170 s;
      - the intro shows the newspaper and map, the helicopter over the stormy sea with clouds, reflections and fog, and the spiral cloud vortex with sun rays, glow and islands;
      - scenery, lighting, particles and post-effects are correct.
    - Open: **skinned characters are torn into spikes** (cabin cutscene, vs `7d806262` / `b65308ce` / `93229f46`). Checked and found correct:
      - **attributes:** a4 RGBA8 weights, a6 RGBA8UI bone indices, delivered as 4 integers by the int-attribute patch;
      - **shader logic:** each index × 0x30 → `LDC.64 c[0x5][R+0x00..0x28]`, 3×4 matrices;
      - **bone palette:** uniform block 2, 6,672 bytes = 139 bones, real matrices;
      - **updates:** `UpdateUniformBuffer` is GPU-ordered (`do_write_memory`);
      - **pool mirror:** `[nvn] pool_mirror = off` changes nothing.

      Next suspects:
      - the bone palette address being the same for all character draws, while the game rewrites it per character; the trace prints host memory at trace time, not per draw;
      - the weighted sum (translation of the FFMA chain or of the 64-bit `LDC.64` pair order);
      - per-instance data (a8/a10 RG16 attributes).

      A trace that prints the palette per draw (at replay time) is the next tool. `[nvn] dump_shader = <hashes>` (with `dump_missing` set) captures a loaded block's code for `sassdecode`.
    - Frame rate in the 3D intro is about 22–25 fps at the game's 4K output with dynamic resolution, on the RX 5700 XT. Lowering the reported display mode to 1080p is the next performance step (goal: 60 fps).
      *(Corrected in milestone 91: the 3D scene renders at about 1300x730 under the game's dynamic resolution, and the runtime already reports 1920x1080 docked. Resolution was not the limit.)*
88. **Skinned characters fixed: a translator register-overlap bug.**
    - The palette was fine: all 139 bones were valid when printed in full. The uniform-block trace now prints long blocks over several lines of 96 words.
    - `LDC.64 R54, c[0x5][R54+0x28]` loads two words into R54/R55, and its index register is R54. `EmitLdcAmpere` wrote R54 before building the second word's address, so that address came from the value just loaded. The result was a garbage Z translation for the first bone of every vertex, which smeared characters inside their correct outline.
    - Fixed: every word is loaded before any destination register is written (both LDC paths). LDG already did this; LDS/LDL use Ryujinx's `EmitLoad`, which computes the address once.
    - The cabin cutscene now shows the characters intact (`work/raiders/shot_9.png`). The SPIR-V cache from before the fix is in `shader_cache/spirv.bak-preldc-20261108`.
    - Also fixed: a command-buffer `FenceSync` now gets its serial on the game thread in `nvnQueueSubmitCommands` (`arm_list_fences`). Before, with async submit, the game could wait on such a sync before the render thread replayed it, see its old serial, which had already signalled, and refill a per-frame ring early. The replay only ever raises a serial.
89. **Profiling: the GPU's real cost.**
    - `[prof] trace = <file>` plus `[prof] gpu_passes = true` timestamps every render pass. Passes with one or two draws now carry their fragment shader hash in the label.
    - Scratch scripts sum the trace: `prof_sum.py` (zones per thread), `gpu_busy.py` (union of GPU intervals), `gp_group.py` (passes grouped by type), `zone_args.py` (zones by argument).
    - Cabin cutscene before the fixes below: 28 ms of GPU work per frame. Breakdown:
      - main scene pass: about 10 ms;
      - two full-screen 4-tap blurs (`1ac3fb73`, `9f7fb8a9`): about 7 ms;
      - `write memory`: 106 small GPU-ordered writes, each with two full barriers, about 2–3.5 ms.
90. **fp64 emulation removed from ordinary float math (translator).**
    - The blur's texture coordinates use `FMUL.RZ`. `AmpereEmit` emulated directed rounding exactly with fp64 compares, and non-FTZ FADD/FMUL/FFMA/FSETP with an fp64 denormal branch. fp64 runs at 1/16 rate on the RX 5700 XT.
    - Now off by default; results differ by at most 1 ulp, which graphics never shows. `NXRT_SHADER_EXACT_ROUNDING=1` and `NXRT_SHADER_EXACT_DENORMS=1` restore the exact paths for hwcheck comparisons.
    - The blur shrank from 11,540 to 4,392 bytes of SPIR-V with no fp64 left. GPU time per frame went from 28 to about 19 ms; the main scene pass alone went from 10 to 3.75 ms.
    - The SPIR-V cache from before this change is in `shader_cache/spirv.bak-prefast-20261108`.
91. **Render thread: textures created every frame.**
    - Raiders initializes its per-frame render targets again every frame, in virtual pools. At dynamic resolution most come back at a new size, and each one was a new VkImage, about 1.3 ms.
    - **Recycling:** a released GPU-written image is kept for 8 presents under its exact description (pool and offset or address, size, format, flags, scale), and a matching new texture takes it back. Up to 64 entries; `[nvn] tex_recycle`. A long hold (240 presents / 512 entries) kept too many large images in video memory: GPU time rose to 26 ms.
    - **Image memory pool:** device-local images come from VMA pools with 256 MB blocks (`[gfx] image_pool`). The AMD driver prefers dedicated allocations for render targets, and VMA made one `vkAllocateMemory` per image. Image creation went from 1.39 to 0.74 ms.
92. **Frame-rate targets measured.**
    - The menus run at a locked 60 fps (1% low 57).
    - At about 44 s the game sets present interval 2 (logged now): the story cutscenes target **30 fps**, as on the console. In the cabin cutscene the port reaches about 23 fps.
    - The limit is now the render thread, about 29 ms per frame:
      - loading new textures as the cutscene changes scenes (`tex.ensure`): about 11 ms;
      - texture creation: about 3.7 ms;
      - command replay: the rest.
    - The game thread waits for it in `nvnSyncWait`.
    - Gameplay, where the game is expected to go back to interval 1, is not reached by the scripted test yet.
93. **Long runs reach character creation; what was tried for the frame rate.**
    - A scripted run that presses + then A every few seconds gets through the story intro. At about 217–230 s the game goes back to present interval 1 (60 fps) for **character creation**, which then runs at **about 44–46 fps** (1% low about 21) with no crash in 7 minutes (`shot_18.png`).
    - **Device-memory crash found and fixed:** with `[gfx] image_pool` on, a long run ran out of device memory after about 3 minutes (`VK_ERROR_OUT_OF_DEVICE_MEMORY`, then an access violation in the overlay's ImGui texture upload). With it off the same run lasts 300+ s. It is now off by default; the recycler alone is fine.
    - Character creation profile:
      - **GPU:** 12.8 ms per frame, inside the 16.7 ms budget. The scene renders at up to 2560x1440 with 3840x2160 output passes, so the game outputs 4K.
      - **Render thread:** command replay is the limit. In relative terms: per-draw setup about 35% (feedback-copy checks about 15%), compute dispatches about 32% (about 1 ms each), texture work about 30%.
      - **Game thread:** waits for the render thread in `nvnSyncWait`.
    - **Tried and not kept (all still configurable):**
      - `[nvn] rt_skip_upload`: a fresh compressible render target starts cleared instead of being uploaded. It blacked out the cabin background and doubled GPU time, so those targets carry data the game relies on.
      - `[nvn] gpu_untile_virtual`: GPU untile for virtual-pool textures. Render thread −2 ms, GPU +6 ms, no fps gain.
      - `[nvn] compute_shadows = false`: no change (45.4 vs 44 fps).
      - `frames_ahead = 2` (it lives in `controls.ini`, which overrides `[latency]` in the run ini): no change measured, about 45 fps. The startup log still printed 1, so whether it applied is unconfirmed.
    - **Background texture prep (texprep) misses Raiders almost entirely:** 216 jobs scheduled, 0 used, budget full of early large textures. It only starts on `BindTexture*` or on registration of textures of 1 MB or more, while Raiders reads most textures through handles in uniform buffers. In a 60 s cutscene window the render thread prepared 3,371 *uncompressed* textures itself (formats 5/10/61, COMPRESSIBLE: per-frame render targets) and 6 compressed ones.
    - **Visible bug:** blotchy black/pink patches on the player character's skin (neck, arms) in character creation.
    - **Next:**
      - the skin patches (trace one character-creation frame);
      - making per-frame render-target uploads cheap without dropping their data;
      - reporting a 1080p display so the game stops rendering 4K output passes;
      - measuring gyro in gameplay.
94. **Skin holes fixed; character creation at a locked 60 fps; cutscenes paced at 30.** (2026-11-08, afternoon)
    - **Skin holes (neck, arms) — fixed.**
      - **Mechanism.** The scene is drawn twice. A depth pre-pass writes depth, and the colour pass tests for *equal* depth without writing. The body's pre-pass pixel shader (`3cf740bdb5cc99de`) discards a pixel when `mask(uv) × alpha < 0.5`, where alpha is `c4[124].z` = 1.0 and the threshold is `c3[0x54c]` = 0.5. A pixel the pre-pass discards is never drawn by the colour pass either, so the sea shows through.
      - **Root cause.** The mask is a BC4 (RGTC1) texture that the game *renders into*: it binds the BC4 texture as a colour target through a view of an 8-byte format, one pixel per 4x4 block, with the viewport in blocks (for example 100x60 for a 400x240 RGTC2 texture). Vulkan cannot attach a BC image, so `tex_attachment_view` returned the dummy view, the render went nowhere, and the mask kept stale bytes. The log showed this as "attachment format is not compatible with the image" for six textures: four of them at the start of character creation, plus earlier ones at 32 s and 44 s.
      - **Fix.** `tex_bc_target`, `tex_bc_target_begin` and `tex_bc_target_end` (in `nvn_gpu_texture.cpp`, called from `ensure_pass` / `end_pass`).
        - The pass renders into a stand-in image of the view's format (32-bit integer formats through their float alias, as for other targets), a quarter of the size. The stand-in is loaded with the level's current blocks first.
        - After the pass, `vkCmdCopyImage` copies it into the BC level; Vulkan allows copies between compressed and uncompressed formats with the same block size.
        - Stand-ins are freed with the texture's image.
      - **Result.** Skin is solid (`work/raiders/shot_1.png` of the cc4 run).
      - **Ruled out on the way.** Position mismatch between the pre-pass and colour-pass vertex shaders: their math is identical and they read the same bone palette at the same address. Contraction is also ruled out, because `NoContraction` is emitted.
    - **Character creation 45 → 60 fps.**
      - **Self-time profile** (new scratch script `prof_self.py`, which subtracts nested zones). The render thread spent **8 ms per frame in `vmaCreateImage`** (`vk.create_image` zone, new): 27 image creations per frame. Earlier summaries hid this because `tex.create` only logs creations over 30 µs.
      - **Why there were so many.** The game's dynamic resolution changed the render size almost every frame (724 distinct sizes in 10 s), so the recycler (milestone 92) rarely found the same size again: 26% of images were taken back. Evicting the oldest kept image instead of refusing new ones when the recycler is full did not change that.
      - **Why the resolution kept changing.** `ReportCounter TIMESTAMP` is answered with the CPU clock while the render thread replays (`timestamp_ns()`), so the "GPU time" the game measures between two reports is our replay time.
      - **The setting.** New `[nvn] gpu_timestamp_scale` (default 1) compresses the time between reports within one submission. At **0.4** the game settles at **2560x1440** and stays there:
        - recycler 99.8% taken back;
        - `vk.create_image` 8 → 0.26 ms per frame;
        - character creation **60.0 fps, 1% low 54–56** (before: 45, low about 20), for 2+ minutes with no crash.
      - **Turned on** in `work/raiders/raiders-user.ini` and `raiders-play.ini` (backups `*.bak-20261108-ts`).
      - **Caveat.** The game no longer lowers its resolution for our slow replays. In heavier gameplay the GPU at 2560x1440 is not measured yet. The faithful version is real GPU timestamps (`vkCmdWriteTimestamp`, written to the report before the fence that follows it signals); that is future work.
    - **Cutscene pacing.**
      - **The problem.** The story cutscenes ask for present interval 2 (30 fps) and play a fixed number of frames (about 4,150). Nothing used the interval for pacing. At about 24 fps they ran slower than intended (173 s instead of 138 s). Once the replay could keep up, they ran at about 45 fps, 1.5× too fast (93 s).
      - **The fix.** `nvnWindowSetPresentInterval` / `nvnWindowInitialize` now set `frame_limiter_set_interval`, and the limiter divides its rate by it (`[display] honor_present_interval`, default on).
      - **Result.** Cutscenes run at **30.0 fps** and take 139 s, menus and character creation at 60.
    - Logs: `work/raiders/win-cc1..6.log` (cc1: traced frame, cc3: profile with full uniform blocks and texture dumps, cc5: profile at scale 0.4, cc6: final run).
95. **Cutscene lighting, two aborts, video-memory pressure.** (2026-11-08, evening)
    - **Cutscene lighting fixed.** Shaded surfaces had no ambient light: the cabin interior was black and the helicopter's inside was a flat brown.
      - **Cause.** The lighting probes are 256x256x6 textures that the game makes as 2D arrays and samples as cube arrays. The image was not cube-compatible, so the view failed and the shaders read the black dummy ("cube array needs a cube-compatible image").
      - **Fix.** `shape_of` marks square 2D arrays with a multiple of 6 layers as cube-compatible.
      - **Result.** `work/raiders/shot_1.png` of the pl1 run shows the lit cabin.
    - **Abort at 245 s fixed (svcMapMemory).** A thread stack whose source is `MapPhysicalMemory` memory (kept as `kMemAlias`) was refused with 0xD401. On the console that memory is MemoryState_Normal and may be aliased, so `map_memory` now accepts it.
    - **Abort at 351 s fixed (nfp `GetDeviceState`).** The call answered "device not found" (0x8073) and the game aborts on any error. It now answers state Initialized (0): a reader with no amiibo on it.
    - **Video memory.**
      - **What happened.** At a fixed 2560x1440, cutscene camera cuts keep about 130 extra full-size targets alive, so usage swings between 2 and 4+ GB against a 6.5 GB budget. Two runs failed allocations (at 63 s and 100 s). Every failed texture was then sent again each frame, 40+ MB per frame, which gave about 10 fps.
      - **Scale 0.6 is worse.** With `gpu_timestamp_scale = 0.6` the resolution moves again; 3,000 images were live at once and allocations reached 13 GB. 0.4 stays.
      - **Changes.**
        1. The recycler is also capped by bytes (`[nvn] tex_recycle_mb`, 512).
        2. An out-of-memory image creation now releases the kept images and every finished destruction (`free_memory_now`: wait for the GPU, then retire) and retries once.
        3. Images of textures in a virtual pool of at most 1 GB are placed in one shared block per pool, at twice their offset (`Device::alloc_image_memory` / `create_image_at`, `PlacedBlock`; `[nvn] place_virtual_images`, `place_pool_max_mb`). Textures that overlap on the console then overlap here too.
      - **Measured** (pl1 run, 600 s): peak usage 4.1 GB, no failed allocation and no crash. Cutscenes at 30.0 fps, menus and character creation at 60.0 (1% low about 54).
      - **Not proven.** The peak is about the same as before placement, so its effect on memory is not shown yet. Most of the cutscene images are likely in pools over 1 GB, or in plain pools; this needs checking.
    - **Scripted input cannot finish character creation.** (Solved for testing: the user played through once; tests now load copies of that save, `save-user-backup-20261108`.)
96. **Gameplay reached; 10 → ~30 fps; a GPU hang and gyro direction.** (2026-11-08, night)
    - **The user's first gameplay session hung** about 20 s after character creation: `vkQueueSubmit2` returned `VK_ERROR_DEVICE_LOST`. It happened with image placement on, and has not happened since. Runs from the save with placement off and on both reach the beach tutorial.
    - **Why gameplay ran at about 10 fps.**
      - The game thread waited about 84 ms a frame in `nvnSyncWait`, while the render thread did about 15 ms of work and was otherwise idle. So the GPU took about 85-100 ms per frame.
      - **Profiler fixes needed to see it.** Earlier traces ended before gameplay because the event buffer filled after about 130 s. New `[prof] trace_start_s` delays recording. GPU timestamp pools are now made whenever a trace is configured, not only while it is recording.
      - **Cause: "feedback copies".** About 80 draws a frame (fragment shader `c31c813b`, an RG8 target with D32S8 depth, probably the ink map) sample the attachment they render to. Before each one, the port copied the whole image with every level: 35 ms of GPU time a frame, plus a pass break and a full barrier each time.
    - **Changes** (`nvn_replay.cpp` `feedback_prepare`).
      1. Only the draw's viewport∩scissor rectangle of the attachment's level is copied (`[nvn] feedback_copy_region`). Each copy image tracks the rectangles drawn since its last copy. A draw that touches none of them needs no copy and no pass break. Clears, texture copies, downsamples and dispatches invalidate the copy.
      2. The pass ends with tracked barriers instead of a full barrier.
      3. Images that have a feedback copy are rendered in `GENERAL` layout (`[nvn] feedback_general`), so the copy reads them in place. The layout change to TRANSFER_SRC and back cost about 24 ms a frame on the RX 5700 XT.
    - **Measured** (gameplay, beach tutorial): about 10 fps, then about 25, then 26-38 fps. Feedback-copy GPU time went from 35 to 6 ms a frame. A 260 s run had no crash; video memory peaked at 5.6 of 6.5 GB.
    - **One run hung after its frame rate rose** (gpL, placement off). At 142 s, 89 MB staging buffers failed, buffers fell back to dummies, and the game then waited forever on an unsubmitted serial. Memory pressure is still the risk.
    - **Gyro.** The user reported left/right reversed, so yaw (SDL y) is negated (`[pad] gyro_invert_yaw`, default on). The user also reports it is very sensitive; the units look right (rad/s divided by 2π), so this is not explained yet.
    - **Beach glitch trace** (gpN: image placement on, frame at 175 s, `work/raiders/trace-placeon/`).
      - The main HDR scene target (R11G11B10F 2560x1440) contains inf.
      - The main depth buffer (DEPTH32F) holds values up to 9e33, in rectangular blocks: the pattern of another live image sharing its memory (on AMD, also the depth compression metadata).
      - The bloom chain then averages ~11,000 in blue: the washed-out, white look.
      - Image placement is therefore **off by default** now (`[nvn] place_virtual_images`). It never lowered the peak either.
      - A clean comparison frame of the beach with placement off is still missing: timed traces landed on the boat (clean, no inf; scene at 256x144) and on a black loading frame. Glitches were also seen with placement off (run gpA), so placement is probably not the only cause.
    - **Resolution.** In one traced boat frame the game rendered its 3D scene at **256x144**, its minimum. The dynamic resolution still collapses when frames are slow, even with `gpu_timestamp_scale = 0.4`, and that looks very blurry.
    - **Right stick as gyro** (user request, for pads without motion sensors). `[pad] stick_gyro = auto|on|off` (default auto: on when no pad has a sensor), `stick_gyro_speed` (deg/s at full deflection, 200), `stick_gyro_invert_y`. The stick's deflection becomes the six-axis angular velocity, and the stick itself is withheld from the game. Set to `on` in `raiders-user.ini` / `raiders-play.ini`. Built, not run.
    - **Later the same night.**
      - **Freeze fixed.** An 89 MB staging buffer failed every frame (875 times in a minute) for a texture upload and froze the game. Uploads that cannot get one buffer now free finished work and retry, then go up in slices of at most 16 MB (`upload_in_slices`). A texture is no longer left "dirty" to fail again every frame.
      - **Black output frames.** The 4K output pass sampled an old 384x216 texture object, while this frame's scene went into a 256x144 one. New `tex_stale_alias` / `tex_sync_from` (`[nvn] alias_sync`) copy a newer same-format image over the same storage start into the image being read. In the next run (gpS) it never fired, so whether the two share a storage start is unconfirmed. The trace now prints `vp<pool>+<offset>/<size>` for pool textures, to check.
      - **Glitches persist with image placement off** (gpS screenshots): horizontal striping, a mesh-like repeating pattern on the ground, and white ground. Placement was at most one cause. The striping looks like rows read with the wrong layout. Next: trace the frame that shows it, and compare the striped target's tiling (block height) and the texture objects reading it.
      - **The stripes are in the main HDR scene target itself** (gpT trace at 230 s, `trace_t135187`). The scene's draws produce them: 1,038 draws, none skipped. Two ideas were ruled out:
        - depth-test rounding: an equal-test depth bias (`[nvn] depth_equal_bias` / `depth_equal_slope`, now default 0) changed nothing;
        - image placement: off, and the stripes stay.
      - A later shot also shows regular vertical bands across the ground (gpU shot_5), the look of tiled memory read as a plain image. **Current lead:** textures the game makes over memory the GPU just drew into through another object of another format. The port never writes GPU results back to game memory, so such a texture is uploaded from stale, tiled bytes. The trace shows such reuse: an RGBA8 2560x1440 and an R11G11B10F 1280x720 both start at `+52ac000`.
97. **The video-memory leak behind gameplay's black frames, glitches and slowness.** (2026-11-08, late night)
    - **Finding.** In every gameplay run with image placement off, allocations grew by ~430 MB/s to 37 GB, and from about 33 s every new texture failed (`vmaCreateImage` -2). That gave 11,000+ failed textures and 400,000-1,100,000 skipped draws per run, which explains most of the black scenes and missing passes. The game's texture-object count was flat (~8,490) while live images grew ~27/s. All the extra images were `R11G11B10F 2560x1440` render targets (new periodic `live images:` report).
    - **Cause.** Commands are replayed after the game has already finalized or re-initialized the texture objects they bind. `tex_or_null` made a new image for such a finalized object, and nothing released it, because its release had already run.
    - **Fix, in two steps.**
      1. Images made for finalized textures are released at the start of the next submission (`g_orphans`, `tex_poll_cpu_writes`). The leak stopped (live images flat at ~1,850), but ~100 `vkCreateImage` a frame (42 ms) dropped the hideout scene to 12 fps.
      2. `texture_registry_find_alias_virtual`: a texture (finalized or new) over the same virtual-pool offset with the same shape as a live texture shares that texture's image, as `texture_registry_find_alias` already did for plain pools. Image creation left the profile.
    - **Result** (hideout dialogue): **12 → ~42 fps**, the game thread waits 14 ms/frame on the GPU (was 69), live images flat at ~1,630, 0 failed allocations. The 1% low is ~6 fps (frames up to ~170 ms): stutters, next.
    - **Feedback copies now follow the image, not the texture object.** Copies keyed by the object went idle, because Raiders makes new objects for the same render target every frame. `fb_sweep` dropped them every 128 submissions and they were made again, the 6400x6400 ink map's included. `FbClone::src` now records the attachment's image: a new object over the same image takes the existing copy. `fb_general` / `feedback_note_draw` compare image pointers instead of dereferencing possibly finalized key objects. Result: 42 → 53 fps with the profiler on.
    - **The remaining 150 ms stall every 1.2 s came from the profiler.** During it every thread was blocked at once. Without `[prof] trace`, the hideout scene runs at a **locked 60 fps** (1% low 48-53, worst frame 21-28 ms, 0 stalls in 60 s, `win-np1.log`). Measure stutters without the trace.
    - **Same night.**
      - The writer table (`g_last_writer`, `tex_stale_alias` / `tex_sync_from`, `same_shape_gpu_writer`) no longer holds references to texture objects. Entries name the image and are removed in `tex_destroy`.
      - The ocean and hideout scenes now render cleanly (gpX shot_3, gpZ2 shots).
      - The beach stripes have not been re-checked since the leak fix.
98. **Beach stripes traced to the ground draw's inputs; GPU write-back added; a new memory jump.** (2026-11-09, early morning)
    - **Where the stripes come from** (trace snapshots by fragment shader, `trace_snap_fs`, runs bc3/bc4):
      - The depth pre-pass leaves the scene target black.
      - **Draw `vs 810284f1 / fs f0fe5663`**, the beach ground (402 indices, ~200x260 world units, EQUAL depth), writes the striped image over the whole target.
      - Later colour draws do not change it.
    - **The ground samples ~15 textures**, among them game-generated ones (DXT5 1370x1675, RGTC2 548x670 x3, DXT1 548x670, **RG32F 274x335**, **R11G11B10F 274x335**). Their game memory is all zeros (`mem 0%`).
      - The block-compressed ones are rendered through the BC stand-in path (milestone 94) at area load. The GPU copy of an RGTC2 one decodes to a **valid normal-map atlas** (`work/raiders/cmpskin/ground_bc5_gpu.png`), so that path is fine.
      - Lead: the float lookup tables. The upload log shows **`RGBA8SN 548x670` read over storage the GPU drew as `RGBA32F 274x335`**, and `468x574 <- 234x287`, with the same byte count. The game draws a table in one format and reads the bytes in another. The port never wrote GPU results back to game memory, so the reader uploads zeros or stale bytes and the ground samples its atlas at garbage coordinates.
    - **New `writeback_gpu_writer`** (`[nvn] gpu_writeback`, default on). Before such an upload (plain pool, same storage start, different format/shape, GPU-written colour writer), the writer's image is read back and tiled into game memory, as `download_texture` does. Once per write epoch. The writer table entries now keep the writer's `TextureBuilderInfo` and storage pointer.
      - **Not verified.** In the one run with it (bc6) it never fired: no "game memory filled from texture" lines. Possible reasons: the writer is not a plain-pool texture, the reader is created before the writer's entry exists, or the format check failed. Check the conditions against the logged pair next.
    - **New memory jump in that run (bc6).**
      - At ~180 s, live images went 1,270 → 3,100 and `vmaCreateImage` failed again (94 lines); gameplay ran at ~20 fps.
      - New groups: **34x `DEPTH32F 2048x2048x2`** (shadow maps, ~33 MB each) and 35x `R16F 256x256` render targets. The rest of the growth is spread over many groups (probably an area load).
      - 34 shadow maps where the game uses one or two points to duplication: probably replayed old objects that `texture_registry_find_alias_virtual` does not match (shape or offset differ), or images the orphan release misses. Next: log the shadow-map texture objects (vpool, offset, shape) as they are made.
    - **Still wrong.**
      - Glitches in the beach view: horizontal smearing in the background, and a white band or plane in the foreground (`work/raiders/gpM-shots/shot_2.png`). Not re-checked after milestone 97.
      - The Y button does nothing (the binding `btn_y = Y` is correct).
      - Real GPU timestamps instead of `gpu_timestamp_scale`.
 The Escape/Return loop goes back and forth there, so gameplay, the gyro and the two abort fixes past that point are still not exercised by a test run.

99. **Handheld mode: the beach at 60 fps with no out-of-memory; the write-back fixed and made safe.** (2026-11-09, morning)
    - **Write-back now fires.** Raiders' tables live in virtual pools: the reader's bytes are reached through `texture_virtual_direct`. A writer layout that runs past the reader's range is tiled into a temporary buffer, and only the reader's bytes are copied. Run bc9 logs both pairs: `RGBA8SN 548x670 <- RGBA32F 274x335` and `468x574 <- 234x287`.
      - **It did not remove the stripes in the wide view.** Those tables are not the cause, or not the only one.
    - **Crash fixed (bc9, 195 s, `amdvlk64.dll` in `image_barrier`).** `finish()` inside the write-back runs the deferred destroys, which could free the writer's image before the copy. The entry is now looked up again after `finish()`.
    - **The write-back is limited to reinterpretations:** another format with the same pixel byte count. In docked mode, dynamic resolution remade targets at a slightly different size over the same memory (3066x1725 over 3072x1728). The write-back read those back with a GPU stall each time. `[nvn] gpu_writeback_any = true` restores every shape.
    - **Cause of the ~184 s collapse (docked).**
      - The game renders the beach at **3072x1728** internally.
      - When frames run slow, its dynamic resolution resizes the scene targets almost every frame (1516x853 … 2274x1279).
      - Each new size is a new set of images plus 25–47 MB of uploads per frame. That makes frames slower still, so the size drops again, and video memory runs out (`vmaCreateImage` -2), followed by black and garbage frames.
    - **`[am] operation_mode = handheld`** (now in `raiders-user.ini` and `raiders-play.ini`; backups `*.bak-20261109-handheld`).
      - The game uses its handheld resolution.
      - Run bc10, 300 s: no crash, no out-of-memory.
      - Render targets hold ~2.3 GB, stable.
      - Gameplay runs at **60 fps**: the gyro-tutorial view of the sand, from 250 s on, 1% low ~52 fps.
      - The sand renders cleanly (`work/raiders/shots-bc10/shot_12.png`, `shot_16.png`).
      - The wide view during the opening dialogue (110–250 s) runs at 31–45 fps and still shows horizontal stripes in the sky and distance (`shots-bc10/shot_2.png`).
    - **Diagnostics.** The live-image report now gives the MB of render targets in use and of images kept for reuse, and the top 14 groups.

100. **Beach-sky stripes traced to the sky dome's colour draw failing its depth test** (2026-11-09, morning; solved in milestone 102: unaligned vertex streams)
    - **Where.** Trace runs tr1–tr6 of the wide dialogue view at 150 s, handheld, with snapshots per draw.
      - The scene target (`R11G11B10F 1920x1080` at `vp+0x872000`) is never cleared. Where nothing is shaded in a frame, the previous frame's fogged picture stays, and each frame's fog blends over it again: the stripes.
      - **The sky dome** (radius ~998, 2280 indices, mesh at `61c2e9a000`) is drawn in the depth pre-pass by `vs 3ada151e / fs 2ef1dbac` (LESS, writes depth).
      - It is coloured later by **`vs 898f9f8a / fs fca4e033`**: EQUAL, no depth write, so relaxed to LEQUAL; in the pass where depth is read-only. The sky texture is `BPTC_UFLOAT 4096x1024`.
    - **The colour draw fails its depth test.**
      - With `[nvn] depth_test_off_fs = "fca4e033"` (new diagnostic, off by default), the shot shows a proper blue sky with clouds and no stripes (`work/raiders/shots-tr6/shot_1.png`; it also paints over the ground, so it is not a fix).
      - `depth_equal_bias = -256`, slope `-1`: no change.
      - `-100000` (~0.006), slope `-4`: the sky appears with some stripe rows left, and distant land washes out (`shot` of run b2).
      - So the colour draw's depth is 1e-5 … 6e-3 *farther* than what the pre-pass stored.
    - **Ruled out:**
      - The two vertex shaders, dumped with `dump_shader` and the harness (`work/raiders/dump-sky/*.spv`; GLSL text despite the name), compute position with identical math: `z = min(clip.z, w)`, `precise` throughout.
      - Same constant buffers (`c3` camera = trace `ubo0`, `c4` model = `ubo1`) in both draws.
      - Same mesh, depth mode (`NEAR_IS_MINUS_W`, both pipeline paths chain depth-clip-control, which the driver supports), polygon offset (off in both) and viewport.
      - The depth image is cleared to 1 before the pre-pass and not re-uploaded.
    - **Later the same morning: the real culprit is a different draw** (runs ro1, gp1, tr7–tr9, nf1, pm1).
      - Also ruled out: the read-only-depth transition (`[nvn] depth_ro = false`, new diagnostic) and the graphics pipeline library (`gpl = false`). Neither changed the stripes.
      - **Raw depth reads** (`snap_*DEPTH32F.png.bin`; scratch `depthrows.py`, `flatrows.py`):
        - The sky area does not hold the dome's depth.
        - It holds rows that are each constant across the full width and grow with height, with two interleaved families of values. That is a horizontal surface, and only some rows of it pass.
      - Snapshots every 25 draws, then every draw, name **pre-pass draw `vs 44dba4ef / fs 55647ed7`** (tr8 #453).
        - A 546-index mesh with 32-bit indices: local bounds −60..119 × 0.8..19.8 × −36..116, identity model matrix, culling off, polygon offset on.
        - It adds depth to ~577k pixels, 171 of them whole rows.
      - Its fragment shader alpha-tests a **GPU-generated `RGBA_DXT5_SRGB 1170x1435`** (stand-in path, all 9 levels rendered; probably the ink/paint map).
        - Test: `texture(...).a * clamp(c5[124].z) >= c3[84].w`, else discard.
        - The texture's blocks are sound at every level (`dxt5alpha.py` on the `trace_dump_tex` dump: ~35% coverage, proper downscales).
      - **`debug_null_format = "RGBA_DXT5_SRGB:1170x1435"`** (dummy texture, so the test discards everything) gives a **correct wide beach**: blue sky with clouds, the rock, floating islands, the shard teal instead of white (`shots-nf1/shot_1.png`). Not a fix: ink would be lost too.
      - Not the device-local pool copy (`pool_mirror = off`: unchanged).
      - Follow-ups (runs mk1, ic1, ub1, ds3, po1):
        - **Coverage.** `[nvn] debug_mask_fs = "55647ed7"` (new diagnostic: forces that shader's colour writes) shows the draw covering the whole area above the horizon, as one uniform sheet.
        - **Indices.** `[nvn] index_copy = 32` (new diagnostic: 32-bit indices copied from game memory per draw) changes nothing, so it is not stale index data.
        - **Threshold.** The fragment `c3` here is the 2240-byte material block, so `c3[84].w` is in bounds. Its value is `0x00000007`, a denormal flushed to 0, so the alpha test never discards: the draw writes depth wherever its triangles go, on the console too. The game never calls `SetAlphaRef`.
        - **The colour draw.** The same mesh (`61c4b710a8`, 546 indices) is coloured by **`vs 810284f1 / fs f0fe5663`** (tr8 #848; the "beach ground" draw of milestone 98).
          - Its position math (GLSL dump `work/raiders/dump-sky/810284f108644d1a.glsl`) is identical to the pre-pass shader `44dba4ef`.
          - The camera (`ubo0`) and model (`ubo1`, identity) constants are bit-identical, and so is the polygon offset (enables 7, factor −0.5, units −0.1).
        - **Polygon offset.** `[nvn] polygon_offset = false` (new diagnostic): the wide-view stripes are unchanged; the top-down sand view breaks into stripes instead.
      - **Where this stands.** In the wide view, a ground layer's pre-pass depth and its colour draw do not match row by row, with every input that could differ checked identical. The stored depth of the bad area comes in two interleaved row families, which looks more like rows alternating between two surfaces (or frames) than rounding.
      - **Also striped, and clearing by itself:** the top-down tutorial view is striped for its first ~30 s, then clean for the rest of the run, in run bc10 and in run bc11 (screenshot row-difference score 15–22, then 2).
      - **Next ideas:**
        - Check whether the game renders interlaced or checkerboarded (alternate rows per frame) through some state the replay ignores: a sample mask, window rectangles, a stencil row mask.
        - Compare the depth of a failing row with the colour draw's computed depth, by giving `fs f0fe5663` a depth output through a diagnostic.

101. **The wide beach view from 31–45 fps to 52–58 fps; 60 fps everywhere else** (2026-11-09, morning; handheld)
    - **What the profile showed** (GPU pass profile of the wide view, `prof-gpW.txt`, scratch `gp_group.py`). Two costs that are not the game's rendering:
      - **83 feedback copies a frame, 5.0 ms.** The game stamps ~170 ink splats a frame into its ink map (`RG8 6400x6400`, `fs c31c813b`). Each samples the map it is bound to, so the replay copies its region first.
        - The splats write **no colour channel** (write mask 0, stencil only). Even so, `feedback_note_draw` marked each splat's rectangle dirty, so the next splat copied again and broke the pass.
        - Now a draw that writes no channel of an attachment, or has rasterizer discard on, leaves the copy valid (`[nvn] feedback_mask_aware`, default on).
        - Result: 5.6 copies and 0.87 ms a frame.
      - **~670 GPU-ordered writes a frame, 2.6 ms** (`UpdateUniformBuffer`, back to back). Each had a full memory barrier before and after.
        - Now one barrier opens a run of writes. A write over bytes an earlier write of the run touched gets its own barrier. One barrier closes the run before the next op of another kind and at the end of the submission (`[nvn] write_batch`, default on).
    - **Measured, run bc11, 300 s, no profiler.**
      - The wide dialogue view: 52.4 / 54.1 / 58.4 fps, against 40.3 / 31.7 / 33.5 fps in bc10.
      - Gameplay: locked 60 from 161 s to the end, 1% lows 49–54.
      - Run health: no crash, no out-of-memory. The only dip is the area load at ~111 s.
      - The screenshots look the same as bc10 (same scenes, same known stripes).
    - **10-minute soak (run lg1)** stays stable. After the wide view (52–57 fps), it holds 57–60 fps to 600 s, with 1% lows 47–55. Still running at the end: p50 16.6 ms, p99 19.5 ms. No crash, no out-of-memory. Video memory stays at ~4.0 GB used of a 6.5 GB budget; live images stay flat around 1,370.
    - **New diagnostics** from the sky investigation, all off unless set:
      - `[nvn] depth_test_off_fs`, `debug_mask_fs`: by fragment shader hash;
      - `[nvn] depth_ro = false`, `polygon_offset = false`;
      - `[nvn] index_copy = 32`.
      - Per-level BC stand-in creation is now logged.

102. **Beach-sky stripes fixed: unaligned vertex streams are repacked** (2026-11-09, morning; closes milestone 100)
    - **Cause.** The game's ink-able ground layers (the meshes behind the depth pre-pass `vs 44dba4ef / fs 55647ed7` and the colour draws `vs 810284f1 / fs f0fe5663` and `vs 784cd406 / fs 591f39e8`) store their vertices with a **69-byte stride**, and one attribute sits at +65.
      - The Switch GPU fetches vertex attributes at any byte address.
      - Vulkan requires each attribute to be aligned to its component size (4 bytes for floats); anything else is undefined. On the RX 5700 XT the fetch is rounded down, so most vertices of these meshes landed at garbage positions.
      - The garbage triangles became thin full-width slivers. In the depth pre-pass they covered the sky with wrong depth, so the sky dome failed its depth test there, and the never-cleared scene target showed last frame's fog: the stripes.
      - The same cause produced the top-down tutorial view's stripes during its first ~30 s.
    - **How it was found** (runs tr10, vc1, uc1, va1–va4; scratch `cam.py`, `dseq.py`, `stripe.ps1`):
      - With the logged camera (at (−89.9, 4.8, 177.1), looking down 14°), the culprit mesh can only reach the right part of the screen. Yet its draw changed 591k depth pixels, including 254 full-width rows. So the GPU was not using the vertex positions in game memory.
      - New diagnostics, off by default:
        - `[nvn] vertex_copy_fs` copies a draw's vertex streams fresh from game memory.
        - `[nvn] ubo_copy_fs` does the same for its uniform blocks.
        - Neither changed anything, so the data was right and the layout was the problem.
      - The trace now prints each vertex binding's address, size and stride; it showed `stride 69`.
    - **The fix** (`[nvn] vertex_realign`, default on; `nvn_replay.cpp`, `build_key` and `begin_draw`):
      - **Detection.** For each vertex binding of a pipeline, the required alignment is the largest component size of its attributes; a packed format needs its whole size. A binding whose stride or attribute offsets break that alignment gets a repacked layout: each attribute at the next aligned offset, the stride rounded up (69 → 20, 48 or 60 bytes, depending on which attributes the shader reads). The new offsets go into the pipeline key.
      - **Per draw.** The draw binds a copy of the stream in that layout, built on the CPU through the upload ring (18–25 KB per draw here). A stream whose only problem is an unaligned start address is copied as it is.
      - **GPU-written streams.** The game also updates part of these vertices on the GPU, so the "settled" check refused the CPU copy for some draws (the player then turned into a white silhouette, run va2). Those streams are now repacked from the pool bytes as they stand: a GPU-updated byte may be a frame old, and the positions are not. Results held only in the device copy are still excluded.
      - Every distinct repack is logged ("vertex stream realigned"), as is any stream that still cannot be repacked.
    - **Result** (run va4, 120–210 s, screenshots every 10 s):
      - Stripe score (row-to-row minus column-to-column difference) at or below 0.4 in every shot after the first two. The first two are 3.3 and 2.2: real detail, not stripes. Run va1 scored 9–23 on the same scenes.
      - The wide beach view shows a blue sky with clouds, the rock, the floating islands and the moss layers. The tutorial top-down view is clean from its first shot.
      - Frame rate unchanged or better: wide view 56–57 fps (bc11: 52–54), gameplay a locked 60.
    - **10-minute soak with the fix (run lg2):**
      - Frame rate: 57–60 fps in the wide view, then a locked 60 from 140 s to 600 s, with p99 18–24 ms. Run lg1 had two 57-fps windows; lg2 has none.
      - Stability: no crash, no out-of-memory, no stream left unrepacked.
      - All 8 screenshots (one a minute) score at or below 2.3, with no stripes.

103. **Right stick as a camera; Xbox button layout** (2026-11-09, afternoon; user reports, not yet confirmed by the user)
    - **Aim drift fixed.** With `stick_gyro`, the pad has no accelerometer. The runtime's orientation fusion (`pad_orient`) kept pulling the turned virtual pad back toward "lying face up", so the aim slid on its own.
      - Now, when the stick drives the gyro, the fusion has no gravity pull (tilt gain 0).
      - The accelerometer the game reads is computed from the virtual pad's current orientation, so neither the runtime nor the game corrects it anywhere.
    - **New `[pad]` keys (main ini):**
      - `stick_gyro_invert_x`;
      - `stick_gyro_curve` (radial deflection^curve; 1 = linear);
      - `stick_gyro_yaw_axis` (1 = the pad's y axis, the old behaviour; 2 = its z axis).
    - **The user's settings** (`raiders-user.ini`, `raiders-play.ini`; backups `*.bak-20261109-camera`):
      - speed 120 deg/s, down from 200;
      - curve 1.6;
      - both axes inverted: the user reported up/down and left/right swapped.
    - **Buttons.** `controls.ini` `label_abxy = 0` (by position: Xbox A, bottom, is Switch B, and so on), in `do\controls.ini` and `work\raiders\controls.ini` (backups `*.bak-20261109-abxy`).
    - **Tests.** The `[play] selftest` script has a new step, `vpad:<command>`, which drives the test-only virtual SDL pad (`vpad:attach xbox`, `vpad:axis ry -0.3`, ...).
      - Run gy1 confirmed that the stick turns the camera in the "Tilt up slowly" tutorial.
      - A scripted pass through the tutorial is not finished.
    - **Later the same afternoon: the game's own stick camera.**
      - The game has Options > Motion Controls ON/OFF, with Sensitivity, Up/Down and Left/Right per TV/handheld (`Mals/USen` text).
      - With it OFF, the right stick is the game's normal camera. The user inis now have `stick_gyro = off`, so the stick reaches the game.
      - The user found and set the option.

104. **Freezes of 0.4–2 s in new areas fixed: the pipeline key file was appended per key on the render thread** (2026-11-09, evening; user report "choppy")
    - **Measured.**
      - The user's 17-minute session averaged 52–60 fps, with 1% lows of 34–49 and single frames of 50–580 ms.
      - Scripted runs on a copy of the user's save:
        - Run us4 (cache warm): ~60 fps, no freezes.
        - Run us6 (fresh pipeline cache, walking and turning): freezes of 2.0 s, 1.1 s and 0.5 s (×3). Each went with 180–2,081 new pipelines, about 2.7 ms of render-thread replay per new pipeline.
      - Not the cause: frames in flight. With `[latency] frames_ahead = 2`, `gpu_frames_ahead = 2` (run us5) the result was no better.
    - **Cause** (profile us8, new zones in `graphics_pipeline`): `prewarm_record` took 693 ms of every second during a freeze.
      - It appended each new key to `pipeline_keys.bin` by opening the file twice (a size check, then an append), writing one 40-odd-byte record and closing it.
      - It ran on the render thread, under the prewarm lock.
      - On Windows each open/close costs milliseconds (and Defender scans it).
    - **Fix.**
      - New keys are queued in memory.
      - The `nvn-cache-save` thread appends them in one write every 2 s, and at shutdown.
      - The file format is unchanged.
    - **Result** (run us9, same route, fresh cache):
      - Freezes over 50 ms: 15 → 4, all during the first area load (worst 294 ms).
      - From 30 s on: 59–60 fps, 1% lows 38–48, worst frame 30 ms. Before: 42–57 fps, lows 5–45.
      - The key file is still written (287 KB).
    - **Diagnostics.** New `[prof]` zones: `draw.build_key`, `pipe.ensure_cache`, `pipe.prewarm_init`, `pipe.prewarm_take`, `pipe.gpl_ready`, `pipe.prewarm_record`.
    - **Camera, the same evening.**
      - The user's save had Motion Controls OFF on the Handheld Mode page but ON on the TV/Tabletop Mode page, and the game uses the TV/Tabletop settings in this port. So the pitch stayed on the (absent) gyro: the camera pointed down, and left/right still worked.
      - The save is encrypted, so the option cannot be patched in the file. It was switched with a scripted virtual pad on a copy (runs mc1–mc5) and the copy installed into `save-test-user`. Backup: `save-user-backup-20261109-1720`.
      - The working sequence: X (menu), R (Options), down (TV/Tabletop), A, down ×3 (Up/Down is skipped while motion is on), right, right, A, then B, B.
      - Verified (mc5): "Game saved!", a normal third-person camera, right stick up looks up and down looks down.
    - **The user's verdict on the camera:** it "feels so good". The ink no longer looks too shiny; the glare came from the old steep downward view.

105. **Settings menu in Splatoon style, with a camera section** (2026-11-09, evening; compiled, not yet run: the user's game held `sra.exe`)
    - **The game's font.** `gfx/game_font.cpp` reads `romfs/Font/Font.Nin_Ounce_NVN2.bfarc.zs` from the player's own files and decodes `scft/SpMain.bfotf`:
      - zstd: `loader/zstd_plain.cpp`, the bundled decoder built a second time without ZBIC, inside a namespace;
      - SARC;
      - an XOR key taken from the 8-byte header.
      - The overlay uses it as its main font, with the default font merged in for missing glyphs (`[gfx] menu_game_font`, `menu_font_name`).
    - **Theme** (`[gfx] menu_theme = splat`, the default): ink-teal panels, lime selection, orange hover, cream text, rounded shapes.
    - **Controller access.**
      - View + Menu (Back + Start) together opens and closes the menu (`Overlay::feed_gamepad`).
      - While it is open, the pad drives ImGui's gamepad navigation: D-pad or left stick to move, A (bottom) to activate, B to go back, bumpers to switch tabs.
    - **Title hint.** For the first 30 s, "PC settings: View + Menu (or F1)" shows bottom left (`[gfx] menu_hint_s`; left out of screenshots).
    - **Pages** in the order the user asked for: Controls (opens on "Controller and camera"), Graphics, Display and frame rate, Sound, Game, About.
    - **Camera section** (the right-stick layer in front of the game's stick camera):
      - camera sensitivity (`look_sens`);
      - new response curve (`[pad] look_curve`, 1–3);
      - camera dead zone;
      - invert up/down;
      - new invert left/right (`[pad] invert_x`).
    - Afterwards the user set the menu size: `[gfx] menu_scale` (default 1.4), "perfect size".

106. **Video memory over budget in a busy area: 42–50 fps → 60** (2026-11-09, evening; measured in the user's own play)
    - **Measured.**
      - The slow-frame log (`[diag] hitch_ms = 22` in `raiders-user.ini`) showed 30–40 ms of GPU waits per slow frame, with the render thread idle.
      - The profiler, started mid-play by the new `[prof] trace_trigger = "prof_go"` (it records from the moment that file appears), confirmed it.
    - **Cause.**
      - VMA's shared 256 MB blocks: 7.5 GB of blocks held 5.2 GB of images, fragmented by the game's churn of render targets.
      - Video memory usage reached 6,761 MB against a 6,540 MB budget, so Windows paged and the GPU stalled. Windows put the GPU's 3D engine at 74% busy, with the stalls on top.
    - **Fix.** `[gfx] image_dedicated_mb` (default 4): optimal-tiling images of at least that size get a dedicated allocation (size from `vkGetDeviceImageMemoryRequirements`).
    - **Result in the user's play, same area.**
      - 58–61 fps, median 16.7 ms.
      - Usage 3.7 GB (was 6.8).
    - **Also:**
      - the GPU pass profiler's timestamp limit, 1024 → 8192 per submission (the 3D scene was missing from it);
      - image-recycler limits made settings (`[nvn] tex_recycle_keep` 60, `tex_recycle_max` 192, `tex_recycle_mb` 768). The A/B (rc0/rc1) showed no difference: there are no evictions now.
    - **Not done, on the user's word:** letting the game's dynamic resolution see real GPU time (`gpu_timestamp_scale`). The user does not want the resolution lowered.

107. **Uniform-buffer writes merged: the hideout deck from 50–56 fps to a locked 60** (2026-11-09, evening)
    - **Measured** (the user's current save; the scripted walk shows the deck with the camera behind the player):
      - 50–56 fps, median 17–19.4 ms.
      - ~2,600 `UpdateUniformBuffer` writes a frame, each its own `vkCmdUpdateBuffer`.
    - **Fix** (`[nvn] write_merge`, default on):
      - The writes of a run are staged in the upload ring and sent as one `vkCmdCopyBuffer` per (staging, target) buffer pair when the run ends (before any other kind of op, and at the end of a submission).
      - A write over bytes a pending write covers flushes the pending ones first and gets a barrier, as before, so the order of writes is kept.
    - **Result** (run wm1 against rc1, same route and save):
      - 59.8–60.0 fps every 10 s window, median 16.7 ms, 1% lows 44–50 (were 35–40).
      - The screenshots render correctly.
    - **Regression found by the user: swimming in ink stopped working. Fixed.**
      - Cause: a report op writes a placeholder, then copies the query result over it in the same op (`vkCmdCopyQueryPoolResults`). With the placeholder merged, it landed after the copy and zeroed the result, so the game's ink-under-the-squid test read 0.
      - Fix: only `UpdateUniformBuffer` writes are merged (`do_write_memory(..., mergeable)`); report and event writes are recorded at once.
      - The user confirmed swimming works with merging on.
    - **Fullscreen** (user report: F11 and the menu did not give fullscreen).
      - F11 now toggles fullscreen like Alt+Enter (`[gfx] fullscreen_key_f11`).
      - Picking a window mode in the menu applies at once, where it used to wait for Apply.
      - The page follows the mode set by the keys, so a later Apply does not switch the window back.
      - The user confirmed it works.
    - **A whole mission, scripted** (run ms1, the user's save):
      - X menu, Board, the robot's dialogue, then "Scayl Buried Ruins": walking, shooting, a fight, the results, back to the hideout, the treasure map.
      - 60.0 fps in every 10 s window of play (96–240 s and 262–322 s), median 16.7 ms, 1% lows 47–56.
      - The dips are only the mission load (~85 s) and the results/return load (~250 s).
    - **Still open.**
      - Occasional 25–30 ms frames (1% lows ~45).
      - The too-shiny ink.
      - The title-screen settings menu: the game font `SpMain.bfotf` decodes from `romfs/Font/Font.Nin_Ounce_NVN2.bfarc.zs` (SARC, XOR key from the header) to a CFF OpenType font.
