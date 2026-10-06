$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$toolchain = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-riscv32-esp\riscv32-esp-elf\bin'
$build = Join-Path $root '.pio\host-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$object = Join-Path $build 'rotation_layout_c6.o'
# Only compile layout: the host Arduino adapter contains no fields used in these
# production structures. Pointer/alignment sizes come from the C6 target ABI.
& "$toolchain\riscv32-esp-elf-g++.exe" -std=c++17 -march=rv32imac -mabi=ilp32 -DCONFIG_IDF_TARGET_ESP32C6=1 "-I$root\tests\host" "-I$root\src" "-I$root\.pio\libdeps\xiao_esp32s3\ArduinoJson\src" -c "$root\tests\host\rotation_layout.cpp" -o $object
if ($LASTEXITCODE -ne 0) { throw 'Rotation layout compilation failed' }
& "$toolchain\riscv32-esp-elf-nm.exe" -S --size-sort --radix=d $object | Select-String ' B .*_size$'
if ($LASTEXITCODE -ne 0) { throw 'Rotation layout inspection failed' }
