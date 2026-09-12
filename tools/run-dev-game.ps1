[CmdletBinding()]
param(
    [string]$Checkpoint = 'Route 101',
    [switch]$Fresh,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$studioRoot = Split-Path -Parent $PSScriptRoot
$devRoot = Join-Path $studioRoot 'build/dev-session'
$manifestPath = Join-Path $devRoot 'handoff.json'
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw 'The private native developer build is not prepared. See docs/developer-mode.md.'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files) {
    $path = Join-Path $devRoot $entry.name
    if (-not (Test-Path -LiteralPath $path) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Prepared developer build changed or missing: $($entry.name). Rebuild the handoff."
    }
}
if ($Checkpoint -notmatch '^[a-zA-Z0-9][a-zA-Z0-9 _-]{0,47}$') { throw 'Invalid checkpoint name.' }
$checkpoints = Join-Path $devRoot 'checkpoints'
$state = Join-Path $checkpoints ($Checkpoint + '.state')
if (-not $Fresh -and -not (Test-Path -LiteralPath $state -PathType Leaf)) { throw "Checkpoint missing: $Checkpoint" }
Write-Host "RubyVR Developer build: $($manifest.source_commit)"
Write-Host 'Camera follow-up: focus 3D to walk relative to the view; J/L orbit, R resets north-up. Esc > Camera in the original window offers presets.'
Write-Host 'In the original Ruby window: Esc > Developer for pause/step, speed and noclip; Checkpoints for named save/load.'
if ($Check) { Write-Host 'PASS: prepared developer inputs verified'; return }
try {
    $sessionLock = [IO.File]::Open((Join-Path $devRoot 'session.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch { throw 'This developer session is already running. Close its Ruby window before reopening.' }
try {
$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = Join-Path $devRoot 'RubyRecomp.exe'
$psi.WorkingDirectory = $manifest.game_directory
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($key in @($psi.EnvironmentVariables.Keys)) {
    if ($key -like 'RUBYVR_*' -or $key -in @('GBARECOMP_INPUT_RECORD','GBARECOMP_INPUT_REPLAY')) { $psi.EnvironmentVariables.Remove($key) }
}
$psi.EnvironmentVariables['PATH'] = 'C:\msys64\mingw64\bin;' + $env:PATH
$psi.EnvironmentVariables['RUBYVR_DEV_DIR'] = $checkpoints
$psi.EnvironmentVariables['RUBYVR_DEV_START_CHECKPOINT'] = $Checkpoint
$psi.EnvironmentVariables['RUBYVR_VIEWER'] = '1'
$psi.EnvironmentVariables['RUBYVR_WORLD_DEBUG'] = '1'
$psi.EnvironmentVariables['RUBYVR_BUILD_MODE'] = 'diorama'
$psi.EnvironmentVariables['RUBYVR_OVERRIDES'] = Join-Path $devRoot 'review-pack.json'
$psi.EnvironmentVariables['GBARECOMP_PRESENT_IN_PLACE'] = '1'
$psi.EnvironmentVariables['GBARECOMP_SELFHEAL_RECOMPILE'] = '0'
$psi.EnvironmentVariables['GBARECOMP_COVERAGE_JSON'] = Join-Path $devRoot 'coverage.json'
$psi.EnvironmentVariables['GBARECOMP_MISS_FRAG'] = Join-Path $devRoot 'misses.toml.frag'
$config = Join-Path $manifest.game_directory 'variants/ruby/game.toml'
$testSave = Join-Path $devRoot 'test-session.sav'
$psi.Arguments = '--no-launcher --window --scale 4 --volume 0 --config "' + $config + '" --save "' + $testSave + '"'
if (-not $Fresh) { $psi.Arguments += ' --load-state "' + $state + '"' }
$process = [Diagnostics.Process]::Start($psi)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
Set-Content -LiteralPath (Join-Path $devRoot 'last-run.out.log') -Value $stdout.Result -Encoding UTF8
Set-Content -LiteralPath (Join-Path $devRoot 'last-run.err.log') -Value $stderr.Result -Encoding UTF8
if ($process.ExitCode -ne 0) { throw "Developer game exited with $($process.ExitCode). Logs are in build/dev-session." }
} finally { $sessionLock.Dispose() }
