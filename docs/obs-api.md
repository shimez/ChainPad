# OBS HTTP / Worker integration — Phase 2 M2

> Current integration: standard S3/C5/C6 builds enable these APIs; only C3 remains
> unsupported. S3-only statements and size tables below describe historical milestones.
> See [final integration](obs-integration.md).

M2 is complete with **conditional user approval** following Host tests, five
target builds and normal-S3 hardware verification. The limitations below are
accepted for M2; no additional verification phase is required for this milestone.
M3 WebUI work is recorded separately below; the M2 backend contract is retained.

Phase 3 Action schema, admission/completion diagnostics and work-1 verification:
[OBS Action Chain](obs-actions.md).

Phase 3 work-2 scene cache and asynchronous refresh endpoints:
[OBS scenes and Action editor](obs-scenes.md). Earlier resource/receive limits
in this document describe their respective historical milestones.

## Ownership and boot

Normal ESP32S3 builds use one loop-owned `obscontrol::Controller`, holding one M1
Store. Setup calls `begin()` after Config/LittleFS initialization and
`backendsBegin()` (which starts the existing Worker). Missing loads disabled
defaults and submits them without creating a file. Invalid/unreadable storage
disables only OBS. Existing Config permission, Actions, OSC, HID and MIDI are
independent of the OBS settings error.

`Controller::tick(globalAllowed)` composes Config/maintenance permission with
settings availability, accepted application and the OBS stop latch. The main loop
calls it before and after `web.handleClient()`; unavailable permission always
calls `obsTick(false)`, including subsequent iterations. Apply failure stops OBS
immediately. Stop is a request, not confirmation of socket shutdown or cancellation
of an already in-flight operation.

OBS TEST builds retain the original console ownership and global permission path.
They do not instantiate Controller, load OBS files or register the settings API.
Test commands remain RAM-only. `/api/status.obs.settingsOwner` is `testConsole`;
its connection state comes from the same Worker. There is no second Worker.
The normal settings endpoints return the existing 404 in TEST and non-S3 builds.

## API contract (normal S3)

All OBS API responses have `Cache-Control: no-store`. Password never appears in
settings, status or error responses. Store files remain plaintext as described in
[obs-settings.md](obs-settings.md).

### GET /api/obs — 200

```json
{"ok":true,"storageState":"missing","settingsAvailable":true,"settings":{"enabled":false,"ip":"","port":4455,"passwordConfigured":false},"applyState":"accepted","error":null}
```

`storageState`: `missing`, `ready`, `corrupt`, `unsupportedVersion`, `ioError`.
Only Missing/Ready expose settings. Other states return `settings:null` and
`settingsAvailable:false`; unknown password state is never reported as empty.
`applyState`: `notRequested`, `accepted`, `failed`. Accepted means mailbox
notification succeeded, not execution or connection acknowledgement.

### PUT /api/obs

Content-Type: `application/json` (also accepts `application/json; charset=utf-8`).
Send Content-Length; transfer-encoded requests are rejected. Complete JSON must
fit 1024 bytes including optional `replaceInvalid`.

```json
{"enabled":true,"ip":"192.0.2.10","port":4455,"password":"","replaceInvalid":false}
```

Enabled/ip/port are mandatory; password omission preserves the freshly verified
saved value, empty string clears, nonempty replaces. Optional `replaceInvalid`
must be boolean. M1's bounded parser removes this envelope flag, then Store
validates the update and resolves password; HTTP does not implement another
settings validator. The same M1 field validator also runs before envelope
reserialization so floating `4455.0` cannot turn into an accepted integer `4455`.
M1's documented parser semantics apply.

| Result | HTTP | saved | applyState |
| --- | ---: | --- | --- |
| Persisted and apply accepted | 200 | true | accepted |
| Invalid input/body/type/size | 400 | false | unchanged unless settings became unavailable |
| Replacement confirmation missing | 409 | false | notRequested for unavailable settings |
| Reset/Restart/recovery conflict | 409 | false | current state |
| Persistence/read/verification/rename failure | 500 | false | current state; rename resets to notRequested |
| Persisted but apply rejected | 503 | true | failed |

