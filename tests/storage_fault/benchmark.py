"""Compare H3 HEAD and worktree GET without checkout or modifying source files."""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                              'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
json = root / '.pio/libdeps/xiao_esp32s3/ArduinoJson/src'
for label in ['2d689ad', 'worktree']:
    build = root / '.pio/storage-fault-tests/benchmark' / label
    build.mkdir(parents=True, exist_ok=True)
    for name in ['model.h', 'model.cpp', 'model_types.h', 'rotation.h', 'rotation.cpp', 'capabilities.h', 'json_wire.h', 'main.cpp']:
        data = (root / 'src' / name).read_bytes() if label == 'worktree' else subprocess.check_output(['git', 'show', f'{label}:src/{name}'], cwd=root)
        (build / name).write_bytes(data)
    main = (build / 'main.cpp').read_text(encoding='utf-8')
    handler = re.search(r'^  web\.on\("/api/config", HTTP_GET, \[\] \{\n.*?^  \}\);', main, re.M | re.S)
    assert handler
    (build / 'get.inc').write_text(handler.group(), encoding='utf-8')
    args = ['/nologo', '/O2', '/std:c++17', '/EHsc', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS',
            '/DCHAINPAD_HOST_TEST=1', '/DCHAINPAD_HAS_USB=0', '/DCONFIG_IDF_TARGET_ESP32C6=1', '/DARDUINOJSON_POOL_CAPACITY=128']
    args += [f'/I"{p}"' for p in [build, root/'tests/host', root/'tests/storage_fault', json]]
    args += [f'/Fe:"{build / "get.exe"}"', f'/Fo"{build.as_posix()}/"']
    args += [f'"{p}"' for p in [root/'tests/storage_fault/benchmark.cpp', build/'model.cpp', build/'rotation.cpp']]
    command = f'"{vs}\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul && cl.exe ' + ' '.join(args) + f' && "{build / "get.exe"}"'
    print(label, flush=True)
    subprocess.run(command, shell=True, cwd=root, check=True)
print('Host C6 compile-time configuration only; not ESP32 timing or network performance.')
