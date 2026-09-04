# OutRun2006Tweaks — FFB Fork

> **Fork of [emoose/OutRun2006Tweaks](https://github.com/emoose/OutRun2006Tweaks)** adding force feedback and telemetry for steering wheels. Part of the [OutRun 2006 Redux](../outrun2006-redux) project.

## FFB Fork Changes

### Force feedback (`src/hooks_dinputffb.cpp`)

The PC port ships no force feedback at all. This fork reads the game's own
physics out of `EVWORK_CAR` and drives a wheel from it.

Forces are produced through **DirectInput**, in `WheelFfb.dll` from
[dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
(vendored under `lib/toolkit`, pinned in `lib/toolkit/VERSION`). SDL3's haptic
API was tried first and abandoned: `SDL_UpdateHapticEffect` is silently ignored
by MOZA and other direct-drive bases, and destroying and recreating an effect at
60 Hz just vibrates.

The model is **centre-out**. A virtual spring on steering position is the
backbone — it is what the arcade cabinet's mechanical centring did — damped by
the game's own steering derivative. Lateral road load is a *secondary* term that
**lightens** as grip is lost, rather than the whole force, which had the
relationship backwards: the wheel was heaviest exactly when the tyres let go.

| Channel | How it is rendered | Source |
|---|---|---|
| Centring spring | constant force | steering position × speed curve |
| Damper | constant force | the game's steering derivative |
| Cornering load | constant force | lateral slide, × grip factor |
| Weight transfer | modulates the above | speed delta over 6 frames |
| Wall impact | constant-force impulse | speed loss + contact flag, direction from 8 frames before the hit |
| Gear shift | symmetric double pulse | gear change; nets to zero, because a real shift jolt is longitudinal |
| Road texture | hardware `GUID_Sine` | the game's own decompiled surface LUT × speed |
| Tyre slip / idle | hardware `GUID_Sine` | drift depth, or throttle at a standstill |

Vibration runs on hardware periodic effects because synthesising 25–40 Hz
through 60 Hz constant-force updates loses about a quarter to zero-order-hold
roll-off and more again to the compressor. If the driver offers no periodics,
a constant-force fallback synthesises them, capped at 15 Hz.

### Telemetry

A 311-byte Forza "Data Out" packet to `127.0.0.1:8000` (SimHub, MOZA Pit House)
plus a shared-memory struct. RPM is synthesised — OutRun exposes none. Throttle
and brake come from the input remap layer's own reads, so they are true pedal
travel; when no wheel is bound they are reported as absent rather than as zero,
which a brake light would read as "released".

### Installing

Copy **both** DLLs next to `OR2006C2C.EXE`:

```
dinput8.dll        the mod
WheelFfb.dll       the force-feedback device layer
```

`WheelFfb.dll` is loaded at runtime, so if it is missing the game still starts —
it just has no force feedback, and says so in the log. The build copies it next
to `dinput8.dll` automatically.

You also need the [x86 VC redist](https://aka.ms/vs/17/release/vc_redist.x86.exe).

### Configuration (`[FFB]` section in `OutRun2006Tweaks.ini`)

```ini
[FFB]
DirectInputFFB = false      ; master enable
FFBDevice = -1              ; -1 = the device the input remap layer chose
FFBGlobalStrength = 1.0     ; overall scale
FFBSpringStrength = 0.45    ; centring spring on steering position
FFBDamperStrength = 0.10    ; damper on the steering derivative
FFBSteeringWeight = 0.55    ; lateral road load (the secondary term)
FFBGripLoss = 0.6           ; how much the wheel lightens in a drift, 0-1
FFBWeightTransfer = 0.8     ; brake/throttle load modulation
FFBLateralDeadzone = 1.5    ; lateral noise-floor clip
FFBWallImpact = 1.0         ; collision jolt
FFBGearShift = 0.3          ; shift thunk
FFBRoadTexture = 0.6        ; periodic: surface roughness
FFBTireSlip = 0.35          ; periodic: drift chatter
FFBEngineIdle = 0.08        ; idle/launch rumble
FFBUsePeriodicEffects = true ; false forces the constant-force fallback
FFBInvertForce = false
FFBDiagnosticLog = false    ; signal ranges every 2 s - see below
```

### Known issue: the steering field is unverified

`FFBSpringStrength` and `FFBDamperStrength` are almost certainly doing far less
than their values suggest. The field read as steering position (`field_1D0`)
never exceeds 0.011 in the validation log, while the field read as its
derivative (`field_1D4`) reaches 0.54 — a derivative cannot outrun its own
integral by fifty times, so at least one label is wrong.

Set `FFBDiagnosticLog = true` and drive one lap. The `FFB STEERSCAN` line
reports the observed range of the six candidate fields; the steering one is
whichever reaches about ±1, changes sign with the corner, and returns to zero on
the straights. Do not tune the spring or damper before that.

### Architecture note

Force-feedback initialisation is **deferred to the first `GamePlCar_Ctrl` tick**,
not done in `DllMain`: DirectInput needs a valid window handle, and the loader
lock forbids most of what setup wants to do. The same rule is why teardown only
zeroes the wheel and does not release COM objects — that path reliably faulted
and used to swallow the autocentre restore.

The wheel is opened on its **own exclusive handle**, selected by the DirectInput
instance GUID the remap layer already chose, while that layer keeps polling the
same device non-exclusively. Sharing one handle meant a poll-side re-`Acquire()`
destroyed the downloaded effects mid-corner.

---

*Original README follows:*

# OutRun2006Tweaks
[![GitHub Downloads](https://img.shields.io/github/downloads/emoose/OutRun2006Tweaks/total)](https://github.com/emoose/OutRun2006Tweaks/releases)

A wrapper DLL that can patch in some minor fixes & tweaks into OutRun 2006: Coast 2 Coast.

Latest builds can be found under the releases section: https://github.com/emoose/OutRun2006Tweaks/releases

**Tweaks will also point the game to new multiplayer servers**, just head to the multiplayer section in-game and pick a username & password there!

Online games are regularly setup on the OutRun2006Tweaks discord: https://discord.gg/GFjKAMg83t

### Features
**Graphics:**
- UI can now scale to different aspect ratios without stretching
- Game scene & UI textures can be extracted from game, and replaced with higher-resolution versions
- Allows disabling vehicle LODs, reducing the ugly pop-in as they get closer
- Fixed Z-buffer precision issues that caused heavy Z-fighting and distant object pop-in
- Lens flare effect now loads from correct path without needing to change game files
- Stage objects such as traffic cones now only disappear once they're actually off-screen
- Fixes certain effects like engine backfiring which failed to appear when using controllers
- Anisotropic filtering & transparency supersampling can be forced, greatly reducing aliasing around the edges of the track
- Reflection rendering resolution can be increased from the default 128x128
- Restores the car base shadow from the C2C console ports, which was missing on PC for some reason
- Allows using higher-quality models for Alberto/Clarissa/Jennifer, which were otherwise left unused

**Gameplay:**
- Points game toward new online servers, restoring the online multiplayer modes
- Restored XInput rumble code from the Xbox release can be enabled inside INI, allowing gear shifts/drifts/crashes/etc to give feedback
- Xbox Series impulse triggers are supported and can be tweaked inside INI
- Steering deadzone can be customized from the default 20%
- Horn button can be made functional during normal gameplay, outside of the "honk your horn!" girl requests
- Allows randomizing the set of highway animations to use, instead of only using the set for the game mode being played
- In-game HUD can be optionally toggled via bindable keypress
- Manual Transmission (MT) can be set as the default for C2C menus
- Passing all the C2C missions might unlock something new 🐱

**Bugfixes:**
- Built-in framelimiter to prevent speedups, framerate can be partially unlocked with game running at 60FPS internally
- Prevents save corruption bug when remapping controls with many input devices connected
- Fixed C2C ranking scoreboards not updating on Steam and other releases due to faulty anti-piracy checks
- Pegasus animation's clopping sound effect will now end correctly
- Text related to the online service can optionally be hidden
- Automatically disables DPI scaling on the game window to fix scaling issues
- Fixes issues with shading on certain character/stage models (eg. the ending cutscene models)
- Allows particles like grass/gravel to be drawn correctly, like in the console versions
- Game can be forced to run on a single core, to help with freezing issues on some modern systems
- Bink movie files larger than 1024 pixels will now play without crashes
- Game crashes will now write a crash report into CrashDumps folder (please feel free to post any crash reports to the issues page!)

**Enhancements:**
- Game can now run in borderless windowed mode; mouse cursor will now be hidden while game is active
- Will use desktop resolution for the game if outrun2006.ini isn't present
- Load times heavily reduced via improved framelimiter
- Draw distance for the stage can be increased, greatly reducing pop-in/fade-ins on the level
- Music can now be loaded from uncompressed WAV or lossless FLAC files, if they exist with the same filename
- Allows intro splash screens to be skipped
- Music track can be changed mid-race via Z and X buttons, or Back/RS+Back on controller (`CDSwitcher` must be enabled in INI first)

All the above can be customized via the OutRun2006Tweaks.ini file.

The partial FPS unlock allows game to render out at higher FPS, **but will still run at 60FPS internally**.  
This won't give as much benefit as a true framerate unlock since frames will be repeated, but it can help reduce load times & improve some effects like the reflections update rate.  
(high refresh rate monitors that have poor 60Hz response times may also benefit from this too)

### Setup
Since Steam/DVD releases are packed with ancient DRM that doesn't play well with DLL wrappers, this pack includes a replacement game EXE to run the game with.

This EXE should be compatible with both the Steam release & the original DVD version, along with most OR2006 mods.

To set it up:

- Extract the files from the release ZIP into your **Outrun2006 Coast 2 Coast** folder, where **OR2006C2C.EXE** is located, replacing the original EXE.
- Edit **OutRun2006Tweaks.ini** to customize the tweaks to your liking (by default all tweaks are enabled, other than `CDSwitcher`)
- **Important:** Install the latest x86 VC redist from (https://aka.ms/vs/17/release/vc_redist.x86.exe), a redist from 2024 is needed for Tweaks to launch correctly (**even if you already have it installed please try installing it again**)
- Run the game, your desktop resolution will be used by default if `outrun2006.ini` file isn't present.
- (optional) the [SoundtrackFix package](https://github.com/emoose/OutRun2006Tweaks/releases/download/v0.3.0-release/OutRun2006Tweaks-SoundtrackFix-1.0.zip) can be applied to fix the missing first 2 seconds in "Rush a Difficulty"
- (optional) texture improvements can be found in the texture pack releases thread (please feel free to create your own too!): https://github.com/emoose/OutRun2006Tweaks/issues/20

Steam Deck/Linux users may need to run the game with `WINEDLLOVERRIDES="dinput8=n,b" %command%` launch parameters for the mod to load in.

### Building
Building requires Visual Studio 2022, CMake & git to be installed, with those setup just clone this repo and then run `generate_2022.bat`.

If the batch script succeeds you should see a `build\outrun2006tweaks-proj.sln` solution file, just open that in VS and build it.

(if you have issues building with this setup please let me know)

### Thanks
Thanks to [debugging.games](http://debugging.games) for hosting debug symbols for OutRun 2 SP (Lindburgh), very useful for looking into Outrun2006.

(**if you own any prototype of Coast 2 Coast or Online Arcade** it may also contain debug symbols inside, which would let us improve even more on the C2C side of the game - please consider getting in touch at my email: lucknut.xbl at gmail dot com)
