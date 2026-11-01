param(
    [ValidateSet('mingw64', 'ucrt64', 'clang64', 'msvc', 'default')][string]$Toolchain = 'mingw64',
    [string]$MsysRoot = 'C:\msys64',
    [int]$Jobs = 4,
    [switch]$TuiOnly, [switch]$GuiOnly, [switch]$SkipTests, [switch]$Package,
    [string[]]$CMakeArgs = @()
)
$ErrorActionPreference = 'Stop'
if ($TuiOnly -and $GuiOnly) { throw 'Choose either TuiOnly or GuiOnly.' }
if ($Jobs -lt 1) { throw 'Jobs must be positive.' }
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskOldPath = $env:PATH
Push-Location $taskRoot
try {
    $taskConfigure = @('-S', '.', '-B', "build/$Toolchain", '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
    if ($Toolchain -in @('mingw64', 'ucrt64', 'clang64')) {
        $taskPrefix = Join-Path $MsysRoot $Toolchain
        $taskBin = Join-Path $taskPrefix 'bin'
        $taskCompiler = if ($Toolchain -eq 'clang64') { 'clang++.exe' } else { 'g++.exe' }
        if (!(Test-Path -LiteralPath (Join-Path $taskBin $taskCompiler))) { throw "Install the $Toolchain MSYS2 packages first; see docs/BUILDING.md." }
        $env:PATH = "$taskBin;$env:PATH"
        $taskConfigure += @("-DCMAKE_CXX_COMPILER=$taskBin/$taskCompiler", "-DCMAKE_MAKE_PROGRAM=$taskBin/ninja.exe", "-DCMAKE_PREFIX_PATH=$taskPrefix")
    } elseif ($Toolchain -eq 'msvc') {
        if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Use a Visual Studio x64 Developer PowerShell or Developer Command Prompt.' }
        if (!$env:VCPKG_ROOT) { throw 'Set VCPKG_ROOT to a bootstrapped vcpkg checkout; see docs/BUILDING.md.' }
        $taskConfigure += @("-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_TARGET_TRIPLET=x64-windows')
    }
    $taskConfigure += "-DNOVA_BUILD_GUI=$(@{ $true = 'OFF'; $false = 'ON' }[[bool]$TuiOnly])"
    $taskConfigure += "-DNOVA_BUILD_TUI=$(@{ $true = 'OFF'; $false = 'ON' }[[bool]$GuiOnly])"
    $taskConfigure += "-DNOVA_BUILD_TESTS=$(@{ $true = 'OFF'; $false = 'ON' }[[bool]$SkipTests])"
    & cmake @taskConfigure @CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    & cmake --build "build/$Toolchain" --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if (!$SkipTests) {
        & ctest --test-dir "build/$Toolchain" --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    }
    if ($Package) {
        & python scripts/package.py --build-dir "build/$Toolchain" --platform "windows-x64-$Toolchain"
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
    }
} finally { Pop-Location; $env:PATH = $taskOldPath }
