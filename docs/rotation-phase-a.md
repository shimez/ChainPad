# Rotation Value: Phase A

This is the Phase A checkpoint report. Phase B subsequently integrates the model
into Config; see [rotation-phase-b.md](rotation-phase-b.md) for current storage and
sizes. The layout probe now reports the integrated Config rather than the old
Phase A comparison struct.

Phase A provides only data types, validation, single delivered-step boundary
handling and pure value mapping. It does not connect Rotation to Config, storage,
input dispatch, transports, Runtime, or WebUI. No v2 serialization exists yet.

## Model

`src/rotation.h` defines Mode, Axis, dedicated OSC Int/Float and MIDI CC outputs,
and a capacity-parameterized settings container. Defaults: ActionChain, 20 steps,
initial 0, Stop, zero outputs. Zero configured outputs is valid in either mode.
Capacity is independent of the existing 224 Actions; inactive settings validate too.

Position and N are uint32_t, with N restricted to 1..65535. Typed integer fields
cannot retain fractions: Phase B parsers must reject fractional/out-of-range JSON
**before** converting to these types. MIDI transport normalization likewise belongs
to the later import/decode layer; this model accepts USB/BLE/Both destinations.

The range union has trivial aggregate members. When choosing OscFloat, assign
`range.floating = RotationFloatRange{0, 1}` to activate and initialize that member.
Do not only change the kind tag and then read a different union member.

`stepRotation` returns validity, not whether Position changed. The caller compares
old/new Position before creating a Generation. It handles one delivered event,
without changing the existing input acquisition/aggregation algorithm.

Mapping validates its inputs and leaves its result unchanged on failure. Integer
mapping rounds the full signed weighted numerator, not the delta from Start.
Float normalization rejects non-finite/out-of-float32-range doubles, permits
underflow to signed zero, and interpolation uses double intermediates. Endpoints
are returned directly. These functions allocate no heap memory.

## Measurement and tests

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host.ps1
powershell -ExecutionPolicy Bypass -File tests/measure_rotation_layout.ps1
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e xiao_esp32c6 -j 4
```

C6 target ABI compile-only measurements (not a hardware runtime measurement):

| Type | Bytes |
|---|---:|
| RotationAxis | 12 |
| RotationRange | 8 |
| RotationOutput | 208 |
| RotationMappedValue | 12 |
| RotationSettings<16> | 3348 |
| Existing Config | 79300 |
| Config plus separate 16-output settings in a probe struct | 82648 |

Actual Config is unchanged. C6 firmware static RAM remains 135444 bytes.
The host x64 Config is 79640 bytes due to pointer/alignment differences; it must
not be substituted for the C6 measurement.

Tests cover N=1/20/65535, Stop endpoints, Wrap period including 65536, invalid axis
and position, full ascending int32 sweep against an independent reference,
descending endpoints, signed midpoint rounding and neighbors, constant mappings,
MIDI mapping/ranges, maximum-length/unterminated OSC address, finite float32 bounds,
opposite-sign extreme interpolation, subnormal values, underflow, NaN/infinities,
signed-zero endpoint preservation, empty settings and copied settings isolation.

## Provisional capacity proposal

Start hardware evaluation with **16 outputs**, not a finalized public limit.
Settings cost is `20 + 208 * capacity` bytes: 1684 for 8, 3348 for 16, 6676 for 32.
An Action is 352 bytes, so dedicated outputs save 2304 bytes over sixteen Action
records alone, while retaining the full 192-byte address allowance per output.
The 16-output settings addition is about 4.2% of current C6 Config size.

Pending state, JSON scratch memory, filesystem records and actual transport load
are NOT included in these figures. Measure them in subsequent approved phases;
16 Outputs with Both can fan out to 32 MIDI messages per Generation on S3.
C6 tests BLE after normalization; Both requires S3 hardware evaluation.

Before Phase B, confirm provisional capacity and integrate the model with Config
copy/assignment and mandatory v2 transaction validation. No Phase A defect currently
blocks that work. Host tests and C6 compilation are not an on-device capacity or
latency qualification.
