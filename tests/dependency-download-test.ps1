[CmdletBinding()]
param(
    [string] $ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Stop'

$helperPath = Join-Path $ProjectRoot 'scripts/dependency-download.ps1'
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Download helper was not found at '$helperPath'."
}

. $helperPath

$testRoot = Join-Path $ProjectRoot ".build/dependency-download-test-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

try {
    $fixturePath = Join-Path $testRoot 'fixture.zip'
    $destinationPath = Join-Path $testRoot 'download.zip'
    [IO.File]::WriteAllText($fixturePath, 'dependency fixture')
    $expectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixturePath).Hash
    $state = [PSCustomObject]@{ Attempts = 0 }

    $flakyDownload = {
        param($Uri, $Destination)

        $state.Attempts++
        if ($state.Attempts -eq 1) {
            return 22
        }

        Copy-Item -LiteralPath $fixturePath -Destination $Destination
        return 0
    }.GetNewClosure()

    Get-VerifiedDownload -Uri 'https://example.invalid/dependency.zip' `
        -Destination $destinationPath `
        -Sha256 $expectedHash `
        -MaxAttempts 3 `
        -RetryDelaySeconds 0 `
        -DownloadCommand $flakyDownload

    if ($state.Attempts -ne 2) {
        throw "Expected two download attempts, observed $($state.Attempts)."
    }

    if (-not (Test-Path -LiteralPath $destinationPath -PathType Leaf)) {
        throw 'The verified dependency was not moved into place.'
    }

    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $destinationPath).Hash -ne $expectedHash) {
        throw 'The downloaded dependency hash does not match the fixture.'
    }

    Write-Host 'Dependency download retry test passed.'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -Recurse -Force -LiteralPath $testRoot
    }
}
