param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [string]$BundleRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$EvidenceDir = (Join-Path (Get-Location) 'Stage0Evidence'),

    [string]$InstalledDllPath,

    [string]$ProcessEvidencePath,

    [switch]$BaselineOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Require-File {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "required Stage 0 file is missing: $Path"
    }
}

function Resolve-Python {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return @{ Command = $python.Source; Prefix = @() }
    }

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return @{ Command = $py.Source; Prefix = @('-3') }
    }

    throw 'Python 3 is required to run the Stage 0 verifier'
}

$root = (Resolve-Path -LiteralPath $BundleRoot).Path
$log = (Resolve-Path -LiteralPath $LogPath).Path
$manifest = Join-Path $root 'build\windows\x64\release\STAGE0_BUILD.txt'
$dll = Join-Path $root 'build\windows\x64\release\UIRepo_F4.dll'
$pdb = Join-Path $root 'build\windows\x64\release\UIRepo_F4.pdb'
$provenance = Join-Path $root 'build\windows\x64\release\COMMONLIB_PROVENANCE.txt'
$verifier = Join-Path $root 'scripts\stage0-verify-log.py'
$repeatedVerifier = Join-Path $root 'scripts\stage0-verify-repeated-lifecycle.py'

Require-File $manifest
Require-File $dll
Require-File $pdb
Require-File $provenance
Require-File $verifier
Require-File $repeatedVerifier
Require-File $log

if (-not $BaselineOnly -and [string]::IsNullOrWhiteSpace($InstalledDllPath)) {
    throw 'final Stage 0 evidence collection requires -InstalledDllPath so the installed plugin is proven identical to the CI artifact'
}
if (-not $BaselineOnly -and [string]::IsNullOrWhiteSpace($ProcessEvidencePath)) {
    throw 'final Stage 0 evidence collection requires -ProcessEvidencePath from stage0-run-fallout.ps1 so normal process exit is proven'
}

$manifestValues = @{}
$manifestHashes = @{}
foreach ($line in Get-Content -LiteralPath $manifest) {
    if ($line -match '^([^=]+)=(.*)$') {
        $manifestValues[$matches[1].Trim()] = $matches[2].Trim()
        continue
    }
    if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$') {
        $artifactName = Split-Path -Leaf $matches[2].Trim()
        $manifestHashes[$artifactName] = $matches[1].ToLowerInvariant()
    }
}
foreach ($required in @('workflow_sha', 'source_head_sha', 'commonlib_sha')) {
    if (-not $manifestValues.ContainsKey($required) -or [string]::IsNullOrWhiteSpace($manifestValues[$required])) {
        throw "STAGE0_BUILD.txt is missing $required"
    }
}
foreach ($artifactName in @('UIRepo_F4.dll', 'UIRepo_F4.pdb', 'COMMONLIB_PROVENANCE.txt')) {
    if (-not $manifestHashes.ContainsKey($artifactName)) {
        throw "STAGE0_BUILD.txt is missing SHA-256 for $artifactName"
    }
}

$bundleDllHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dll).Hash.ToLowerInvariant()
$bundlePdbHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $pdb).Hash.ToLowerInvariant()
$provenanceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $provenance).Hash.ToLowerInvariant()
if ($bundleDllHash -ne $manifestHashes['UIRepo_F4.dll']) {
    throw 'CI bundle DLL hash does not match STAGE0_BUILD.txt'
}
if ($bundlePdbHash -ne $manifestHashes['UIRepo_F4.pdb']) {
    throw 'CI bundle PDB hash does not match STAGE0_BUILD.txt'
}
if ($provenanceHash -ne $manifestHashes['COMMONLIB_PROVENANCE.txt']) {
    throw 'CommonLib provenance hash does not match STAGE0_BUILD.txt'
}

$provenanceLines = @(Get-Content -LiteralPath $provenance)
if ($provenanceLines.Count -lt 2 -or $provenanceLines[0] -notmatch '^commonlib_sha=([0-9a-fA-F]{40})$') {
    throw 'COMMONLIB_PROVENANCE.txt is missing its CommonLibF4 SHA or recursive submodule status'
}
$provenanceCommonLib = $matches[1].ToLowerInvariant()
if ($provenanceCommonLib -ne $manifestValues['commonlib_sha'].ToLowerInvariant()) {
    throw "CommonLib provenance SHA does not match STAGE0_BUILD.txt ($provenanceCommonLib != $($manifestValues['commonlib_sha']))"
}
$badSubmodules = @($provenanceLines | Select-Object -Skip 1 | Where-Object { $_ -match '^[\-+U]' })
if ($badSubmodules.Count -ne 0) {
    throw "COMMONLIB_PROVENANCE.txt contains missing, mismatched, or conflicted submodules:`n$($badSubmodules -join [Environment]::NewLine)"
}
Write-Host 'PASS: recursive CommonLibF4 provenance matches the CI manifest'

