[CmdletBinding()]
param([string] $Map = 'MAP_OLDALE_TOWN', [string] $Overrides = '', [string] $Out = '', [switch] $Fresh, [switch] $TerrainExample, [switch] $TerrainRegions, [switch] $Connected,
    [string] $Mingw = 'C:\msys64\mingw64\bin', [string] $ReviewIndex = 'build/coverage/studio/index.json')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo 'build/rubyvr_gui.exe'
$env:PATH = $Mingw + ';' + $env:PATH
if (-not (Test-Path -LiteralPath $exe)) { throw 'Run tools/build.ps1 first.' }
if ($TerrainRegions) {
    if ($TerrainExample -or $Overrides) { throw 'Use -TerrainRegions by itself, or choose -TerrainExample / -Overrides.' }
    $Fresh = $true
    $Overrides = Join-Path $repo 'build/terrain-regions/regions.json'
    & python (Join-Path $repo 'tools/build-terrain-region-example.py') --out $Overrides
    if ($LASTEXITCODE -ne 0) { throw 'Regional terrain generation failed. Prepare local assets first.' }
}
if ($TerrainExample) {
    if ($Overrides) { throw 'Use either -TerrainExample or -Overrides.' }
    $Fresh = $true
    if (-not $PSBoundParameters.ContainsKey('Map')) { $Map = 'MAP_ROUTE101' }
    $Overrides = Join-Path $repo 'build/terrain-review/seam-fixture.json'
    & python (Join-Path $repo 'tools/terrain-seam-fixture.py') --out $Overrides
    if ($LASTEXITCODE -ne 0) { throw 'Terrain example generation failed. Prepare local assets first.' }
}
if (-not $Out) {
    $name = if ($Fresh) { 'my-scenery-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8) + '.json' } else { 'my-scenery.json' }
    $Out = Join-Path $repo ('build/' + $name)
}
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
$resumePersonal = $false
if (-not $Overrides) {
    $resumePersonal = (-not $Fresh) -and (Test-Path -LiteralPath $Out)
    $Overrides = if ($resumePersonal) { $Out } else { Join-Path $repo 'mod-assets/voxel-world-v6.json' }
}
$Overrides = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Overrides)
if (-not (Test-Path -LiteralPath $Overrides)) { throw 'Generate the starter pack with tools/prepare-assets.py first, or pass -Overrides.' }
$sessionInput = ''
# The editor protects its input template. When resuming the same personal file,
# take a read-only session baseline so the personal output remains writable.
if ($resumePersonal -and [string]::Equals($Overrides, $Out, [StringComparison]::OrdinalIgnoreCase)) {
    $sessionInput = Join-Path $repo ('build/session-input-' + [Guid]::NewGuid().ToString('N') + '.json')
    Copy-Item -LiteralPath $Overrides -Destination $sessionInput -ErrorAction Stop
    $Overrides = $sessionInput
}
Push-Location -LiteralPath $repo
try {
    # Read the audited starter ledger; never write reviews from the launcher.
    # Personal work can differ; the browser labels that snapshot as historical.
    if ((Test-Path -LiteralPath 'build/coverage/ledger.sqlite') -and (Get-Command python -ErrorAction SilentlyContinue)) {
        & python tools/coverage-ledger.py export-studio --out $ReviewIndex
        if ($LASTEXITCODE -ne 0) { Write-Warning 'Review export failed; any previous snapshot remains available. Re-sync coverage to refresh it.' }
    }
    # Keep native arguments in an array: a one-item if-expression unwraps to a
    # string, and splatting that string passes each character separately.
    [string[]] $studioArgs = @('--map', $Map, '--mode', 'diorama', '--overrides', $Overrides,
        '--out', $Out, '--review-index', $ReviewIndex)
    if ($Connected) { $studioArgs += '--connected' }
    & $exe @studioArgs
    if ($LASTEXITCODE -ne 0) { throw "Studio exited with code $LASTEXITCODE" }
} finally {
    Pop-Location
    if ($sessionInput -and (Test-Path -LiteralPath $sessionInput)) {
        Remove-Item -LiteralPath $sessionInput
    }
}
