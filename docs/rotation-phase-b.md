# Rotation Value: Phase B (storage and Preset boundary)

This phase integrates settings only. Rotation Position, Generation, pending sends,
mapping Preview and the dedicated editor are not implemented here. Capacity is
**16 Outputs for evaluation**, not the final product limit.

## Formats

- Full Preset: `format: chainpad-configuration`, `version: 2`, `schemaVersion: 2`.
- Device Config: `schemaVersion: 2`.
- LittleFS header: `storageVersion: 2`.
- Key Preset: `chainpad-key-preset`, `version: 1` (unchanged).
- No v1 full-Preset or LittleFS migration.
- Full Presets omit Wi-Fi credentials; import preserves the current Wi-Fi fields.

The existing 28 Chains retain their IDs and storage. Config additionally owns:

```json
{
  "encoderRotation": {
    "mode": "actionChain",
    "rotationValue": {
      "rangeSteps": 20,
      "initialPosition": 0,
      "boundary": "stop",
      "outputs": []
    }
  }
}
```

Mode is `actionChain` or `rotationValue`; Boundary is `stop` or `wrap`.
Both sides are always required, retained and validated. Empty Outputs are valid.

Output examples:

```json
{"protocol":"osc","transport":"wifi","address":"/Light","type":"float","start":0,"end":1}
{"protocol":"osc","transport":"wifi","address":"/Count","type":"int","start":-10,"end":10}
{"protocol":"midi","message":"cc","transport":"both","channel":1,"number":7,"start":127,"end":0}
```

Integer fields are checked before narrowing. OSC Float endpoints normalize to
float32. The wire/storage encoder emits precise JSON number literals because
ArduinoJson's default float serialization loses endpoint precision. No string-valued
number fields are introduced. MIDI transports normalize to BLE on C3/C6/C5.
Browser import reports normalization and duplicate destinations after normalization.
Duplicates are retained, not rejected or merged.

`json_wire.h` also escapes all otherwise-literal C0 control characters in JSON.
This fixes an existing ArduinoJson boundary issue discovered with maximum OSC
String settings: e.g. U+0001 must be emitted as `\u0001`, not a literal byte.
Record size checks count the escaped wire bytes; storage and Config GET use the
same writer. Common escapes and UTF-8 bytes remain as emitted by ArduinoJson.

## Transaction API

1. `POST /api/config/begin` with `{"schemaVersion":2,"network":{...}}`.
2. `PUT /api/config/chain?token=T&input=I` for I=0..27 in order.
3. `PUT /api/config/rotation?token=T` with the `encoderRotation` object above.
4. `POST /api/config/commit?token=T`.

Network includes device Wi-Fi credentials and OSC destination. A missing or
unsupported schema, invalid record, or missing stage cannot commit. The active
file and active configuration remain unchanged on validation/write/rename failure.
The full pending file is validated before atomic rename. If the subsequent active
read fails, output is disabled and the error requests a restart; partial settings
are not used to dispatch actions.

LittleFS contains one header, 28 Chain records, and one Rotation record. Chain
scratch (5636 bytes) is freed before allocating Rotation scratch (3348 bytes).
The device never allocates a full Config copy for storage. Record size remains
bounded at 24576 bytes. Wi-Fi-only save stages the existing Rotation settings too.
Current Position, Generation and pending send state are not represented in Config
and cannot enter the encoded records or Full Preset.

## Legacy protection and recovery

`/api/status` exposes `storageState` (`missing`, `ready`, `unsupportedVersion`,
`corrupt`, `ioError`) and `configOutputsAllowed`.

With a v1 header, the original file is retained, settings output is disabled, and
the setup AP/admin UI remains available. Wi-Fi credentials from v1 are not applied.
The Config GET returns an empty v2 editing template; status/UI explicitly identify
it as an unloaded configuration, not the contents of the legacy file.

Ordinary begin/Wi-Fi saves cannot replace v1. The recovery button explicitly asks
to replace the old configuration, and only that path supplies
`replaceUnsupported: true` to begin. The old active file survives until a fully
validated replacement commits. No automatic format, deletion or conversion occurs.
Corrupt files are separately reported and are not treated as unsupported versions.

Phase B advertises `rotationRuntimeSupported: false`; a stored Rotation Value
mode does not run the inactive CW/CCW chains through physical inputs or trigger API.
Position execution and active-only All Notes selection belong to Phase C.

## Verification

```powershell
powershell -ExecutionPolicy Bypass -File tests/run_host.ps1
node tests/test_presets.cjs
powershell -ExecutionPolicy Bypass -File tests/measure_rotation_layout.ps1
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e xiao_esp32c6 -j 2
```

