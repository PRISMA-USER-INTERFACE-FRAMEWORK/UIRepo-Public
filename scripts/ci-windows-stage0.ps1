$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE"
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'Stage 0 native Windows validation must run on Windows'
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$git = Get-Command git.exe -ErrorAction Stop
$xmake = Get-Command xmake.exe -ErrorAction Stop
Write-Host "git=$($git.Source)"
Write-Host "xmake=$($xmake.Source)"
Invoke-Checked -FilePath $git.Source -Arguments @('--version')
Invoke-Checked -FilePath $xmake.Source -Arguments @('--version')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "Visual Studio locator is missing: $vswhere"
}

$visualStudio = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($visualStudio)) {
    throw 'Visual Studio C++ x64 build tools are not installed'
}
Write-Host "Visual Studio C++ toolchain=$visualStudio"

Write-Host '========== pin CommonLibF4 (commonlibf4-nomad) =========='
$commonLibPin = (Get-Content -Raw (Join-Path $root 'deps\commonlibf4.ref')).Trim()
if ($commonLibPin -notmatch '^[0-9a-f]{40}$') {
    throw "invalid CommonLibF4 pin: $commonLibPin"
}

$dependencyRoot = Join-Path $env:RUNNER_TEMP 'uirepo-stage0-native'
$commonLibPath = Join-Path $dependencyRoot 'commonlibf4'
Remove-Item -Recurse -Force $dependencyRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $commonLibPath | Out-Null

# The clone is public; an optional read token reaches git through GIT_CONFIG_* so it is neither on a command line nor in any config file.
$env:GIT_TERMINAL_PROMPT = '0'
$gitOptions = @('-c', 'core.longpaths=true')
if ($env:COMMONLIB_READ_TOKEN) {
    $env:GIT_CONFIG_COUNT = '1'
    $env:GIT_CONFIG_KEY_0 = "url.https://x-access-token:$($env:COMMONLIB_READ_TOKEN)@github.com/.insteadOf"
    $env:GIT_CONFIG_VALUE_0 = 'https://github.com/'
}

Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'init'))
Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'remote', 'add', 'origin', 'https://github.com/G-A-R-D-E-N/commonlibf4-nomad.git'))
Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'fetch', '--depth', '1', 'origin', $commonLibPin))
Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'checkout', '--detach', 'FETCH_HEAD'))
Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'submodule', 'sync', '--recursive'))
Invoke-Checked -FilePath $git.Source -Arguments ($gitOptions + @('-C', $commonLibPath, 'submodule', 'update', '--init', '--recursive'))
Remove-Item Env:GIT_CONFIG_COUNT, Env:GIT_CONFIG_KEY_0, Env:GIT_CONFIG_VALUE_0, Env:COMMONLIB_READ_TOKEN -ErrorAction SilentlyContinue

$actualCommonLibPin = (& $git.Source -C $commonLibPath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommonLibPin -ne $commonLibPin) {
    throw "CommonLibF4 pin mismatch: expected $commonLibPin, got $actualCommonLibPin"
}

$submoduleStatus = @(& $git.Source -C $commonLibPath submodule status --recursive)
if ($LASTEXITCODE -ne 0) {
    throw 'could not read recursive CommonLibF4 submodule status'
}
$badSubmodules = @($submoduleStatus | Where-Object { $_ -match '^[\-+U]' })
if ($badSubmodules.Count -ne 0) {
    throw "CommonLibF4 has missing, mismatched, or conflicted recursive submodules:`n$($badSubmodules -join [Environment]::NewLine)"
}
Write-Host "CommonLibF4=$actualCommonLibPin"
foreach ($line in $submoduleStatus) {
    Write-Host "submodule $($line.Trim())"
}

$env:UIREPO_COMMONLIBF4_PATH = $commonLibPath
$env:UIREPO_COMMONLIBF4_VERIFIED_SHA = $actualCommonLibPin

