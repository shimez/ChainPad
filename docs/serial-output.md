# S3 USB CDC diagnostic output

Base: `e5c2c65` (`feature/obs-websocket-integration`). This change does not alter the OBS Worker.

## Contract

- S3 USBCDC only. C3/C5/C6 retain their existing HWCDC path, including the existing `h` timeout behavior.
- One setup/loop-owned static output object: 4096-byte ring plus small control/statistics fields. No new task or mutex.
- Reserve a whole diagnostic, format into uncommitted ring space, then commit. Capacity/format failure cancels the whole diagnostic, preserving earlier data.
- Only one reservation at a time. The pump does not expose staged bytes. Ring wrap is supported for reservation, formatting and transmission.
- Every buffered formatting call uses a 512-byte stack scratch buffer, not Arduino `Print::printf`'s dynamic allocation. A line that cannot fit is rejected, never silently truncated.
- Reservations: `m`/periodic/save 2560, `h` 3072, OBS status 768, scene admission burst 1536, each OBS result 64, trace line 384 bytes. Boot/help use 1024/256 bytes.
- Setup queues its messages after normal subsystem initialization. There is no application CDC output before this point. Boot ROM/panic/framework messages are outside this application's diagnostic delivery contract.
- Loop tail calls the pump once: at most 64 bytes and one USBCDC write, timeout zero, no flush or retry loop. Partial and zero writes preserve the unsent suffix.
- Normal diagnostics are refused before collection if reservation fails. Their rejection does not block inputs, Engine or network processing.
- OBS settings/scene/panic execute even without output space; their omitted replies increment `commandReplyDrops`. No operation is repeated to recover a reply.
- OBS status drains at most eight results, reserving 64 bytes **before** each `obsNextResult`. If status/body or result reservation cannot proceed, `resultDeferred` increments and results remain in the existing FIFO. A subsequent status retries collection. Worker `resultDrops` retains its original meaning.
- HID recording pauses the pump. Trace dump advances only after line admission; a rejected line is retried from the frozen snapshot. The recording itself is unchanged.

## Output loss and USB sessions

`SERIAL output loss: ...` coalesces rejected output and is appended after retained data has drained. The notification's capacity probe does not increment rejection counters. Repeated rejected trace attempts cannot fill the ring with notifications and starve the next trace line.

On observed USB disconnection, application bytes are retained. On observed reconnection, the pump first writes:

```text
SERIAL session resumed; retained suffix follows (USB delivery unknown)
```

It then resumes the retained suffix, **not** the original message from its beginning. The boundary marker starts with a newline. A suffix after a physical disconnect may start in the middle of a diagnostic line. This is explicitly not an intact-message delivery guarantee across USB sessions. Bytes already accepted by the USB driver are not replayed because the PC may already have received them.

Connection changes are sampled by the loop pump. A disconnect/reconnect entirely between samples, including a trace-paused interval, cannot be identified by this mechanism. The first connection does not prepend a reconnect marker. Reset loses RAM output as before.

## Statistics

OBS status, `m`, `h` and periodic/save diagnostics append a `SERIAL` statistics line:

- `txPartial`, `txZero`: sink write results, **not lost-message counts**.
- `outputRejected`: refused output reservations (trace retries count per attempt).
- `formatRejected`: cancelled diagnostics due to format/size failure.
- `commandReplyDrops`: executed OBS command batches whose reply was not retained.
- `resultDeferred`: status/result collection attempts deferred for output capacity; not an exact count of pending results.
- `queuedBytes`, `maxQueuedBytes`: committed unsent bytes and their high-water mark (maximum 4096).
- `pumpMaxUs`: observed maximum pump duration. Paused calls are excluded. USB connection probing at the call site and synchronous diagnostic collection are not included.

The printed statistics snapshot precedes commit of its own diagnostic. Counters are boot-local uint32 values. A short/zero write is recovered without decrementing its historical counter.

