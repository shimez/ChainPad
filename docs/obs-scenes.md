# OBS Action editor and scene discovery — Phase 3 / Work 2

Work-1 baseline: `6a82a09`. The [seven-operation Action schema](obs-actions.md)
and Config/storage v2 and Key Preset v1 envelopes are unchanged.

## Editor and preservation

The existing Action editor creates and edits all seven OBS operations. Scene
switch shows sceneName, mute operations show inputName, and only SetInputMute
shows the boolean mute control. Other operations have no additional parameters.
Operation summaries are Japanese. Existing OSC/MIDI/HID/Wait editors remain.

Scene names use a free-text input plus a `シーン一覧から選択` button. The button
refreshes the list and opens an inline, unfiltered native selection list. Select
a candidate and press `選択したシーンを使用` to apply; Cancel/Escape leaves the
text unchanged. No candidate is automatically selected/applied. A valid name
absent from a fresh, complete fetched list gets a red
advisory but remains savable. Unavailable, failed, stale and loading lists never
prove absence. Discovery never modifies Action names or Config. Name validation
remains 1..192 UTF-8 bytes without control characters or malformed Unicode.
The list is a snapshot, not live OBS event synchronization; even during its
freshness window OBS names can change.

Unsupported devices show `OBS（非対応）` and explain execution is skipped. They
still allow creation/editing and preserve parameters through Config save/load
and Full/Key Presets. Connection/discovery APIs remain normal-S3-only; their
routes are not registered on non-S3 or OBS TEST builds.

Scene discovery is manual-only: neither device boot, page load, Ready transitions
nor connection generation changes request a list. Only the selection button starts
a refresh. The existing visible-page status poll fetches scene results only while
a discovery is loading (bounded to 20 seconds), never on an idle Ready connection.
Requests are single-flight; no second timer is added. Hidden pages do not initiate
discovery requests. Freshness expiry removes absence warnings and shows a neutral
free-input hint; the selection button fetches a newer list.

## Ownership and bounds

Internal `Operation::GetSceneList` uses the existing request FIFO4, in-flight1
and result FIFO8. It is explicitly excluded from Action parsing/serialization.
HTTP admits a request without waiting for OBS. The Worker owns socket I/O and
parsing into the nonpublished slot of a fixed two-slot SceneList pool. A successful
list is handed to the loop by swapping ownership, without copying its payload.
`obsActionsTick()` remains the sole normal-build result FIFO consumer. It routes
discovery results into the loop-owned cache; these do not increment Action
completion diagnostics. Worker counters still include discovery. Normal Actions
and discovery share FIFO admission/order, so refresh can return busy.

Each pool slot includes request ID and connection generation. A result must match
the pending ID; freshness requires Ready and the current generation. Disconnect,
generation change or age >=60000 ms makes the cache nonauthoritative. A 20000 ms
pending watchdog also covers dropped result records. Pending refreshes coalesce.
Ordinary discovery failure retains previous names with complete:false. No
truncated list is published as complete.

| Bound | Value |
|---|---:|
| Scene count | 32 |
| Name bytes | 192 UTF-8 bytes per name |
| Combined names including NUL terminators | 4096 B |
| WebSocket frame / assembled message | 16384 B |
| Discovery JSON allocator budget | 32768 B |
| Other protocol message / JSON budget | 2048 / 8192 B |
| Worker stack | 8192 B, unchanged |

Invalid names/count/combined bytes produce local result -10 (`listLimitOrInvalid`).
Malformed or oversized transport/JSON may end the connection through the existing
failure path (`requestFailed` in the cache). Request timeout remains 3 s;
handshake timeout remains 5 s. Panic/epoch cancellation and no replay are retained.

## HTTP contract

Both routes send `Cache-Control: no-store` and JSON. They do not access Flash.

- `GET /api/obs/scenes`: 200, immediate cache snapshot.
- `POST /api/obs/scenes/refresh`: empty body; 202 accepted or coalesced,
  429 FIFO busy, 503 unavailable, 409 maintenance, 400 invalid body.