Responses also contain the current public settings/storage state and a fixed
`error:{code,message}` on failure. Codes include M1's `invalidInput`, `tooLarge`,
`passwordRequired`, `replaceRequired`, `ioError`, `verifyFailed`, `renameFailed`,
plus `invalidRequest`, `maintenance`, `applyFailed`, `noMemory`,
`methodNotAllowed`, `responseFailed`. No code/message contains input text.
Response encoding failure preserves `saved` and `applyState` in a small fallback.

The Arduino WebServer raw-body callback preserves byte lengths (including raw
NUL) and copies only up to 1024 bytes into temporary heap storage. Oversized JSON
is rejected from Content-Length before that copy and the connection is closed.
The framework has its own fixed raw receive buffer; truncated/aborted requests
cannot commit. GET does not reread Flash. Requests are serialized on the loop;
Store/Controller must not be called concurrently from another task.

The existing WebServer parses headers and multipart forms before route execution.
Multipart is not an OBS input format and cannot commit, but its framework parsing
cost is not bounded by the OBS JSON buffer. This change does not replace the
shared server's transport/parser or add authentication/TLS. Production logging
remains `CORE_DEBUG_LEVEL=0`; OBS raw handling never logs body content.

### Rename failure

The PUT remains HTTP 500 / saved:false even if a new active file actually exists.
Controller immediately calls `obsTick(false)`, sets applyState to notRequested,
and invokes Store::load. Only that reload determines public settings/state.
Ready permits GET of the verified old or new settings, but `blockedReason` stays
`reapplyRequired`; there is no automatic apply. A subsequent reconnect or successful
PUT can clear the latch. Corrupt/IoError keeps settings unavailable and OBS stopped.
Physical rollback/atomicity under arbitrary media faults is not claimed.

### POST /api/obs/reconnect

Send an empty body. Uses only Controller's trusted saved RAM snapshot; no Flash
write and no unsaved request settings. Ready and terminal authentication/Close
errors can both be retried using `obsApplySettings`.

- 202: accepted, do not wait for connection.
- 409: `disabled`, `settingsUnavailable` or `maintenance`.
- 503: `applyFailed`; immediate and persistent OBS stop request.

### GET /api/status

Existing fields are retained. Normal S3 adds:

```json
{"obs":{"storageState":"ready","applyState":"accepted","blockedReason":null,"stopRequested":false,"connectionState":"Ready","lastError":null,"lastClose":null}}
```

Connection state is the existing Worker snapshot: Disabled, Disconnected,
Connecting, Authenticating, Ready, Error. The snapshot uses the existing lifecycle
epoch checks to hide stale-generation Ready immediately after settings/reconnect.
`loopIntervalMaxUs` measures the maximum interval between actual outer-loop entries;
`saveMaxUs` measures the maximum synchronous Store::save duration (both since boot,
microseconds). They do not reuse Worker obsTick timing, which now has multiple
calls within one loop. These two maxima support hardware acceptance measurements.
`lastError`/`lastClose` are numeric Worker diagnostics or null, never server text.
They are historical counters/codes, not an acknowledgement of the latest apply.
Blocked reasons: `settingsUnavailable`, `applyFailed`, `reapplyRequired`,
`disabled`, `globalOutputsBlocked`. `stopRequested` does not assert physical
disconnection; Disabled is the Worker's logical state when permission is off.

### GET /api/capabilities

Adds boolean `obsWebSocket` (S3 true, C3/C5/C6 false) and `obsSettingsApi`
(normal S3 true, TEST/non-S3 false). No Config/Preset schema change.

## Verification

