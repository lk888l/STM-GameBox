param(
    [switch]$SkipDebug,
    [switch]$SkipRelease
)

$ErrorActionPreference = 'Stop'

function Assert-NativeSuccess([string]$Step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if (-not $SkipDebug) {
        cmake --preset Debug
        Assert-NativeSuccess 'Debug configure'
        cmake --build --preset Debug
        Assert-NativeSuccess 'Debug build'
    }
    if (-not $SkipRelease) {
        cmake --preset Release
        Assert-NativeSuccess 'Release configure'
        cmake --build --preset Release
        Assert-NativeSuccess 'Release build'
    }

    cmake -S tests -B build/HostTests -G Ninja
    Assert-NativeSuccess 'Host configure'
    cmake --build build/HostTests
    Assert-NativeSuccess 'Host build'
    ctest --test-dir build/HostTests --output-on-failure
    Assert-NativeSuccess 'Host tests'
    & build/HostTests/oled_preview.exe build/HostTests/oled_menu_preview.bmp
    Assert-NativeSuccess 'OLED preview'
}
finally {
    Pop-Location
}
