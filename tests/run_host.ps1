$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
if (-not (Test-Path $json)) { throw 'Run pio run first to install ArduinoJson' }
$build = Join-Path $root '.pio\host-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$sources = @('tests\host\core_tests.cpp','src\model.cpp','src\engine.cpp','src\backends.cpp','src\osc_backend.cpp','src\midi_backend.cpp','src\keyboard_backend.cpp')
$args = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DCHAINPAD_HAS_USB=1',"/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\core_tests.exe`"", "/Fo`"$build\\`"")
$args += $sources | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($args -join ' ') + " && `"$build\core_tests.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Firmware host tests failed' }

$bleSources = @('tests\host\capability_tests.cpp','src\model.cpp','src\engine.cpp','src\backends.cpp','src\midi_backend.cpp','src\keyboard_backend.cpp')
foreach ($chip in @('C3','C6','C5')) {
    $arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS',"/DCONFIG_IDF_TARGET_ESP32${chip}=1", "/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\capabilities_$chip.exe`"", "/Fo`"$build\\`"")
    $arguments += $bleSources | ForEach-Object { "`"$root\$_`"" }
    $command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($arguments -join ' ') + " && `"$build\capabilities_$chip.exe`""
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "BLE-only $chip tests failed" }
}
