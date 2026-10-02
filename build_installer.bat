@echo off
echo Building Quicky Installer...

REM Locate Inno Setup Compiler
set "ISCC_PATH="
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC_PATH=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if "%ISCC_PATH%"=="" if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set "ISCC_PATH=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if "%ISCC_PATH%"=="" if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set "ISCC_PATH=C:\Program Files\Inno Setup 6\ISCC.exe"

if "%ISCC_PATH%"=="" (
    echo Error: Inno Setup 6 compiler (ISCC.exe) not found.
    echo Please install Inno Setup 6 or run: winget install JRSoftware.InnoSetup
    pause
    exit /b 1
)

echo Using Inno Setup at: "%ISCC_PATH%"
"%ISCC_PATH%" "%~dp0installer\quicky_setup.iss"
if %ERRORLEVEL% neq 0 (
    echo Installer build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Installer built successfully!
echo Output: release\Quicky-v1.0.0-Setup.exe
pause
