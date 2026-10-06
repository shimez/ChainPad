# Rotation Value Phase C: Runtime and input routing

`RotationRuntime` owns a saved Axis snapshot, Current Position and active flag.
It is separate from Config and is never serialized. It has no transport, Output
dispatch, Latest-State queue or Generation implementation (those belong to Phase D).

## Lifecycle

- Successful boot/load initializes active Rotation Value to Initial Position.
- Successful commit applies settings: entering Rotation Value or changing Range
  Steps initializes to the new Initial Position; all other changes retain Position.
- Leaving Rotation Value deactivates the Runtime. Re-entering initializes it.
- Failed validation/staging/commit-before-rename leaves Runtime untouched.
- Post-rename read failure deactivates Runtime along with the unavailable Config.
- Cancel/HID release/input overflow use the existing panic path, which does not
  modify Runtime. Transport connection changes likewise do not modify it.
- No lifecycle operation or physical rotation sends Rotation OSC/MIDI in Phase C.

Input acquisition/aggregation is unchanged. Engine routing handles CW input 26 and
CCW input 27: Rotation Value updates Position through the existing tested Stop/Wrap
helper; Action Chain mode executes its original Chain. Key/Push routing is unchanged.
The public trigger API rejects inputs 26/27 in Rotation Value mode; it cannot be
used as a Position-control API. All Notes enumerates Key/Push plus CW/CCW only while
the latter are active. It does not track old notes or synthesize mode-switch Note Off.

## Status and diagnostics

`GET /api/status` adds `encoderRotation`:

```json
{"mode":"rotationValue","rangeSteps":20,"runtimeActive":true,"currentPosition":5,"outputSendingSupported":false}
```

In Action Chain mode or an unavailable configuration, `runtimeActive=false` and
`currentPosition=null` (not zero). Range and mode are the device's saved settings.
The Configurator renders this saved Runtime separately from its editing summary.
Capabilities advertise `rotationRuntimeSupported=true` and
`rotationOutputSendingSupported=false`. Serial diagnostics include Runtime size,
active state, saved Range and Position (`n/a` when inactive).

## Verification

Host regression tests exercise actual save/commit/load transitions, 0 Outputs,
physical-event Engine routing, Stop/Wrap at N=1/65535, retain/reset rules,
cancel/transport epoch preservation, absence of Rotation MIDI messages, and
active-only All Notes with Key/Push/CW/CCW membership. Existing storage and Preset
tests remain in the regression suite. API-only tests cannot establish physical
encoder direction or generate a real input-queue overflow; these must be reported
separately from the deterministic host tests.

C6 ABI measurements: `Config=82648` bytes (unchanged from Phase B),
`RotationRuntime=20` bytes, `RotationOutput=208` bytes,
`EncoderRotationSettings=3348` bytes. The Runtime contains no Output array or
pending-send storage. Failed-save retention is also covered by host tests.

## C6 checkpoint 2 results

Build/image checks passed: static RAM **138804 bytes** (Phase B 138788; +16
after linker layout), flash 1543986 bytes. Runtime sizeof remains 20 bytes;
static-section totals need not increase by exactly that amount due to layout.
All host tests (including C3/C6/C5 capability suites) and Preset regressions passed.

Physical steps were performed by the user and observed through the status API:

| Test | Observed result |
|---|---|
| N=20, Initial=5, zero Outputs; CW3 then CCW2 | 5 -> 8 -> 6, no Actions accepted |
| N=65535 Stop; CW2, CCW1, CW1 | 65535 -> 65535 -> 65535 -> 65534 -> 65535 |
| N=65535 Wrap, individually checked | CW: 65535 -> 0; CCW: 0 -> 65535 |
| N=1 Stop; CCW1, CW2, CCW1 | 0 -> 0 -> 1 -> 1 -> 0 |
| N=1 Wrap; CW2, CCW1 | 0 -> 1 -> 0 -> 1 |
| Action Chain mode, CW/CCW each containing Wait 0 | accepted count 0 -> 1 -> 2; Current null |
| Those Chains retained, Rotation Value mode, CW1 | Position 7 -> 8; accepted count stayed 2 |

At Position 6, independent Key, Push, Linked Output, Boundary and Initial-only
saves all retained 6. `/api/panic` retained 6; CW/CCW trigger calls returned 409
and retained 6. Changing Range to 65535 initialized to the new Initial=65535.
Mode-only re-entry at N=20 initialized to 7. Initial-only change to 3 retained 7;
restart then initialized to 3. No Rotation sending path exists in this build;
host mock MIDI output remained empty during Rotation steps and lifecycle changes.
All Notes active membership was checked with host transport mocks.

Recorded real-device resource values:

- During physical/routing tests: API freeHeap about 123164..127652 bytes;
  lowest recorded minFreeHeap 96632 bytes (across the recorded test boots).
- Intermediate serial snapshot: free=130532, minFree=103868, largest=112628,
  stack low-water=5440 bytes.
- After restoring the prior empty configuration: serial free=129316,
  minFree=103396, largest=110580, stack low-water=5472 bytes.
- Final LittleFS: active=948 bytes, pending absent, used=12288 / 851968 bytes.

These are checkpoints/cumulative boot minima, not a complete load qualification.
One staging HTTP connection timed out; retry began a new transaction and succeeded.
Physical observation logs and final diagnostics are retained outside the repository
under `%LOCALAPPDATA%\Temp\opencode\phase-c-*`.

**Deferred with user approval:** no real input overflow occurred in these tests.
The common cancellation path retains Runtime in host tests; actual overflow-time
retention remains a later load-diagnostics item and is not marked hardware-verified.

The original pre-Phase-C v2 configuration was restored after the tests (empty
Chains/Outputs, Action Chain mode, original network settings). Firmware remains
Phase C. Phase D/Latest-State sending is not implemented.
