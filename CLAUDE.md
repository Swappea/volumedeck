# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A C++ X-Plane plugin (`.xpl`) that draws an on-screen volume panel with 8 knobs, adjusted by mouse wheel, without pausing the sim. It is a C++ port of `B2VolumeControl.lua`; the original Lua behavior is the spec for UI layout and config semantics.

## Build

`XPLANE_SDK_DIR` must point at the X-Plane SDK root (the directory containing `CHeaders/` and `Libraries/`) or CMake fails at configure time. **Point it at `./SDK` and build with `-DSDK_VERSION=440`** — the plugin draws with `XPLMPanelGraphics`, which only exists in SDK 4.4.0, so it requires X-Plane 12.4.4 or newer and will not load on anything earlier. The in-tree copy is SDK 4.4.0-b1 and is gitignored — it is present in the working tree and absent from the repo, so a fresh clone has to supply its own. `SDK_VERSION` gates which `XPLM*` macros get defined; 440 is the only value that compiles the current source, since the drawing layer needs `XPLM440`.

The build scripts are POSIX shell and assume a Linux host. On Windows, `docker-build-all.sh` will not work as written (`sudo chown`, POSIX bind-mount paths). The `WIN32` branch of `CMakeLists.txt` passes GCC-style link flags (`-static-libstdc++`, `-Wl,--allow-multiple-definition`), so a native Windows build needs MinGW-w64 (e.g. MSYS2), not MSVC — `cl.exe` would reject those flags.

```bash
export XPLANE_SDK_DIR="$(pwd)/SDK"   # SDK 4.4.0-b1, in-tree but gitignored

./build.sh                # native build → build/<platform>_x64/VolumeDeck.xpl
./docker-build-all.sh     # all 3 platforms via Docker → dist/VolumeDeck-v1.0-XPlane.zip
./create-package.sh       # package whatever is already in build/ into dist/
./install.sh "/path/to/X-Plane 12"   # copy native build into the sim's plugins dir
```

There are no tests and no linter. Verification is: build cleanly, install, launch X-Plane, and read the `XPLMDebugString` output in `X-Plane 12/Log.txt` (every code path logs with a `VolumeDeck: [TAG]` prefix).

Before blaming the plugin for a load failure, check the sim exports the API:
`nm -g "$XP/Resources/plugins/XPLM.framework/XPLM" | grep _XPLMCreateFont` (macOS).
12.4.3 has 298 XPLM exports and no panel-graphics API at all; 12.4.4 has 401.

`SDK_VERSION` defaults to 440, which is the only value the current source compiles with, so a bare `cmake ..` works. Anything lower fails at `unknown type name 'XPLMFontHandle'`.

### Windows native build (MSYS2 / MinGW-w64)

Verified working recipe — run from an MSYS2 shell with `/mingw64/bin` on `PATH`:

```bash
export PATH=/mingw64/bin:$PATH
export CMAKE_GENERATOR="MinGW Makefiles"
export XPLANE_SDK_DIR="$(pwd -W)/SDK"   # native Windows path, not /f/...
cmake -S . -B build -DSDK_VERSION=440 && cmake --build build   # → build/win_x64/VolumeDeck.xpl
```

Three Windows-specific gotchas, all of them load-bearing:

- **Generator must be Makefiles, not Ninja.** `add_xplane_plugin()` puts the output subdirectory inside `OUTPUT_NAME` (`win_x64/VolumeDeck.xpl`) rather than using `LIBRARY_OUTPUT_DIRECTORY`. The Ninja generator turns that into a self-referential phony edge and dies with `multiple rules generate win_x64/VolumeDeck.xpl`. Makefiles tolerate it. Fixing it properly means switching to `LIBRARY_OUTPUT_DIRECTORY` plus a plain `OUTPUT_NAME`, which changes the artifact path logic on all three platforms.
- **`XPLANE_SDK_DIR` must be a `F:/...` style path, not `/f/...`.** The MinGW CMake is a native Windows binary and does not understand MSYS mount paths.
- **`$OSTYPE` is `cygwin`** under the current msys2-runtime, not `msys`. `build.sh` and `install.sh` test for it explicitly.

