# VolumeDeck - X-Plane Plugin

On-screen volume control for X-Plane without pausing the simulator.

## Features

- 8 volume controls: Master, Exterior, Interior, Pilot, Copilot, Radio, Enviro, UI
- **Third-party add-on channels**, starting with X-ATC-Chatter, in their own group on
  the panel captioned with the plugin they belong to
- Mouse wheel adjustment, or 30 bindable X-Plane commands (keyboard, joystick, Stream Deck)
- Separate interior/exterior volume settings per knob
- Per-channel mute/unmute
- A Settings window (Plugins > VolumeDeck > Settings) for choosing which channels
  appear and which add-ons the plugin is allowed to control
- Two layouts: a vertical column or a horizontal row, switchable at runtime
- Draggable UI, per-aircraft settings
- Works on Linux, Windows, and macOS

## Usage

1. **Show**: Move mouse to upper right corner, click the speaker icon
2. **Adjust**: Mouse wheel over any knob
3. **Toggle mode**: Click a knob to enable separate interior/exterior volumes
4. **Switch layout**: Click the two-bar icon to change between column and row
5. **Move**: Drag the four-way arrow icon to reposition
6. **Save**: Click the floppy icon (red = unsaved changes, green = saved)

The header strip runs right to left: speaker, floppy (save), two-bar (layout),
four-way arrow (drag). Everything except the speaker appears only while the panel
is open.

**Settings** lives in the X-Plane menu bar under *Plugins > VolumeDeck > Settings*.
The same menu has Show / Hide Panel and Save Now.

Each knob sweeps clockwise as the volume rises: minimum at 7:30, midpoint at 12:00,
maximum at 4:30. The amber arc shows how much of the travel is used.

## Third-party add-on support

VolumeDeck can drive volume DataRefs belonging to other plugins, so one panel covers
the whole soundscape rather than just X-Plane's own mixer.

