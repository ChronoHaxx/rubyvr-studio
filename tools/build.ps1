[CmdletBinding()]
param([string] $Mingw = 'C:\msys64\mingw64\bin', [int] $Jobs = 4)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $Mingw 'g++.exe'))) { throw 'Install the MSYS2 mingw64 packages listed in docs/building.md, or pass -Mingw.' }
$env:PATH = $Mingw + ';' + $env:PATH
& cmake -S $repo -B (Join-Path $repo 'build') -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build (Join-Path $repo 'build') --target rubyvr_gui rubyvr_studio -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Studio build failed.' }

# Put MinGW runtime dependencies beside the executables. Python review jobs and
# Explorer launches must not rely on inheriting this shell's temporary PATH.
$buildDir = Join-Path $repo 'build'
$objdump = Join-Path $Mingw 'objdump.exe'
if (-not (Test-Path -LiteralPath $objdump)) { throw 'MinGW objdump.exe is required to collect runtime DLLs.' }
$pending = [System.Collections.Generic.Queue[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($binary in @('rubyvr_gui.exe', 'rubyvr_studio.exe')) { $pending.Enqueue((Join-Path $buildDir $binary)) }
while ($pending.Count) {
    $binary = $pending.Dequeue()
    $imports = & $objdump -p $binary
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect runtime imports: $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch '^\s*DLL Name:\s*([A-Za-z0-9_.-]+\.dll)\s*$') { continue }
        $dll = $Matches[1]
        if (-not $seen.Add($dll)) { continue }
        $sourceDll = Join-Path $Mingw $dll
        # Windows system DLLs are supplied by the OS, never copied into build.
        if (-not (Test-Path -LiteralPath $sourceDll)) { continue }
        Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $buildDir $dll) -Force
        $pending.Enqueue($sourceDll)
    }
}
