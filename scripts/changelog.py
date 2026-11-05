"""Generate and validate bounded Configurator release history from CHANGELOG.md.

No network, Git mutation, or publication. The caller supplies the build metadata
whose version and commit bind the generated history to its firmware artifact.
"""
import argparse
import json
from pathlib import Path
import re

SCHEMA_VERSION = 1
MAX_JSON_BYTES = 256 * 1024
MAX_RELEASES = 256
MAX_ITEMS = 64  # Per category, per version.
MAX_TEXT_LENGTH = 1024  # Unicode code points, not UTF-8 bytes.
MAX_SAFE_INTEGER = 9007199254740991
CATEGORIES = {"追加機能": "features", "修正・改善": "improvements"}
VERSION_PATTERN = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def version_key(value):
    if not isinstance(value, str) or not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError("Version must be major.minor.patch without prefixes or suffixes")
    parts = tuple(map(int, value.split(".")))
    if any(part > MAX_SAFE_INTEGER for part in parts):
        raise ValueError("Version component exceeds JavaScript safe integer range")
    return parts


def validate_commit(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40}", value):
        raise ValueError("Firmware commit must be a full lowercase SHA")


def validate_items(items):
    if not isinstance(items, list) or len(items) > MAX_ITEMS:
        raise ValueError("Invalid history item list or too many items")
    for text in items:
        if not isinstance(text, str) or not text.strip() or len(text) > MAX_TEXT_LENGTH:
            raise ValueError("History item must be nonempty bounded text")
        if any(ord(char) < 32 or ord(char) == 127 or 0xD800 <= ord(char) <= 0xDFFF for char in text):
            raise ValueError("History item contains a control character or invalid Unicode")


def parse_changelog(text):
    """Parse two simple bullet sections per version; preserve other notes in MD.

    Blank lines and indented paragraph wrapping are allowed. Inline Markdown is
    retained as literal text. Additional ### sections and Unreleased are omitted.
    """
    records, seen_sections = {}, {}
    current, category, fence = None, None, None
    for number, line in enumerate(text.lstrip("\ufeff").splitlines(), 1):
        stripped = line.strip()
        marker = re.match(r"^(`{3,}|~{3,})", stripped)
        if fence:
            if re.fullmatch(re.escape(fence[0]) + "{" + str(len(fence)) + r",}\s*", stripped):
                fence = None
            continue
        if marker:
            if category:
                raise ValueError(f"Line {number}: code blocks are not history items")
            fence = marker[1]
            continue
        heading = re.fullmatch(r"##\s+(.+?)\s*", line)
        if heading:
            current, category = None, None
            label = heading[1]
            if label == "Unreleased":
                continue
            version = label.split()[0]
            version_key(version)
            if version in records:
                raise ValueError(f"Duplicate version: {version}")
            if len(records) >= MAX_RELEASES:
                raise ValueError("Too many release history entries")
            records[version] = {"version": version, "features": [], "improvements": []}
            seen_sections[version] = set()
            current = version
            continue
        heading = re.fullmatch(r"###\s+(.+?)\s*", line)
        if heading:
            category = CATEGORIES.get(heading[1]) if current else None
            if category:
                if category in seen_sections[current]:
                    raise ValueError(f"Duplicate section in {current}: {heading[1]}")
                seen_sections[current].add(category)
            continue
        if not category or not stripped:
            continue
        items = records[current][category]
        bullet = re.fullmatch(r"[-*]\s+(.+)", line)
        if bullet:
            items.append(bullet[1].strip())
        elif line.startswith(("  ", "\t")) and items and not re.match(r"[-*+#>]|[0-9]+[.)]\s", stripped):
            items[-1] += " " + stripped
        else:
            raise ValueError(f"Line {number}: expected a bullet or indented continuation")
    if fence:
        raise ValueError("Unclosed code block in changelog")
    if not records:
        raise ValueError("No release history")
    for version, record in records.items():
        if seen_sections[version] != set(CATEGORIES.values()):
            raise ValueError(f"Missing history section in {version}")
        for category in CATEGORIES.values():
            items = record[category]
            if not items:
                raise ValueError(f"Empty section in {version}; use '- なし' explicitly")
            if items == ["なし"]:
                record[category] = []
            elif "なし" in items:
                raise ValueError(f"'なし' cannot be mixed with history items in {version}")
            validate_items(record[category])
    return sorted(records.values(), key=lambda record: version_key(record["version"]), reverse=True)


def _json_bytes(document):
    try:
        return (json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        raise ValueError("Invalid history JSON value") from error


def validate_changes(document, firmware_version, firmware_commit):
    """Reject an incompatible or mismatched history instead of showing partial data."""
    version_key(firmware_version)
    validate_commit(firmware_commit)
    if not isinstance(document, dict) or set(document) != {"schemaVersion", "firmwareVersion", "firmwareCommit", "releases"}:
        raise ValueError("Invalid history object fields")
    if type(document["schemaVersion"]) is not int or document["schemaVersion"] != SCHEMA_VERSION:
        raise ValueError("Unsupported history schema")
    if document["firmwareVersion"] != firmware_version or document["firmwareCommit"] != firmware_commit:
        raise ValueError("History and firmware metadata do not match")
    records = document["releases"]
    if not isinstance(records, list) or not 1 <= len(records) <= MAX_RELEASES:
        raise ValueError("Invalid release history list")
    previous = None
    for record in records:
        if not isinstance(record, dict) or set(record) != {"version", "features", "improvements"}:
            raise ValueError("Invalid release history fields")
        key = version_key(record["version"])
        if key > version_key(firmware_version):
            raise ValueError("Unpublished version in release history")
        if previous is not None and key >= previous:
            raise ValueError("History versions must be unique and in descending numeric order")
        previous = key
        validate_items(record["features"])
        validate_items(record["improvements"])
    if records[0]["version"] != firmware_version:
        raise ValueError("History for the published firmware is missing")
    if len(_json_bytes(document)) > MAX_JSON_BYTES:
        raise ValueError("History JSON exceeds size limit")
    return document


def generate_changes(text, firmware_version, firmware_commit):
    target = version_key(firmware_version)
    validate_commit(firmware_commit)
    records = [record for record in parse_changelog(text) if version_key(record["version"]) <= target]
    document = {"schemaVersion": SCHEMA_VERSION, "firmwareVersion": firmware_version,
                "firmwareCommit": firmware_commit, "releases": records}
    return validate_changes(document, firmware_version, firmware_commit)


def encode_changes(document, firmware_version, firmware_commit):
    validate_changes(document, firmware_version, firmware_commit)
    return _json_bytes(document)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def decode_changes(payload, firmware_version, firmware_commit):
    if not isinstance(payload, bytes) or len(payload) > MAX_JSON_BYTES:
        raise ValueError("Invalid history payload or size limit exceeded")
    try:
        document = json.loads(payload.decode("utf-8"), object_pairs_hook=_unique_object)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise ValueError("Invalid history JSON") from error
    return validate_changes(document, firmware_version, firmware_commit)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--changelog", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True, help="Generated build.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    info = json.loads(args.build.read_text(encoding="utf-8"))
    document = generate_changes(args.changelog.read_text(encoding="utf-8"), info["version"], info["commit"])
    payload = encode_changes(document, info["version"], info["commit"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f'Generated {len(document["releases"])} release histories for {info["version"]}')


if __name__ == "__main__":
    main()