- Wrong method: 405. Response allocation/encoding failure: 500 with
  `ok:false,state:"error",complete:false`.

```json
{"ok":true,"state":"ready","complete":true,"requestId":0,"generation":3,"error":null,"ageMs":120,"scenes":["ゲーム画面"]}
```

`state` is loading/error/unavailable/stale/ready. `complete` means a successful,
currently fresh list, not historical completeness. `requestId` is the pending ID
or zero; `generation` is the current connection generation. `ageMs` is time since
the cached successful result or null. `error` is null or a fixed local string:
timeout/listLimitOrInvalid/requestFailed/resultUnavailable/busy/unavailable.
`scenes` may contain retained old names when complete is false. HTTP admission
status is authoritative for POST; its cache error field is not a method/body
validation error code. No OBS server comments or credentials are returned.

## Verification

- `tests/run_obs.ps1`: PASS — protocol bounds, 16 KiB frame boundary, production
  Worker/Engine/cache, mixed normal/discovery results, stale/generation handling,
  Panic, oversize failure and TEST-console isolation.
- `tests/run_obs_control.ps1`: PASS — production HTTP handler/cache adapter,
  GET, accepted/coalesced refresh, 400/405/409/429/503 and settings regressions.
- `tests/run_host.ps1`: PASS — full/trace/C3/C5/C6 codec, persistence, execution
  and existing Action regressions. `tests/run_obs_settings.ps1`: PASS.
- Node: `test_hid_action_ui`, `test_obs_scenes_ui`, `test_presets`, `test_obs_ui`,
  `test_stop_chain_save`, `test_builtin_config`, `storage_fault/webui_get`: PASS.
  Coverage includes all seven editors, applicable fields, unsupported Full/Key
  preservation, free input/advisory, freshness, single-flight and existing UI.
- Local browser fixture: candidate names, red advisory with valid free input,
  save/reload of that name, unsupported indication and parameter preservation
  confirmed. These are browser/Host results, not physical OBS/device results.

## Resources

Original work-2 (`af0a928`) build RAM/Flash (bytes), relative to work 1:

| Target | RAM | RAM delta | Flash | Flash delta |
|---|---:|---:|---:|---:|
| S3 | 141964 | +26968 | 1608758 | +9728 |
| C3 | 136364 | 0 | 1459631 | +7088 |
| C5 | 65768 | 0 | 1675417 | +7088 |
| C6 | 141580 | 0 | 1621800 | +7088 |
| S3 OBS TEST | 138636 | +22776 | 1599510 | +7864 |

Normal S3/C3/C5/C6 and S3 OBS TEST final PlatformIO builds PASS, run sequentially.
TEST retains the Worker discovery buffers but excludes the loop scene cache/API.
S3 linked symbols confirm three
4172 B scene buffers (scratch/mailbox/cache), 92 B mailbox control, 16400 B
assembly (including metadata; +14336 B), request storage864 B, result storage128 B,
settings storage152 B and Worker stack8192 B. No scene buffer is placed on stack.
Action352 B and target Config82648 B layout are unchanged by work 2.

Individual S3 machine-code entry frames: worker1136 B, Protocol::receive352 B,
obsScenesRefresh240 B, obsScenesJson160 B. These exclude nested callees and do
not establish runtime headroom. Transient memory also includes the discovery
JSON budget, WebSocket receive allocation (up to the frame limit plus overhead),
HTTP JSON document and output (bounded to10001 B including terminator), and
existing framework/socket allocations. Static RAM totals are not peak heap usage.

## Work 3 handoff

Verify actual OBS effects for all seven operations, initial/reconnected/manual
scene discovery, rapid input, mixed protocol latency, long-running behavior and
runtime Worker/loop stack headroom and heap coexistence on normal S3 firmware.
Work 2 does not claim these physical results. Existing synchronous HTTP/Flash
stalls and bounded-result FIFO drops remain relevant. No Push is part of work 2.

## Work-2 UI follow-up

