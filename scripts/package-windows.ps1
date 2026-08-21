[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'Release',

    [string] $OutputDirectory = 'dist',

    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion -lt [Version]'7.2.0') {
    throw 'PowerShell 7.2 or newer is required.'
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Windows build failed with exit code $LASTEXITCODE."
    }
}

$PackageRoot = Join-Path $ProjectRoot "release/$Configuration/obs-virtual-audio"
$RequiredFiles = @(
    'bin/64bit/obs-virtual-audio.dll',
    'data/locale/en-US.ini'
)

foreach ($relativePath in $RequiredFiles) {
    $requiredPath = Join-Path $PackageRoot $relativePath
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Package validation failed: required file '$relativePath' was not found."
    }

    if ((Get-Item -LiteralPath $requiredPath).Length -eq 0) {
        throw "Package validation failed: required file '$relativePath' is empty."
    }
}

$cmakeContents = Get-Content (Join-Path $ProjectRoot 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match(
    $cmakeContents,
    'project\s*\(\s*obs-virtual-audio\s+VERSION\s+(?<version>\d+\.\d+\.\d+)',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)
if (-not $versionMatch.Success) {
    throw 'Could not determine the plugin version from CMakeLists.txt.'
}

$Version = $versionMatch.Groups['version'].Value
$resolvedOutputDirectory = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $ProjectRoot $OutputDirectory
}

New-Item -ItemType Directory -Force -Path $resolvedOutputDirectory | Out-Null
$ArchivePath = Join-Path $resolvedOutputDirectory "obs-virtual-audio-$Version-windows-x64.zip"
Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $ArchivePath
Compress-Archive -LiteralPath $PackageRoot -DestinationPath $ArchivePath -CompressionLevel Optimal

if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Package archive was not created at '$ArchivePath'."
}

$archive = Get-Item -LiteralPath $ArchivePath
if ($archive.Length -eq 0) {
    throw "Package archive '$ArchivePath' is empty."
}

Write-Host ''
Write-Host 'Package validation complete.'
Write-Host "Archive: $($archive.FullName)"
Write-Host "Size: $($archive.Length) bytes"
