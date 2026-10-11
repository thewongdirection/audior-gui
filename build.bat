@echo off
REM ============================================================
REM  build.bat - Build audior-gui and run its unit tests.
REM
REM    build.bat                 x86 and x64: the app and the unit tests
REM    build.bat x64             one architecture only (x64 or x86)
REM    build.bat tests           build only the unit tests (fast edit-test loop)
REM    build.bat notest          build, but do not run the unit tests
REM    build.bat hardware        also build the hardware tests (they record, so they are not run)
REM    build.bat x64 screenshots also render docs\screenshots\*.png from the x64 app (the pictures
REM                              in the README and docs; run it whenever the window changes)
REM
REM  Output, per architecture (bin\x86 and bin\x64):
REM    Audior-GUI.exe     the application
REM    run_tests.exe      the unit tests (run by this script)
REM    hardware_tests.exe the tests that record on this PC's audio devices (hardware option only)
REM  Intermediate files go to build\<arch>.
REM
REM  Finds Visual Studio 2022 / Build Tools itself (via vswhere), so it runs
REM  from any command prompt.
REM
REM  Same size-oriented settings as audior: /O1, the "hybrid CRT" (static VC
REM  runtime and C++ library, in-box Universal CRT), so the program needs no
REM  redistributable and no .NET.  Set CL=/WX to treat warnings as errors.
REM ============================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set "ARCHS="
set "WANT_APP=1"
set "RUN_TESTS=1"
set "WANT_HW=0"
set "WANT_SHOTS=0"
:parse
if "%~1"=="" goto :parsed
if /i "%~1"=="x64"    ( set "ARCHS=!ARCHS! x64"    & shift & goto :parse )
if /i "%~1"=="x86"    ( set "ARCHS=!ARCHS! x86"    & shift & goto :parse )
if /i "%~1"=="tests"  ( set "WANT_APP=0"           & shift & goto :parse )
if /i "%~1"=="notest" ( set "RUN_TESTS=0"          & shift & goto :parse )
if /i "%~1"=="hardware" ( set "WANT_HW=1"          & shift & goto :parse )
if /i "%~1"=="screenshots" ( set "WANT_SHOTS=1"    & shift & goto :parse )
echo Unknown option: %~1
echo Usage: build.bat [x64] [x86] [tests] [notest] [hardware] [screenshots]
exit /b 2
:parsed
if not defined ARCHS set "ARCHS=x86 x64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio 2022 or Build Tools with the C++ workload was not found.
    exit /b 1
)

for %%a in (%ARCHS%) do (
    call :build %%a
    if errorlevel 1 goto :fail
)

if "%WANT_SHOTS%"=="1" (
    call :shots
    if errorlevel 1 goto :fail
)

echo.
echo Build SUCCESS: %ARCHS%
exit /b 0

:fail
echo.
echo Build FAILED.
exit /b 1

REM ---- render the screenshots the documentation shows, with the x64 app ----
REM  The scenarios are drawn by the program itself (simulated audio, a fixed clock and sample paths), so
REM  the pictures hold nothing personal.  Every file named here is used by README.md or docs\*.md.
:shots
if not exist "bin\x64\Audior-GUI.exe" (
    echo The screenshots are rendered by bin\x64\Audior-GUI.exe: build x64 too.
    exit /b 1
)
if not exist "docs\screenshots" mkdir "docs\screenshots"
for %%s in (recording idle saved error clipping hotkeys bluetooth-mic) do (
    start "" /wait "bin\x64\Audior-GUI.exe" --screenshot "docs\screenshots\%%s.png" --scenario %%s
    if errorlevel 1 (
        echo Scenario %%s failed.
        exit /b 1
    )
)
echo Documentation screenshots written to docs\screenshots.
exit /b 0

REM ---- build one architecture (%1 = x86 or x64) ----
:build
setlocal EnableDelayedExpansion
set "ARCH=%~1"
set "OUT=bin\%ARCH%"
set "OBJ=build\%ARCH%"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul 2>nul
echo Building audior-gui (%ARCH%)...

set "CFLAGS=/nologo /W4 /O1 /Gy /MT /std:c++17 /Zc:__cplusplus /permissive- /utf-8 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX"
set "LFLAGS=/OPT:REF /OPT:ICF /NODEFAULTLIB:libucrt.lib ucrt.lib"
set "GUILIBS=user32.lib gdi32.lib shell32.lib dwmapi.lib d2d1.lib dwrite.lib windowscodecs.lib"
REM The recording engine: COM, the registry (saved hotkeys), WASAPI scheduling (avrt) and Media Foundation.  Media Foundation is
REM delay-loaded so the program also starts on Windows N editions that lack it.
set "ENGLIBS=ole32.lib uuid.lib advapi32.lib avrt.lib mfplat.lib mfreadwrite.lib mfuuid.lib delayimp.lib /DELAYLOAD:mfplat.dll /DELAYLOAD:mfreadwrite.dll"

REM The logic shared by the app and the tests: compiled once per architecture.
set "CORE_SRC="
for %%f in (src\core\*.cpp src\engine\*.cpp src\preferences.cpp) do set "CORE_SRC=!CORE_SRC! %%f"
set "CORE_OBJ="
for %%f in (!CORE_SRC!) do set "CORE_OBJ=!CORE_OBJ! %OBJ%\%%~nf.obj"
cl.exe %CFLAGS% /c /Fo"%OBJ%\\" !CORE_SRC! || goto :buildfail

set "TEST_SRC="
for %%f in (tests\*.cpp) do set "TEST_SRC=!TEST_SRC! %%f"
cl.exe %CFLAGS% /Fo"%OBJ%\\" /Fe"%OUT%\run_tests.exe" !TEST_SRC! !CORE_OBJ! /link %LFLAGS% %ENGLIBS% || goto :buildfail

REM Only libraries that ship with Windows may be imported (see tests\check_imports.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File tests\check_imports.ps1 %OUT%\run_tests.exe || goto :buildfail

REM Run the tests before compiling the UI, so a UI error never hides a test result.
if not "%RUN_TESTS%"=="1" goto :build_app
"%OUT%\run_tests.exe" || goto :buildfail

:build_app
if not "%WANT_APP%"=="1" goto :build_hardware
set "UI_SRC=src\main.cpp src\console.cpp"
for %%f in (src\ui\*.cpp) do set "UI_SRC=!UI_SRC! %%f"
cl.exe %CFLAGS% /Fo"%OBJ%\\" /Fe"%OUT%\Audior-GUI.exe" !UI_SRC! !CORE_OBJ! ^
    /link %LFLAGS% /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest %GUILIBS% %ENGLIBS% || goto :buildfail
powershell -NoProfile -ExecutionPolicy Bypass -File tests\check_imports.ps1 %OUT%\Audior-GUI.exe || goto :buildfail

:build_hardware
if not "%WANT_HW%"=="1" goto :builddone
set "HW_SRC="
for %%f in (tests\hardware\*.cpp) do set "HW_SRC=!HW_SRC! %%f"
cl.exe %CFLAGS% /Fo"%OBJ%\\" /Fe"%OUT%\hardware_tests.exe" !HW_SRC! ^
    /link %LFLAGS% /SUBSYSTEM:CONSOLE user32.lib ole32.lib mfplat.lib mfreadwrite.lib mfuuid.lib winmm.lib || goto :buildfail

:builddone
endlocal & exit /b 0

:buildfail
endlocal & exit /b 1
