@echo off
setlocal
rem Build example_all_functions.exe with the VS x64 toolchain.
rem Run from a "x64 Native Tools Command Prompt" or let this script find vcvars.

set "ROOT=%~dp0"
cd /d "%ROOT%"

where cl >nul 2>&1
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
  )
)

where cl >nul 2>&1
if errorlevel 1 (
  echo Could not find cl.exe. Open an "x64 Native Tools Command Prompt for VS" and re-run.
  exit /b 1
)

cl /nologo /W3 /O2 /std:c11 /I. edifabric_x12.c example_all_functions.c /Fe:example_all_functions.exe /link
if errorlevel 1 exit /b 1

echo.
echo Built example_all_functions.exe
echo Run: example_all_functions.exe
endlocal
