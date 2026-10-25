"""Embed the configurator; support nested Windows toolchain archives."""
from pathlib import Path
from urllib.parse import quote
import runpy

Import("env")

root = Path(env.subst("$PROJECT_DIR"))
runpy.run_path(str(root / 'scripts/builtin_config.py'))['generate']()
favicon = 'data:image/svg+xml,' + quote((root / 'web/favicon.svg').read_text(encoding='utf-8'), safe='')
asset = '#pragma once\n#include <Arduino.h>\n'
for filename, symbol in (("index.html", "WEB_UI"), ("wifi.html", "WIFI_UI")):
    html = (root / "web" / filename).read_text(encoding="utf-8")
    html = html.replace('href="favicon.svg"', 'href="' + favicon + '"')
    html = html.replace('<script src="maintenance.js"></script>', '<script>\n' + (root / 'web/maintenance.js').read_text(encoding='utf-8') + '\n</script>')
    html = html.replace('<script src="wifi-password.js"></script>', '<script>\n' + (root / 'web/wifi-password.js').read_text(encoding='utf-8') + '\n</script>')
    if filename == "index.html":
        html = html.replace('<script src="obs-settings.js"></script>', '<script>\n' + (root / 'web/obs-settings.js').read_text(encoding='utf-8') + '\n</script>')
        html = html.replace('<script src="builtin-config-data.js"></script>', '<script>\n' + (root / 'web/builtin-config-data.js').read_text(encoding='utf-8') + '\n</script>')
        html = html.replace('<script src="rotation-ui.js"></script>', '<script>\n' + (root / "web/rotation-ui.js").read_text(encoding="utf-8") + '\n</script>')
        html = html.replace('<script src="action-ui.js"></script>', '<script>\n' + (root / "web/action-ui.js").read_text(encoding="utf-8") + '\n</script>')
        html = html.replace('<script src="presets.js"></script>', '<script>\n' + (root / "web/presets.js").read_text(encoding="utf-8") + '\n</script>')
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
