# Regenerates every image in docs/screenshots from the REAL plugin.
#
# Nothing here is a mock-up. The plugin harness builds the actual editor, loads
# a shipped song, and renders each screen to PNG - the same code a host runs -
# so a screenshot cannot drift from what the plugin looks like. When the UI
# changes, run this and commit what it writes.
#
# It renders against the harness's own test stores, never the owner's taught
# controls, takes, logs or API key: the harness redirects all of them before a
# single processor exists.
#
#   powershell -ExecutionPolicy Bypass -File tools\screenshots.ps1
#
# Build first (Build.bat) - this uses whatever the last build produced.

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$exe  = Join-Path $root 'build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe'
$plan = Join-Path $root 'plans\demo-metal.json'
$out  = Join-Path $root 'docs\screenshots'
$tmp  = Join-Path $env:TEMP 'ghostband-screenshots'

if (-not (Test-Path $exe)) { throw "No harness at $exe - run Build.bat first." }

Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
& $exe $plan --snapshot $tmp | Out-Null
if ($LASTEXITCODE -ne 0) { throw "The harness failed - fix that before taking pictures of it." }

# Published name -> what the harness renders. The -docs renders are at the
# default window size, 1180 wide; the hero is the widest the song screen goes.
$map = [ordered]@{
    'hero.png'         = 'editor-song-wide.png'
    'song.png'         = 'editor-song-docs.png'
    'song-editing.png' = 'editor-song-editing.png'
    'settings.png'     = 'editor-settings-docs.png'
    'takes.png'        = 'editor-takes-docs.png'
    'edit.png'         = 'editor-edit-docs.png'
    'calibrate.png'    = 'editor-calibrate-docs.png'
    'about.png'        = 'editor-about-docs.png'
    'theme-paper.png'  = 'editor-song-paper.png'
}

New-Item -ItemType Directory -Force $out | Out-Null
foreach ($name in $map.Keys) {
    $src = Join-Path $tmp $map[$name]
    if (-not (Test-Path $src)) { throw "The harness did not render $($map[$name])." }
    Copy-Item $src (Join-Path $out $name) -Force
    Write-Host "  $name"
}

Write-Host "`nWrote $($map.Count) screenshots to docs\screenshots."
