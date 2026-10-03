"""Embed the configurator; support nested Windows toolchain archives."""
from pathlib import Path

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
asset = '#pragma once\n#include <Arduino.h>\n'
for filename, symbol in (("index.html", "WEB_UI"), ("wifi.html", "WIFI_UI")):
    html = (root / "web" / filename).read_text(encoding="utf-8")
    asset += f'inline const char {symbol}[] PROGMEM = R"CHIMERA(' + html + ')CHIMERA";\n'
target = root / "src/web_assets.h"
if not target.exists() or target.read_text(encoding="utf-8") != asset:
    target.write_text(asset, encoding="utf-8")

platform = env.PioPlatform()
for name, architecture in (("toolchain-xtensa-esp-elf", "xtensa-esp-elf"),
                           ("toolchain-riscv32-esp", "riscv32-esp-elf")):
    directory = platform.get_package_dir(name)
    if directory:
        package = Path(directory)
        nested = package / architecture / "bin"
        if not (package / "bin").is_dir() and nested.is_dir():
            env.PrependENVPath("PATH", str(nested))