The original UI did not send GetSceneList to OBS every three seconds, but it did
GET the cache on every status poll, notify the editor before/after each GET, and
rebuild datalist options even on text input. Browser-native filtering also hid
other candidates when the input already contained a scene name. The separate
unfiltered picker removes those interactions. Async notifications never replace,
assign to or focus the text input. Only explicit Apply changes its value. Option
nodes are rebuilt only when the candidate array changes; Cancel closes the panel
even if the outstanding request finishes later.

The reported stale-list message was an unconditional consequence of the existing
60000 ms server cache TTL, not evidence that the named scene was absent. The TTL
is retained, because no live scene-change events are subscribed. Fresh complete
matches now show no warning; expiration/incomplete/unavailable results show a
neutral free-input hint rather than demanding an update. Only a fresh complete
list can produce a red missing-name advisory. No advisory blocks persistence.

`/api/status.obs.generation` now exposes the existing Worker session generation.
This detects reconnection even if status polling misses the disconnected interval,
without idle scene GETs. Client request epochs discard pre-disconnect/old-generation
replies. Connection transitions invalidate pending results without queuing a refresh.
The Action schema, Worker queues, cache limits and single result consumer remain.

Audio input selection is deferred for the current OBS WebSocket 5.x compatibility
range. Upstream 5.6.3 GetInputList includes `inputKindCaps` and could filter the
audio bit in one request, but 5.5.6/5.4.2 do not. Those lists mix video-only inputs;
kind allowlists or per-input probes would be necessary for general audio selection.
GetSpecialInputs covers only global Desktop/Mic slots, not all mute targets.
References: upstream `src/utils/Obs_ArrayHelper.cpp` in tags 5.6.3 and 5.5.6,
and `docs/generated/protocol.md` (GetInputList/GetSpecialInputs).

Follow-up verification: scene-manager, HID/OBS-editor, OBS-settings UI, Presets,
Stop Chain save/load, builtin Config and WebUI storage-fault Node suites PASS;
`tests/run_obs_control.ps1` PASS including generation publication. Browser fixture
confirmed no scene requests during repeated idle status polls, all candidates
despite unrelated input text, Apply/Cancel, focus/caret preservation across refresh,
empty warning for a matching scene, and free-text Config save/reload on both
supported and unsupported fixtures. A synthetic
composition event was used to check stable input DOM; physical OS IME interaction
has not been manually exercised.

User-reported hardware results for `af0a928`: scene switch, record start/stop,
SetInputMute and ToggleInputMute worked. Stream start/stop remain unverified
because OBS streaming was not configured. These reports do not cover this follow-up.

Follow-up final normal S3/C3/C5/C6 and S3 OBS TEST builds: all PASS, sequentially.
Static RAM is unchanged from `af0a928`; Flash deltas are shown below (bytes).

| Target | RAM | Flash | Flash delta |
|---|---:|---:|---:|
| S3 | 141964 | 1610194 | +1436 |
| C3 | 136364 | 1461039 | +1408 |
| C5 | 65768 | 1676825 | +1408 |
| C6 | 141580 | 1623208 | +1408 |
| S3 OBS TEST | 138636 | 1600918 | +1408 |

Subsequent UI adjustment: removed the OBS settings reload button (initial loading
remains; recovery messages now refer to browser reload). The scene picker trigger
has a caption and the same 42 px control height as adjacent fields. Browser layout
inspection confirms equal input/button top coordinates in a shared row. OBS UI,
Action editor and scene-manager Node suites PASS; normal S3 build PASS, RAM141964 B
(unchanged), Flash1610386 B (+192 B from the follow-up above).

Latest UI refinement: OBS Action selectors/cards (including unsupported cards) use
`🎬 OBS`. Connection text and indicator use red for Error (including authentication
errors), yellow for Connecting/Authenticating and accepted-operation update waits,
gray for Disabled/Disconnected/unknown status, and green for Ready. Historical
error codes do not turn a Ready connection red. Light/dark palettes are supported.
Scene discovery is now manual-only as described above; the original automatic
Ready refresh behavior and its historical test record are superseded.

