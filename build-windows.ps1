# Builds BOTH plugins for Windows: FRACTURE and CRATE, as VST3 (for Ableton Live,
# Reaper, Cubase, Bitwig, FL Studio) and as standalone apps.
#
#   .\build-windows.ps1              build, run the DSP tests
#   .\build-windows.ps1 -Install     ... and copy both VST3s into the system folder
#                                    (run PowerShell as administrator for this)
#   .\build-windows.ps1 -Package     ... and pack dist\ (a zip, plus an installer
#                                    when Inno Setup 6 is installed)
#
# What you need first, and nothing else:
#   Visual Studio 2022 (the free Community edition) with "Desktop development with C++"
#   CMake 3.22 or later (Visual Studio's own copy is fine) and Git
#
# If PowerShell refuses to run scripts, start it with:
#   powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
#
# JUCE is fetched once into .juce and shared between the two builds.
param(
    [switch]$Install,
    [switch]$Package,
    [string]$Company = ""
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

foreach ($tool in "cmake", "git") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Write-Host "$tool is missing. Install it (or run this from a Visual Studio Developer PowerShell)."
        exit 1
    }
}

# ---- one JUCE for both builds
$jucetag = if ($env:JUCE_TAG) { $env:JUCE_TAG } else { "8.0.15" }
$juce = if ($env:JUCE_PATH) { $env:JUCE_PATH } else { Join-Path $root ".juce" }
if (-not (Test-Path (Join-Path $juce "CMakeLists.txt"))) {
    Write-Host "==> fetching JUCE $jucetag (once, about 200 MB)"
    if (Test-Path $juce) { Remove-Item -Recurse -Force $juce }
    git clone --depth 1 --branch $jucetag https://github.com/juce-framework/JUCE.git $juce
    if ($LASTEXITCODE -ne 0) { Write-Host "Could not fetch JUCE. Check your network and try again."; exit 1 }
} else {
    Write-Host "==> using the JUCE already in $juce"
}

function Invoke-Checked([string]$what, [scriptblock]$block) {
    & $block
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

$products = @(
    @{ Dir = "plugin"; Name = "FRACTURE"; Target = "Fracture"; Var = "FRACTURE"; TestArgs = @("plugin\tests\shaper_reference.csv", "plugin\tests\presets.json") },
    @{ Dir = "crate";  Name = "CRATE";    Target = "Crate";    Var = "CRATE";    TestArgs = @() }
)

foreach ($p in $products) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "  $($p.Name)"
    Write-Host "============================================================"
    $build = Join-Path $root "$($p.Dir)\build-windows"
    $cfg = @("-S", (Join-Path $root $p.Dir), "-B", $build, "-A", "x64", "-D$($p.Var)_JUCE_PATH=$juce")
    if ($Company) { $cfg += "-D$($p.Var)_COMPANY=$Company" }
    Invoke-Checked "configuring $($p.Name)" { cmake @cfg }
    Invoke-Checked "building $($p.Name)" {
        cmake --build $build --config Release --parallel `
            --target "$($p.Target)_VST3" "$($p.Target)_Standalone" test_core
    }

    Write-Host "==> DSP measurements"
    $test = Join-Path $build "Release\test_core.exe"
    Invoke-Checked "$($p.Name) DSP tests" { & $test @($p.TestArgs | ForEach-Object { Join-Path $root $_ }) }

    $vst3 = Join-Path $build "$($p.Target)_artefacts\Release\VST3\$($p.Name).vst3"
    Write-Host "    built $vst3"
    if ($Install) {
        $dest = Join-Path $env:CommonProgramFiles "VST3"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        $target = Join-Path $dest "$($p.Name).vst3"
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Copy-Item -Recurse $vst3 $target
        Write-Host "    installed $target"
    }
}

if ($Package) {
    & (Join-Path $root "packaging\windows\make-package.ps1")
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host ""
Write-Host "============================================================"
if ($Install) {
    Write-Host "  both installed in $env:CommonProgramFiles\VST3"
    Write-Host "  In Ableton Live 12: Settings > Plug-Ins, turn on"
    Write-Host "  'Use VST3 Plug-In System Folders', then press Rescan."
} else {
    Write-Host "  both built. Install with -Install (as administrator), or run"
    Write-Host "  the installer that -Package makes."
}
Write-Host "============================================================"
