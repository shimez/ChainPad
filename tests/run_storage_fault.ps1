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
# 32-bit ESP32 ArduinoJson default: 128 slots/pool. Host pointers remain 64-bit;
# allocation byte counts therefore model boundaries, not ESP heap sizes.
foreach ($chip in @('S3','C5','C6')) {
    $usb = if ($chip -eq 'S3') { 1 } else { 0 }
    $args = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DCHAINPAD_HOST_TEST=1',"/DCHAINPAD_HAS_USB=$usb", "/DCONFIG_IDF_TARGET_ESP32${chip}=1",'/DARDUINOJSON_POOL_CAPACITY=128',"/I`"$build`"", "/I`"$root\tests\storage_fault`"", "/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\fault_$chip.exe`"", "/Fo`"$build\\`"")
    $args += $sources | ForEach-Object { "`"$root\$_`"" }
    $command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($args -join ' ') + " && `"$build\fault_$chip.exe`""
    Write-Output "Storage tests: H2 safety regression + H1/H3/H4 UNFIXED characterization: $chip Standard"
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "Storage fault tests failed: $chip" }
}
