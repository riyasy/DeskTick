# Run in a Visual Studio x64 Developer PowerShell:
#   ./tests/run-hit-testing.ps1 -Toolset v143  # VS 2022
#   ./tests/run-hit-testing.ps1               # VS 2026
param([string]$Toolset = 'v145')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = Join-Path $repo 'src/DeskTick/x64/HitTest'
$objects = Join-Path $repo 'src/DeskTick/obj/HitTest'
& MSBuild (Join-Path $repo 'src/DeskTick/DeskTick.vcxproj') /nologo /verbosity:minimal `
    /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$Toolset" `
    /p:WholeProgramOptimization=false /p:GenerateDebugInformation=false `
    "/p:OutDir=$output\" "/p:IntDir=$objects\"
if ($LASTEXITCODE) { throw 'Application build failed' }
$linkObjects = Get-ChildItem $objects -Filter *.obj |
    Where-Object { $_.Name -notin @('clock.obj', 'hit_testing.obj') } |
    ForEach-Object FullName
& cl /nologo /std:c++20 /EHsc /MT /DUNICODE /D_UNICODE `
    "/Fe:$output/hit_testing.exe" "/Fo:$objects/hit_testing.obj" `
    (Join-Path $PSScriptRoot 'hit_testing.cpp') /link /SUBSYSTEM:CONSOLE /DEBUG:NONE @linkObjects
if ($LASTEXITCODE) { throw 'Regression test build failed' }
& (Join-Path $output 'hit_testing.exe')
if ($LASTEXITCODE) { throw 'Hit-testing regression failed' }
