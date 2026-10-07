# Rotation Value Phase D — Latest-State sender

Phase D implementation and the C6 checks below are complete. Phase C baseline:
`1f6934e`; work started from clean `4e59f44`. No Phase E editor/Preview or encoder
acquisition changes. Output capacity remains provisional, not product-qualified.

While testing was paused, the user updated documentation through `b197487` and
removed C6 build artifacts. Those unrelated commits are retained. On resumption,
the device answered as production Phase D with ready v2 storage and boot Initial
Position64; the test endpoint returned404. Probe artifacts were rebuilt in an
external build directory before use. USB power loss was not assumed to preserve
Runtime Position or pending.

## Ownership and lifecycle

`RotationRuntime::step` compares old/new Position before publishing. Each actual
change increments a RAM-only uint32 generation (modulo wrap) and maps all Outputs
from one Position using Phase A functions. Equal mapped values are not suppressed.
No-change Stop steps neither generate nor cancel pending. Zero Outputs still
advance Position/generation. No generation, pending, or diagnostics is persisted.

`rotation_sender.cpp` owns a fixed `[16][3]` array for Output × Wi-Fi/USB/BLE.
Each slot has a 4-byte numeric union, uint32 generation and valid flag. Addresses,
Channel/CC and global OSC destination stay in Config: successful apply discards
pending before the next send. There is no heap allocation for pending, no Config
copy, and no merging of duplicate Outputs. The 16 Output capacity remains provisional.

Each slot is 12 bytes with current target alignment: 48 reserved slots = 576 bytes.
At most 32 slots are simultaneously valid (16 Both MIDI Outputs); OSC uses one
slot per Output. Metadata/counters bring sender-owned storage to 652 bytes.
Runtime Position storage remains separate. These sizes were confirmed on C6.

Connection callbacks only increment atomic epochs; loop-owned code observes them
before publish/send and discards the affected route. USB/BLE use existing MIDI
subscription epochs. OSC observes STA disconnect/lost-IP/got-IP and AP client
connect/disconnect/stop events; conservatively invalidates Wi-Fi pending on any
of those changes, including changes while another interface remains available.
Wi-Fi is available with a connected STA or any Setup AP client, as for OSC Actions.

Unavailable destinations have no pending. One API attempt consumes a slot whether
accepted or failed; there is no retry/replay. Successful Config apply, panic and
input-overflow panic discard pending without changing Position or regenerating it.
Failed validation/save before commit retains pending. No reconnect auto-send.

## Scheduling

Ordinary backends, resumed Chains and up to 16 input events execute before Rotation
sends. Rotation uses **4 API attempts per loop**, scanning at most 48 slots with a
persistent round-robin cursor. Slots whose ordinary MIDI FIFO is nonempty are
skipped without consuming the send budget; other Outputs/routes can progress.
Failed writes consume budget but do not stop scanning. Rotation MIDI calls
`midiWrite` directly and is never inserted into the normal MIDI FIFO.

This is a count budget, not a wall-clock or latency guarantee. Continuous ordinary
MIDI load can starve Rotation on that route, intentionally preserving Action
priority. Latest-state preferred does not guarantee final delivery, arrival order,
cross-transport atomicity or duplicate-destination final values.

## Diagnostics

`/api/status.encoderRotation` adds generation, snapshot (last generated Position),
pending, pendingBytes, senderBytes, overwritten, discarded, unavailable, accepted
and failed. Arrays are in Wi-Fi / USB MIDI / BLE MIDI order. Counters wrap modulo
uint32 and restart at boot. Snapshot can differ from initialized Current Position
until a real step occurs. Discard counts valid pending invalidated by lifecycle,
connection or API failure; unavailable counts skipped destination publications.
Overwrite counts replacements of valid slots, not generations. Serial `m` prints
these counters on demand; no per-step logging was added. Existing periodic `d`
logging remains disabled at boot.

## Verification status

