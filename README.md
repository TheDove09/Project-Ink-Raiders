# Project: Ink Raiders

A native Windows port of *Splatoon Raiders* (Nintendo Switch 2): the game's ARM64 code is recompiled to x64 ahead of time, and its
NVN graphics calls run on Vulkan. It is not an emulator.

**This repository contains no game code, assets or keys.** You need your own copy of the game, dumped from your own console. The
game's code and data are read from your dump on your PC, at build time and at run time, and are never redistributed.

## Status

- Playable from the title screen through the hideout and missions, at 60 fps on the test machine (Ryzen 5 5500, Radeon RX 5700 XT):
  a scripted mission holds 60.0 fps, with brief dips only while an area loads.
- The right stick drives the game's own camera (set the game's Options > Motion Controls to OFF). Xbox, PlayStation and Switch pads work.
- A start menu ("Bring your own game") and a settings menu (F1, or View + Menu on a controller) drawn in the game's own style from your files.
- Known issues and the full engineering log: [PORT_STATUS.md](PORT_STATUS.md). A short summary of the latest state: [HANDOFF.md](HANDOFF.md).

## What you need

- Your own dump of *Splatoon Raiders* with the 1.1.0 update: a `.nup` file (or `.nsp` / `.xci` plus your console's `prod.keys`).
- Windows 10 or 11, a GPU with Vulkan 1.3 (AMD, NVIDIA or Intel), and about 20 GB of free space.

## Building

The build recompiles the game's code from your dump, so it can only be done with your own files.

1. Install the build tools: Visual Studio 2022 C++ Build Tools (with the LLVM/Clang component), CMake, Ninja, Python 3.13, the .NET 8
   SDK, Rust and Node.js 24 or newer.
2. Extract the ExeFS and RomFS from your dump into a working folder outside this repository, for example:

   ```powershell
   node tools/raiders/extract-exefs.mjs <your-game.nup> <work>\raiders\exefs-1.1.0 update exefs
   node tools/raiders/extract-exefs.mjs <your-game.nup> <work>\raiders\game\romfs update romfs
   ```

   The extractor only reads your file and checks every extracted file's hash.
3. Recompile the game's modules, then build the program:

   ```powershell
   tools\raiders\build-modules.ps1
   tools\raiders\build-native.ps1 -Jobs 8
   ```

The program is `InkRaiders.exe` in the build folder. Start it with your game files:

```powershell
InkRaiders.exe --game <work>\raiders --exefs <work>\raiders\exefs-1.1.0 --ini <your settings ini>
```

## Controls

- Controller: as on the console. Buttons follow their position by default (the bottom button is Switch B); the settings menu can switch
  to "by label".
- Keyboard and mouse: see the settings menu, page Controls.
- F11 or Alt+Enter: fullscreen. F1, or View + Menu together on a controller: the settings menu.

## License

The port's own code is licensed under GPL-3.0 ([LICENSE](LICENSE)); third-party components keep their own licenses (see their folders).
*Splatoon* and *Nintendo Switch 2* are trademarks of Nintendo. This project is not affiliated with or endorsed by Nintendo. See
[LEGAL.md](LEGAL.md).
