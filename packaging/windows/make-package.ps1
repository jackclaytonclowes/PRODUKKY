# Packs BOTH Windows builds into dist\:
#   Fracture-and-Crate-<version>-Windows.zip          the VST3s, the apps, the read-me
#   Fracture-and-Crate-<version>-Windows-Setup.exe    an installer, when Inno Setup 6 is present
#
#   .\build-windows.ps1          # build them first
#   .\packaging\windows\make-package.ps1
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here "..\..")).Path

$version = (Select-String -Path (Join-Path $root "crate\CMakeLists.txt") -Pattern '^project\(CRATE VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = "0.1.0" }

$fracture = Join-Path $root "plugin\build-windows\Fracture_artefacts\Release"
$crate = Join-Path $root "crate\build-windows\Crate_artefacts\Release"
foreach ($pair in @(@($fracture, "FRACTURE"), @($crate, "CRATE"))) {
    if (-not (Test-Path (Join-Path $pair[0] "VST3\$($pair[1]).vst3"))) {
        Write-Host "$($pair[1]) is not built. Run .\build-windows.ps1"
        exit 1
    }
}

$dist = Join-Path $root "dist"
$stage = Join-Path $dist "windows-stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage\VST3", "$stage\Standalone apps" | Out-Null
foreach ($pair in @(@($fracture, "FRACTURE"), @($crate, "CRATE"))) {
    Copy-Item -Recurse (Join-Path $pair[0] "VST3\$($pair[1]).vst3") "$stage\VST3\$($pair[1]).vst3"
    $app = Join-Path $pair[0] "Standalone\$($pair[1]).exe"
    if (Test-Path $app) { Copy-Item $app "$stage\Standalone apps\$($pair[1]).exe" }
}
Copy-Item (Join-Path $here "READ ME FIRST.txt") "$stage\READ ME FIRST.txt"

Write-Host "==> zip"
$zip = Join-Path $dist "Fracture-and-Crate-$version-Windows.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "    $zip"

$iscc = @(
    (Get-Command iscc -ErrorAction SilentlyContinue | ForEach-Object { $_.Source }),
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($iscc) {
    Write-Host "==> installer"
    & $iscc /Q "/DAppVersion=$version" "/DStage=$stage" "/DOutDir=$dist" (Join-Path $here "installer.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed (exit $LASTEXITCODE)" }
    Write-Host "    $(Join-Path $dist "Fracture-and-Crate-$version-Windows-Setup.exe")"
} else {
    Write-Host "    (Inno Setup 6 not found: zip only. winget install JRSoftware.InnoSetup)"
}
