# Recomp Launcher

A desktop library for Switch 2 `.nup` packages and the native games built from them. It wraps the recompile pipeline
(`tools/pipeline/recomp_pipeline.py`): find your `.nup` files, press **Recompile**, then **Play**.

Tauri 2 (Rust backend, WebView2) with a React + TypeScript interface; about 10 MB.

## What it does

- **Library**: scans folders (Downloads by default; add an external drive in Settings, four folders deep) and files added by drag and
  drop or **Add .nup**. Each title appears once, merged with the games already built in the games folder.
- **Supported / Experimental**: titles with a hand-made profile (Splatoon Raiders) are Supported; others get a generated profile and
  are Experimental (they build; whether they play depends on the runtime).
- **Recompile**: runs the pipeline; the card and the detail panel show the stage (Copying to SSD → Unpacking code → Game profile →
  Translating ARM64 → Linking native exe → Unpacking assets → Finishing), a progress bar, and a collapsible log. One build at a time,
  others queue. Cancel stops the whole build tree.
- **Storage**: a `.nup` on another drive is copied to the work folder (SSD) first; temporary build files are deleted afterwards unless
  "Keep temporary build files" is on; each game ends up in its own folder in the games folder.
- **Play / Open folder / Set cover**: custom cover images are copied into `%LOCALAPPDATA%\RecompLauncher\covers`.
- **Game settings** (built games): display (window / borderless / exclusive fullscreen), console mode (handheld / docked), frame cap,
  VSync, resolution scale, save data folder. Written where the game reads them (`controls.ini` [display], `<profile>.ini` [am] / [fs]),
  changing only those keys.
- **Controller**: D-pad / left stick move between cards, A opens, B closes; arrow keys and Enter / Escape work too.

Settings live in `%LOCALAPPDATA%\RecompLauncher\settings.json`.

## Building

Needs Rust (stable, MSVC), Node.js 24 with npm, and WebView2 (part of Windows 11).

```powershell
cd apps\launcher
npm install
npx tauri build --no-bundle      # -> src-tauri\target\release\recomp-launcher.exe
npx tauri dev                    # live-reload development
```

`npm run dev` alone serves the interface in a browser with a simulated backend (`src/devmock.ts`: sample games and a fake build), for
working on the UI without the app.

## Not yet

Automatic cover art and metadata (NUPs carry no icon or name, so this needs an online source by title ID) and an installer.
There is no OpenGL option: the runtime renders with Vulkan only.
