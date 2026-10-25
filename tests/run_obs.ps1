$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools required' }
$json = Join-Path $root '.pio\libdeps\xiao_esp32s3\ArduinoJson\src'
$ws = Join-Path $root '.pio\libdeps\xiao_esp32s3\WebSockets\src'
if (-not (Test-Path "$ws\WebSockets.cpp.upstream-2.7.2")) { throw 'Build S3 first including the dependency patch' }
$build = Join-Path $root '.pio\obs-tests'
New-Item -ItemType Directory -Force $build | Out-Null
$common = "/nologo /std:c++17 /EHsc /utf-8 /D_CRT_SECURE_NO_WARNINGS /DCHAINPAD_HAS_OBS=1 /DCHAINPAD_OBS_NATIVE_TEST=1 /I`"$root\tests\obs\native`" /Fo:`"$build\\`""
$sources = @('tests\obs\protocol_tests.cpp','tests\obs\native_crypto.cpp','src\obs_protocol.cpp','src\obs_auth.cpp') | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe $common /I`"$json`" /I`"$root\src`" /Fe:`"$build\protocol.exe`" $($sources -join ' ') && `"$build\protocol.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'OBS protocol/auth tests failed' }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe /nologo /c /Fo:`"$build\b64.obj`" `"$ws\libb64\cencode.c`" && cl.exe $common /DESP32 /DWEBSOCKETS_MAX_DATA_SIZE=2048 /DWEBSOCKETS_TCP_TIMEOUT=20 /I`"$ws`" /Fe:`"$build\frames.exe`" `"$root\tests\obs\frame_tests.cpp`" `"$ws\WebSockets.cpp`" `"$build\b64.obj`" && `"$build\frames.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'WebSocket frame patch tests failed' }
$sources = @('tests\obs\backend_tests.cpp','tests\obs\native_crypto.cpp','src\obs_backend.cpp','src\obs_protocol.cpp','src\obs_auth.cpp','src\obs_actions.cpp','src\engine.cpp','src\backends.cpp') | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe $common /I`"$root\tests\obs\runtime`" /I`"$json`" /I`"$root\src`" /Fe:`"$build\backend.exe`" $($sources -join ' ') && `"$build\backend.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'OBS production worker tests failed' }
$sources = @('tests\obs\console_tests.cpp','src\obs_test_console.cpp','src\obs_protocol.cpp','src\obs_actions.cpp') | ForEach-Object { "`"$root\$_`"" }
$command = "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cl.exe $common /DCHAINPAD_OBS_TEST=1 /I`"$json`" /I`"$root\src`" /Fe:`"$build\console.exe`" $($sources -join ' ') && `"$build\console.exe`""
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'OBS test console tests failed' }
