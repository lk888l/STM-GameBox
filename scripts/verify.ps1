# Compatibility entry point; the complete workflow itself is platform-neutral.
param(
    [switch]$SkipDebug,
    [switch]$SkipRelease
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if ($SkipDebug -and $SkipRelease) {
        cmake -DMODE=TEST -P scripts/verify.cmake
    }
    elseif ($SkipDebug) {
        cmake -DPROFILE=Release -P scripts/verify.cmake
    }
    elseif ($SkipRelease) {
        cmake -DPROFILE=Debug -P scripts/verify.cmake
    }
    else {
        cmake -P scripts/verify.cmake
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Verification failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