Remove-Item -Recurse -Force build, .xmake -ErrorAction SilentlyContinue

Write-Host '========== configure native Windows x64 =========='
Invoke-Checked -FilePath $xmake.Source -Arguments @('f', '-p', 'windows', '-a', 'x64', '-m', 'release', '-c', '-y')

$targets = @(
    'uirepo_core_tests',
    'uirepo_policy_tests',
    'uirepo_api_result_tests',
    'uirepo_native_api_contract_tests',
    'uirepo_spatial_navigation_tests',
    'uirepo_d3d11_tests',
    'uirepo_d3d11_target_color_space_tests',
    'uirepo_d3d11_debug_tests',
    'uirepo_d3d11_text_tests',
    'uirepo_d3d11_surface_tests',
    'uirepo_d3d11_surface_device_tests',
    'uirepo_com_identity_tests'
)

Write-Host '========== build native Windows regressions =========='
foreach ($target in $targets) {
    Write-Host "building $target"
    Invoke-Checked -FilePath $xmake.Source -Arguments @('build', '-y', $target)
}

Write-Host '========== build CommonLibF4 plugin with native MSVC =========='
Invoke-Checked -FilePath $xmake.Source -Arguments @('build', '-y', 'UIRepo_F4')

$bin = Join-Path $root 'build\windows\x64\release'
$dll = Join-Path $bin 'UIRepo_F4.dll'
$pdb = Join-Path $bin 'UIRepo_F4.pdb'
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
    throw "missing native MSVC plugin: $dll"
}
if (-not (Test-Path -LiteralPath $pdb -PathType Leaf)) {
    throw "missing native MSVC plugin symbols: $pdb"
}

$provenance = Join-Path $bin 'COMMONLIB_PROVENANCE.txt'
$provenanceLines = @("commonlib_sha=$actualCommonLibPin") + $submoduleStatus
$provenanceLines | Set-Content -LiteralPath $provenance -Encoding ASCII
Write-Host "native plugin=$dll"
Write-Host "native symbols=$pdb"

Write-Host '========== execute native Windows regressions =========='
foreach ($target in $targets) {
    $exe = Join-Path $bin "$target.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "missing native Windows regression executable: $exe"
    }

    Write-Host "running $target"
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "$target failed on native Windows with exit code $LASTEXITCODE"
    }
    Write-Host "PASS: $target"
}

# The ABI harness is the only place the public table is exercised across a
# real module boundary. Its consumers and providers are separate DLLs, so it is
# built and invoked separately from the single-executable regressions above.
Write-Host '========== build and execute the ABI negotiation harness =========='
$abiModules = @(
    'uirepo_abi_provider_v1',
    'uirepo_abi_provider_v2',
    'uirepo_abi_consumer_documents',
    'uirepo_abi_consumer_surfaces',
    'uirepo_abi_consumer_state'
)
foreach ($target in $abiModules + @('uirepo_abi_harness')) {
    Write-Host "building $target"
    Invoke-Checked -FilePath $xmake.Source -Arguments @('build', '-y', $target)
}

$abiPaths = @()
foreach ($module in $abiModules) {
    $modulePath = Join-Path $bin "$module.dll"
    if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
        throw "missing ABI harness module: $modulePath"
    }
    $abiPaths += $modulePath
}

$abiHarness = Join-Path $bin 'uirepo_abi_harness.exe'
if (-not (Test-Path -LiteralPath $abiHarness -PathType Leaf)) {
    throw "missing ABI harness executable: $abiHarness"
}

& $abiHarness @abiPaths
if ($LASTEXITCODE -ne 0) {
    throw "ABI negotiation harness failed on native Windows with exit code $LASTEXITCODE"
}
Write-Host 'PASS: uirepo_abi_harness'

Write-Host 'Stage 0 native Windows/MSVC/CommonLibF4/D3D11 WARP validation passed'