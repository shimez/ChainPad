$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$build = Join-Path $root '.pio\storage-fault-tests'
& "$root\.venv\Scripts\python.exe" "$root\tests\storage_fault\generate.py" $build
if ($LASTEXITCODE -ne 0) { throw 'Handler extraction failed' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
$sources = @('tests\storage_fault\tests.cpp','tests\storage_fault\model_adapter.cpp','src\config_store.cpp','src\factory_reset.cpp','src\rotation.cpp','src\rotation_runtime.cpp','src\rotation_sender.cpp','src\engine.cpp','src\backends.cpp','src\midi_backend.cpp','src\keyboard_backend.cpp')
$sources += 'src\storage_probe.cpp'
$sources += 'tests\probe_platform\probe_stat.cpp'
# 32-bit ESP32 ArduinoJson default: 128 slots/pool. Host pointers remain 64-bit;
# allocation byte counts therefore model boundaries, not ESP heap sizes.
foreach ($chip in @('S3','C5','C6')) {
    $usb = if ($chip -eq 'S3') { 1 } else { 0 }
    $args = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DCHAINPAD_HOST_TEST=1',"/DCHAINPAD_HAS_USB=$usb", "/DCONFIG_IDF_TARGET_ESP32${chip}=1",'/DARDUINOJSON_POOL_CAPACITY=128',"/I`"$build`"", "/I`"$root\tests\storage_fault`"", "/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\fault_$chip.exe`"", "/Fo`"$build\\`"")
    $args += $sources | ForEach-Object { "`"$root\$_`"" }
    $args += "/I`"$root\tests\probe_platform`""
    $args += '/DCHAINPAD_STORAGE_FAULT=1'
    $command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($args -join ' ') + " && `"$build\fault_$chip.exe`" && `"$build\fault_$chip.exe`" --blank-boot && `"$build\fault_$chip.exe`" --h1-reboot && `"$build\fault_$chip.exe`" --h1-persistent"
    Write-Output "Storage tests: H1/H2/H3/H4 safety regression: $chip Standard"
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Storage fault tests failed: $chip" }
}
& node "$root\tests\storage_fault\webui_get.cjs"
if ($LASTEXITCODE -ne 0) { throw 'Config GET WebUI safety regression failed' }
