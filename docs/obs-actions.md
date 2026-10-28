# OBS Action Chain — Phase 3 / Work 1

> 開発版のシーンアイテム表示制御（Enable / Disable / Toggle）は[追加仕様](obs-scene-items.md)を参照。

> OBS execution is now enabled on standard S3/C5/C6 builds; C3 preserves stored
> Actions without executing OBS. See [final integration](obs-integration.md).

This is the historical work-1 record (`6a82a09`). The implemented editor,
scene discovery, current receive limits and resource measurements are documented
in [Phase 3 / Work 2](obs-scenes.md), which supersedes the work-2 handoff below.

Phase 2 baseline: `b964392` (approved). Work 1 adds typed operations, shared
Action persistence, nonblocking dispatch and completion collection. OBS networking
remains S3-only. Scene discovery and the complete editor belong to work 2;
integrated hardware verification belongs to work 3.

## Action schema

Existing Config/storage v2 and Key Preset v1 envelopes remain unchanged. Every
OBS Action requires `protocol: "obs"`, integer `delayMs: 0`, and `operation`:

| operation | Additional required fields | OBS requestType |
|---|---|---|
| `setCurrentProgramScene` | `sceneName`: string | `SetCurrentProgramScene` |
| `startStream` | none | `StartStream` |
| `stopStream` | none | `StopStream` |
| `startRecord` | none | `StartRecord` |
| `stopRecord` | none | `StopRecord` |
| `setInputMute` | `inputName`: string, `inputMuted`: boolean | `SetInputMute` |
| `toggleInputMute` | `inputName`: string | `ToggleInputMute` |

```json
{"protocol":"obs","delayMs":0,"operation":"setCurrentProgramScene","sceneName":"ゲーム画面"}
```

```json
{"protocol":"obs","delayMs":0,"operation":"setInputMute","inputName":"マイク","inputMuted":false}
```

Names are 1..192 UTF-8 bytes, without NUL, ASCII C0 or DEL; malformed UTF-8 is
rejected. Overlength values fail validation rather than being truncated. Names
are preserved without trimming or scene-existence validation. OBS objects reject
extra fields, wrong types, unknown operations and missing parameters. No Action
contains transport, host, password or other connection settings. Use Wait for a
delay, not an OBS-specific timing field. Muting/unmuting is explicit true/false;
Toggle uses the OBS operation directly, without locally inferred mute state.

`Protocol::Obs`, `obsOperation`, `obsTarget` and existing `boolValue` implement
this schema. `obsTarget` overlays the mutually exclusive OSC address buffer.
The shared codec is compiled on every target; no Worker is needed to load, save
or export any of these seven operations. Existing v2 transactions and HTTP
Config routes use this codec unchanged. Preset validation preserves defined OBS
Actions regardless of capabilities. Older firmware still rejects these new
Actions: backward compatibility does not imply downgrade/forward compatibility.
Unknown protocols remain rejected; no arbitrary opaque Action preservation was
introduced. Existing failed-load behavior retains disk contents.

## Execution and interfaces

`src/obs_command.h` defines `obs::Operation` and `obs::Command`:
`operation`, `target[193]`, `muted`. It has no networking dependency.

```cpp
SendResult obsDispatch(const obs::Command&, uint32_t& requestId);
bool obsNextResult(ObsResult&);
// ObsResult: id, success, code, operation
```

Calls run on the loop. Admission copies the command into the existing zero-wait
FIFO; the Worker owns network I/O and request JSON. The old scene-string overload
remains for the TEST console. Only named operations emit a target; only
SetInputMute emits inputMuted. Responses must match both request ID and operation.
Server comments, StopRecord outputPath and ToggleInputMute responseData are not
retained. Completion represents the OBS requestStatus, not an independently
verified output state.

`backends.cpp` routes OBS to `obsActionDispatch()` in `obs_actions.cpp`. The
existing Engine advances on Accepted and skips on Unavailable/Busy/Failed. No
scheduler changes or response-waiting invocation slots were needed. Therefore
HID → OBS → OSC continues before the OBS response. Cross-protocol external
effect ordering is not guaranteed.

`backendsTick()` drains up to eight completion records each loop into loop-owned
diagnostics. This is the single normal-build result consumer. TEST builds leave
the FIFO/settings with the console and skip Action admission. No operation is
automatically replayed, including Toggle after a lost reply or reconnection.

Panic invalidates queued work and closes the session using the existing epoch
checks; a send already claimed/in flight cannot be undone. StopOtherChains does
not cancel already accepted Worker requests. Disconnect/timeout may leave the
external outcome unknown. These existing limitations are preserved.

## Capabilities and status

`/api/capabilities.obsActions` is true only in normal OBS-supported builds.
Non-S3 and OBS TEST builds return false while retaining the shared schema.

`GET /api/status` adds a separate `obsActions` object on all targets:

```json
{"admission":"accepted","requestId":42,"completed":5,"failed":1,
 "lastResult":{"requestId":41,"success":false,"code":600,"operation":"setInputMute"}}
```

