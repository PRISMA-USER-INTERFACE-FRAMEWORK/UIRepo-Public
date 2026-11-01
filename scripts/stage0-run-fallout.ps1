param(
    [Parameter(Mandatory = $true)]
    [string]$F4SELoaderPath,

    [string]$WorkingDirectory,

    [string]$EvidencePath = (Join-Path (Get-Location) 'STAGE0_PROCESS.txt'),

    [string[]]$ArgumentList = @(),

    [ValidateRange(10, 600)]
    [int]$LaunchTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-ProcessEvidence {
    param(
        [Parameter(Mandatory = $true)][string[]]$Lines
    )

    $parent = Split-Path -Parent $EvidencePath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $Lines | Set-Content -LiteralPath $EvidencePath -Encoding UTF8
}

$loader = (Resolve-Path -LiteralPath $F4SELoaderPath).Path
if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    $WorkingDirectory = Split-Path -Parent $loader
} else {
    $WorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path
}

$existing = @(Get-Process -Name Fallout4 -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw 'Fallout4.exe is already running; close it before starting the Stage 0 acceptance launcher'
}

$launchUtc = [DateTime]::UtcNow
$startArgs = @{
    FilePath = $loader
    WorkingDirectory = $WorkingDirectory
    PassThru = $true
}
if ($ArgumentList.Count -ne 0) {
    $startArgs['ArgumentList'] = $ArgumentList
}
$launcher = Start-Process @startArgs

$deadline = [DateTime]::UtcNow.AddSeconds($LaunchTimeoutSeconds)
$game = $null
while ($null -eq $game -and [DateTime]::UtcNow -lt $deadline) {
    $candidates = @(
        Get-Process -Name Fallout4 -ErrorAction SilentlyContinue |
            Where-Object {
                try {
                    $_.StartTime.ToUniversalTime() -ge $launchUtc.AddSeconds(-2)
                } catch {
                    $false
                }
            } |
            Sort-Object StartTime -Descending
    )

    if ($candidates.Count -gt 1) {
        throw 'multiple Fallout4.exe processes appeared during the Stage 0 launch; process evidence is ambiguous'
    }
    if ($candidates.Count -eq 1) {
        $game = $candidates[0]
        break
    }

    Start-Sleep -Milliseconds 250
}

if ($null -eq $game) {
    Write-ProcessEvidence -Lines @(
        "launch_utc=$($launchUtc.ToString('o'))",
        "launcher_path=$loader",
        "launcher_pid=$($launcher.Id)",
        'fallout_started=false',
        "launch_timeout_seconds=$LaunchTimeoutSeconds"
    )
    throw "Fallout4.exe did not appear within $LaunchTimeoutSeconds seconds"
}

$gamePid = $game.Id
$gamePath = $game.Path
$gameDirectory = Split-Path -Parent $gamePath
if (-not [string]::Equals(
        [IO.Path]::GetFullPath($gameDirectory).TrimEnd('\'),
        [IO.Path]::GetFullPath($WorkingDirectory).TrimEnd('\'),
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "detected Fallout4.exe outside the F4SE working directory ($gamePath != $WorkingDirectory)"
}

$gameStartUtc = $game.StartTime.ToUniversalTime()
Write-Host "Stage 0 acceptance game detected: $gamePath PID $gamePid"
Write-Host 'Complete the in-game Stage 0 acceptance procedure, then exit Fallout normally.'

$game.WaitForExit()
$exitUtc = [DateTime]::UtcNow
$exitCode = $game.ExitCode

$launcherExitCode = $null
if ($launcher.HasExited) {
    $launcherExitCode = $launcher.ExitCode
}

$evidence = @(
    "launch_utc=$($launchUtc.ToString('o'))",
    "launcher_path=$loader",
    "launcher_pid=$($launcher.Id)",
    "fallout_started=true",
    "fallout_path=$gamePath",
    "fallout_pid=$gamePid",
    "fallout_start_utc=$($gameStartUtc.ToString('o'))",
    "fallout_exit_utc=$($exitUtc.ToString('o'))",
    "fallout_exit_code=$exitCode"
)
if ($null -ne $launcherExitCode) {
    $evidence += "launcher_exit_code=$launcherExitCode"
}
Write-ProcessEvidence -Lines $evidence

if ($exitCode -ne 0) {
    throw "Fallout4.exe exited with non-zero code $exitCode; Stage 0 normal-exit acceptance failed"
}

Write-Host "PASS: Fallout4.exe exited normally with code 0; evidence: $EvidencePath"
