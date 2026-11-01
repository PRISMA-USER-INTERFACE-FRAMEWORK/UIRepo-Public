$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$PinnedCommonLib = Join-Path $Root 'lib/commonlibf4'
$PreviousCommonLib = $env:UIREPO_COMMONLIBF4_PATH

function Invoke-Checked {
    param([Parameter(Mandatory = $true)][string]$Command)

    $CommandArgs = @($args)
    & $Command @CommandArgs
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE: $($CommandArgs -join ' ')"
    }
}

& (Join-Path $PSScriptRoot 'setup-commonlib.ps1')
$env:UIREPO_COMMONLIBF4_PATH = $PinnedCommonLib

Push-Location $Root
try {
    Invoke-Checked 'xmake' 'f' '-p' 'windows' '-a' 'x64' '-m' 'release' '-y'
    Invoke-Checked 'xmake' 'build' '-y' 'uirepo_core_tests'
    Invoke-Checked 'xmake' 'build' '-y' 'uirepo_d3d11_tests'
    Invoke-Checked 'xmake' 'build' '-y' 'UIRepo_F4'

    $CoreTest = Join-Path $Root 'build/windows/x64/release/uirepo_core_tests.exe'
    $D3DTest = Join-Path $Root 'build/windows/x64/release/uirepo_d3d11_tests.exe'
    $Dll = Join-Path $Root 'build/windows/x64/release/UIRepo_F4.dll'

    foreach ($RequiredFile in @($CoreTest, $D3DTest, $Dll)) {
        if (-not (Test-Path $RequiredFile)) {
            throw "Native build completed without the expected artifact: $RequiredFile"
        }
    }

    Invoke-Checked $CoreTest
    Invoke-Checked $D3DTest

    $Item = Get-Item $Dll
    $Hash = (Get-FileHash -Algorithm SHA256 $Dll).Hash.ToLowerInvariant()
    Write-Host "UIRepo native Windows regressions passed"
    Write-Host "UIRepo_F4 built successfully"
    Write-Host "  $($Item.FullName)"
    Write-Host "  $([Math]::Round($Item.Length / 1KB, 1)) KiB"
    Write-Host "  SHA256 $Hash"
} finally {
    Pop-Location
    if ($null -eq $PreviousCommonLib) {
        Remove-Item Env:UIREPO_COMMONLIBF4_PATH -ErrorAction SilentlyContinue
    } else {
        $env:UIREPO_COMMONLIBF4_PATH = $PreviousCommonLib
    }
}
