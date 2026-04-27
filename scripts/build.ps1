# =============================================================================
# scripts/build.ps1 — top-level build orchestrator (Windows primary path).
# Runs native (CMake) + managed (dotnet) builds and wires their outputs.
# =============================================================================
param(
    [ValidateSet("Debug", "Release")] [string]$Config = "Release",
    [switch]$Clean,
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"

if ($Clean) {
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
}

# ----- Native (CMake) -----
New-Item -ItemType Directory -Force -Path $build | Out-Null
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64
cmake --build $build --config $Config --parallel

# Copy the freshly built demen.dll next to the managed output so the test run
# and the Phase 0 probe find it via default native-library resolution.
$nativeDll = Join-Path $build "$Config\demen.dll"
$managedOut = Join-Path $root "src\managed\DemEn.Game\bin\$Config\net8.0"
New-Item -ItemType Directory -Force -Path $managedOut | Out-Null
Copy-Item -Force $nativeDll $managedOut

# ----- Managed (dotnet) -----
dotnet build (Join-Path $root "DemEn.sln") -c $Config --nologo

if ($RunTests) {
    ctest --test-dir $build -C $Config --output-on-failure
    dotnet test (Join-Path $root "DemEn.sln") -c $Config --nologo
}

Write-Host "DemEn build ($Config) complete." -ForegroundColor Green
