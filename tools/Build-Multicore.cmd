@echo off
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SKYRIM_VS=%%i"
if not defined SKYRIM_VS exit /b 1
call "%SKYRIM_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.51 >nul
if errorlevel 1 exit /b 1
cmake -S "%~dp0.." -B "%~dp0..\build\lab" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%SKYRIM_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" "-DPython3_EXECUTABLE=C:/Python312/python.exe"
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build\lab"
exit /b %errorlevel%
