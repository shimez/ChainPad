"""Package a new tag, or reuse immutable assets when repairing a published release."""
import json
import os
from pathlib import Path
import tempfile

import build_site as site
import publish_release as release


def package(repo):
    ref = os.environ["CHAINPAD_RELEASE_REF"]
    tag = ref.removeprefix("refs/tags/")
    release.version_tuple(tag.removeprefix("v"))
    if ref != f"refs/tags/{tag}" or not tag.startswith("v"):
        raise ValueError("Expected a version tag")
    pages = json.loads(release.command("gh", "api", "--paginate", "--slurp", f"repos/{repo}/releases?per_page=100"))
    existing = next((r for page in pages for r in page if r["tag_name"] == tag and not r["draft"]), None)
    if existing is None:
        site.main()
        return
    # Never rebuild the bytes of a release that is already public.
    with tempfile.TemporaryDirectory() as temp:
        release.command("gh", "release", "download", tag, "--repo", repo, "--dir", temp)
        source = Path(temp)
        info = json.loads((source / "build.json").read_text(encoding="utf-8"))
        names = ["build.json", "manifest.json", "changes.json"]
        names += [f"firmware/ChainPad-{family}.bin" for family in release.FAMILIES]
        for name in names:
            target = site.OUTPUT / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((source / Path(name).name).read_bytes())
        release.validate(site.OUTPUT, require_history=True)
        release.verify_source(site.OUTPUT, info, require_tag=True)
    site.copy_site()
    print(f"Reused immutable assets for {tag}")


if __name__ == "__main__":
    package(os.environ["GITHUB_REPOSITORY"])
