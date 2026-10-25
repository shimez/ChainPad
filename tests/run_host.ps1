$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
if (-not (Test-Path $json)) { throw 'Run pio run first to install ArduinoJson' }
$build = Join-Path $root '.pio\host-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$resetCommand = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe /nologo /std:c++17 /EHsc /utf-8 /I`"$root\src`" /Fe:`"$build\factory_reset_tests.exe`" /Fo`"$build\\`" `"$root\tests\host\factory_reset_tests.cpp`" && `"$build\factory_reset_tests.exe`""
& cmd.exe /d /c $resetCommand
if ($LASTEXITCODE -ne 0) { throw 'Factory Reset tests failed' }
$wifiCommand = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe /nologo /std:c++17 /EHsc /utf-8 /I`"$root\src`" /Fe:`"$build\wifi_setup_tests.exe`" /Fo`"$build\\`" `"$root\tests\host\wifi_setup_tests.cpp`" && `"$build\wifi_setup_tests.exe`""
& cmd.exe /d /c $wifiCommand
if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi setup flow tests failed' }
$platformCommand = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe /nologo /std:c++17 /EHsc /utf-8 /I`"$root\tests\reset_platform`" /I`"$root\tests\probe_platform`" /I`"$root\src`" /Fe:`"$build\factory_reset_platform_tests.exe`" /Fo`"$build\\`" `"$root\tests\host\factory_reset_platform_tests.cpp`" `"$root\src\factory_reset.cpp`" `"$root\src\storage_probe.cpp`" `"$root\tests\probe_platform\probe_stat.cpp`" && `"$build\factory_reset_platform_tests.exe`""
& cmd.exe /d /c $platformCommand
if ($LASTEXITCODE -ne 0) { throw 'Factory Reset platform tests failed' }
$sources = @('tests\host\core_tests.cpp','tests\host\rotation_tests.cpp','tests\host\rotation_storage_tests.cpp','src\rotation.cpp','src\model.cpp','src\config_store.cpp','src\engine.cpp','src\backends.cpp','src\obs_actions.cpp','src\osc_backend.cpp','src\midi_backend.cpp','src\keyboard_backend.cpp')
$args = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS','/DCHAINPAD_HAS_USB=1',"/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\core_tests.exe`"", "/Fo`"$build\\`"")
$args += '/DCHAINPAD_HOST_TEST=1'
$sources += 'src\rotation_runtime.cpp'
$sources += 'src\rotation_sender.cpp'
$sources += 'src\storage_probe.cpp'
$sources += 'tests\probe_platform\probe_stat.cpp'
$args += "/I`"$root\tests\probe_platform`""
$args += $sources | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($args -join ' ') + " && `"$build\core_tests.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Firmware host tests failed' }

# Compile the production recorder with a minimal clock/Print/critical-section adapter.
$traceArgs = @('/nologo','/std:c++17','/EHsc','/utf-8','/DCHAINPAD_HID_TRACE=1',"/I`"$root\tests\hid_trace`"", "/I`"$root\src`"", "/Fe:`"$build\hid_trace_tests.exe`"", "/Fo`"$build\\`"", "`"$root\tests\hid_trace\tests.cpp`"", "`"$root\src\hid_trace.cpp`"")
$traceCommand = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($traceArgs -join ' ') + " && `"$build\hid_trace_tests.exe`""
& cmd.exe /d /c $traceCommand
if ($LASTEXITCODE -ne 0) { throw 'HID trace recorder tests failed' }
$enabledArgs = @('/DCHAINPAD_HID_TRACE=1',"/I`"$root\tests\hid_trace`"") + $args + "`"$root\src\hid_trace.cpp`""
$enabledCommand = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($enabledArgs -join ' ') + " && `"$build\core_tests.exe`""
& cmd.exe /d /c $enabledCommand
if ($LASTEXITCODE -ne 0) { throw 'Trace-enabled firmware host tests failed' }

$bleSources = @('tests\host\capability_tests.cpp','src\rotation.cpp','src\model.cpp','src\config_store.cpp','src\engine.cpp','src\backends.cpp','src\obs_actions.cpp','src\midi_backend.cpp','src\keyboard_backend.cpp')
$bleSources += 'src\rotation_runtime.cpp'
$bleSources += 'src\rotation_sender.cpp'
$bleSources += 'src\storage_probe.cpp'
$bleSources += 'tests\probe_platform\probe_stat.cpp'
foreach ($chip in @('C3','C6','C5')) {
    $arguments = @('/nologo','/std:c++17','/EHsc','/utf-8','/D_CRT_SECURE_NO_WARNINGS',"/DCONFIG_IDF_TARGET_ESP32${chip}=1", "/I`"$root\tests\host`"", "/I`"$root\src`"", "/I`"$json`"", "/Fe:`"$build\capabilities_$chip.exe`"", "/Fo`"$build\\`"")
    $arguments += '/DCHAINPAD_HOST_TEST=1'
    $arguments += "/I`"$root\tests\probe_platform`""
    $arguments += $bleSources | ForEach-Object { "`"$root\$_`"" }
    $command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe " + ($arguments -join ' ') + " && `"$build\capabilities_$chip.exe`""
    & cmd.exe /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "BLE-only $chip tests failed" }
}