C6 target ABI: Config **82648 bytes** (previously 79300; +3348), Output 208,
Axis 12, Rotation settings 3348. Host x64 Config is 82992 due to pointer/alignment
differences. Firmware build static RAM is **138788 bytes** and flash 1542448 bytes.

Host storage tests include 224 maximum-length escaped OSC Actions plus 16 mixed
Outputs, exact float32 endpoint reload, strict numeric validation, missing stages,
bad schema, v1 detection/protection/explicit replacement, corrupt/missing distinction,
Wi-Fi preservation, scratch-allocation failure and Config-copy isolation. A simulated
post-rename apply failure disables output until a successful reload. The mixed stress fixture measured
pending=284858 and active=284874 bytes (the latter includes changed Wi-Fi strings).
These are fixture file sizes, not filesystem allocation or hardware peak RAM.

The host suite also preserves existing Action/Wait/MIDI/HID/storage regression tests,
and tests Rotation BLE normalization for C3/C6/C5. Preset tests cover v2/full,
v1/key, explicit v1/full rejection, inactive settings, duplicate warnings after
normalization, float32 normalization, runtime exclusion and provisional capacity.

Browser fixture checks exercised the actual Configurator import/save/reload flow,
BLE normalization, duplicate warning, and the required Rotation stage. A simulated
unsupported status displayed the recovery panel. Ordinary save omitted replacement
authorization; only the explicit recovery operation supplied it. Actual device
legacy protection is tested separately against Firmware, not inferred from the fixture.

## Hardware checkpoint 1: legacy recovery

On the connected XIAO ESP32C6, the original v1 Config JSON (224 Actions) and the
entire 0x330000..0x3fffff LittleFS partition were backed up before replacement.
The firmware-only update preserved that partition. At first v2 boot:

- `storageState=unsupportedVersion`, output disabled, Setup AP/admin API available.
- Original active file remained 92665 bytes; pending was absent; filesystem used
  102400 / 851968 bytes.
- Missing/old/fractional Schema requests, ordinary v2 begin, Wi-Fi save and trigger
  were rejected; the legacy file remained in place.
- Explicit replacement staged a new empty v2 configuration; commit succeeded.
- Restart restored the new v2 settings exactly.

Backups/logs are local under `%LOCALAPPDATA%\Temp\opencode`, outside the repository.
The JSON backup includes credentials and is not a portable Full Preset. The complete
LittleFS backup SHA256 is
`35977303409f2f8f67430af905faf7528231ca0ed0c45b58be5363898c171482`.
Full-chip backup was not completed (repeatable transfer failure in the application
region); the settings partition and Config JSON backups were completed successfully.

## Hardware checkpoint 1: maximum configuration (passed)

After correcting C0 JSON escaping, the C6 test completed all assertions:

- 224 Actions with 192-byte quote-heavy addresses and 128-byte U+0001 strings,
  plus 16 mixed OSC Int/Float and MIDI CC Outputs.
- N=65535, Initial=32768, Wrap; float32 maximum/subnormal endpoints preserved.
- Both destinations normalized to BLE on C6; no Rotation runtime transmission.
- Missing Rotation stage and fractional Range rejected without applying changes.
- Full Config GET parsed with a strict JSON parser and exactly matched the expected
  normalized settings, both immediately and after restart.
- Wi-Fi-only save and its restart preserved the entire configuration.
- Action Chain mode saved/reloaded inactive Rotation settings and CW/CCW settings.

Measured values (bytes):

| Checkpoint | Result |
|---|---:|
| Maximum active record file, Rotation Value mode | 284876 |
| Replacement pending file, Action Chain mode | 284874 |
| LittleFS used with both files present | 581632 / 851968 |
| LittleFS free with both files present | 270336 |
| LittleFS used after maximum-config commit | 294912 |
| Maximum-config commit-before free heap | 123636 |
| Maximum-config commit-after free heap | 121192 |
| Minimum free heap observed during the save test boot | 60260 |
| Largest free block at those commit checkpoints | 79860 |
| Loop stack low-water mark at maximum-config commit | 5488 |
| Loop stack low-water mark after Wi-Fi save | 5264 |

The heap minimum is the boot's cumulative low-water mark (including HTTP parsing
and deliberately rejected requests), not an isolated Rotation allocation cost.
The maximum-config commit took about 3.9 seconds; the maximum Wi-Fi-only save took
about 10.7 seconds with diagnostics enabled. Generation/transport-load qualification
remains for later phases. Pending was absent after successful commits.

At completion, the device was left on Phase B firmware with an **empty v2
configuration**, original Wi-Fi credentials and original OSC destination. Its
active file is 948 bytes; pending is absent; LittleFS used=12288 bytes. The PC was
returned to its previous Wi-Fi network and the temporary test profile was removed.
The original 224-Action v1 settings remain in the external backups, not the active
v2 configuration. No automatic migration was performed.
