$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
$build = Join-Path $root '.pio\obs-control-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$flags = "/nologo /std:c++17 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /DCHAINPAD_HAS_OBS=1 /DCHAINPAD_OBS_NATIVE_TEST=1 /I`"$root\tests\obs_control\native`" /I`"$root\tests\obs_settings\native`" /I`"$root\tests\obs\native`" /I`"$root\src`" /I`"$json`" /Fo:`"$build\\`" /Fe:`"$build\control.exe`""
$sources = @('tests\obs_control\tests.cpp','src\obs_settings.cpp','src\obs_protocol.cpp','src\obs_control.cpp','src\obs_http.cpp','src\obs_actions.cpp') | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe $flags $($sources -join ' ') && `"$build\control.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'OBS control tests failed (fixture values suppressed)' }
