"""Publish verified Pages firmware as an immutable, versioned GitHub Release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import changelog as history

ROOT = Path(os.environ.get("CHAINPAD_SOURCE_ROOT", Path(__file__).resolve().parents[1])).resolve()
FAMILIES = ("ESP32-S3", "ESP32-C5", "ESP32-C6")


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True, encoding="utf-8").strip()


def version_tuple(value):
    if not isinstance(value, str) or not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value):
        raise ValueError("Release version must be major.minor.patch")
    return tuple(map(int, value.split(".")))


def validate(directory, require_history=False):
    info = json.loads((directory / "build.json").read_text(encoding="utf-8"))
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    version_tuple(info["version"])
    if not re.fullmatch(r"[0-9a-f]{40}", info["commit"]):
        raise ValueError("Invalid firmware commit")
    if manifest["version"] != f'{info["version"]}+{info["commit"][:7]}':
        raise ValueError("Manifest version mismatch")
    files = info["files"]
    if len(files) != len(FAMILIES) or {f["chipFamily"] for f in files} != set(FAMILIES):
        raise ValueError("Unexpected firmware distribution targets")
    expected_builds, assets = [], [directory / "build.json", directory / "manifest.json"]
    ref = history.history_reference(info, required=require_history)
    if ref is not None:
        path = directory / ref["path"]
        history.verify_payload(path.read_bytes(), info)
        assets.append(path)
    elif (directory / "changes.json").exists():
        raise ValueError("Unreferenced history file in legacy distribution")
    for file in files:
        expected = f'firmware/ChainPad-{file["chipFamily"]}.bin'
        if file["path"] != expected:
            raise ValueError("Unexpected firmware path")
        path = directory / expected
        data = path.read_bytes()
        if len(data) != file["bytes"] or hashlib.sha256(data).hexdigest() != file["sha256"]:
            raise ValueError("Firmware size/SHA256 mismatch")
        boot_offset = 0x2000 if file["chipFamily"] == "ESP32-C5" else 0
        if len(data) <= 0x10000 or data[boot_offset] != 0xE9 or data[0x10000] != 0xE9:
            raise ValueError("Invalid merged firmware image")
        expected_builds.append({"chipFamily": file["chipFamily"], "parts": [{"path": expected, "offset": 0}]})
        assets.append(path)
    if sorted(manifest["builds"], key=lambda b: b["chipFamily"]) != sorted(expected_builds, key=lambda b: b["chipFamily"]):
        raise ValueError("Manifest firmware references mismatch")
    return info, assets


def release_notes(changelog, version, commit):
    match = re.search(rf"^## {re.escape(version)}(?:[ \t]+[^\n]*)?\n(.*?)(?=^## |\Z)", changelog, re.M | re.S)
    if not match or not match[1].strip():
        raise ValueError(f"Missing CHANGELOG section for {version}")
    return (f"## 主な変更\n\n{match[1].strip()}\n\n"
            "## インストール\n\n"
            "[Web Installer](https://shimez.github.io/ChainPad/installer/)は常に公開中の最新版をインストールします。\n"
            "このバージョンを指定する場合は添付の統合イメージを使用してください。\n"
            "配布対象はXIAO ESP32S3／C5／C6、書き込み開始アドレスは`0x0`です。C3は配布対象外です。\n\n"
            "`build.json`にコミット・サイズ・SHA256を記載しています。\n"
            "`manifest.json`と`build.json`内のファイル参照は`firmware/`配下を前提としています。\n\n"
            f"Source commit: `{commit}`\n")


def verify_source(directory, info, require_tag=False):
    version, commit = info["version"], info["commit"]
    tag = f"v{version}"
    if require_tag:
        if os.environ.get("CHAINPAD_RELEASE_REF", os.environ.get("GITHUB_REF")) != f"refs/tags/{tag}" or command("git", "rev-parse", "HEAD") != commit:
            raise ValueError("Release must match the triggering tag and checked-out commit")
    # Check the firmware source, not the possibly newer documentation commit.
    source = command("git", "show", f"{commit}:src/main.cpp")
    if f'doc["version"] = "{version}"' not in source:
        raise ValueError("Firmware source version mismatch")
    command("git", "merge-base", "--is-ancestor", commit, "HEAD")
    if history.history_reference(info) is not None:
        text = command("git", "show", f"{commit}:CHANGELOG.md")
        expected = history.encode_changes(history.generate_changes(text, version, commit), version, commit)
        if (directory / "changes.json").read_bytes() != expected:
            raise ValueError("History does not match the firmware commit's CHANGELOG")


def publish(directory, repo, require_tag=False):
    info, assets = validate(directory, require_history=require_tag)
    verify_source(directory, info, require_tag)
    version, commit = info["version"], info["commit"]
    tag = f"v{version}"
    pages = json.loads(command("gh", "api", "--paginate", "--slurp", f"repos/{repo}/releases?per_page=100"))
    releases = [release for page in pages for release in page]
    existing = next((release for release in releases if release["tag_name"] == tag), None)
    if existing and not existing["draft"]:
        print(f"{tag} is already published; leaving its tag, notes and assets unchanged")
        return
    # Never repoint an existing tag, including when recovering a draft.
    refs = json.loads(command("gh", "api", f"repos/{repo}/git/matching-refs/tags/{tag}"))
    ref = next((ref for ref in refs if ref["ref"] == f"refs/tags/{tag}"), None)
    if require_tag and not ref:
        raise ValueError("Triggering version tag is missing from the remote")
    if ref:
        obj = ref["object"]
        for _ in range(8):
            if obj["type"] != "tag":
                break
            obj = json.loads(command("gh", "api", f'repos/{repo}/git/tags/{obj["sha"]}'))["object"]
        if obj["type"] != "commit" or obj["sha"] != commit:
            raise ValueError("Existing version tag points to another commit")
    if existing and existing["target_commitish"] != commit:
        raise ValueError("Existing draft targets another commit")
    changelog = command("git", "show", f"{commit}:CHANGELOG.md")
    notes = directory / "release-notes.md"
    notes.write_text(release_notes(changelog, version, commit), encoding="utf-8")
    if not existing:
        command("gh", "release", "create", tag, "--repo", repo, "--target", commit,
                "--title", f"ChainPad {tag}", "--notes-file", str(notes), "--draft")
    # Upload while draft so a failed upload never exposes a partial new release.
    command("gh", "release", "upload", tag, *map(str, assets), "--repo", repo, "--clobber")
    published_versions = []
    for release in releases:
        if release["draft"] or release["prerelease"]:
            continue
        try:
            published_versions.append(version_tuple(release["tag_name"].removeprefix("v")))
        except ValueError:
            continue
    latest = not published_versions or version_tuple(version) > max(published_versions)
    command("gh", "release", "edit", tag, "--repo", repo, "--notes-file", str(notes),
            "--draft=false", f"--latest={str(latest).lower()}")
    print(f"Published {tag} from {commit}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--require-tag", action="store_true")
    parser.add_argument("--validate-only", action="store_true", help="Verify all assets and provenance without GitHub access")
    args = parser.parse_args()
    if args.validate_only:
        directory = args.directory.resolve()
        info, _ = validate(directory, require_history=args.require_tag)
        verify_source(directory, info, args.require_tag)
        print(f'Validated distribution and provenance for {info["version"]}')
    elif not args.repo:
        parser.error("--repo or GITHUB_REPOSITORY is required")
    else:
        publish(args.directory.resolve(), args.repo, args.require_tag)
