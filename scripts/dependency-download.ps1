function Get-VerifiedDownload {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [Uri] $Uri,

        [Parameter(Mandatory)]
        [string] $Destination,

        [Parameter(Mandatory)]
        [ValidatePattern('^[0-9a-fA-F]{64}$')]
        [string] $Sha256,

        [ValidateRange(1, 10)]
        [int] $MaxAttempts = 4,

        [ValidateRange(0, 60)]
        [int] $RetryDelaySeconds = 2,

        [scriptblock] $DownloadCommand
    )

    if (-not $DownloadCommand) {
        $DownloadCommand = {
            param($Source, $Target)

            & curl.exe --fail --location --silent --show-error --output $Target $Source
            return $LASTEXITCODE
        }
    }

    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $existingHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
        if ($existingHash.Equals($Sha256, [StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "Using cached dependency: $(Split-Path -Leaf $Destination)"
            return
        }

        Remove-Item -Force -LiteralPath $Destination
    }

    $partialPath = "$Destination.partial"

    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt) {
        Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $partialPath

        Write-Host "Downloading $Uri (attempt $attempt of $MaxAttempts)..."
        try {
            $exitCode = & $DownloadCommand $Uri.AbsoluteUri $partialPath
        } catch {
            $exitCode = -1
            Write-Warning "Download attempt $attempt failed: $($_.Exception.Message)"
        }

        if ($exitCode -eq 0 -and (Test-Path -LiteralPath $partialPath -PathType Leaf)) {
            $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $partialPath).Hash
            if ($actualHash.Equals($Sha256, [StringComparison]::OrdinalIgnoreCase)) {
                Move-Item -Force -LiteralPath $partialPath -Destination $Destination
                Write-Host "Downloaded and verified: $(Split-Path -Leaf $Destination)"
                return
            }

            Write-Warning "Download attempt $attempt produced an unexpected SHA-256 hash."
        } else {
            Write-Warning "Download attempt $attempt exited with code $exitCode."
        }

        if ($attempt -lt $MaxAttempts -and $RetryDelaySeconds -gt 0) {
            $delay = [Math]::Min($RetryDelaySeconds * [Math]::Pow(2, $attempt - 1), 10)
            Start-Sleep -Seconds $delay
        }
    }

    Remove-Item -Force -ErrorAction SilentlyContinue -LiteralPath $partialPath
    throw "Unable to download and verify '$Uri' after $MaxAttempts attempts."
}
