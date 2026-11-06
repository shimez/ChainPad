"""Gate publication on the bytes actually served by Pages, not its job status."""
import argparse
import time
from pathlib import Path

import build_site as site
import publish_release as release


def verify(directory, base):
    info, assets = release.validate(directory)
    # Include site identity and the Installer entry points as well as all binaries.
    paths = [p.relative_to(directory).as_posix() for p in assets]
    paths += ["site-build.json", "installer/index.html", "installer/metadata.js"]
    for path in paths:
        if site.fetch(base, path) != (directory / path).read_bytes():
            raise ValueError(f"Public Pages content differs: {path}")
    print(f'Public Pages verified: {info["version"]} / {info["commit"]}')


def wait_for_deployment(directory, base, attempts=12, interval=30):
    for attempt in range(attempts):
        try:
            verify(directory, base)
            return
        except (ValueError, OSError) as error:
            if attempt == attempts - 1:
                raise
            print(f"Pages not verified ({attempt + 1}/{attempts}): {error}", flush=True)
            time.sleep(interval)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--base", default="https://shimez.github.io/ChainPad/")
    args = parser.parse_args()
    wait_for_deployment(args.directory.resolve(), args.base)
