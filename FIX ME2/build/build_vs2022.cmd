@echo off
setlocal
cd /d "%~dp0.."
where msbuild >nul 2>&1
if errorlevel 1 (
  echo ERROR: Open Visual Studio 2022 x64 Native Tools Command Prompt.
  exit /b 2
)
msbuild HypervisorLab.sln /m /p:Configuration=Release /p:Platform=x64 /verbosity:minimal
if errorlevel 1 exit /b %errorlevel%
echo.
echo [1/7] Running offline paging tests...
"out\x64\Release\HvTests.exe"
if errorlevel 1 exit /b 1
echo [2/7] Running introspection tests...
"out\x64\Release\HvIntrospectionTests.exe"
if errorlevel 1 exit /b 1
echo [3/7] Running read-only process manager tests...
"out\x64\Release\HvProcessTests.exe"
if errorlevel 1 exit /b 1
echo [4/7] Running process inspector self-test...
"out\x64\Release\HvProcessInspector.exe" --self-test
if errorlevel 1 exit /b 1
echo [5/7] Running geometry tests...
"out\x64\Release\HvGameTests.exe"
if errorlevel 1 exit /b 1
echo [6/7] Running bounded kernel-transport protocol tests...
"out\x64\Release\HvKernelTransportTests.exe"
if errorlevel 1 exit /b 1
echo [7/7] Running WHP guest lifecycle model tests...
"out\x64\Release\HvWHPGuestTests.exe"
if errorlevel 1 exit /b 1
echo PASS: All available local tests passed.
echo NOTE: HvWHPGuest.exe is a separate Windows-only hardware test; run it manually.
exit /b 0