## PC verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_serial_output.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_obs.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_storage_fault.ps1
pio run -e xiao_esp32s3 -e xiao_esp32c3 -e xiao_esp32c5 -e xiao_esp32c6 -e xiao_esp32c6_hid_trace -j 2
pio run -c platformio.obs-test.ini -e xiao_esp32s3_obs_test -e xiao_esp32s3_obs_trace -j 2
.venv/Scripts/python.exe tests/serial_output/check_artifacts.py
```

The serial suite executes production ring, console, diagnostics and trace code against sink/clock/heap/filesystem adapters. T01–T18 cover byte equality, partial/zero writes, stopped receivers, ring wrap, transactional cancellation, budgets, result dequeue ordering, saturated OBS operations/Panic, frozen trace retry, long pauses and coalesced notifications. It does not measure physical USB timing.

The size fixtures use maximum 32-bit numeric fields, all 16 startup/four allocation-failure records, 32-byte phase and 64-byte allocator names, 64-byte diagnostic reason and 160-byte boot text. Longer text is tested for atomic rejection. They are bounded fixtures, not a claim that arbitrary external strings have a finite printable maximum.

PC measured output sizes: `m` 1582 B, `h` 2677 B, OBS status 559 B, 16 admissions with 10-digit IDs 1520 B, result with 10-digit ID and minimum signed code 57 B. The status fixture plus worst-case growth of its nine statistics fields remains below 768 B. These are host-format measurements; ESP object sizes/addresses can change a few printed characters.

Stage 2 build issues encountered: adding a test environment while another build used the same build root caused PlatformIO to invalidate that root (missing dependency output directory); the new S3 trace environment was also initially rejected by the dependency-patch allowlist. The environment list was finalized, the explicit S3 trace name was allowlisted without changing patch contents, and test environments were scheduled sequentially for retry.

## Stage 2 verification record

All T01–T18 passed: T01 full writes; T02 partial writes; T03 zero writes;
T04 stop/resume and reconnect boundary; T05 full buffer; T06 producer ordering;
T07 result reservation before dequeue; T08 wrap and wrapped cancellation;
T09 format/overflow/cancel/nesting; T10 recovery after refusal; T11 pump limits
and elapsed-time accounting; T12 production output budgets and skipped collection;
T13 saturated settings/scene operations; T14 saturated Panic; T15 frozen trace
retry; T16 100000 stopped iterations without additional allocations; T17 coalesced
loss notices and reservation non-starvation; T18 long trace pause/resume.

OBS Native, host/core/capabilities, existing HID trace, storage-fault S3/C5/C6
(including 932 H4 cases per target), and associated WebUI regressions passed.
WebSockets patch reproducibility/idempotence/rejection checks passed. Linked
artifact isolation and normal four-board partition/image checks passed. The
artifact checker initially failed on Windows cp932 source decoding; explicit
UTF-8 reads resolved it.

Successful build resource measurements (bytes, PlatformIO RAM/Flash):
The final incremental rebuild of all five normal/legacy-trace targets passed;
resource values below were unchanged. Post-build serial artifact checks and the
rebuilt S3 image check also passed. Both explicit S3 test builds passed earlier
with the finalized production sources. Stage 2 PC verification is complete;
physical hardware validation is recorded separately below.

| Environment | RAM | Flash | RAM delta from e5c2c65 | Flash delta |
| --- | ---: | ---: | ---: | ---: |
| xiao_esp32s3 | 114732 | 1568226 | +4176 | +1532 |
| xiao_esp32c3 | 136332 | 1436165 | 0 | -10 |
| xiao_esp32c5 | 65736 | 1651967 | 0 | -10 |
| xiao_esp32c6 | 141548 | 1598362 | 0 | -10 |
| xiao_esp32s3_obs_test | 115780 | 1574174 | +4176 | +1812 |
| xiao_esp32s3_obs_trace | 123820 | 1577782 | no equivalent baseline | no equivalent baseline |
| xiao_esp32c6_hid_trace | 149668 | 1602644 | no equivalent baseline | no equivalent baseline |

The S3 shared object occupies 4168 B of internal `.bss` (4096 B ring plus
72 B control/base); the active-format pointer is 4 B. Total linked RAM growth
is 4176 B including layout effects. OBS Worker stack remains 8192 B. C3/C5/C6
have no buffered-output symbols, object data or marker strings. The output
object has no malloc/new references; existing application allocations remain.
The formatter uses 512 B stack scratch; actual loop stack margin needs hardware
measurement. The simulated sink's 7-us timing verifies accounting only, not
ESP32 performance.

Test images are `.pio/build-obs-test/xiao_esp32s3_obs_test/firmware.bin` and
`.pio/build-obs-test/xiao_esp32s3_obs_trace/firmware.bin`; corresponding
`firmware.factory.bin` files contain the combined images. Stage 2 performs no
hardware upload. Serial changes remain uncommitted on
`feature/obs-websocket-integration`.

## Stage 3-A hardware acceptance (2026-10-24)

User-run hardware: XIAO ESP32S3 / ChainOSCPad PCB, explicit
`xiao_esp32s3_obs_test`, USB CDC at 115200, OBS WebSocket 5.x with authentication.
Full serial logs remain with the user; no connection credentials or full logs
are stored here. Astra reviewed the supplied log against the production code.

A01 boot, A02 m, A03 h, A04 help, A05 repeated m, A06 OBS connection/authentication,
A07 status, A08 alternating m/status, A09 sixteen admission replies, A10 execution
results, A11 no observed missing/incorrectly joined diagnostics, and A12 statistics
all PASS **within the performed and recorded normal-connection scope**.

Final observed SERIAL values: txPartial=0, txZero=0, outputRejected=0,
formatRejected=0, commandReplyDrops=0, resultDeferred=0, queuedBytes=0,
maxQueuedBytes=1376 B, pumpMaxUs=513 us. OBS remained Ready with attempts=1,
error=0, close=0, connectMs=89, authMs=168, responseMs=14, resultDrops=0 and
loopMaxUs=29127 us. Sixteen scene requests produced four Accepted and twelve Busy
replies; IDs 1–4 each returned success=1/code=100 once. Subsequent status counters
agreed: accepted=4, rejected=12, success=4, failed=0. The four-entry request FIFO
explains this outcome, but concurrent Worker dequeue means 4:12 is not a fixed
acceptance criterion.

Final OBS status reported internal free=80760 B, minimum=70316 B,
largest=38900 B, Worker stackLowBytes=3220 B. The last m before the burst reported
loopStackMin=5316 B and inputOverflows=0; these are **not post-burst measurements**.
No exhaustion was observed within this run; long-run margins are not established.

Interpretation confirmed by implementation/log review:

- `loopMaxUs` is the boot-local maximum interval between `obsTick()` calls, not
  an individual operation's execution time or Worker execution duration.
- `queuedBytes` counts committed unsent bytes at snapshot time, excluding the
  diagnostic currently being built. The first m's prior high-water value is normal.
- `maxQueuedBytes=1376` equals 16 admission lines of 86 B each with single-digit
  IDs (including Busy id=0). This is 33.6% committed occupancy of the 4096 B ring;
  admission still requires the full reservation budget, not merely free bytes for
  the eventual text. The PC 1520 B fixture used ten-digit IDs.
- `pumpMaxUs=513 us` is not by itself grounds for an additional code fix. Elapsed
  time can include scheduling/interrupt effects; timeout zero does not mean zero
  execution time. This does not explain or bound the 29127-us loop interval.
- `STACK IDLE not found` means lookup by that task name failed, not stack exhaustion.

Acceptance decision: Stages 1, 2 and 3-A are complete. The original diagnostic
loss/incorrect-joining symptoms did not recur in Stage 3-A. By user decision,
Stage 3-B (backpressure, saturation, partial/zero-write recovery, reconnect,
HID trace abnormal conditions) and Stage 3-C (condition-specific performance and
long-running coexistence) are **deferred, not passed**, and are not prerequisites
for OBS Phase 1 completion. Resume OBS development with Phase 2 design review.

Residual limits: PC tests cover many deferred mechanisms but do not prove their
physical USB behavior. This run exercised neither partial nor zero-byte writes.
USB driver acceptance is not host receipt; reconnect may retain only a suffix,
unobserved connection transitions cannot be marked, and reset loses RAM data.
Real saturated-input coexistence, trace behavior under pressure, timing causes,
and long-run heap/stack margins remain unverified. Deferral is not evidence of a
known defect.

## Deferred hardware verification procedure (Stage 3-B/C reference)

Use the explicit S3 OBS test image. Do not put credentials in source or captured reports. With terminal local echo disabled:

1. Open the monitor after boot. Check complete boot messages. Run `m`, then `h`, then `?`, waiting for each block to drain. Repeat `m` ten times. Lines must remain complete. `formatRejected` should remain zero for normal diagnostics.
2. Apply actual RAM settings and request status:

   ```text
   !{"op":"settings","enabled":true,"ip":"192.0.2.1","port":4455,"password":"REPLACE_LOCALLY"}
   !{"op":"status"}
   ```

   Expect complete state, memory, `authMs`, `responseMs` and SERIAL lines. Example address/password must be replaced locally.
3. Alternate `m` and status; then request:

   ```text
   !{"op":"scene","name":"REPLACE_WITH_EXISTING_SCENE","count":16}
   !{"op":"status"}
   ```

   Admission is not OBS success. Check Busy, individual result IDs/codes, counters and `resultDrops` separately. If output space is exhausted, complete retained blocks plus an explicit loss notification are valid; partial blocks without a USB boundary are not.
4. Retry status after output drains to collect deferred results. Test one nonexistent scene and record its individual result code.
5. Enable `d`, perform supported configuration GET/save operations, and confirm complete save/periodic blocks. Turn `d` off afterwards. Failed output admission must not prevent a save or an OBS Panic.
6. For true backpressure, keep the port open but stop reading long enough to fill the OS/driver receive buffers; submit repeated diagnostics if supported by the harness. A closed terminal alone is not proof of backpressure. Check `txZero`/`txPartial`, bounded queuedBytes, input operation and OBS operation; resume reads and verify retained byte order. Saturation may reject diagnostics as documented.
7. Separately close/reopen or physically reconnect without resetting the firmware where possible. Expect the boundary notification on a loop-observed reconnect and a retained suffix. If the board resets, assess as a new boot rather than a retention test.
8. On the explicit S3 OBS trace build, start recording with `2`, stop with `0`, then dump with `p`. While recording, output may accumulate/refuse but is not pumped. After `0`, output must resume. Repeat with backpressure and check trace header, event sequence and `TRACE END` with no skipped frozen lines. A new recording intentionally cancels an unfinished dump as before.
9. Measure separate boots for OBS disabled/enabled, diagnostics idle/repeated, and Wi-Fi loss/AP-only. Record `loopMaxUs`, `pumpMaxUs`, Heap minimum/largest, loop stack, input overflow and actual input/output behavior. Compare the earlier 70127-us loop interval without treating it as an automatic failure threshold.

LittleFS queries, Wi-Fi/AP setup, existing temporary Strings and other diagnostics collection remain synchronous. Nonblocking CDC output does not eliminate those costs. USB driver acceptance is not PC display acknowledgment.
