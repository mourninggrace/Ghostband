@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem Packages a release zip: the VST3 bundle with the driver profiles and the
rem preset songs inside it, which is what a person actually needs.
rem
rem The build output is NOT that. CMake writes the DLL and a moduleinfo.json and
rem nothing else, and Install.bat is what adds Resources\profiles and
rem Resources\plans on the way to Program Files. A zip of the build output alone
rem would install and then be unable to name a single instrument or open a single
rem song, so this stages the same two folders Install.bat does.
rem
rem Refuses to package a build that does not pass its own tests, and refuses if
rem the working tree is dirty - a release nobody can reproduce from a commit is
rem worse than no release.

set "STAGE=out\release"
set "BUNDLE=%STAGE%\Ghostband.vst3"
set "SRC=build\GhostbandPlugin_artefacts\Release\VST3\Ghostband.vst3"

for /f "delims=" %%V in ('powershell -NoProfile -Command "(Select-String -Path CMakeLists.txt -Pattern 'project\(Ghostband VERSION ([0-9.]+)').Matches[0].Groups[1].Value"') do set "VERSION=%%V"
if "%VERSION%"=="" (
    echo Could not read the version out of CMakeLists.txt.
    exit /b 1
)

echo Ghostband %VERSION%
echo.

rem ---- the tree must be committed -----------------------------------------
for /f "delims=" %%S in ('git status --porcelain') do (
    echo Working tree is not clean. Commit or stash first:
    git status --short
    exit /b 1
)

for /f "delims=" %%H in ('git rev-parse --short HEAD') do set "COMMIT=%%H"
echo commit %COMMIT%
echo.

rem ---- build ----------------------------------------------------------------
call "%~dp0Build.bat"
if errorlevel 1 exit /b 1

rem ---- and it has to pass ---------------------------------------------------
echo.
echo Running the harness
"build\ghostband_plugin_test_artefacts\Release\ghostband_plugin_test.exe" > "%TEMP%\gb-release-tests.txt" 2>&1
if errorlevel 1 (
    echo.
    echo Tests FAILED - not packaging. See %TEMP%\gb-release-tests.txt
    findstr /c:"FAIL" "%TEMP%\gb-release-tests.txt"
    exit /b 1
)
for /f "delims=" %%N in ('findstr /c:"  PASS" "%TEMP%\gb-release-tests.txt" ^| find /c /v ""') do set "PASSES=%%N"
echo   %PASSES% checks passed

rem ---- stage ----------------------------------------------------------------
echo.
echo Staging %BUNDLE%
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"

xcopy "%SRC%" "%BUNDLE%\" /E /I /Y /Q >nul
if errorlevel 1 (
    echo Could not stage the bundle. Did Build.bat produce one?
    exit /b 1
)

rem Profiles straight from the repository, not through install-profiles: that
rem tool exists to PRESERVE what somebody taught on their own machine, and a
rem release must ship what is committed rather than whatever this machine has
rem been calibrated to.
xcopy "profiles\*.json" "%BUNDLE%\Contents\Resources\profiles\" /I /Y /Q >nul
if errorlevel 1 exit /b 1

echo previous.json> "%TEMP%\gb-skip.txt"
xcopy "plans\*.json" "%BUNDLE%\Contents\Resources\plans\" /I /Y /Q /EXCLUDE:%TEMP%\gb-skip.txt >nul
if errorlevel 1 exit /b 1

rem ---- zip ------------------------------------------------------------------
set "ZIP=out\Ghostband-%VERSION%-win64.zip"
if exist "%ZIP%" del /q "%ZIP%"

powershell -NoProfile -Command "Compress-Archive -Path '%BUNDLE%' -DestinationPath '%ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (
    echo Could not write %ZIP%
    exit /b 1
)

echo.
echo Packaged %ZIP%
rem certutil rather than Get-FileHash: whichever powershell.exe is first on PATH
rem here is old enough not to have Get-FileHash, and certutil ships with Windows
rem itself. A checksum that only prints on some machines is not a checksum.
powershell -NoProfile -Command "$f=Get-Item '%ZIP%'; '  {0:N1} MB' -f ($f.Length/1MB)"
for /f "skip=1 delims=" %%H in ('certutil -hashfile "%ZIP%" SHA256') do (
    if not "%%H"=="" if "!SHA!"=="" set "SHA=%%H"
)
echo   SHA256 !SHA!
echo.
echo Contents:
rem No pipes in here. Inside a quoted powershell -Command a bare "|" is fine to
rem cmd, but escaping it as "^|" - the reflex from an unquoted context - passes
rem the caret through to PowerShell, which cannot parse it.
powershell -NoProfile -Command "$n=(Get-ChildItem '%BUNDLE%' -Recurse -File); $p=@($n.Where({$_.FullName -like '*\Resources\profiles\*'})); $s=@($n.Where({$_.FullName -like '*\Resources\plans\*'})); '  {0} files, {1} profiles, {2} songs' -f $n.Count, $p.Count, $s.Count"
