$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$files = @(
    'scripts\ci-windows-stage0.ps1',
    'scripts\stage0-collect-evidence.ps1',
    'scripts\stage0-run-fallout.ps1'
)

foreach ($relativePath in $files) {
    $path = Join-Path $root $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "required Stage 0 PowerShell file is missing: $relativePath"
    }

    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $path,
        [ref]$tokens,
        [ref]$errors) | Out-Null

    if ($errors.Count -ne 0) {
        $messages = @($errors | ForEach-Object { $_.Message })
        throw "PowerShell syntax validation failed for $relativePath`n$($messages -join [Environment]::NewLine)"
    }

    Write-Host "PASS: PowerShell syntax $relativePath"
}

Write-Host 'Stage 0 PowerShell acceptance scripts parsed successfully'
