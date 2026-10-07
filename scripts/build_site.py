"""Package CI-built firmware and a same-origin ESP Web Tools manifest for Pages."""
import hashlib
import argparse
import json
import os
import re
import shutil
import subprocess
import markdown
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "_site"
CHIPS = ("s3", "c3", "c6", "c5")
FIRMWARE_PATHS = ("src", "web", "include", "lib", "boards", "platformio.ini", "partitions*.csv", "scripts/build.py")


def firmware_changed(commit):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Invalid published firmware commit")
    result = subprocess.run(["git", "diff", "--quiet", commit, "HEAD", "--", *FIRMWARE_PATHS], cwd=ROOT)
    if result.returncode not in (0, 1):
        raise RuntimeError("Published firmware commit is not available in checkout history")
    return result.returncode == 1


def fetch(base, path):
    request = Request(base.rstrip("/") + "/" + path, headers={"Cache-Control": "no-cache"})
    with urlopen(request, timeout=60) as response:
        return response.read()


def reuse_published(base, metadata):
    """Keep firmware provenance intact; never label old binaries with the site commit."""
    info = json.loads(metadata)
    manifest_data = fetch(base, "manifest.json")
    manifest = json.loads(manifest_data)
    expected = {f"firmware/ChainPad-ESP32-{chip.upper()}.bin": f"ESP32-{chip.upper()}" for chip in CHIPS}
    files = info["files"]
    if len(files) != 4 or {f["path"] for f in files} != set(expected):
        raise ValueError("Published firmware file set is incomplete")
    builds = [{"chipFamily": expected[f["path"]], "parts": [{"path": f["path"], "offset": 0}]} for f in files]
    if sorted(manifest["builds"], key=lambda b: b["chipFamily"]) != sorted(builds, key=lambda b: b["chipFamily"]):
        raise ValueError("Published manifest does not match build metadata")
    if manifest["version"] != f'{info["version"]}+{info["commit"][:7]}':
        raise ValueError("Published manifest version mismatch")
    payloads = {}
    for file in files:
        if file["chipFamily"] != expected[file["path"]]:
            raise ValueError("Published chip family mismatch")
        data = fetch(base, file["path"])
        if len(data) != file["bytes"] or hashlib.sha256(data).hexdigest() != file["sha256"]:
            raise ValueError(f'Published firmware checksum mismatch: {file["path"]}')
        payloads[file["path"]] = data
    for path, data in {**payloads, "build.json": metadata, "manifest.json": manifest_data}.items():
        target = OUTPUT / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def prepare(base):
    # Compare to what is actually deployed, not merely the previous push. This also
    # covers queued site updates following a failed firmware deployment.
    metadata = fetch(base, "build.json")
    rebuild = firmware_changed(json.loads(metadata)["commit"])
    if not rebuild:
        reuse_published(base, metadata)
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"rebuild={'true' if rebuild else 'false'}\n")
    print("Firmware changed: build required" if rebuild else "Verified published firmware reused; no firmware build")


def copy_site():
    shutil.copytree(ROOT / "site", OUTPUT, dirs_exist_ok=True, ignore=shutil.ignore_patterns("*.md", ".gitkeep"))
    source = ROOT / "site/getting-started/index.md"
    template = (ROOT / "scripts/templates/getting-started.html").read_text(encoding="utf-8")
    content = markdown.markdown(source.read_text(encoding="utf-8"), extensions=["fenced_code", "tables", "toc", "sane_lists"], output_format="html")
    destination = OUTPUT / "getting-started/index.html"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(template.replace("<!-- DOCUMENT_CONTENT -->", content), encoding="utf-8")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    (OUTPUT / "site-build.json").write_text(json.dumps({"commit": commit}) + "\n", encoding="utf-8")


def main():
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    version = re.search(r'doc\["version"\] = "([^"]+)"', source).group(1)
    OUTPUT.mkdir(exist_ok=True)
    copy_site()
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", metavar="PUBLISHED_URL", help="Decide whether a build is needed; verify and reuse unchanged published firmware")
    parser.add_argument("--site-only", action="store_true", help="Overlay site content onto the verified firmware prepared earlier")
    parser.add_argument("--preview", action="store_true", help="Generate local site preview without requiring firmware artifacts")
    args = parser.parse_args()
    if args.prepare:
        prepare(args.prepare)
    elif args.preview:
        copy_site()
    elif args.site_only:
        if not (OUTPUT / "build.json").is_file() or not (OUTPUT / "manifest.json").is_file():
            raise RuntimeError("Run --prepare before --site-only")
        copy_site()
    else:
        main()
