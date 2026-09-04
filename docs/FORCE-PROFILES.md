# Force profiles — swapping the feel without rebuilding

Every number that decides how the wheel feels lives in `force-profiles.ini`,
next to `dinput8.dll` in the game folder. It is read when the game starts, so
changing a tune is editing a text file and relaunching.

Pick one in `OutRun2006Tweaks.ini`:

```ini
[FFB]
FFBProfile = legacy
```

| Value | What you get |
|---|---|
| `legacy` | the model built into this mod, unchanged. The default, and the reference to judge everything else against |
| `arcade-outrun@1` | the shared toolkit model, tuned to match `legacy` as closely as its structure allows |
| `arcade-outrun@2` | a lighter variant: stronger centring spring, much less lateral load |
| `arcade-generic@1` | a neutral arcade tune, not specific to OutRun |
| `simlite@1` | art of rally's model, no virtual spring at all. Will feel wrong here; useful as a sanity check |

A profile that fails to load never costs you force feedback. The mod falls back
to `legacy` and says why in `OutRun2006Tweaks.log`.

## Why the split exists

Working out what the car is doing stays in this mod: reading the car struct,
the game's own decompiled surface-roughness table, detecting a wall hit from
the speed window, estimating drift depth. That part really is about OutRun.

Deciding how those signals should *feel* — the centring spring, the damper,
how much the wheel lightens as grip goes, weight transfer, the shape of a crash
jolt, the smoothing and slew that stop a direct-drive base oscillating — is not
about OutRun at all. Four projects had written their own version of it. That
half now comes from
[dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit),
and a profile is its tuning.

## Making your own

Copy a section into `force-profiles.user.ini` beside the shipped file, give it
a new id, and change what you want. That file is never overwritten when the mod
is updated, and a profile there beats one of the same name in the shipped file.

```ini
[mytune@1]
inherits = arcade-outrun@1
model.spring.strength = 0.7
model.lateral.weight  = 0.3
```

`inherits` means you only write what you are changing, so the diff between two
tunes is the list of what you actually changed.

Two rules worth keeping:

- **Never edit a published version in place.** If `arcade-outrun@1` means
  something different today than it did last week, every note about how it drove
  is worthless. Add `@2`.
- **A typo is reported, not ignored.** A key the build does not recognise is
  logged as a warning rather than silently doing nothing.

## The dials, roughly

| Key | What it does |
|---|---|
| `model.spring.strength` | centring force per unit of steering angle. The backbone of the arcade feel |
| `model.spring.fullSpeedMps` | speed at which the spring reaches full authority |
| `model.damper.strength` | resistance to turning the wheel quickly. Raise it if a direct-drive base oscillates |
| `model.lateral.weight` | how much cornering load you feel. This carried the whole force before the redesign |
| `model.lateral.gripLoss` | how much the wheel *lightens* in a drift. The arcade cue |
| `model.load.weightTransfer` | extra weight under braking, less under power |
| `model.impact.strength` | wall hit |
| `model.shift.strength` | gear thunk. Nets to zero, so it is a jolt rather than a shove |
| `shaper.strength` | the profile's own level. `FFBGlobalStrength` is still your master dial on top |
| `shaper.softSaturation` | 1.0 = `tanh`. Keeps detail near the limit instead of clipping |
| `shaper.slewPerSecond` | caps how fast force can change. The first thing to lower if the wheel buzzes |
| `shaper.peakLimit` | hard ceiling on output |

The full list, with units, is at the top of `force-profiles.ini`.

## Known caveat

`arcade-outrun@1` carries this game's shipped constants, but it is **not**
bit-identical to `legacy`. Two differences are known:

- The spring's speed curve has a softer knee. `legacy` used
  `min(1, s/0.25) x (0.35 + 0.65 s)`; the shared model uses a single
  "full authority at" speed, set to 22.5 m/s as the closest fit.
- `legacy` synthesises road texture as two sine waves when the wheel has no
  hardware periodic effects; the shared model uses one.

Also worth knowing before you judge any of this: the steering input this mod
reads is very probably mis-scaled, so the spring and damper have never really
been felt. See the note in `CLAUDE.md`, and run one lap with
`FFBDiagnosticLog = true` first.