Verification of this refinement: OBS-settings UI, scene-manager, HID/OBS editor
and Presets Node suites PASS. Browser computed colors match the text/indicator
in both themes; startup and repeated status checks issued zero scene requests,
and manual refresh issued one. Normal S3 final build PASS: RAM141964 B unchanged,
Flash1611154 B (+768 B from `228ac24`). No Worker or persistence changes.

## Fixed two-buffer ownership optimization

S3 hardware measurement procedure and opt-in capture commands:
[OBS memory test](obs-memory-test.md).

The previous three-buffer resource record above is historical. The current
implementation has two backend-owned SceneList slots (4172 B each) and a loop-owned
const view, replacing Worker scratch, the payload-copy mailbox and loop cache.
The 16400 B Assembly and its allocation policy remain unchanged.

- `scenePublished` identifies the loop-owned slot. Only the loop's successful
  `obsTakeSceneList(requestId, generation)` changes it. HTTP generation and result
  consumption both run synchronously on the same loop, so a slot is not released
  while an HTTP response is serializing it.
- Before a discovery request starts, the Worker clears `sceneReady` under the
  existing gate and reserves the other slot. Parsing writes only this reserved
  slot, without holding the gate. Ordinary Actions do not access either list.
- The Worker fills ID/generation and publishes `sceneReady` under the gate after
  complete validation. It then posts the ordinary completion record. Parsing,
  HTTP encoding and network I/O never run inside the ownership critical section.
- Take requires ready data, exact request ID/generation and a currently valid
  Ready lifecycle. It swaps the published index under the gate and returns a const
  view. Failed takes keep the old cache and mark it nonauthoritative.
- Partial/invalid results never publish. Disconnect/Panic processing abandons the
  nonpublished result, retaining the public slot. Lifecycle checks also reject a
  result if Panic/settings invalidation occurs before Worker disconnect handling.
- A loop watchdog only clears the pending request: it never releases/reassigns a
  Worker slot. If a completion was dropped or expired, the Worker reclaims that
  nonpublished slot when starting the next discovery. Delayed completion IDs cannot
  take the new result. A repeated take cannot swap a slot twice.

No heap allocations, extra task, result consumer or waiting admission were added.
The public HTTP/Action schemas, old-list retention, manual-only WebUI discovery,
scene limits, assembly size and JSON budgets remain unchanged.

Verification: `tests/run_obs.ps1` PASS with added parked-result/normal-Action
interleaving, wrong ID/generation, double-take rejection, partial parse failure,
Panic before consumption, loop watchdog, dropped result FIFO recovery and request
timeout/reconnect/reuse cases. `tests/run_obs_control.ps1` and full/trace/C3/C5/C6
`tests/run_host.ps1` PASS. Scene-manager, HID/OBS editor and Presets Node tests PASS.
These are production-code Host tests, not S3 runtime memory/stack measurements.

Normal S3 linked measurement against `dc39178`: static RAM141964 ->137708 B
(-4256 B), Flash1611154 ->1611294 B (+140 B). The savings include the removed
4172 B payload, obsolete queue metadata and linker alignment changes. `nm` confirms
sceneBuffers8344 B, sceneCache pointer4 B, sceneReady1 B, scenePublished1 B;
the former sceneStorage/sceneScratch/sceneControl symbols are absent. Assembly
remains16400 B and taskStack8192 B. There is no new scene-related heap allocation.

Final sequential builds all PASS:

| Target | Static RAM B | Flash B |
|---|---:|---:|
| S3 | 137708 | 1611294 |
| C3 | 136364 | 1461999 |
| C5 | 65768 | 1677785 |
| C6 | 141580 | 1624168 |
| S3 OBS TEST | 138540 | 1601958 |

Non-S3 static RAM is unchanged. TEST static RAM drops96 B from its previous
138636 B: it already excluded the loop cache, so it does not get the normal-S3
one-list reduction. Non-S3/TEST Flash totals also include recent WebUI refinements
since their last builds and are not isolated optimization deltas. Runtime heap and
stack margins remain for the normal-S3 hardware measurement.
