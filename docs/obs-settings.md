# OBS settings persistence — Phase 2 M1

## Scope and ownership

ESP32S3 only (`CHAINPAD_HAS_OBS`), using the existing fixed-size `ObsSettings`.
M1 has no setup/loop caller: it does not change normal OBS connection behavior.
One setup/loop owner must serialize all Store operations, including future HTTP
requests and factory reset. These synchronous operations must not run in Worker
or concurrently with partition formatting. No Config copy or Action dependency
is introduced. Existing Config v2, Preset, export/import and USB CDC are unchanged.

## M2-facing API

`src/obs_settings_store.h`, namespace `chimera::obssettings`:

- `Store::load(Error&)`: inspect disk, return `State` and update availability.
- `Store::state()`: `Missing`, `Ready`, `Corrupt`, `UnsupportedVersion`, `IoError`.
- `Store::settings()`: borrowed pointer valid only while the Store is unchanged;
  non-null only in Missing/Ready. Copy the fixed settings before asynchronous use.
- `Store::save(json, size, replaceInvalid=false)`: synchronous validate/stage/commit;
  `Error::None` means persistence succeeded, not Worker application or connection.
- `encodePublic(settings, out, capacity)`: bounded JSON without password; returns
  byte count excluding terminating NUL, or zero. Only call with available settings.
- `stateName`/`errorName`: fixed nonsecret codes, never raw input or server comments.

Update JSON requires `enabled` (bool), `ip` (string), and `port` (integer 1–65535).
Only `password` may be omitted; unknown fields are rejected. `replaceInvalid` is
a separate argument, not a setting. Password omission uses freshly verified disk
settings, or empty password for Missing. It is rejected without a trusted source.
Explicit empty string clears; nonempty string replaces; null/wrong types fail.
Failed decoding leaves its output unchanged.

IPv4 is limited to 15 bytes, digits/dots and Arduino `IPAddress::fromString`, as
used by Worker. Empty IP is allowed only when disabled. Disabled retains entered
connection values. Hostnames, URL syntax and IPv6 are rejected. Password uses
the shared `obs::validText`: valid UTF-8, at most 128 bytes, no NUL/forbidden
controls, no trimming or normalization. JSON escaping is decoded before validation.
The Native IPAddress adapter approximates the Arduino parser; target builds use
the actual parser. JSON input is at most 1024 bytes, nesting limit 2, with no raw
NUL or non-whitespace trailing input. ArduinoJson parsing semantics apply (this
is not a separate strict lexical JSON parser; duplicate keys use parser behavior).

## Files and transaction

Uses the already-mounted `settings` LittleFS at `/littlefs`. Never mounts,
formats or reinitializes it. Stored logical paths:

- `/obs-settings.json`: active, all fields mandatory, independent `storageVersion:1`.
- `/obs-settings.tmp`: staging only, never an input to boot/load/password retention.

Example shape (password is deliberately empty):

```json
{"storageVersion":1,"enabled":false,"ip":"","port":4455,"password":""}
```

**Password is plaintext on LittleFS.** Public JSON instead has only enabled, ip,
port and `passwordConfigured`. Do not log request bodies, stored JSON, settings
structs or private buffers. No OBS password is added to Config export or Preset.
Temporary-buffer wiping is best-effort, not a secure-memory guarantee.

Save re-inspects active disk to detect corruption since the last load, validates
the complete update and resolves password before opening tmp. POSIX VFS is used
because Arduino File flush/close hide errors. Save checks write count, fsync and
close, reopens/revalidates tmp and compares all settings, then renames over active.
Only successful rename publishes the candidate to saved RAM. There is no unlink
of active, retry with deletion, or fallback to tmp. A leftover tmp is ignored on
load and overwritten by a later authorized save. Write/verify failures retain the
old active file and saved RAM in the tested fault model.

