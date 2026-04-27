# =============================================================================
# scripts/package.ps1 — build Release + assemble a shippable folder.
# Invariant #5: cold-launch-to-playable ≤ 10 s on a clean Windows Sandbox.
# Usage:
#   .\scripts\package.ps1 [-Out .\out\DemEn]
# Produces a self-contained folder containing:
#   demen.dll, DemEn.Game.exe, shaders/spirv/*.spv, assets/**, README.txt
# =============================================================================
param(
    [string]$Config = "Release",
    [string]$Out    = ".\out\DemEn"
)

$ErrorActionPreference = "Stop"

Write-Host "[package] Building native core (CMake, $Config) ..."
cmake --preset default | Out-Null
cmake --build --preset $Config.ToLower()

Write-Host "[package] Building managed (.NET 8, AOT, $Config) ..."
dotnet publish src/managed/DemEn.Game/DemEn.Game.csproj `
    -c $Config -r win-x64 --self-contained true /p:PublishAot=true

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Path $Out | Out-Null

# Payload.
Copy-Item build/Release/demen.dll          $Out -Force
Copy-Item src/managed/DemEn.Game/bin/$Config/net8.0/win-x64/publish/DemEn.Game.exe $Out -Force
Copy-Item shaders/spirv                     $Out/shaders -Recurse
Copy-Item assets                            $Out/assets  -Recurse

@"
DemEn — Layer 1 (voxel simulation, physics substrate).
Launch DemEn.Game.exe. ESC closes.

Prerequisites:
  * Windows 10/11 x64
  * Vulkan 1.3 runtime (usually provided by the GPU driver)
First launch primes pipeline_cache.bin next to the exe; subsequent
runs come up in under a second.
"@ | Out-File -FilePath $Out/README.txt -Encoding ASCII

Write-Host "[package] Done -> $Out"