`tests/run_obs_control.ps1` compiles the production controller, HTTP handler,
codec and Store. Only WebServer transport, POSIX boundary and Worker public API
are adapted. Cases cover boot states, GET/status privacy, PUT validation/password,
save-versus-apply failures, continuous stop, global permission, maintenance,
reconnect, interrupted/oversized/NUL bodies and four ambiguous rename outcomes.
It does not claim to execute the real Arduino HTTP parser or a LittleFS emulator.
The existing Worker suite additionally checks stale-generation Ready suppression
against the production Worker before its next iteration.

Host verification completed for M2:

- `tests/run_obs_control.ps1`: PASS, including startup apply rejection, rename
  reload/reapply latch, unknown-password replacement, floating-port rejection and
  unsigned loop-timer wraparound.
- `tests/run_obs_settings.ps1`: PASS (M1 codec/Store and fault matrix).
- `tests/run_obs.ps1`: PASS (production Worker and RAM-only console).
- `tests/run_host.ps1`: PASS (Action/OSC/HID/MIDI/Config/rotation/reset).
- `tests/run_storage_fault.ps1`: PASS (S3/C5/C6 Config/Wi-Fi/reset safety and WebUI).
- `node tests/test_presets.cjs`: PASS.

The Worker Host build retains existing WebSockets MSVC C5208 warnings. Host
results do not establish ESP HTTP transport timing, LittleFS device behavior or
physical OBS connectivity.

### Final build and resource record

All five builds PASS with the final production sources (PlatformIO Espressif32
55.3.311 / Arduino 3.3.11, release). Byte counts below are PlatformIO static
RAM/Flash usage; deltas use the committed M1 baseline, not its forced-link probe.

| Target | RAM B | RAM delta B | Flash B | Flash delta B |
| --- | ---: | ---: | ---: | ---: |
| S3 normal | 114916 | +184 | 1581402 | +13176 |
| S3 OBS TEST | 115780 | 0 | 1574574 | +364 |
| C3 | 136332 | 0 | 1436259 | +94 |
| C5 | 65736 | 0 | 1652061 | +94 |
| C6 | 141548 | 0 | 1598460 | +98 |

Final `nm`/`objdump` inspection confirms normal S3 retains Controller, Store::save
and registerObsHttp. The Controller symbol is 176 B (including Store and timing
counters); image RAM deltas also include linker layout/alignment. TEST retains
`obsTestByte` but no Controller/HTTP/Store symbols. For C3/C5/C6 the control, HTTP,
codec and Store object text/data/bss are all zero and their ELF symbols absent.

Individual S3 machine-code stack frames: Controller::put 64 B, tick 32 B,
statusJson 160 B, HTTP send 416 B, handle 96 B, unwrapRequest 304 B and
Store::save 512 B. These are individual frames, **not** maximum call-chain stack
usage or a measured loop stack margin.

Temporary heap includes the HTTP body (1025 B), unwrapped update (1025 B), and
Store scratch (up to 1025 B). M1 codec documents use an 8192 B allocator budget
per operation, sequentially; response output is 768 B. JSON response documents,
WebServer receive/send buffers, allocator overhead and existing Worker memory
also consume heap, so those buffer sizes are not a whole-device peak estimate.
Storage operations run synchronously on the loop; no new Worker/task is created.
Observed runtime results and remaining diagnostic limits are recorded below.

## Hardware verification and conditional acceptance

User-provided Stage A/B/C reports and logs cover XIAO ESP32S3 normal firmware
with authenticated OBS WebSocket on port 4455. Hardware PASS: missing-settings
boot, settings save/restore, connection, reboot auto-connect, manual reconnect,
OBS stop/restart automatic recovery, authentication failure (4009), recovery after
password correction, Disabled/Enabled switching, omitted-password preservation,
existing ChainPad functions while connected and normal operating performance.
GET exposed passwordConfigured rather than the password. The observed 33-second
monitoring interval is not a measured reconnect duration: OBS restart time was
not recorded precisely.