- C6 pre-test state was schema v1 with 16 Actions, not the Phase C settings.
- Current Config and full 0x330000..0x3fffff LittleFS partition were backed up to
  `%LOCALAPPDATA%\Temp\opencode\chainpad-before-phase-d-*` before flashing.
- Host regressions, C6 production/probe verification and S3 build checks passed.
- Phase E and push have not been performed.

Completed checkpoints so far:

- Full host regression suite passed, including C3/C6/C5 BLE capability builds.
  Sender tests cover snapshot values, duplicate slots, value-equal generations,
  generation wrap, failed-save retention, save/panic discard, per-route epoch
  discard, unavailable/reconnect silence, API failure without retry, normal FIFO
  priority and sustained round-robin service under continuous generation updates.
- Preset JS regressions passed. C6 and S3 builds and image checks passed.
- C6 static RAM: 139452 bytes (+648 vs Phase C); flash: 1547340 bytes.
  Config=82648, Runtime=20, pending=576, sender=652 bytes confirmed on device.
- Final rebuilt/flashed C6 production firmware SHA256:
  `2f79e9ed9305e5b4b8366aaf2644fdd05f2ac8c3683869b4c95d4404530b282f`.
  Earlier production test image:
  `791b811ea8d10a6943d77d9e3508ab66326ead9647d283d4e2cc4b03db6f8ad9`.
- S3 image SHA256:
  `17e2d5f60c2f42ac2f300ad07e9c4ac87213123bb9549d69390de32dee388a19`.
  S3 physical USB/Both testing was not performed.
- New firmware detected `unsupportedVersion` after flashing. An explicitly
  authorized fresh v2 configuration was created; no automatic v1 migration.
- C6 initial/save without rotation: generation=0, pending=0, all accepted=0.
- Windows retained a stale MIDI port which failed to open. User re-paired the
  device; OS MIDI input and Firmware BLE subscription then worked normally.
- Physical N=20 steps 5->6->7->8->7->6 received OSC Int -40/-30/-20/-30/-40,
  float32 .4/.3/.2/.3/.4 and five independent BLE CC127=0 messages on Channel16.
- Physical N=1 Stop: four steps CCW/CW/CW/CCW created only two generations and
  received the stored Int/Float/CC endpoints. N=1 Wrap CW/CW/CCW generated and
  received 1->0->1 correctly.
- Real save tests retained Position for Key/Push/Outputs/Boundary/Initial edits
  and panic. Range change initialized to 32768; mode reentry initialized to 32768;
  Initial-only edit to 123 retained 32768 until reboot initialized to 123.
  Lifecycle capture received no OSC/MIDI messages. CW/CCW API requests returned409.
- MIDI port close/reopen was silent, but Windows **kept the BLE subscription**;
  this is not evidence of an actual transport disconnect/reconnect.
- First 16-Output stress capture showed 1072 overwritten slots and 26 failed OSC
  API attempts discarded, with no input overflow or Chain rejection. However its
  final counters reset near a concurrent serial open/close. A no-serial repeat is
  was performed below; the first capture is not accepted as a clean continuous-run test.

Evidence is external under `%LOCALAPPDATA%\Temp\opencode\phase-d-*` and
`verify_phase_d.py`. First stress capture is retained as
`phase-d-stress-with-serial.json`. Serial sampling is not continuous.

No-serial repeat: 100 seconds, 16 Outputs, physical rotation with short incomplete
HTTP requests (300 ms) and Key/Push trigger traffic. No reboot, no traffic errors,
no input overflow or Chain rejection. 57 generations produced 912 destination
publications: 544 overwrites + 176 OSC accepts + 184 BLE accepts + 8 OSC API
failures discarded = 912, pending=0. Captured 22 packets for each of eight OSC
Outputs, 184 CCs, 85 normal Note Ons and 85 Note Offs, plus 84 Key and 84 Push OSC
markers. This confirms coexistence/progress, not a latency guarantee. Lowest
sampled freeHeap=111612 and boot cumulative minFreeHeap=84884 bytes. A prior
16-Output serial snapshot recorded stack low-water=5440 bytes.

