"""Package CI-built firmware and a same-origin ESP Web Tools manifest for Pages."""
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "_site"
CHIPS = ("s3", "c3", "c6", "c5")


def main():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    version = re.search(r'doc\["version"\] = "([^"]+)"', source).group(1)
    OUTPUT.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "site/index.html", OUTPUT / "index.html")
    (OUTPUT / "firmware").mkdir(exist_ok=True)
    builds, files = [], []
    for chip in CHIPS:
        family = f"ESP32-{chip.upper()}"
        firmware = ROOT / f".pio/build/xiao_esp32{chip}/firmware.factory.bin"
        data = firmware.read_bytes()
        # Merged images are padded from zero, including C5's bootloader at 0x2000.
        boot_offset = 0x2000 if chip == "c5" else 0
        if data[boot_offset] != 0xE9 or data[0x10000] != 0xE9:
            raise ValueError(f"Invalid merged image offsets: {family}")
        path = f"firmware/ChainPad-{family}.bin"
        shutil.copyfile(firmware, OUTPUT / path)
        builds.append({"chipFamily": family, "parts": [{"path": path, "offset": 0}]})
        files.append({"chipFamily": family, "path": path, "bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {"name": "ChainPad", "version": f"{version}+{commit[:7]}",
                "new_install_prompt_erase": True, "builds": builds}
    for name, value in (("manifest.json", manifest),
                        ("build.json", {"version": version, "commit": commit, "files": files})):
        (OUTPUT / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    print(f"Packaged {len(builds)} firmware images for {commit[:7]} in {OUTPUT}")


if __name__ == "__main__":
    main()
