@echo off
rem ---------------------------------------------------------------------------
rem  Paint Matcher - Qt Installer Framework helper
rem
rem  Step 1 (stage):  build_installer.bat stage <path\to\PaintMatcher.exe>
rem      Copies PaintMatcher.exe and data\paints.csv into
rem      packages\com.paintmatcher.app\data
rem
rem  Step 2 (you):    run windeployqt on the staged exe, for example
rem      windeployqt --release --no-translations ^
rem          installer\packages\com.paintmatcher.app\data\PaintMatcher.exe
rem
rem  Step 3 (build):  build_installer.bat build [path\to\QtIFW\bin]
rem      Runs binarycreator and writes PaintMatcherSetup-1.0.1.exe next to this script.
rem      binarycreator is found from the argument, %%QTIFWDIR%%\bin, or PATH.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "PKG_DATA=%SCRIPT_DIR%packages\com.paintmatcher.app\data"
set "SAMPLE_DB=%SCRIPT_DIR%..\data\paints.csv"
set "OUTPUT=%SCRIPT_DIR%PaintMatcherSetup-1.0.1.exe"

if /I "%~1"=="stage" goto :stage
if /I "%~1"=="build" goto :build
echo Usage:
echo   %~nx0 stage ^<path\to\PaintMatcher.exe^>
echo   %~nx0 build [path\to\QtIFW\bin]
exit /b 1

:stage
if "%~2"=="" (
    echo ERROR: give the path to the built PaintMatcher.exe
    exit /b 1
)
if not exist "%~2" (
    echo ERROR: "%~2" not found
    exit /b 1
)
if not exist "%PKG_DATA%" mkdir "%PKG_DATA%"
if not exist "%PKG_DATA%\data" mkdir "%PKG_DATA%\data"
copy /Y "%~2" "%PKG_DATA%\PaintMatcher.exe" >nul || exit /b 1
copy /Y "%SAMPLE_DB%" "%PKG_DATA%\data\paints.csv" >nul || exit /b 1
echo Staged into %PKG_DATA%
echo Next: run windeployqt on "%PKG_DATA%\PaintMatcher.exe", then "%~nx0 build".
exit /b 0

:build
if not exist "%PKG_DATA%\PaintMatcher.exe" (
    echo ERROR: PaintMatcher.exe is not staged. Run "%~nx0 stage <exe>" first.
    exit /b 1
)
if not exist "%PKG_DATA%\Qt6Core.dll" if not exist "%PKG_DATA%\Qt6Cored.dll" (
    echo ERROR: Qt DLLs not found in %PKG_DATA%
    echo Run windeployqt on "%PKG_DATA%\PaintMatcher.exe" first.
    exit /b 1
)
if exist "%PKG_DATA%\README.txt" del /Q "%PKG_DATA%\README.txt"
if exist "%PKG_DATA%\settings.ini" (
    echo Removing settings.ini left over from a test run so it is not packaged.
    del /Q "%PKG_DATA%\settings.ini"
)

set "BINARYCREATOR="
if not "%~2"=="" if exist "%~2\binarycreator.exe" set "BINARYCREATOR=%~2\binarycreator.exe"
if not defined BINARYCREATOR if defined QTIFWDIR if exist "%QTIFWDIR%\bin\binarycreator.exe" set "BINARYCREATOR=%QTIFWDIR%\bin\binarycreator.exe"
if not defined BINARYCREATOR for %%B in (binarycreator.exe) do set "BINARYCREATOR=%%~$PATH:B"
if not defined BINARYCREATOR (
    echo ERROR: binarycreator.exe not found. Pass the QtIFW bin folder, set QTIFWDIR, or add it to PATH.
    exit /b 1
)

echo Using %BINARYCREATOR%
"%BINARYCREATOR%" --offline-only -c "%SCRIPT_DIR%config\config.xml" -p "%SCRIPT_DIR%packages" "%OUTPUT%" || exit /b 1
echo Created %OUTPUT%
exit /b 0