The first run's reset was reproduced without load by opening/closing COM10 with
pySerial defaults: uptime 234121->8081 ms, generation 57->0. Thus subsequent
timing/connection tests do not open Serial. It is a host DTR/RTS reset effect.

Real Bluetooth OFF + one physical step: eight OSC packets received, eight BLE
publications unavailable, pending=0. Bluetooth ON and renewed MIDI port capture
then received no OSC/MIDI messages, while Position65/generation1 were retained.

## Hardware pending fault-injection build

To deterministically inspect nonempty pending on real C6 hardware, an opt-in
`CHAINPAD_ROTATION_TEST` build adds only a drain hold. POST
`/api/rotation-test/hold?enabled=1` holds sending; `enabled=0` releases it.
Connection epoch observation, publication, overwrite and lifecycle discard all
continue normally. The default production build has neither state nor endpoint.
This is not a Configurator feature and must never be used for distributed images.
Tests must finish by restoring the production build and verifying endpoint absence.

Build via `PLATFORMIO_BUILD_FLAGS="-DCORE_DEBUG_LEVEL=0 -DCHAINPAD_ROTATION_TEST=1"`
in a dedicated process; retain the checked production image separately first.
The probe image SHA256 was
`65d4704c6f2bd33bb31b0ae69e11934f65816b380d6066ecc073358c0640c69e`.
Writing verified the flash hash before use. Hardware results with eight OSC and
eight BLE Outputs, all ordinary Chains empty:

| Operation | Measured result |
|---|---|
| Hold, N=1 Stop, Initial=0; physical CW twice | Position1, generation1, pending16, accepted0; unchanged Stop step preserved pending |
| Actual PC Bluetooth OFF, no rotation | pending16->8, discarded0->8; generation/Position unchanged; only BLE removed |
| Bluetooth ON, no rotation; release hold | eight OSC packets, zero MIDI messages; pending0, generation1 unchanged |
| Hold; physical CCW once | Position0, generation2, pending16 |
| Save/apply same settings | pending16->0, discarded8->24; Position0/generation2 unchanged |
| Release hold, capture5 seconds | zero OSC/MIDI messages; no regeneration |
| Hold; physical CW once | Position1, generation3, pending16 |
| Explicit execution stop/HID release | pending16->0, discarded24->40; Position1/generation3 unchanged |
| Release hold, capture5 seconds | zero OSC/MIDI messages |

Normal C6 firmware was then rebuilt without the test macro, image-checked,
flashed with hash verification, and verified to return404 for the test endpoint.
`tests/check_image.py` now rejects probe endpoints in production images; checking
the probe requires explicit `--rotation-probe`, and `--build-dir` supports an
isolated build directory. The negative production-gate check also passed.

## Final state, remaining scope and handoff

- Final C6: normal Phase D firmware, ready v2, Action Chain mode, zero Actions and
  zero Outputs; original pre-test Wi-Fi credentials and OSC destination retained.
  After reboot generation/pending/sends=0; Wi-Fi connected. Sampled freeHeap126568,
  minFreeHeap108996 bytes. This is the empty final config, not the max-output test.
- The starting **v1 16 Actions are not installed in the final v2 configuration**.
  Original Config and complete LittleFS remain in the pre-Phase-D external backups;
  the completed test configuration is also archived externally. No migration was
  added and no recovery from assumed Phase C device state was used.
- No unresolved defect remains in the tested Phase D scope. Actual input-overflow
  load testing is deliberately excluded; its existing panic path invokes the
  tested discard operation. S3 USB/Both hardware qualification and final Output
  capacity/load qualification remain later work (S3 code built, Both host-tested).
- Phase E: dedicated editor, Preview and duplicate warning UX. Keep Initial,
  saved Current and browser Preview distinct; generations/pending are RAM-only.
  Existing Phase B minimal summary/warnings were not expanded. Only the existing
  phase-support notice was updated to avoid claiming Rotation sends are absent.
- No always-on serial logging was added. The caller controls diagnostics; avoid
  default pySerial DTR/RTS open/close during continuity tests on COM10.
