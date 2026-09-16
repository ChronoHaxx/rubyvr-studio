# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [string]$Checkpoint = '',
    [string]$SessionDirectory = '',
    [switch]$Fresh,
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
if (-not $SessionDirectory) {
    $SessionDirectory = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'handoff.json')) {
        $PSScriptRoot
    } else { Join-Path (Split-Path -Parent $PSScriptRoot) 'build/dev-session' }
}
$runner = Join-Path $PSScriptRoot 'run-dev-game.py'
if (-not (Test-Path -LiteralPath $runner)) { throw 'The launcher is incomplete: run-dev-game.py is missing. Prepare the local session again.' }
$python = Get-Command python -ErrorAction SilentlyContinue
$prefix = @()
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
    $prefix = @('-3')
}
if (-not $python) { throw 'Python 3 is needed by this local demo launcher. Install Python 3, then reopen this command. See docs/demo-runner.md.' }
$arguments = @($runner, '--session', $SessionDirectory)
if ($Checkpoint) { $arguments += @('--checkpoint', $Checkpoint) }
if ($Fresh) { $arguments += '--fresh' }
if ($Check) { $arguments += '--check' }
& $python.Source @prefix @arguments
if ($LASTEXITCODE -ne 0) { throw 'RubyVR could not start or exited with an error. See the message above; your existing saves have been kept.' }
