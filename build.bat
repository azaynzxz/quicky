@echo off
echo Building Quicky...

if not exist build mkdir build
cd build

cmake ..
if %ERRORLEVEL% neq 0 (
    echo CMake generation failed! Make sure Visual Studio and C++ build tools are installed.
    pause
    exit /b %ERRORLEVEL%
)

cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo Build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build successful! Quicky is located at:
echo build\Release\Quicky.exe
echo You can run it now by double-clicking it.
pause