Ready/Disabled, with and without key/encoder operation, produced maximum loop
intervals of 12.523–13.251 ms. Two RAM snapshots 6.446 seconds apart showed internal
free RAM 75064/75060 B, minimum free 54928 B, largest block 31732 B, PSRAM free
8297028 B and loopStackMin 5184 B. No growing memory consumption was observed in
that short interval; this is not a long-duration leak or worst-case margin test.

### Accepted limitations and future improvements

- After settings changes, loopIntervalMaxUs reached approximately 312–326 ms and
  saveMaxUs approximately 110–112 ms. Another interval reached 534.124 ms. HTTP
  processing and LittleFS saving execute synchronously on loopTask and can delay
  input-event consumption/Action execution. The timing breakdown and cause of
  534 ms are unknown. These are independent maxima since boot; subtracting them
  does not yield a processing-time breakdown. M2 retains this structure; reassess
  with targeted timing if an operational problem appears.
- The existing OBS Worker is an independent FreeRTOS task, `chainpad-obs`, with
  an 8192 B static stack, priority 1, pinned to ARDUINO_RUNNING_CORE. It measures
  its own High Water Mark, but normal firmware has no output path for that value.
  Its hardware stack margin remains unverified. loopStackMin is for loopTask,
  not the Worker. Exposing the existing Worker measurement is a future diagnostic
  improvement, not an M2 completion gate.
- lastError=-1 is an internal generic connection-end/attempt-failure diagnostic
  without a more specific code. Successful automatic reconnect does not clear
  it, so Ready with lastError=-1 is consistent with the current implementation.
  M3 must use connectionState as the primary status and show Ready as connected;
  lastError is historical diagnostic information, not proof of a current failure.
  applyState=accepted means notification acceptance, not connection success.

Detailed HTTP boundary cases, explicit empty-password clearing, storage failures,
rename failures, Corrupt/UnsupportedVersion and other injected error paths rely
primarily on the existing Host tests. They were not all exercised on hardware.
The supplied logs do not establish every HTTP status/header assertion (including
no-store and Disabled reconnect 409), every transport/error-state combination of
existing Actions, or every Config/Preset export check in the reference procedure.
These are not marked hardware PASS and are not additional M2 approval gates.
Host tests do not guarantee physical Flash atomicity under arbitrary media faults.

### Original hardware procedure (reference, not an additional test phase)

The procedure below records intended checks, not a claim that every step was
performed. The results and scope above govern M2 acceptance. Flash **normal S3** firmware from
`.pio/build/xiao_esp32s3/firmware.bin` using the established project procedure.
Do not use OBS TEST firmware for these tests. Keep OBS WebSocket 5.x enabled on
an IPv4-reachable host. Allow its port through the host firewall.

Use PowerShell 7 or Windows PowerShell; replace the addresses below. Keep request
bodies/passwords out of transcripts, screenshots and shared logs.

```powershell
$base = 'http://CHAINPAD_IP'
$obsIp = 'OBS_HOST_IPV4'
$port = 4455
function Get-Obs { Invoke-RestMethod "$base/api/obs" }
function Get-ObsStatus { (Invoke-RestMethod "$base/api/status").obs }
function Set-Obs($enabled, $password) {
  $value = @{ enabled=$enabled; ip=$obsIp; port=$port }
  if ($PSBoundParameters.ContainsKey('password')) { $value.password=$password }
  $json = $value | ConvertTo-Json -Compress
  Invoke-RestMethod "$base/api/obs" -Method Put -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($json))
}
# Obtain the correct password without echo/history; use '' if OBS auth is disabled.
$secure = Read-Host 'OBS password' -AsSecureString
$credential = New-Object System.Net.NetworkCredential('', $secure)
$obsPassword = $credential.Password
```

1. On a device with no OBS settings yet, boot and run `Get-Obs` / `Get-ObsStatus`.
   Expect missing, enabled:false, Disabled, no OBS connection. Do not erase existing
   Config merely to produce Missing; report this case pending if already configured.