Missing means verified absence on a mounted root: defaults are available but
unsaved and no file is created. Corrupt/version/read errors preserve files and
make settings unavailable, clearing the cached copy. A failed disk inspection
during save also invalidates availability; it never publishes the candidate.
Ordinary input/write failures with healthy disk retain the existing RAM snapshot.

Readable Corrupt/UnsupportedVersion files require `replaceInvalid=true` plus
explicit password or clear. Validation still applies. IoError is not replaceable:
restore readable storage and retry/load first. Settings-partition probe failures
always fail closed. Error/state are separate: UnsupportedVersion may have
`Error::None` because JSON parsed successfully; callers must inspect State.
Storage errors do not become global Action Chain stop conditions.
Existing Factory Reset formats settings and consequently removes these files.

## Rename evidence and limits

The installed Arduino FS `VFSImpl::rename` calls POSIX rename directly without
deleting the destination. The installed S3
`framework-arduinoespressif32-libs/esp32s3/lib/libjoltwallet__littlefs.a` was
inspected with nm/objdump: `vfs_littlefs_rename` delegates to `lfs_rename` under
semaphore protection, checking open descriptors; fsync delegates to
`lfs_file_sync`. All OBS file descriptors are closed before rename.
The installed S3 `dependencies.lock` identifies `joltwallet/littlefs` **1.22.2**,
component hash `fab83a8a57de01b05441557d78e9d10c755413a1836af4b2762b539add0c14b1`.
Its upstream tag pins LittleFS commit `6cb4e86540eca0d9ba62500a298385c9d863c8be`.
Inspection of `lfs_rename_` at that revision confirms same-type destination
replacement: destination delete/create/move attributes are passed together to
`lfs_dir_commit`, rather than a prior independent remove operation. Path/type
validation errors return before that commit. A later commit I/O error is not a
universal rollback guarantee. References:

- <https://github.com/joltwallet/esp_littlefs/tree/v1.22.2>
- <https://github.com/littlefs-project/littlefs/blob/6cb4e86540eca0d9ba62500a298385c9d863c8be/lfs.c>

Native rename faults model failure **before replacement** and verify old-file
retention; interrupted tmp never replaces active. Arbitrary media failure or an
ambiguous commit inside LittleFS is not proven to preserve old bytes by these
tests. A rename error returns `Error::Rename` and retains the previous RAM copy;
M2 should treat persisted disk state as uncertain, reload before further use and
report failure rather than claiming rollback. No physical power-cut atomicity,
Flash endurance or universal rollback guarantee is asserted. Stronger guarantees
require installed-source fault injection and hardware testing; do not substitute
a delete-then-rename procedure.

## Verification and resource method

`powershell -File tests/run_obs_settings.ps1` compiles the real codec/protocol and
Store with an in-memory POSIX boundary. It covers defaults, missing, save/load,
password preserve/change/clear, disabled retention, escaped UTF-8 roundtrip,
public privacy, input failure retention, validation boundaries, corrupt/version,
explicit replacement, stale corruption, unmounted storage, read/open/stat/close
errors, short/full writes, sync/write-close failure, tmp open/read/verification
failure, rename failure and interrupted tmp. Config/pending sentinel bytes remain
unchanged. Failure output is line-number-only, not fixture values.
This boundary is not a LittleFS emulator. Exhaustive allocator-failure injection,
physical power cuts, endurance and target runtime stack watermark are unperformed.

Normal builds can dead-strip the new implementation. For honest M2 cost evaluation:

```text
pio run -c platformio.obs-settings-link.ini -e xiao_esp32s3 -j 2
```

This separate build root forces `chainpadObsSettingsLinkProbe` and one Store into
the ELF, retaining load/save/public/stored codecs without executing the probe.
Inspect Xtensa `entry` instructions in the linked ELF for per-function frames;
these are not a complete runtime stack bound (ArduinoJson/VFS/allocator calls add
depth). JSON scratch uses heap, at most
1025 bytes; the sequential JSON allocator budget is 8192 bytes including headers.
Save uses fixed-size baseline/candidate/verification settings, not a full Config.

