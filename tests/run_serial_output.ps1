$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
$build = Join-Path $root '.pio\serial-output-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$sources = @('tests\serial_output\tests.cpp','src\serial_output.cpp','src\obs_test_console.cpp','src\obs_protocol.cpp','src\hid_trace.cpp') | ForEach-Object { "`"$root\$_`"" }
$flags = "/nologo /std:c++17 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /DCHAINPAD_SERIAL_NATIVE_TEST=1 /DCHAINPAD_HAS_OBS=1 /DCHAINPAD_OBS_NATIVE_TEST=1 /DCHAINPAD_OBS_TEST=1 /DCHAINPAD_HID_TRACE=1 /I`"$root\tests\serial_output\native`" /I`"$root\tests\obs\native`" /I`"$root\src`" /I`"$json`" /Fo:`"$build\\`" /Fe:`"$build\serial.exe`""
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe $flags $($sources -join ' ') && `"$build\serial.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Serial output tests failed' }
