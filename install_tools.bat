@echo off
echo Installing CMake...
winget install -e --id Kitware.CMake --accept-package-agreements --accept-source-agreements

echo.
echo Installing Visual Studio C++ Build Tools...
echo (A Windows User Account Control prompt may appear, please click Yes)
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" --accept-package-agreements --accept-source-agreements

echo.
echo Installation complete! 
echo IMPORTANT: You MUST close this terminal and open a new one so that 'cmake' is added to your PATH.
pause
