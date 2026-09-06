@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem Switch every song between drum kits in one command.
rem
rem SSD5 is shelved rather than deleted. It is calibrated, verified and works,
rem and if MINDst turns out to have problems the way back should be one command
rem rather than an edit across eleven files - which is the only reason anyone
rem keeps a thing they have stopped using.
rem
rem   SwitchDrums mndst   - every song plays MINDst Drums   (channel 12)
rem   SwitchDrums ssd5    - every song plays SSD5 Terry Date (channel 10)
rem   SwitchDrums         - says which is in use

set "TARGET=%~1"

if "%TARGET%"=="" (
    echo Songs currently name:
    for %%F in (plans\*.json) do (
        for /f "tokens=2 delims=:" %%A in ('findstr /c:"drum_profile" "%%F"') do (
            set "LINE=%%A"
            set "LINE=!LINE:"=!"
            set "LINE=!LINE:,=!"
            echo    %%~nxF  !LINE!
        )
    )
    echo.
    echo Usage: SwitchDrums mndst   ^|   SwitchDrums ssd5
    exit /b 0
)

if /i "%TARGET%"=="mndst" (
    set "FROM=profiles/ssd5-terry-date.json"
    set "TO=profiles/mndst-drums.json"
    set "NAME=MINDst Drums"
) else if /i "%TARGET%"=="ssd5" (
    set "FROM=profiles/mndst-drums.json"
    set "TO=profiles/ssd5-terry-date.json"
    set "NAME=SSD5 Terry Date"
) else (
    echo Unknown kit "%TARGET%". Use mndst or ssd5.
    exit /b 1
)

echo Switching every song to %NAME% ...

powershell -NoProfile -Command ^
  "$from='%FROM%'; $to='%TO%'; $n=0;" ^
  "Get-ChildItem 'plans\*.json' | ForEach-Object {" ^
  "  $t = Get-Content $_.FullName -Raw;" ^
  "  if ($t -match [regex]::Escape($from)) {" ^
  "    Set-Content -Path $_.FullName -Value ($t -replace [regex]::Escape($from), $to) -NoNewline;" ^
  "    $n++ } };" ^
  "Write-Host ('  ' + $n + ' songs updated')"

echo.
echo Done. Run Install.bat to put the change in the plugin bundle.
echo The kit's channel comes from its own profile - MINDst 12, SSD5 10 - so
echo nothing in Settings needs touching.