| Add-on | Channel | DataRef | Range |
| --- | --- | --- | --- |
| [X-ATC-Chatter](https://stickandrudderstudios.com/x-atc-chatter/) (SRS) | Chatter | `SRS/X-ATC-Chatter/chatter_volume` | 0.0 - 1.0 |

How it behaves:

- **Detection is continuous, both ways.** Plugin load order is not guaranteed, so
  VolumeDeck keeps checking once a second rather than giving up at startup. An add-on
  enabled in Plugin Admin mid-session is picked up when it appears; one that is
  disabled or unloaded disappears from the panel and from Settings just as quickly.
  Both the owning plugin's enabled state and its DataRef are checked, because a
  disabled plugin can leave its DataRefs registered behind it.
- **Nothing is written until the channel is switched on.** Open Settings to turn an
  add-on channel off and VolumeDeck stops touching that DataRef entirely - no writes
  on load, on a view change, or from a command.
- **Add-on levels are saved globally, not per aircraft**, because chatter volume is a
  property of the add-on rather than of the aeroplane. X-Plane's own channels stay
  per aircraft. One Save writes both.
- Add-on channels get the same `up` / `down` / `mute_toggle` commands as every other
  channel, and they exist in the binding UI whether or not the add-on is installed -
  so a keybinding does not evaporate when you uninstall something.
- If a channel is not detected, its commands do nothing and its knob is not drawn.

Adding another add-on is one row in `src/Channels.h` - slug, label, owner, DataRef
and the range its units use. Everything else (knob, commands, config, settings row)
follows from that entry.

## Commands

The plugin registers 30 custom commands. Bind them to a key or joystick button in
X-Plane's own settings, or trigger them over the local web API (see below) from a
Stream Deck or any other external controller.

Each `up` / `down` step is **0.02** (2%), clamped to the 0.0-1.0 range. Holding a
command down repeats at roughly **10 steps per second**.

### Volume channels

| Command | Description |
| --- | --- |
| `volumedeck/master/up` | Master volume up by 2% |
| `volumedeck/master/down` | Master volume down by 2% |
| `volumedeck/master/mute_toggle` | Mute master, or restore the level it had before muting |
| `volumedeck/exterior/up` | Exterior volume up by 2% |
| `volumedeck/exterior/down` | Exterior volume down by 2% |
| `volumedeck/exterior/mute_toggle` | Mute exterior, or restore the level it had before muting |
| `volumedeck/interior/up` | Interior volume up by 2% |
| `volumedeck/interior/down` | Interior volume down by 2% |
| `volumedeck/interior/mute_toggle` | Mute interior, or restore the level it had before muting |
| `volumedeck/pilot/up` | Pilot volume up by 2% |
| `volumedeck/pilot/down` | Pilot volume down by 2% |
| `volumedeck/pilot/mute_toggle` | Mute pilot, or restore the level it had before muting |
| `volumedeck/copilot/up` | Copilot volume up by 2% |
| `volumedeck/copilot/down` | Copilot volume down by 2% |
| `volumedeck/copilot/mute_toggle` | Mute copilot, or restore the level it had before muting |
| `volumedeck/radio/up` | Radio volume up by 2% |
| `volumedeck/radio/down` | Radio volume down by 2% |
| `volumedeck/radio/mute_toggle` | Mute radio, or restore the level it had before muting |
| `volumedeck/enviro/up` | Environment volume up by 2% |
| `volumedeck/enviro/down` | Environment volume down by 2% |
| `volumedeck/enviro/mute_toggle` | Mute enviro, or restore the level it had before muting |
| `volumedeck/ui/up` | UI volume up by 2% |
| `volumedeck/ui/down` | UI volume down by 2% |
| `volumedeck/ui/mute_toggle` | Mute ui, or restore the level it had before muting |

### Panel

| Command | Description |
| --- | --- |
| `volumedeck/panel/toggle` | Show or hide the on-screen volume panel |
| `volumedeck/panel/save` | Save the current volumes for the loaded aircraft |
| `volumedeck/panel/layout_toggle` | Switch between the column and row layout |

Notes:

- Mute is remembered in memory only. It is not written to the config file, so a muted
  channel comes back unmuted after a sim restart.
- Adjusting a channel with `up` / `down` cancels its mute.
- Commands do nothing for the first few seconds after load, while the plugin probes
  which volume DataRefs this aircraft actually allows writing to.
- A channel whose DataRef this aircraft refuses to write is skipped by its commands.
- Hiding an X-Plane channel in Settings only takes its knob off the panel. Its
  commands keep working, so a keybinding never stops responding without explanation.
  Switching an **add-on** channel off is different: that stops the writes as well,
  because the DataRef belongs to another plugin.

## DataRefs

The plugin creates no DataRefs of its own; it reads and writes X-Plane's built-in
sound DataRefs. All eight are `float`, range **0.0 - 1.0**, and normally writable
(some aircraft override individual ones - the plugin probes for this at startup).

| DataRef | Knob | Description |
| --- | --- | --- |
| `sim/operation/sound/master_volume_ratio` | Master | Master output level. Scales every other channel. |
| `sim/operation/sound/exterior_volume_ratio` | Exterior | Aircraft exterior sounds (engines, airframe) as heard from outside. |
| `sim/operation/sound/interior_volume_ratio` | Interior | Aircraft interior sounds as heard from the cockpit. |
| `sim/operation/sound/pilot_volume_ratio` | Pilot | Pilot voice. |
| `sim/operation/sound/copilot_volume_ratio` | Copilot | Copilot voice. |
| `sim/operation/sound/radio_volume_ratio` | Radio | Radio and ATC audio. |
| `sim/operation/sound/enviro_volume_ratio` | Environment | Environmental and ambient sounds (weather, world). |
| `sim/operation/sound/ui_volume_ratio` | UI | User-interface sounds (clicks, warnings). |

One further DataRef is read (never written), to detect view changes:

| DataRef | Description |
| --- | --- |
| `sim/graphics/view/view_is_external` | `int`, read-only. 0 = interior view, 1 = exterior. Drives the separate interior/exterior volume swap. |

Because a command cannot carry a value, setting an *absolute* level ("master to 50%")
is not expressible as one. Write the DataRef directly instead - no plugin involvement
needed:

```bash
curl -g 'http://localhost:8086/api/v2/datarefs?filter[name]=sim/operation/sound/master_volume_ratio'
curl -X PATCH 'http://localhost:8086/api/v2/datarefs/<id>/value' -H 'Content-Type: application/json' -d '{"data":0.5}'
```

## Controlling it over the web API

X-Plane 12.1.1+ ships a local web server (default port 8086). Commands became
addressable through it in 12.1.4.

Look up a command id by name, then activate it:

```bash
# -g stops curl treating the [ in filter[name] as a glob
curl -g 'http://localhost:8086/api/v2/commands?filter[name]=volumedeck/master/up'

# duration 0 = press and release
curl -X POST 'http://localhost:8086/api/v2/command/<id>/activate' -H 'Content-Type: application/json' -d '{"duration":0}'

# a non-zero duration holds the command down, so it repeats
curl -X POST 'http://localhost:8086/api/v2/command/<id>/activate' -H 'Content-Type: application/json' -d '{"duration":0.5}'
```

**Look commands up by name, not by hard-coded id.** Ids are assigned at load time and
shift when plugins are added or removed.

For press-and-hold buttons the WebSocket interface at `ws://localhost:8086/api/v2` is a
better fit: send `command_set_is_active` with `is_active: true` and no `duration` to
hold, then `is_active: false` to release.
## Releases

Prebuilt plugins are on the [Releases page](../../releases).

Releases are cut by hand, not automatically. The **Build** workflow (Actions tab) can be
run manually with a version like `v1.0`; it builds all three platforms and opens a *draft*
release with the zip attached. The tag is only created, and the release only becomes
visible, when the draft is published. Leaving the version empty just builds and attaches
the zip as a workflow artifact.

## Installation

Extract the release package to X-Plane plugins directory:

```bash
unzip VolumeDeck-v1.0-XPlane.zip
cp -r VolumeDeck "/path/to/X-Plane 12/Resources/plugins/"
```

## Building

### Environment Setup

```bash
export XPLANE_SDK_DIR="/path/to/xplane/SDK"   # must be SDK 4.4.0 or newer
export MACOS_OPENGL_HEADERS_DIR="/path/to/macos-opengl-headers"  # Optional for macOS
```

Build with `-DSDK_VERSION=440`. Older values still compile the CMake project but will
not define `XPLM440`, and the panel-graphics and browser APIs will be missing.

On Windows, build with MinGW-w64 (MSYS2) and the Makefiles generator:

```bash
export PATH=/mingw64/bin:$PATH
export CMAKE_GENERATOR="MinGW Makefiles"
export XPLANE_SDK_DIR="F:/path/to/SDK"
./build.sh
```

#### Extracting macOS OpenGL Headers (legacy)

> Not needed for the current build. The plugin drew with OpenGL until it moved to
> `XPLMPanelGraphics`, and the built `.xpl` no longer imports any GL library. This is
> kept only for reference, and because `toolchain-mac.cmake` still honours
> `MACOS_OPENGL_HEADERS_DIR` if you set it.


To get OpenGL headers for cross-compilation, **on a macOS machine**:

```bash
# Create headers directory
mkdir -p macos-sdk-headers/Frameworks/OpenGL/Headers

# Copy OpenGL headers from macOS SDK
cp -r /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/OpenGL.framework/Headers/* \
     macos-sdk-headers/Frameworks/OpenGL/Headers/

# Package it
tar -czf macos-opengl-headers.tar.gz macos-sdk-headers/

# Transfer to Linux
scp macos-opengl-headers.tar.gz user@linux-machine:~/
```

### Build Commands

```bash
# Single platform (native)
./build.sh

# All platforms (Docker)
./docker-build-all.sh
```

### Docker Build

Builds for all 3 platforms automatically:

```bash
./docker-build-all.sh
```

Output: `dist/VolumeDeck-v1.0-XPlane.zip`

## Configuration

Settings are stored in:
```
X-Plane 12/Output/preferences/VolumeDeck.dat
```

A line-oriented text file (format version 3):

```
VERSION 3
X:2550 Y:1400                  # panel position, omitted while auto-positioned
LAYOUT 1                       # 0 = column, 1 = row
CHANNEL master 1               # one per channel: is it on the panel / controlled
CHANNEL xatc_chatter 1
ADDON xatc_chatter 0.45 -1     # add-on levels: global, interior + exterior
Cessna_172SP.acf 0.04 -1 ...   # per aircraft: 8 interior/exterior pairs
```

Two scopes on purpose: X-Plane's channels are saved per aircraft, add-on levels once
for everything. The Plugins menu says so, so the split is not a surprise after the
fact.

Saving is manual - click the floppy icon, use *Save now* in Settings, or run
`volumedeck/panel/save`. Mute state is deliberately not saved, so a muted channel
comes back unmuted after a restart.

A version 2 file (no `CHANNEL` / `ADDON` lines) loads unchanged and is rewritten in
the new format on the next save. Unknown lines are ignored, so a file written by a
newer build does not break an older one.

## Known behaviour

Disabling and re-enabling VolumeDeck in Plugin Admin is safe: the flight loop is
stopped and restarted rather than duplicated, a dragged panel keeps its position, and
any add-on writability probe in flight puts the channel's level back before standing
down.

## Requirements

- **Build**: CMake 3.16+, C++17 compiler
- **Runtime**: X-Plane **12.4.4 or newer**
- **SDK**: X-Plane SDK 4.4.0 (XPLM440)

The plugin draws with the `XPLMPanelGraphics` API introduced in SDK 4.4.0, so it will
not load on X-Plane 12.4.3 or earlier. Text is rendered with `Roboto-Regular.ttf` from
X-Plane's own `Resources/fonts`, so there is nothing extra to install.

## Credits

This plugin stands on two earlier projects:

- **[B2VolumeControl.lua](https://github.com/B2VideoGames/B2VolumeControl)** by
  **[B2videogames](https://github.com/B2VideoGames)** — the original FlyWithLua script whose
  UI layout and per-aircraft configuration behaviour this plugin reproduces.
- **[volume-control-xplane-plugin](https://github.com/verres1/volume-control-xplane-plugin)**
  by **[verres1](https://github.com/verres1)** — the original C++ port of that script, which
  this repository continues from.

Changes in this fork: rendering moved from OpenGL to the `XPLMPanelGraphics` API (SDK 4.4.0),
boxel-correct coordinates for scaled displays, 27 bindable X-Plane commands, per-channel mute,
and a switchable column/row layout.

## AI assistance

Much of this fork was written with **Claude** (Anthropic), via Claude Code — including
the port from OpenGL to `XPLMPanelGraphics`, the pixel/boxel coordinate fix, the command
layer, and the knob rendering. The work was directed, reviewed and tested by a human
against X-Plane 12.4.4 on Windows; nothing here was merged unverified.

Flagging this because you are about to load a binary into your simulator and deserve to
know how it was produced. The Linux and macOS builds in particular have **not** been
run by anyone — see Requirements.

## License

[MIT](LICENSE), inherited from the projects credited above.