A good build is a PE32+ x86-64 DLL exporting `XPluginStart/Enable/Disable/Stop/ReceiveMessage`, whose only imports are `KERNEL32`, `msvcrt` and `XPLM_64.dll`. Check with `objdump -p build/win_x64/VolumeDeck.xpl`. Any `libstdc++-6.dll` or `libgcc_s_seh-1.dll` import means the static link flags were dropped, and the plugin will fail to load on machines without MSYS2.

### Cross-compilation

`docker-build-all.sh` builds the `Dockerfile` image (Ubuntu 22.04 + MinGW-w64 + OSXCross with the macOS 12.3 SDK), mounts the repo at `/workspace` and the SDK read-only at `/xplane-sdk`, then runs `docker-build.sh` inside it. `docker-build.sh` is the in-container script — it assumes `/workspace` and is not meant to be run on the host.

- Windows target: `toolchain-win.cmake` (MinGW). Links `-static -static-libstdc++ -static-libgcc` so the `.xpl` has no runtime DLL dependencies.
- macOS target: `toolchain-mac.cmake` (OSXCross, `o64-clang++`). The macOS leg is allowed to fail — `docker-build.sh` continues with Linux+Windows. Optional `MACOS_OPENGL_HEADERS_DIR` supplies real OpenGL framework headers extracted from a Mac (see README); without it the OSXCross defaults are used.

`CMakeLists.txt` falls back to INTERFACE (stub) `xplm`/`xpwidgets` targets when the SDK libraries aren't found, which happens under cross-compilation. This is deliberate — X-Plane resolves XPLM symbols at load time (mac bundles use `-undefined dynamic_lookup`, Linux uses `-nodefaultlibs -rdynamic`), so a link that appears to have no XPLM library is still correct.

Output naming is imposed by `add_xplane_plugin()`: for SDK ≥ 300 the artifact is `<platform>_x64/VolumeDeck.xpl` with empty PREFIX/SUFFIX, which is the directory layout X-Plane 11/12 expects inside `Resources/plugins/VolumeDeck/`.

## Architecture

Four files, split by X-Plane SDK role:

- `src/main.cpp` — the SDK entry points (`XPluginStart/Enable/Disable/Stop`) and all input callbacks. On enable it creates a single full-screen, undecorated `xplm_WindowLayerFloatingWindows` window purely as a mouse/wheel sink; it draws nothing.
- `src/VolumeDeck.h/.cpp` — a singleton (`VolumeDeck::getInstance()`) holding all UI state, the 8 knobs, config I/O, and the drawing layer built on `XPLMPanelGraphics`.
- `src/VolumeCommands.h/.cpp` — the 27 custom X-Plane commands. Self-contained: a static binding table plus one shared handler, talking to `VolumeDeck` only through its public methods.
- `src/main.cpp` and `VolumeDeck` are coupled through the small public surface on the class (`toggleControlBox`, `adjustKnobVolume`, `isMouseOverKnobPublic`, `isOver*Icon`, drag methods). `main.cpp` does **no coordinate arithmetic** — every hit test lives on `VolumeDeck` beside the code that draws the thing, so art and click target cannot drift apart. Adding a clickable element means a draw call plus an `isOverX()` method, then one call in `MouseClickHandler`.

### Two callbacks, different jobs

