# Starts Gig Performer 5, first clearing any copy of it that has already quit
# but not exited.
#
# WHY: Kontakt 8 (8.13.1, measured 2026-09-26) hangs while it is being shut
# down. Loaded into a bare test host, it finished its work and then never let
# the process exit - two runs of two - while SSD5, MODO Bass 2, IRON 2 and
# Ghostband all exited cleanly. So any Gig Performer session with a Kontakt
# instrument in it closes its window on Quit and leaves GigPerformer5.exe
# running, invisible, holding memory and possibly the audio driver. This is
# Native Instruments' fault to fix; this script makes it not your chore.
#
# A copy counts as a leftover only if it has NO WINDOW and has been running for
# more than 20 seconds - a GP5 that is still starting up has no window either,
# and must not be killed. A copy WITH a window is a real session: it is brought
# to the front and nothing new is started.
#
# Every clear is logged to %APPDATA%\Ghostband\gp-launcher.log, so how often
# this happens is on record rather than a feeling.

# The parameters exist so the logic can be tested against a harmless program;
# the defaults are Gig Performer.
param (
    [string] $Exe         = 'C:\Program Files\Gig Performer 5\GigPerformer5.exe',
    [string] $ProcessName = 'GigPerformer5',
    [int]    $GraceSeconds = 20,
    [string] $Log         = (Join-Path $env:APPDATA 'Ghostband\gp-launcher.log')
)

$ErrorActionPreference = 'SilentlyContinue'
$exe = $Exe
$log = $Log
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null

function Write-Log ($text) {
    Add-Content -Path $log -Value ((Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '  ' + $text)
}

$running = @(Get-Process -Name $ProcessName)

# A real session is open: show it, start nothing.
$live = $running | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if ($live) {
    (New-Object -ComObject WScript.Shell).AppActivate($live.Id) | Out-Null
    exit 0
}

$leftovers = $running | Where-Object {
    $_.MainWindowHandle -eq 0 -and ((Get-Date) - $_.StartTime).TotalSeconds -gt $GraceSeconds
}

foreach ($p in $leftovers) {
    $age = [int]((Get-Date) - $p.StartTime).TotalMinutes
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit(10000) | Out-Null
    Write-Log ("cleared a Gig Performer that had quit but not exited   pid {0}, started {1:yyyy-MM-dd HH:mm}, {2} min old" -f $p.Id, $p.StartTime, $age)
}

Start-Process -FilePath $exe
