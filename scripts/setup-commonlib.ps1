$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$PinFile = Join-Path $Root 'deps/commonlibf4.ref'
$Destination = Join-Path $Root 'lib/commonlibf4'
$Repository = 'https://github.com/G-A-R-D-E-N/commonlibf4-nomad.git'
$Pin = (Get-Content -Raw $PinFile).Trim()

if ($Pin -notmatch '^[0-9a-fA-F]{40}$') {
    throw "CommonLibF4 pin must be a 40-character commit SHA: $PinFile"
}

function Invoke-GitChecked {
    $GitArgs = @($args)
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git failed ($LASTEXITCODE): git $($GitArgs -join ' ')"
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path $Destination) | Out-Null

if (Test-Path $Destination) {
    if (-not (Test-Path (Join-Path $Destination '.git'))) {
        throw "Refusing to replace non-git directory: $Destination"
    }

    $Dirty = (& git -C $Destination status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect existing CommonLibF4 checkout"
    }
    if ($Dirty) {
        throw "CommonLibF4 checkout has local changes. Clean or move it before syncing the pinned revision."
    }

    Invoke-GitChecked -C $Destination remote set-url origin $Repository
    Invoke-GitChecked -C $Destination fetch origin
} else {
    Invoke-GitChecked clone --recurse-submodules $Repository $Destination
}

Invoke-GitChecked -C $Destination checkout --detach $Pin
Invoke-GitChecked -C $Destination submodule sync --recursive
Invoke-GitChecked -C $Destination submodule update --init --recursive

$Actual = (& git -C $Destination rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Actual -ne $Pin) {
    throw "CommonLibF4 revision mismatch. Expected $Pin, got $Actual"
}

$SubmoduleStatus = @(& git -C $Destination submodule status --recursive)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect CommonLibF4 recursive submodules"
}
$BadSubmodules = @($SubmoduleStatus | Where-Object { $_ -match '^[\-\+U]' })
if ($BadSubmodules.Count -ne 0) {
    throw "CommonLibF4 has missing or mismatched submodules:`n$($BadSubmodules -join [Environment]::NewLine)"
}

Write-Host "CommonLibF4 (commonlibf4-nomad) ready at $Actual"
foreach ($Line in $SubmoduleStatus) {
    Write-Host "  submodule $($Line.Trim())"
}
Write-Host "Build with: xmake f -p windows -a x64 -m release -y"
Write-Host "            xmake build -y UIRepo_F4"