$installedHash = $null
if (-not [string]::IsNullOrWhiteSpace($InstalledDllPath)) {
    Require-File $InstalledDllPath
    $installedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $InstalledDllPath).Hash.ToLowerInvariant()
    if ($installedHash -ne $bundleDllHash) {
        throw "installed UIRepo_F4.dll does not match the CI artifact ($installedHash != $bundleDllHash)"
    }
    Write-Host 'PASS: installed UIRepo_F4.dll matches the CI artifact'
}

$processEvidence = $null
$processValues = @{}
if (-not [string]::IsNullOrWhiteSpace($ProcessEvidencePath)) {
    Require-File $ProcessEvidencePath
    $processEvidence = (Resolve-Path -LiteralPath $ProcessEvidencePath).Path
    foreach ($line in Get-Content -LiteralPath $processEvidence) {
        if ($line -match '^([^=]+)=(.*)$') {
            $processValues[$matches[1].Trim()] = $matches[2].Trim()
        }
    }

    foreach ($required in @(
        'launcher_path',
        'fallout_started',
        'fallout_path',
        'fallout_pid',
        'fallout_start_utc',
        'fallout_exit_utc',
        'fallout_exit_code')) {
        if (-not $processValues.ContainsKey($required) -or [string]::IsNullOrWhiteSpace($processValues[$required])) {
            throw "STAGE0_PROCESS.txt is missing $required"
        }
    }

    if ($processValues['fallout_started'] -ne 'true') {
        throw 'STAGE0_PROCESS.txt does not prove Fallout4.exe started'
    }
    if ($processValues['fallout_exit_code'] -ne '0') {
        throw "Fallout4.exe did not exit normally (exit code $($processValues['fallout_exit_code']))"
    }

    $falloutPidValue = 0
    if (-not [int]::TryParse($processValues['fallout_pid'], [ref]$falloutPidValue) -or $falloutPidValue -le 0) {
        throw "STAGE0_PROCESS.txt has an invalid Fallout PID: $($processValues['fallout_pid'])"
    }

    if ((Split-Path -Leaf $processValues['fallout_path']) -ne 'Fallout4.exe') {
        throw "STAGE0_PROCESS.txt does not identify Fallout4.exe: $($processValues['fallout_path'])"
    }
    $launcherDirectory = [IO.Path]::GetFullPath((Split-Path -Parent $processValues['launcher_path'])).TrimEnd('\')
    $falloutDirectory = [IO.Path]::GetFullPath((Split-Path -Parent $processValues['fallout_path'])).TrimEnd('\')
    if (-not [string]::Equals($launcherDirectory, $falloutDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw "STAGE0_PROCESS.txt records Fallout outside the F4SE loader directory ($falloutDirectory != $launcherDirectory)"
    }

    $gameStart = [DateTimeOffset]::Parse($processValues['fallout_start_utc'])
    $gameExit = [DateTimeOffset]::Parse($processValues['fallout_exit_utc'])
    if ($gameExit -lt $gameStart) {
        throw 'STAGE0_PROCESS.txt has an exit timestamp before the game start timestamp'
    }

    Write-Host "PASS: Fallout4.exe process evidence reports normal exit code 0 (PID $falloutPidValue)"
}

$python = Resolve-Python
$verifyArgs = @(
    $verifier,
    '--manifest', $manifest,
    '--dll', $dll,
    '--pdb', $pdb,
    '--provenance', $provenance
)
if (-not $BaselineOnly) {
    $verifyArgs += '--full-cycle'
}
$verifyArgs += $log

New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
$evidence = (Resolve-Path -LiteralPath $EvidenceDir).Path
$verifyOutputPath = Join-Path $evidence 'stage0-verifier.txt'

Write-Host '========== Stage 0 log/artifact verification =========='
$invokeArgs = @($python.Prefix) + $verifyArgs
$verifyOutput = & $python.Command @invokeArgs 2>&1
$verifyOutput | Tee-Object -FilePath $verifyOutputPath
if ($LASTEXITCODE -ne 0) {
    throw "Stage 0 verifier failed with exit code $LASTEXITCODE"
}

if (-not $BaselineOnly) {
    $repeatedOutputPath = Join-Path $evidence 'stage0-repeated-lifecycle.txt'
    Write-Host '========== Stage 0 repeated lifecycle verification =========='
    $repeatedArgs = @($python.Prefix) + @($repeatedVerifier, $log)
    $repeatedOutput = & $python.Command @repeatedArgs 2>&1
    $repeatedOutput | Tee-Object -FilePath $repeatedOutputPath
    if ($LASTEXITCODE -ne 0) {
        throw "Stage 0 repeated lifecycle verifier failed with exit code $LASTEXITCODE"
    }
}

Copy-Item -LiteralPath $log -Destination (Join-Path $evidence 'UIRepo_F4.log') -Force
Copy-Item -LiteralPath $manifest -Destination (Join-Path $evidence 'STAGE0_BUILD.txt') -Force
Copy-Item -LiteralPath $dll -Destination (Join-Path $evidence 'UIRepo_F4.dll') -Force
Copy-Item -LiteralPath $pdb -Destination (Join-Path $evidence 'UIRepo_F4.pdb') -Force
Copy-Item -LiteralPath $provenance -Destination (Join-Path $evidence 'COMMONLIB_PROVENANCE.txt') -Force
if ($processEvidence) {
    $processDestination = Join-Path $evidence 'STAGE0_PROCESS.txt'
    if (-not [string]::Equals(
            [IO.Path]::GetFullPath($processEvidence),
            [IO.Path]::GetFullPath($processDestination),
            [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $processEvidence -Destination $processDestination -Force
    }
}

$systemPath = Join-Path $evidence 'system.txt'
$systemLines = @(
    "collected_utc=$([DateTime]::UtcNow.ToString('o'))",
    "workflow_sha=$($manifestValues['workflow_sha'])",
    "source_head_sha=$($manifestValues['source_head_sha'])",
    "commonlib_sha=$($manifestValues['commonlib_sha'])",
    "bundle_dll_sha256=$bundleDllHash",
    "bundle_pdb_sha256=$bundlePdbHash",
    "commonlib_provenance_sha256=$provenanceHash"
)
if ($installedHash) {
    $systemLines += "installed_dll_sha256=$installedHash"
}
if ($processValues.ContainsKey('fallout_pid')) {
    $systemLines += "fallout_path=$($processValues['fallout_path'])"
    $systemLines += "fallout_pid=$($processValues['fallout_pid'])"
    $systemLines += "fallout_exit_code=$($processValues['fallout_exit_code'])"
}
$systemLines | Set-Content -LiteralPath $systemPath -Encoding UTF8

try {
    $os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
    Add-Content -LiteralPath $systemPath -Value "`n[operating_system]"
    $os | Format-List | Out-String | Add-Content -LiteralPath $systemPath
} catch {
    Add-Content -LiteralPath $systemPath -Value "`n[operating_system]`nCIM query failed: $($_.Exception.Message)"
}

try {
    $gpu = Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, VideoProcessor, AdapterRAM
    Add-Content -LiteralPath $systemPath -Value "`n[gpu]"
    $gpu | Format-List | Out-String | Add-Content -LiteralPath $systemPath
} catch {
    Add-Content -LiteralPath $systemPath -Value "`n[gpu]`nCIM query failed: $($_.Exception.Message)"
}

$checklistPath = Join-Path $evidence 'MANUAL_CHECKLIST.txt'
@'
Stage 0 manual observations still requiring a human pass/fail decision:
[ ] Built-in UIRepo native runtime probe was visible.
[ ] DirectWrite probe text was readable and correctly positioned.
[ ] Verifier-confirmed alt-tab cycles completed without visible corruption or crash.
[ ] Verifier-confirmed repeated resize cycles completed without visible corruption or stale rendering.
[ ] Supported windowed/borderless mode transitions recovered correctly.
[ ] New game reached gameplay.
[ ] Verifier-confirmed MainMenu returns reached a stable visible MainMenu.
[ ] Existing save loaded and reached gameplay.
[ ] Repeated MainMenu/session cycle completed without visible corruption or crash.
[ ] No UIRepo-attributable D3D debug-layer errors were observed.
[ ] No runaway per-frame logging was observed.
[ ] Fallout closed without a visible crash dialog or hang after process evidence recorded exit code 0.
'@ | Set-Content -LiteralPath $checklistPath -Encoding UTF8

Write-Host "Stage 0 objective evidence collected at: $evidence"
Write-Host 'Complete MANUAL_CHECKLIST.txt after the in-game observations. Do not close Stage 0 until every item passes.'
