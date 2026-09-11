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
rem not working. So this refuses rather than misleads.
rem
rem IT ASKS THE FILE, NOT THE PROCESS LIST. The old version waited for
rem GigPerformer5.exe to leave tasklist, which is the wrong question twice over.
rem A host that is running but has not loaded Ghostband holds nothing, and is
rem blocked for no reason. And a process that has already EXITED can sit in
rem tasklist indefinitely as a zombie entry while another process holds a handle
rem to it - three of them did, all reporting HasExited=True, while the DLL
rem itself was perfectly free. Opening the file for exclusive write answers the
rem only question that actually matters.
set "TARGETDLL=%DST%\Contents\x86_64-win\Ghostband.vst3"
set "WAITED=0"

:waitforlock
if not exist "%TARGETDLL%" goto hostclosed

powershell -NoProfile -Command "try{$f=[IO.File]::Open($env:TARGETDLL,'Open','ReadWrite','None');$f.Close();exit 0}catch{exit 1}"
if not errorlevel 1 goto hostclosed

if %WAITED% GEQ 15 (
    echo.
    echo Something still has the plugin open, so installing would silently leave
    echo the old build in place. Close your host and run this again.
    echo.
    echo   If your host is already closed, check Task Manager for a leftover
    echo   process still holding it.
    exit /b 1
)

if %WAITED%==0 echo Waiting for the plugin to be released...
ping -n 2 127.0.0.1 >nul
set /a WAITED+=1
goto waitforlock

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

rem xcopy adds and overwrites but it never removes, so a preset deleted from the
rem project stayed inside the bundle forever and went on being offered by Load
rem plan. Two did for several sessions: a scratch calibration file and a
rem superseded twin-guitar preset, both removed from the project in 220e598.
rem
rem "Delete anything here that is not in plans" would be the obvious rule and is
rem the wrong one. Save as... opens on whatever song is loaded, so starting from
rem a bundled preset puts the save dialog in this very folder - and an installer
rem that sweeps away a song somebody saved there is a far worse bug than the one
rem it fixes. So this removes only what a previous run of this installer put
rem there, by name, from the manifest it wrote. Anything else is left alone.
set "PLANDIR=%DST%\Contents\Resources\plans"
set "MANIFEST=%PLANDIR%\shipped.txt"
if exist "%MANIFEST%" (
    for /f "usebackq delims=" %%P in ("%MANIFEST%") do (
        if not exist "plans\%%P" (
            echo   no longer shipped, removing: %%P
            del /q "%PLANDIR%\%%P" 2>nul
        )
    )
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


rem Record what was shipped, so the next install knows what it may remove.
rem Written after the copy rather than before, so a copy that failed does not
rem leave a manifest claiming files that are not there.
if exist "%MANIFEST%" del /q "%MANIFEST%"
for %%P in (plans\*.json) do (
    echo %%~nxP | find /i "previous" >nul || echo %%~nxP>> "%MANIFEST%"
)

echo.
echo Installed. Rescan plugins in your host if it does not pick it up.