### M1 verification record (2026-10-25)

- OBS settings Native: PASS, including write-close/tmp-read faults and input
  failure retention added during review.
- Existing OBS protocol/auth, patched frames, production Worker and console: PASS.
  The first foreground runner timed out; the complete background rerun passed.
  Existing MSVC WebSockets C5208 warnings remain.
- Existing storage-fault suite: PASS on S3/C5/C6, including probe, Config/Wi-Fi
  fault retention, factory reset, builtin upgrade and WebUI GET regression.
- S3 normal, S3 OBS TEST, C3/C5/C6 normal builds: PASS. The first concurrent S3
  normal build failed in package-script cleanup after linking; sequential retry
  passed. Run target builds sequentially because the installed platform mutates
  shared package helper files even across separate build roots.
- USB CDC-specific tests were not rerun: serial implementation and its callers
  are unchanged. Phase 1 Stage3-B/C remain deferred, not passed.
- C3/C5/C6: both new objects have text/data/bss **0/0/0** and the ELF has no
  `obssettings::` symbols. All previously tracked production files are unchanged.

Normal build sizes (bytes; baseline is Phase 1 final):

| Build | RAM before → after | Flash before → after | RAM / Flash delta |
| --- | ---: | ---: | ---: |
| S3 normal | 114732 → 114732 | 1568226 → 1568226 | 0 / 0 |
| S3 OBS TEST | 115780 → 115780 | 1574174 → 1574210 | 0 / +36 |
| C3 normal | 136332 → 136332 | 1436165 → 1436165 | 0 / 0 |
| C5 normal | 65736 → 65736 | 1651967 → 1651967 | 0 / 0 |
| C6 normal | 141548 → 141548 | 1598362 → 1598362 | 0 / 0 |

The OBS TEST +36-byte difference is the measured build delta; it is not the cost
of the new Store. Normal S3 dead-strips unused APIs. Forced-link results must be
used to estimate the retained M1 implementation instead.

Forced-link S3: **RAM 114876 B / Flash 1574642 B**, versus normal
114732 / 1568226: **+144 B RAM / +6416 B Flash** (includes link probe).
`nm -S -C` confirms Store load/save, decodeUpdate/decodeStored,
encodePublic/encodeStored and the linker-root probe all have defined text symbols.
The retained Store symbol is 152 B; net RAM delta differs because of other
link-layout changes. This is static RAM, not peak heap during save.

Linked Xtensa function-entry frames (`objdump -d -C`):

| Function | Frame bytes |
| --- | ---: |
| Store::save | 512 |
| Store::load | 336 |
| readFile | 144 |
| decodeUpdate / decodeStored | 112 / 128 |
| fields | 288 |
| parse | 160 |
| encode | 192 |

The nested save → decodeUpdate → fields → text path already accounts for
992 B of these frames before deeper library calls; 512 B is not a total-stack
claim. No large automatic JSON/Config buffer is present. Runtime stack headroom
must be measured when M2 adds its HTTP caller and request/response buffers.
The first resource-build attempts exposed config override/script ordering and
probe include-path issues; these were corrected before this successful ELF was
measured. Disassembly is used because a post-script `-fstack-usage` flag did not
reach the separately cloned production compilation environment.

## M2 follow-up

Add HTTP request limits and single-owner execution, load during setup, and apply
only available trusted settings. Report saved, apply accepted/failed and actual
connection state separately. OBS-specific failure must actively call
`obsTick(false)` and keep permission false on subsequent loops; merely skipping
calls does not stop Worker. Socket shutdown is asynchronous; in-flight operations
remain uncertain. Reconnect uses trusted saved RAM, does not rewrite Flash or
consume unsaved UI edits, rejects Disabled and reapplies settings to clear terminal
errors. HTTP/API and WebUI work await separate approval after M1 review.
