"""Check generated ESP partition offsets and merged-image placement."""
import argparse
import hashlib
import struct
from pathlib import Path

LAYOUTS = {
    "xiao_esp32s3": (8, 0x600000, 0x630000),
    "xiao_esp32c3": (4, 0x300000, 0x330000),
    "xiao_esp32c6": (4, 0x300000, 0x330000),
    "xiao_esp32c5": (8, 0x600000, 0x630000),
}


def check(environment, build_dir=None, rotation_probe=False):
    flash_mb, app_size, config_offset = LAYOUTS[environment]
    build = (Path(build_dir) if build_dir else Path(__file__).resolve().parents[1] / ".pio/build") / environment
    partitions = (build / "partitions.bin").read_bytes()
    entries = {}
    for index in range(0, len(partitions), 32):
        magic = struct.unpack_from("<H", partitions, index)[0]
        if magic != 0x50AA:
            break
        _, kind, subtype, offset, size, label, _ = struct.unpack_from("<HBBII16sI", partitions, index)
        entries[label.rstrip(b"\0").decode()] = (kind, subtype, offset, size)
    assert entries["factory"] == (0, 0, 0x10000, app_size), entries
    assert entries["settings"] == (1, 0x82, config_offset, flash_mb * 1024 * 1024 - config_offset), entries
    assert "config_nvs" not in entries
    ranges = sorted((item[2], item[2] + item[3]) for item in entries.values())
    for previous, following in zip(ranges, ranges[1:]):
        assert previous[1] <= following[0], "Partition overlap"
    assert ranges[-1][1] <= flash_mb * 1024 * 1024
    app = (build / "firmware.bin").read_bytes()
    factory = (build / "firmware.factory.bin").read_bytes()
    assert len(app) <= entries["factory"][3]
    assert factory[0x10000:0x10000 + len(app)] == app
    assert factory[0x8000:0x8000 + len(partitions)] == partitions
    assert app[0] == 0xE9 and app[3] >> 4 == (3 if flash_mb == 8 else 2), "Incorrect flash header"
    chip = environment.removeprefix("xiao_").upper()
    assert f"XIAO {chip} / ChainOSCPad PCB".encode() in app, "Wrong board identity in firmware"
    assert (b"ChainPad MIDI\0" in app) == (environment == "xiao_esp32s3"), "Unexpected USB MIDI descriptor"
    assert (b"/api/rotation-test/hold\0" in app) == rotation_probe, "Rotation test hook must not appear in production images"
    print(f"PASS {environment}: partitions / image placement / {flash_mb}MB header / board identity / USB descriptor gating")
    print("firmware.bin SHA256", hashlib.sha256(app).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", nargs="+", choices=LAYOUTS, default=["xiao_esp32s3"])
    parser.add_argument("--build-dir", help="Alternate PlatformIO build directory")
    parser.add_argument("--rotation-probe", action="store_true", help="Explicitly check a hardware test-only image; never distribute it")
    args = parser.parse_args()
    for environment in args.env:
        check(environment, args.build_dir, args.rotation_probe)