2. Run `Set-Obs $true $obsPassword`. Expect saved:true / accepted (HTTP 200).
3. Poll `Get-ObsStatus` every second; record transition to Ready and time to Ready.
   Confirm GET/status contain no password and OBS reports the client connected.
4. `Invoke-RestMethod "$base/api/restart" -Method Post`. Wait for reboot and verify
   ready storage / accepted apply / automatic Ready without another PUT.
5. `Invoke-RestMethod "$base/api/obs/reconnect" -Method Post`. Expect HTTP 202;
   observe reconnection and Ready. Do not require observing every transient state.
6. With OBS authentication enabled, `Set-Obs $true 'intentionally-wrong'`.
   Saving/apply acceptance should succeed; status later becomes authentication
   Error. This is a connection error, not a storage failure.
7. `Set-Obs $true $obsPassword`; verify Ready. Also reconnect from a terminal error
   using saved settings after restoring the server's matching password if practical.
8. Stop OBS, observe disconnection/error, start OBS again and verify recovery.
   If a terminal Close requires explicit reconnect, record code and behavior.
9. `Set-Obs $false` (password omitted); expect saved/accepted, Disabled and OBS
   disconnect. Reconnect must return 409. GET retains IP/port/passwordConfigured.
10. With OBS Ready, auth-failed and Disabled, run the same existing OSC/HID/MIDI
    Action Chains; verify successful outputs and no added global stop condition.
    Check existing Config GET/Preset export for unchanged structure and no OBS secret.

Capture HTTP status and JSON for each step (PowerShell 5 throws on non-2xx; inspect
the response status/body in the exception or use a REST client). Check no-store via
`curl.exe -i "$base/api/obs"`. Record firmware build, OBS version, network setup,
time to Ready/recovery and existing-action outcomes. Do not include request secrets.

For resource acceptance, record `/api/status.obs.loopIntervalMaxUs` and `saveMaxUs`
before and after repeated PUT/reconnect operations, plus the loop stack low-water
mark from existing `m` diagnostics (`h` also lists task stack minima). Synchronous Flash and HTTP receive/send can delay loop processing; these
measurements are necessary before claiming real-time margin. No power-cut or
deliberate file-corruption test is required; rename faults are covered on Host.

M3 must distinguish saved, apply accepted/failed and actual connection state;
preserve password omission semantics and explicitly confirm invalid-file replacement.

## Phase 2 M3 — WebUI

The existing single-page configurator places an independent OBS WebSocket panel
directly below OSC. OSC keeps its existing responsive fields and Config-wide
save/apply path. OBS uses only its own settings/reconnect APIs and is excluded
from Config and Presets. Its panel is shown only when both `obsWebSocket` and
`obsSettingsApi` are true (hidden on non-S3 and OBS TEST builds).

- Enable, IPv4, port and password remain editable while OBS is disabled. IPv4
  and integer port 1–65535 are checked before saving; API validation is final.
- Blank password omits the property. A new password replaces it. The unchecked
  by default "保存時にパスワードをクリア" checkbox disables password input and
  schedules an empty-string update at Save OBS. Unchecking cancels deletion and
  re-enables input; confirmed persistence resets the checkbox. There is no
  password-clear dialog. Saved secrets are never fetched or filled in.
- Confirmed persistence clears the password input, including `503/saved:true`.
  That result is explicitly shown as saved with an apply/response problem, not
  connected or an ordinary unsaved failure. Failed/unknown saves retain edits;
  unknown outcomes recommend reloading before retrying.
- Readable invalid storage requires explicit replacement confirmation and a new
  password or explicit clear; `replaceInvalid:true` is never sent silently.
- Reconnect uses saved settings; it is disabled for a disabled saved configuration
  or unchecked Enable control. HTTP 202 means request accepted only. Save and
  Reconnect are serialized; Config saving and OBS operations also exclude one
  another while in progress.
