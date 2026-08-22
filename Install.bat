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
tasklist /fi "imagename eq GigPerformer5.exe" 2>nul | find /i "GigPerformer5.exe" >nul
if not errorlevel 1 (
    echo.
    echo Gig Performer 5 is running and is holding the plugin open.
    echo Close it first, then run this again.
    exit /b 1
)

echo Installing to "%DST%"
xcopy "%SRC%" "%DST%\" /E /I /Y /Q
if errorlevel 1 (
    echo.
    echo Copy failed. If this says access denied, run this from an elevated prompt.
    exit /b 1
)

echo.
echo Installed. Rescan plugins in your host if it does not pick it up.
