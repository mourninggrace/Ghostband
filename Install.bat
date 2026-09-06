@echo off
setlocal
cd /d "%~dp0"

set "SRC=build\GhostbandPlugin_artefacts\Release\VST3\Ghostband.vst3"
set "DST=%CommonProgramFiles%\VST3\Ghostband.vst3"

if not exist "%SRC%" (
    echo Ghostband.vst3 not found. Run Build.bat first.
    exit /b 1
)

rem A host keeps the plugin DLL open while it is loaded, and the copy then
rem silently leaves the old build in place - which looks exactly like the fix
rem not working. Refuse rather than mislead.
rem
rem WAIT rather than refuse outright. Gig Performer takes several seconds to
rem actually exit - it unloads every plugin and saves its state on the way out -
rem so "closed" and "gone from the process list" are not the same moment. This
rem refused twice on a host that had been closed, which sends you back to check
rem something you had already done. Fifteen seconds of patience costs nothing
rem and covers the gap.
set "WAITED=0"
:waitforhost
tasklist /fi "imagename eq GigPerformer5.exe" 2>nul | find /i "GigPerformer5.exe" >nul
if errorlevel 1 goto hostclosed

if %WAITED% GEQ 15 (
    echo.
    echo Gig Performer 5 is still running after 15 seconds and is holding the
    echo plugin open. Close it, then run this again.
    exit /b 1
)

if %WAITED%==0 echo Waiting for Gig Performer 5 to finish closing...
ping -n 2 127.0.0.1 >nul
set /a WAITED+=1
goto waitforhost

:hostclosed

echo Installing to "%DST%"
xcopy "%SRC%" "%DST%\" /E /I /Y /Q
if errorlevel 1 (
    echo.
    echo Copy failed. If this says access denied, run this from an elevated prompt.
    exit /b 1
)

rem Driver profiles live inside the bundle so the built-in song can name real
rem instruments and still work on a machine that has no copy of this repository.
rem
rem Not xcopy /Y. Anyone whose profiles resolve to the bundle rather than to a
rem checkout of this repository does their teaching and calibrating in these
rem files, and a straight overwrite destroyed all of it on every install. This
rem carries those blocks across; it prints every one it keeps.
echo Bundling driver profiles
"build\bin\ghostband.exe" install-profiles "profiles" "%DST%\Contents\Resources\profiles"
if errorlevel 1 (
    echo.
    echo Could not install the driver profiles.
    exit /b 1
)

rem The preset songs ship inside the bundle too, so Load plan opens on them
rem rather than on an empty Documents folder on a machine that has never seen
rem this repository.
echo Bundling preset songs
rem EXCLUDE keeps the backups the plugin writes when saving over a plan out of
rem the shipped presets - "demo-band-previous" is not a song anyone chose.
echo previous.json> "%TEMP%\gb-skip.txt"
xcopy "plans\*.json" "%DST%\Contents\Resources\plans\" /I /Y /Q /EXCLUDE:%TEMP%\gb-skip.txt
if errorlevel 1 (
    echo.
    echo Could not copy the preset songs.
    exit /b 1
)

echo.
echo Installed. Rescan plugins in your host if it does not pick it up.