- Device and OBS status share a single-flight request and a 3-second polling
  delay after completion. Hidden pages/navigation stop scheduling; visibility
  return resumes polling. Responses started before an OBS mutation cannot replace
  its post-operation status. Polls never refill the editor. Failed status fetches
  show that the current state is unknown.
- Ready displays Connected even with historical lastError=-1. Authenticating is
  also displayed; Error with 4009 explains authentication failure. Unknown codes
  remain numeric diagnostics, without guessed meanings.

### M3 verification (approved base implementation, before UI refinement)

- `node tests/test_obs_ui.cjs`: PASS — real editor logic, supported/unsupported
  capabilities, preserve/change/clear/undo, validation, saved-versus-apply failure,
  replacement confirmation, reconnect, single-flight/visibility/navigation,
  stale response suppression and edit retention.
- `node tests/test_hid_action_ui.cjs`, `node tests/test_presets.cjs`,
  `node tests/storage_fault/webui_get.cjs`: PASS — existing UI/Config/Preset paths.
- Wi-Fi password, Stop Chain save/load, builtin Config, Maintenance and Rotation
  preview Node regressions: PASS.
- Normal S3/C3/C5/C6 and S3 OBS TEST builds: PASS. Final UI bytes and navigation
  polling guard verified in all five firmware binaries. Static RAM is unchanged
  from M2. Flash B: S3 1593818, C3 1448667, C5 1664469, C6 1610868,
  S3 TEST 1586974. Normal-S3 firmware.bin SHA256:
  `b6c7be7e13d8b3e400c7f55bf69e374dc2554b6c718d81a224ef445366f555fc`.
- Local browser fixture (`node tests/obs_ui_server.cjs`): PASS — actual page load,
  OBS-only PUT, existing OSC Config save, reconnect, unsupported UI, desktop
  horizontal fields, OSC above OBS, and 320/390px Light/Dark without horizontal
  overflow. Fixture tests do not establish physical device results.
- Normal-S3 hardware smoke: PASS after user firmware upload/reboot. The actual
  device-served WebUI displayed stored settings and Connected, including the
  initial historical lastError=-1 without reporting a current connection error.
  Save OBS sent an unchanged enabled/IP/port with password omitted; HTTP 200,
  saved:true and applyState:accepted were confirmed. Public settings remained
  unchanged and the password field stayed empty. Automatic polling returned to
  Connected. Reconnect returned HTTP 202, displayed request acceptance rather
  than connection success, then automatic polling showed Connected again.
  Observed status-response intervals were 3066–3077 ms (3-second delay plus HTTP).
  The only mutations were PUT /api/obs and POST /api/obs/reconnect; Config/OSC
  saving was not triggered. Actual desktop layout had OSC above OBS, horizontal
  OBS fields and no horizontal overflow. M2 fault tests were not repeated.

M2's synchronous HTTP/Flash latency and unexposed Worker stack measurement remain
known limitations. This UI does not change Worker, storage, API or Preset formats.

### M3 UI refinement

The approved M3 implementation is preserved in commit `0d57794`. The subsequent
UI-only refinement uses the same native details/summary structure as Wi-Fi:
"OSC送信先設定" and "OBS WebSocket設定" start collapsed and open independently.
The existing OBS connection display lives in its summary, updating through the
same polling while collapsed. Opening state is not persisted. The password-clear
checkbox described above replaces the former Clear/Undo buttons and dialog.
Wi-Fi and OSC saving, OBS API semantics and unsupported-build hiding are retained.

Refinement verification: OBS UI, Wi-Fi password and WebUI storage-recovery Node
tests PASS. Browser fixture confirms initial collapse, independent/simultaneous
opening, automatic status refresh while collapsed, OSC save, OBS save/reconnect,
checkbox input disabling/re-enabling, unsupported hiding, and 320/390px open and
closed layouts in Light/Dark without horizontal overflow. The refinement has not
been flashed to hardware; the hardware smoke above covers the approved M3 base.
Normal-S3 build PASS: static RAM 114916 B, Flash 1594010 B.
