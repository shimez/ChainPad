"""Check linked S3 output ownership, non-S3 isolation and preserved OBS worker."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
packages = Path.home() / '.platformio/packages'

def symbols(chip, path):
    arch = 'xtensa' if chip == 's3' else 'riscv32'
    package = 'toolchain-xtensa-esp-elf' if chip == 's3' else 'toolchain-riscv32-esp'
    nm = next((packages / package).rglob(f'{arch}-esp-elf-nm.exe'))
    return subprocess.check_output([str(nm), '-S', '-C', '--defined-only', str(path)], text=True)

for chip in ('s3', 'c3', 'c5', 'c6'):
    build = root / '.pio/build' / ('xiao_esp32' + chip)
    linked = symbols(chip, build / 'firmware.elf')
    image = (build / 'firmware.bin').read_bytes()
    if chip == 's3':
        assert 'chimera::serialout::Output::pump' in linked
        shared = [line for line in linked.splitlines() if 'chimera::serialout::(anonymous namespace)::shared' in line]
        assert len(shared) == 1
        size = int(shared[0].split()[1], 16)
        assert 4096 <= size < 4352, size
        worker = [line for line in linked.splitlines() if 'chimera::(anonymous namespace)::taskStack' in line]
        assert len(worker) == 1 and int(worker[0].split()[1], 16) == 8192
        print('PASS S3 shared ring/control object:', shared[0], '; Worker stack=8192 B')
    else:
        assert 'chimera::serialout::' not in linked
        assert not symbols(chip, build / 'src/serial_output.cpp.o').strip()
        assert b'SERIAL session resumed;' not in image and b'SERIAL txPartial=' not in image
        print('PASS', chip, 'no buffered output symbols/object/data')

for env in ('xiao_esp32s3_obs_test', 'xiao_esp32s3_obs_trace'):
    build = root / '.pio/build-obs-test' / env
    linked = symbols('s3', build / 'firmware.elf')
    assert 'chimera::serialout::Output::pump' in linked and 'chimera::obsTestByte' in linked
    if env.endswith('trace'):
        assert 'chimera::hidtrace::dumpTick' in linked
    print('PASS', env, 'buffered output linked')

protected = ['src/obs_backend.cpp', 'src/obs_backend.h', 'src/obs_lifecycle.h',
             'src/obs_protocol.cpp', 'src/obs_auth.cpp', 'src/engine.cpp', 'src/model.h',
             'src/config_store.cpp', 'src/wifi_setup_flow.h', 'web']
assert not subprocess.check_output(['git', 'diff', 'e5c2c65', '--', *protected], cwd=root)
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
assert 'console.printf(' not in main and 'console.println(' not in main and 'console.write(' not in main
assert main.count('serialout::output().pump(') == 1
source = (root / 'src/serial_output.cpp').read_text(encoding='utf-8')
assert 'malloc(' not in source and 'new ' not in source and '.flush(' not in source
print('PASS application CDC output centralized; OBS/Engine/storage/Wi-Fi/UI unchanged; no output heap allocation')
