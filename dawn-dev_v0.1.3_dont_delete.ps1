# Build Dawn from this checkout; deployment uses the shared backup and migration installer.
[CmdletBinding()]
param(
    [string] $GameRoot,
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [switch] $BuildOnly,
    [switch] $Restore,
    [switch] $ResetSettings,
    [switch] $ClearCache
)
$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'Dawn\Dawn.vcxproj'
$installer = Join-Path $PSScriptRoot 'tools\install\Install-Dawn.ps1'
if ($ResetSettings -or $ClearCache) {
    if (-not $GameRoot -or -not (Test-Path -LiteralPath (Join-Path $GameRoot 'destiny2.exe'))) {
        throw 'Pass -GameRoot with the installed destiny2.exe folder to reset settings or caches.'
    }
    if (Get-Process destiny2 -ErrorAction SilentlyContinue) { throw 'Close the game before resetting runtime files.' }
    $resolvedGame = (Resolve-Path -LiteralPath $GameRoot).Path
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    foreach ($relative in @('Dawn','bin\x64\Dawn')) {
        $runtime = Join-Path $resolvedGame $relative
        if ($ResetSettings) {
            $settings = Join-Path $runtime 'settings.json'
            if (Test-Path -LiteralPath $settings) { Move-Item -LiteralPath $settings -Destination "$settings.$stamp.bak" }
        }
        if ($ClearCache) {
            $cache = Join-Path $runtime 'cache'
            if (Test-Path -LiteralPath $cache) {
                Get-ChildItem -LiteralPath $cache -Filter '*.bin' -File | ForEach-Object {
                    Move-Item -LiteralPath $_.FullName -Destination "$($_.FullName).$stamp.bak"
                }
            }
        }
    }
}
if ($BuildOnly) {
    if ($Restore) { throw '-BuildOnly and -Restore cannot be combined.' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'Install Visual Studio Build Tools with the v145 C++ toolset.' }
    & $msbuild $project /nologo /m /v:minimal "/p:Configuration=$Config" /p:Platform=x64 /p:PreferredToolArchitecture=x64
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} else {
    $options = @{ Config = $Config; NoLaunch = $true; Restore = $Restore }
    if ($GameRoot) { $options.GameRoot = $GameRoot }
    & $installer @options
}
