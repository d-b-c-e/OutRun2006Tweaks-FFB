# Native output ABI override

The wheel settings device dropdown needs strict selection by saved instance GUID.
The v0.8.0 native implementation can fall back to another actuator when a GUID
disappears or refuses acquisition; prechecking the list cannot close that race.

Only these two files adopt official toolkit **v0.13.0 / native 0.6.0, x86**:

- `lib/toolkit/native/x86/WheelFfb.dll`
- `lib/toolkit/include/wheelffb.h`

The authoritative component record is
[`lib/toolkit/NATIVE-PROVENANCE.json`](../lib/toolkit/NATIVE-PROVENANCE.json).
Schema 1 records baseline/override package versions, native version, architecture,
export count, source commit, archive SHA256, and each overridden file's relative
path, archive path and SHA256. `NATIVE-VERSION` gives the override package pin.
`VERSION` remains the **v0.8.0** baseline; model, profile reader, Forza encoder,
proxy header and profile data remain byte-identical. There is no force retuning.

The inspected header adds strict selection and finite-burst exports. This mod
uses `SetStrictDeviceSelection(1)`; it does not use the new burst functions.
The full package was deliberately not copied: its force-model header also
changes force behavior, outside this UI change. Shipped tunes are unchanged.

The official archive SHA256 is
`2f73f5427465cd85e8d969ee7ecaf7d9c2cb08ed923da80c4ac7c7234af1b0cc`.
Its native DLL SHA256 is
`95db6175354db9018ef6143291e293e75864d96919c22928aa19e6d11c8cbe31`.
Source: `dd0ef20ad0cdaccc7a67f10a707dbd2a27a6efe9`.
The coordinator verified the release asset digest, package checks and existing
MOZA R12 zero-output x86 ABI smoke logs. This consumer did not repeat hardware
testing or deploy the DLL. Live UI/output acceptance remains untested.

The consumer passes a verified nonzero game HWND owned by its process. This
avoids relying on the package's known zero-HWND fallback problem. Switching
targets zeroes and releases the old output first, preserves FFB Off, and waits
until the panel closes and normal driving gates pass before attempting output.

The local `Sync-Toolkit.ps1` refuses an include/native-x86 sync while this override
exists. A deliberate reviewed replacement must name both parts and use
`-ReplaceNativeOverride`; only successful replacement clears the override
markers. `-Force` alone does not bypass it. Build output still copies the pinned
DLL beside `dinput8.dll`; distribute the pair from the same build. Do not label
the whole vendored toolkit as v0.13.0.
