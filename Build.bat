@echo off
setlocal
cd /d "%~dp0"

rem Find CMake. A standalone install used to be on PATH and then was not, which
rem broke the build with "cmake is not recognized" and nothing else to go on.
rem Visual Studio ships one, so fall back to that rather than fail.
set "CMAKE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
    for /f "delims=" %%I in ('
        "%ProgramFiles(x86)%\Microsoft Visual Studio\Installerswhere.exe" -latest -property installationPath
    ') do set "VSDIR=%%I"
    if defined VSDIR set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMakein\cmake.exe"
)

"%CMAKE%" --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo Could not find CMake. Install it, or add it to PATH.
    exit /b 1
)

"%CMAKE%" -S . -B build -A x64 %*
if errorlevel 1 exit /b 1

"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo Built: build\bin\ghostband.exe