- `admission`: latest Action attempt: `notRequested`, `unsupported`,
  `testConsoleOwned`, `disabled`, `disconnected`, `accepted`, `busy`, `failed`.
- `requestId`: latest attempt's accepted ID; zero for a rejected attempt.
- `completed`: successfully drained completions; `failed`: drained failures,
  including local discard/timeout. Counters reset on boot and can wrap.
- `lastResult`: latest drained completion, or null before any completion.
  This is independent of the latest admission and need not have the same ID.
- Positive codes are OBS requestStatus codes. Existing local codes include
  -2 discarded/unknown external outcome, -3 send failure, -4 response timeout,
  -6 request generation failure.

Unavailable maps to `unsupported` on non-S3; on supported builds it maps to
`disabled` for logical Disabled, otherwise `disconnected`. This is an admission
summary, not a replacement for Phase 2 `blockedReason` and connection state.
Engine accepted/skipped counts remain admission counts. Worker counters and
`resultDrops` remain distinct from the drained-result counters above. This is
bounded diagnostics, not a full per-input execution history or completion log.

The current WebUI safely shows and preserves loaded/imported OBS Actions with a
read-only placeholder. It does not expose an incomplete OBS Action selector or
editor. The complete unsupported indication/editing experience is work 2.

## Verification

- `tests/run_host.ps1`: real shared codec, v2 transaction/save/load/re-encode,
  all seven operations, boolean types, strict extra/unknown fields, UTF-8 and
  192/193-byte limits; unsupported dispatch and HID → OBS → OSC continuation.
  Runs in full/trace and C3/C5/C6 host configurations alongside existing tests.
- `tests/run_obs.ps1`: real protocol and Worker plus real Engine/backend/Action
  adapter. Seven operations over 21 sequential dispatches, success/failure,
  continuation before Worker scheduling, continuous draining without new drops,
  full/unavailable admission, StopOtherChains, Toggle timeout/disconnect without
  replay. Existing FIFO/Panic race/epoch/reconnect/auth/frame tests retained.
  Non-OBS output transports are test sinks and FreeRTOS/socket are native
  adapters; this is not an actual S3/OBS hardware test. TEST-console ownership is
  checked separately. Protocol tests check wrong IDs/types and target bounds.
- `tests/run_obs_settings.ps1`, `tests/run_obs_control.ps1`: PASS.
- `tests/run_storage_fault.ps1`: S3/C5/C6 regression suite PASS, including
  interrupted saves, allocation failures and HTTP save/load failure handling.
- Node `test_presets.cjs`, `test_hid_action_ui.cjs`, `test_obs_ui.cjs`: PASS.
  Full/Key preservation, read-only OBS rendering, existing editor/settings
  behavior and strict validation covered.
- Normal S3/C3/C5/C6 and S3 OBS TEST PlatformIO builds: PASS, run sequentially.

## Resources and limits

Target ABI compile-only measurement (`tests/measure_rotation_layout.ps1`):
Action 352 B, Config 82,648 B, both unchanged. Host Config is 82,992 B due to
64-bit pointer layout. Command is 195 B, result 16 B, Action diagnostics 32 B.

S3 linked symbols: request storage 864 B (unchanged: new fields fit old padding),
result storage 128 B (was 96 B), settings storage 152 B (unchanged), Worker stack
8,192 B (unchanged). Request FIFO4/result FIFO8/in-flight1 remain unchanged.
The Action adapter has one bounded Command local; there is no per-Action heap
allocation, new task or larger target-name field in Config. Runtime stack
high-water marks and heap coexistence must be checked on hardware in work 3.
S3 object disassembly shows fixed entry frames of 1,136 B for `worker()` and
304 B for `obsActionDispatch()`; these exclude nested callees and are not
measurements of maximum runtime stack use.

| Target | Static RAM B | Flash B |
|---|---:|---:|
| S3 | 114996 | 1599030 |
| C3 | 136364 | 1452543 |
| C5 | 65768 | 1668329 |
| C6 | 141580 | 1614712 |
| S3 OBS TEST | 115860 | 1591646 |

Static RAM delta from Phase 2: S3 normal/TEST +80 B, other normal targets +32 B.
S3 Flash delta from the UI-refinement build: +5,020 B. Non-S3 published M3-base
Flash comparisons also include the later Phase 2 UI refinement, so they are
not isolated work-1 deltas. HTTP/Flash loop stalls documented in Phase 2 remain;
result FIFO overflow is still possible during stalls and reported by resultDrops.
Receive limit remains 2 KiB, JSON budget 8 KiB, response timeout 3 s, handshake
timeout 5 s. No scene discovery or generic request transport has been added.

## Work 2 handoff

Use this exact Action schema and `obsActions` capability. Add operation editing,
complete unsupported indication and scene-name ComboBox without rewriting saved
names. Discovery needs bounded larger-response handling, cache/API ownership and
distinction between unavailable/stale/incomplete lists and absent names. Keep one
completion consumer when introducing discovery; do not compete for the FIFO.
Work 3 verifies real OBS effects, latency, rapid input, mixed protocols and stack
headroom. C6/C5 networking remains outside this implementation.