- `DrawWindowCallback` (in `main.cpp`, the window's own draw function) does all rendering. There is **no** `XPLMRegisterDrawCallback` — direct drawing is deprecated and runs in pixel coordinates, which cannot agree with the boxel coordinates the window's mouse events use.
- `flightLoopCallback` (1 Hz, `xplm_FlightLoop_Phase_AfterFlightModel`) handles screen-resize, the startup probe, view transitions, font retries, and calls `syncKnobFromDataRef()` for every knob so cached state stays correct **even while the panel is closed**. `drawKnob()` alone would only sync while visible, and `updateVolumesForViewChange()` would then write stale values back over a command-driven change.

### Commands

27 commands named `volumedeck/<channel>/{up,down,mute_toggle}` plus `volumedeck/panel/{toggle,save,layout_toggle}`. Channel slugs must stay in step with `knobNames[]` in `VolumeDeck.cpp` and `CHANNELS[]` in `VolumeCommands.cpp` — they are two hand-maintained lists of the same eight strings.

Lifecycle is split deliberately, per the note in `XPLMUtilities.h:555` that commands outlive the plugin that created them: `create()` in `XPluginStart` (so they reach the binding UI and web API even before enable), `registerHandlers()` in `XPluginEnable`, `unregisterHandlers()` in `XPluginDisable`. Skipping the unregister leaves a dangling handler across a plugin reload.

One handler serves all 27; the refcon is a `CmdBinding*` into a static table, which also stores each command's own repeat deadline. Two timing rules matter:

- `xplm_CommandContinue` fires *every frame*, so `CMD_ADJUST` throttles to `REPEAT_INTERVAL` (0.1s) via `XPLMGetElapsedTime()`. `Begin` always steps once immediately so a tap stays responsive.
- Every handler returns early while `isReady()` is false, i.e. during the ~3s startup probe, which would otherwise overwrite whatever the command just set.

Commands cannot carry a value, so "set master to 50%" is not expressible as one. That case is served with no plugin code at all by `PATCH /api/v2/datarefs/{id}/value` against `sim/operation/sound/*_volume_ratio`.

Driving them over the local web API (12.1.4+, and this repo is developed against 12.4.4):

```bash
curl -g 'http://localhost:8086/api/v2/commands?filter[name]=volumedeck/master/up'
curl -X POST 'http://localhost:8086/api/v2/command/<id>/activate' -d '{"duration":0}'
```

`-g` is required — curl otherwise parses the `[` in `filter[name]` as a glob and fails with exit 3. `duration:0` is press-and-release; a non-zero duration holds the command down, which is what exercises the repeat path.

### Knob rendering

A knob is a solid face, a dark track arc over the full travel, an amber arc from the
minimum to the current value, a pointer, and a hub. It is deliberately **not** a filled
arc: `drawFilledArc()` fills from the centre, so a sweep with a gap produces a wedge cut
out of the disc — the "Pac-Man" look the original had.

`knobAngle(v)` is `225° − v × 270°`, normalised. GL-style angles increase
counter-clockwise, so subtracting makes the pointer sweep **clockwise** as volume rises:
7:30 at 0.0, 12:00 at 0.5, 4:30 at 1.0, with the ring gap at the bottom. The original
`v * 300 + 210` swept the opposite way, which reads as backwards against every physical
knob.

Text uses `Roboto-Regular.ttf` from X-Plane's own `Resources/fonts` via `XPLMGetSystemPath`,
so nothing is bundled. `VolumeKnob::displayName` holds the capitalised label (`ui` becomes
`UI`, not `Ui`); `name` stays lowercase because it is the command-slug identity.
### Knob model and the `exteriorVolume` sentinel

Each `VolumeKnob` wraps one `sim/operation/sound/*_volume_ratio` dataref. `exteriorVolume` is overloaded and is the central piece of state to understand before touching volume logic:

- `>= 0` — separate interior/exterior mode; `interiorVolume` and `exteriorVolume` are both live and swapped into the dataref on view change.
- `KNOB_SINGLE_MARK` (-1) — single-value mode; only `interiorVolume` is used.
- `KNOB_FAILED_TEST` (-2) — the dataref is not writable for this aircraft. Such knobs are drawn differently, refuse mode toggling, and never set `saveRequired`.

The -2 state is produced by a three-stage probe driven by `initialTestStage` in the flight loop: stage 1 stashes the current volume and writes `0.03125`, stage 2 (next second) reads back to see whether the write stuck, restores the original, and records the result; stage 3 loads the config. Config loading is deliberately last so it overwrites the probe's sentinels for knobs that did work.

Two invariants hold this together; breaking either greys out all eight knobs:

- **Stage 1 parks the real volume in `exteriorVolume`** — a fourth meaning on top of the three above. It is `>= 0`, so `updateVolumesForViewChange()` reads it as split mode and writes it back over the test value. The flight loop's view-change block is therefore guarded by `initialTestStage == 0`, like the `syncKnobFromDataRef` loop above it.
- **`KNOB_FAILED_TEST` must never be persisted.** It describes one run's probe, not a user setting, and config load runs *after* the probe — so a saved `-2` overrides a good probe result on every later launch, permanently. `saveConfig()` writes it as `KNOB_SINGLE_MARK`; `loadConfig()` collapses any negative value from disk to `-1`.

### Config file

`X-Plane 12/Output/preferences/VolumeDeck.dat`, a line-oriented text format: `VERSION <n>`, an optional `X:<x> Y:<y>` panel position, then one line per aircraft — the `.acf` filename followed by 8 `interior exterior` float pairs. Save rewrites the whole file, preserving other aircraft's lines. `FILE_FORMAT_VERSION` is 2; a loaded file with version ≤ 1 leaves `saveRequired` set so the entry is rewritten in the current format. Saving is manual — the user clicks the floppy icon or runs `volumedeck/panel/save`.

`loadConfig()` falls back to `getLegacyConfigPath()` (`VolumeControl.dat`) when
`VolumeDeck.dat` does not exist, so settings survive the rename from the plugin this
forked from. It is read-only and flags `saveRequired` so the first save writes the new
name; the old file is never modified or deleted. Do not remove this until the upstream
plugin is long gone.

An optional `LAYOUT <0|1>` line stores the chosen layout. It needed no format bump because the parser already skips lines it does not recognise — new optional keys can be added the same way. Mute state is deliberately *not* persisted, which is why `preMuteVolume` stays out of the file.

### Coordinate system

Everything is in **boxels**, never pixels. A boxel is a virtual pixel that X-Plane scales by the user's UI scale, so on a 3840x2160 display at 1.5x scaling the desktop is 2560x1440 boxels. Modern windows are positioned in boxels and their mouse events arrive in boxels, while the legacy `XPLMGetScreenSize()`/`XPLMGetMouseLocation()` return pixels.

Mixing the two is the bug this code was built wrong around originally: the panel was placed from pixel coordinates (`mainX` = 3830) while clicks arrived in boxels (max 2560), so the hit tests were mathematically unreachable and every click fell through to the sim. Use `XPLMGetScreenBoundsGlobal()` and `XPLMGetMouseLocationGlobal()` only — `readScreenBounds()` wraps the former.

All UI positions derive from `mainX`/`mainY`, the anchor at the bottom-right of the speaker icon, Y increasing upward. `updateKnobPositions()` recomputes `knob.x/y` and runs only from `drawControlPanel()`, so hit tests are valid only for knobs that have been drawn at least once.

The header icon strip is laid out by the shared constants `ICON_SOUND_W`, `ICON_SAVE_CX`, `ICON_LAYOUT_CX`, `ICON_DRAG_CX`, `ICON_HALF`, `DRAG_HALF`. Both the art and the `isOver*Icon()` hit tests read them, so the two cannot drift — do not reintroduce literal offsets.

`autoPosition` snaps the panel to the top-right corner and follows screen-size changes; dragging clears it, and dragging back near the corner restores it.

Two layouts share all of this. `LAYOUT_VERTICAL` is a column with labels in a strip to the left of each knob; `LAYOUT_HORIZONTAL` is a row with labels centred underneath. `panelWidth()`/`panelHeight()` and `updateKnobPositions()` branch on `layout`, and `isMouseOverKnob()` only includes the label strip in the vertical case.

Row cells are sized by `horizontalCell()`, which measures all eight labels with `XPLMFontMeasureString` and takes the widest. Do not replace it with a fixed pitch: a constant narrower than the widest label makes adjacent labels collide ("InteriorExteriorMaster").

## Releases

`.github/workflows/build.yml` is dispatch-only and **never publishes on its own** - there
is deliberately no tag trigger. Run it from the Actions tab:

- no `version` input: builds all platforms, attaches the zip as a workflow artifact.
- `version` set (e.g. `v1.0`): also opens a **draft** release with the zip attached. The
  git tag is not created until a human publishes the draft, so the description is always
  written by hand.

Jobs build Linux and Windows (MinGW cross, matching `toolchain-win.cmake`) on Ubuntu, and
a universal macOS binary on macos-latest. The macOS job is `continue-on-error` because no
human has ever run that binary in the sim.

`.github/fetch-sdk.sh` downloads the SDK per job, since it cannot be vendored (see
`LICENSE`), and fails early if the SDK predates XPLM440 rather than dying mid-compile.
Bump `SDK_URL` in the workflow when a newer SDK is needed.

The Windows job **fails the build** if `libstdc++`/`libgcc` appear in the binary's imports.
That is the regression that would make the plugin silently refuse to load on machines
without MSYS2, and it is invisible without the check.

Two traps worth remembering: `core.filemode` is false on Windows, so any new script —
under `.github/` or at the repo root — needs `git update-index --chmod=+x`, or CI fails
with "Permission denied" and `./build.sh` fails the same way on a fresh clone; and
the tag a draft creates points at the commit CI actually built, not at whatever `main`
has drifted to by publish time.

A draft cannot be marked Latest (`422: Latest release cannot be draft or prerelease`).
Publish first, then `gh release edit <tag> --latest` if the badge has not moved.

## Conventions

- Every callback body is wrapped in `try { ... } catch (...)` that logs and swallows. An exception escaping into X-Plane's callback would take down the sim, so keep this pattern for any new callback.
- Log with `XPLMDebugString` using the existing `VolumeDeck: [TAG]` prefixes (`[INIT]`, `[LOOP]`, `[CONFIG]`, `[ENABLE]`, `[ERROR]`) — this is the only debugging channel.
- Drawing goes through `XPLMPanelGraphics` via the helpers on the class (`drawFilledCircle`, `drawArc`, …). There is no global colour or line-width state as there was in GL, so `setColor()`/`setLineWidth()` stash values that each primitive passes along.
- **Never create or destroy a panel-graphics resource inside the draw callback.** `XPLMCreateFont` there is a hard runtime violation that takes the sim down with "Never call this function from within a panel draw callback" — it is not documented in the headers or the SDK docs tree, only enforced at runtime. The font is built in `initialize()` and retried from the flight loop; `drawString()` skips text when it is missing rather than creating one.
- `CMakeLists.txt` globs `src/*.cpp`, so new source files need no build edits, but a fresh CMake configure.
- **`XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1)` sits under `#if APL` in `XPluginStart` and must stay ahead of any path call.** Without it macOS returns HFS paths (`Macintosh HD:Users:…`); handing one back to X-Plane (e.g. `XPLMFontAddFace`) is fatal, not an error return — it downs the sim and blames the plugin. Deliberately not enabled on Windows/Linux: Linux is identical either way, and Windows would switch to `C:/…`, changing builds that work today.
- `CMakeLists.txt` still links `opengl32` (Windows) and the OpenGL framework (macOS) from the pre-panel-graphics era. Harmless leftovers — no GL symbols are referenced any more, and `objdump -p` confirms the built `.xpl` imports only `KERNEL32`, `msvcrt` and `XPLM_64.dll`.
