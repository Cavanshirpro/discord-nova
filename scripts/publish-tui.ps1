param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [string]$RuntimeDirectory = ''
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
$taskExe = Join-Path $taskBuild 'discord-nova-tui.exe'
if (!(Test-Path -LiteralPath $taskExe)) { throw "TUI executable missing in $taskBuild" }
if (!$RuntimeDirectory) {
    $taskCompiler = Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt') | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:[^=]+=' } | Select-Object -First 1
    if ($taskCompiler) { $RuntimeDirectory = Split-Path ($taskCompiler -replace '^[^=]+=','') -Parent }
}
$taskOldPath = $env:PATH
try {
    if ($RuntimeDirectory) { $env:PATH = "$RuntimeDirectory;$env:PATH" }
    & $taskExe --self-test
    if ($LASTEXITCODE) { throw 'Refusing to promote a TUI that fails its model tests' }
    $taskCapabilities = (& $taskExe --capabilities | Out-String) | ConvertFrom-Json
    if ($LASTEXITCODE) { throw 'TUI capability check failed' }
    $taskHash = (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($RuntimeDirectory -match '[\r\n"]') { throw 'Invalid runtime directory' }
    $taskAssets = @(Get-Item -LiteralPath $taskExe)
    $taskAssets += @(Get-ChildItem -LiteralPath $taskBuild -Filter '*.dll' -File | Sort-Object Name)
    $taskCa = Join-Path $taskBuild 'cacert.pem'
    if (Test-Path -LiteralPath $taskCa) { $taskAssets += Get-Item -LiteralPath $taskCa }
    $taskAssetHashes = @($taskAssets | ForEach-Object { $_.Name + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
    $taskSha = [Security.Cryptography.SHA256]::Create()
    try {
        $taskBundleHash = [BitConverter]::ToString($taskSha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($taskAssetHashes -join "`n")))).Replace('-','').ToLowerInvariant()
    } finally { $taskSha.Dispose() }
    $taskId = $taskBundleHash.Substring(0, 16)
    $taskDestination = Join-Path $taskRoot "build/live-tui/$taskId"
    New-Item -ItemType Directory -Force -Path $taskDestination | Out-Null
    $taskPublishedExe = Join-Path $taskDestination 'discord-nova-tui.exe'
    if (!(Test-Path -LiteralPath $taskPublishedExe)) {
        Copy-Item -LiteralPath $taskExe -Destination $taskPublishedExe
        Get-ChildItem -LiteralPath $taskBuild -Filter '*.dll' -File | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $taskDestination $_.Name)
        }
        $taskCa = Join-Path $taskBuild 'cacert.pem'
        if (Test-Path -LiteralPath $taskCa) { Copy-Item -LiteralPath $taskCa -Destination (Join-Path $taskDestination 'cacert.pem') }
    }
    if ((Get-FileHash -LiteralPath $taskPublishedExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskHash) { throw 'Published executable hash mismatch' }
    foreach ($taskAsset in $taskAssets) {
        $taskCopy = Join-Path $taskDestination $taskAsset.Name
        if (!(Test-Path -LiteralPath $taskCopy) -or
            (Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $taskAsset.FullName -Algorithm SHA256).Hash) {
            throw "Published bundle asset mismatch: $($taskAsset.Name)"
        }
    }
    if ($taskCapabilities.browser_profiles -and !(Test-Path -LiteralPath (Join-Path $taskDestination 'cacert.pem'))) { throw 'Browser build requires its CA bundle' }
    & $taskPublishedExe --demo --smoke-test
    if ($LASTEXITCODE) { throw 'Published TUI failed startup; current pointer was not changed' }
    $taskRevision = (& git -C $taskRoot rev-parse HEAD).Trim()
    $taskManifest = [ordered]@{
        executable = $taskPublishedExe
        sha256 = $taskHash
        bundle_sha256 = $taskBundleHash
        build_directory = $taskBuild
        source_revision = $taskRevision
        published_utc = [DateTime]::UtcNow.ToString('o')
        runtime_directory = $RuntimeDirectory
        capabilities = $taskCapabilities
    }
    # The tiny CMD pointer needs no interpreter startup when the user opens Nova.
    # Its target lives in an immutable hash directory, so running sessions do not
    # lock the next linker output or prevent publication of a newer build.
    $taskRuntimeEscaped = $RuntimeDirectory.Replace('%','%%')
    $taskPointer = "@set `"taskNovaExe=%~dp0live-tui\$taskId\discord-nova-tui.exe`"`r`n@set `"taskNovaRuntime=$taskRuntimeEscaped`"`r`n"
    $taskNames = @('tui-current')
    if ($taskCapabilities.native_media) { $taskNames += 'tui-current-native' }
    foreach ($taskName in $taskNames) {
        $taskJson = Join-Path $taskRoot "build/$taskName.json"
        $taskCmd = Join-Path $taskRoot "build/$taskName.cmd"
        [IO.File]::WriteAllText("$taskJson.tmp", ($taskManifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText("$taskCmd.tmp", $taskPointer, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath "$taskJson.tmp" -Destination $taskJson -Force
        Move-Item -LiteralPath "$taskCmd.tmp" -Destination $taskCmd -Force
    }
    Write-Output "Published validated TUI $taskId"
} finally { $env:PATH = $taskOldPath }
