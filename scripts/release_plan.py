"""Separate main validation/site updates from tagged firmware publication."""
import json
import os
import re
import changelog

import build_site as site
from publish_release import command, release_notes, version_tuple

BASE = "https://shimez.github.io/ChainPad/"


def plan(ref, before="", force=False):
    tagged = ref.startswith("refs/tags/")
    if not tagged and ref != "refs/heads/main":
        raise ValueError("Only main or a version tag may run this workflow")
    text = (site.ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    changelog.parse_changelog(text)  # Main validates edits but never publishes them.
    if tagged:
        tag = ref.removeprefix("refs/tags/")
        if not tag.startswith("v"):
            raise ValueError("Expected vmajor.minor.patch tag")
        version = tag[1:]
        version_tuple(version)
        source = (site.ROOT / "src/main.cpp").read_text(encoding="utf-8")
        if f'doc["version"] = "{version}"' not in source:
            raise ValueError("Tag and firmware version do not match")
        release_notes((site.ROOT / "CHANGELOG.md").read_text(encoding="utf-8"), version, "validation")
        commit = command("git", "rev-parse", "HEAD")
        command("git", "merge-base", "--is-ancestor", commit, "origin/main")
        changelog.generate_changes(text, version, commit)
    metadata = site.fetch(BASE, "build.json")
    info = json.loads(metadata)
    if tagged:
        published = version_tuple(info["version"])
        candidate = version_tuple(version)
        if candidate == published and info["commit"] != commit:
            raise ValueError("Same version is already deployed from another commit")
        # Re-running the same release can repair a failed deployment/publication.
        deploy = candidate >= published
        return {"rebuild": True, "deploy": deploy, "release": True}
    # Main must never ship an untagged firmware build, even when validation builds it.
    site.reuse_published(BASE, metadata)
    known_before = bool(re.fullmatch(r"[0-9a-f]{40}", before)) and before != "0" * 40
    rebuild = force or not known_before or site.firmware_changed(before)
    return {"rebuild": rebuild, "deploy": True, "release": False}


if __name__ == "__main__":
    result = plan(os.environ["GITHUB_REF"], os.environ.get("BEFORE_SHA", ""),
                  os.environ.get("FORCE_FIRMWARE") == "true")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        for key, value in result.items():
            output.write(f"{key}={str(value).lower()}\n")
    print(result)
