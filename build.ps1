# Full release build: icon -> exe -> sign -> installer -> sign. Run from the project root.
#   .\build.ps1            (uses icons\icon.ico as-is)
#   .\build.ps1 -Icon C    (re-render icons\icon.ico from concept C first)
param([string]$Icon = "", [switch]$NoSign)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { $vs = "C:\Program Files\Microsoft Visual Studio\18\Community" }
$iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
$sign = "$env:USERPROFILE\Tools\CodeSignTool\sign.bat"

if ($Icon) { python "$root\icons\make_icons.py" $Icon }

Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
$ErrorActionPreference = "Continue"   # vsdevcmd prints a harmless vswhere warning on stderr
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" 2>$null | Out-Null
$ErrorActionPreference = "Stop"
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
Set-Location $root
Stop-Process -Name SendToScreen -Force -ErrorAction SilentlyContinue
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release | Out-Null
cmake --build build
& "$root\build\tests.exe"
if ($LASTEXITCODE -ne 0) { throw "unit tests failed" }

if (-not $NoSign) { & $sign "$root\build\SendToScreen.exe"; if ($LASTEXITCODE -ne 0) { throw "signing exe failed" } }
& $iscc "$root\installer\SendToScreen.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
$setup = Get-ChildItem "$root\release\SendToScreen_Setup_v*.exe" | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $NoSign) { & $sign $setup.FullName; if ($LASTEXITCODE -ne 0) { throw "signing installer failed" } }
"Installer: $($setup.FullName) ($([math]::Round($setup.Length / 1KB)) KB)"
