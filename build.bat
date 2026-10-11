@echo off
REM ============================================================
REM  build.bat - Build audior-gui and run its unit tests.
REM
REM    build.bat                 x86 and x64: the app and the unit tests
REM    build.bat x64             one architecture only (x64 or x86)
REM    build.bat tests           build only the unit tests (fast edit-test loop)
REM    build.bat notest          build, but do not run the unit tests
REM    build.bat asr             also build the transcription helper, audior_asr.exe (x64 only; it
REM                              compiles whisper.cpp with CMake, so it takes a few minutes the first time)
REM    build.bat vulkan          the same, and with the Vulkan backend for the graphics card (ggml-vulkan.dll;
REM                              needs the Vulkan SDK from https://vulkan.lunarg.com/sdk/home)
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
set "WANT_ASR=0"
set "WANT_VK=0"
set "WANT_HW=0"
set "WANT_SHOTS=0"
:parse
if "%~1"=="" goto :parsed
if /i "%~1"=="x64"    ( set "ARCHS=!ARCHS! x64"    & shift & goto :parse )
if /i "%~1"=="x86"    ( set "ARCHS=!ARCHS! x86"    & shift & goto :parse )
if /i "%~1"=="tests"  ( set "WANT_APP=0"           & shift & goto :parse )
if /i "%~1"=="notest" ( set "RUN_TESTS=0"          & shift & goto :parse )
if /i "%~1"=="asr"    ( set "WANT_ASR=1"           & shift & goto :parse )
if /i "%~1"=="vulkan" ( set "WANT_ASR=1" & set "WANT_VK=1" & shift & goto :parse )
if /i "%~1"=="hardware" ( set "WANT_HW=1"          & shift & goto :parse )
if /i "%~1"=="screenshots" ( set "WANT_SHOTS=1"    & shift & goto :parse )
echo Unknown option: %~1
echo Usage: build.bat [x64] [x86] [tests] [notest] [asr] [vulkan] [hardware] [screenshots]
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
if "%WANT_ASR%"=="1" (
    call :asr
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

REM ---- the optional transcription helper (x64): whisper.cpp from third_party, built with the CMake and Ninja
REM      that come with Visual Studio; intermediates go to build\asr-x64 ----
:asr
setlocal EnableDelayedExpansion
set "CMAKEDIR=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
if not exist "%CMAKEDIR%\CMake\bin\cmake.exe" (
    echo CMake was not found: add the "C++ CMake tools for Windows" component to Visual Studio.
    goto :asrfail
)
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
set "PATH=%CMAKEDIR%\CMake\bin;%CMAKEDIR%\Ninja;%PATH%"
set "VKFLAG=OFF"
if "%WANT_VK%"=="1" (
    set "VKFLAG=ON"
    if not defined VULKAN_SDK for /d %%d in (C:\VulkanSDK\*) do set "VULKAN_SDK=%%d"
    if not defined VULKAN_SDK (
        echo The Vulkan SDK was not found: install it from https://vulkan.lunarg.com/sdk/home
        goto :asrfail
    )
)
echo Building audior_asr (x64, Vulkan !VKFLAG!)...
REM The Vulkan shaders are built by a project inside the project, whose files sit so deep that a checkout in a
REM folder of any length hits Windows' 260 characters: build from a short drive letter that stands for it.
set "DRV="
if "%WANT_VK%"=="1" (
    for %%L in (Z Y X W V U T) do if not defined DRV if not exist %%L:\ set "DRV=%%L:"
    if not defined DRV goto :asrfail
    subst !DRV! "%CD%" || goto :asrfail
    pushd !DRV!\
)
cmake -S src\asr -B build\asr-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DAUDIOR_ASR_VULKAN=!VKFLAG! -Wno-dev || goto :asrfail
cmake --build build\asr-x64 || goto :asrfail
if defined DRV (
    popd
    subst !DRV! /d
    set "DRV="
)
if not exist "bin\x64" mkdir "bin\x64"
REM No backend may be left over from an earlier build.
del /q "bin\x64\ggml*.dll" "bin\x64\whisper.dll" 2>nul
copy /y "build\asr-x64\bin\audior_asr.exe" "bin\x64\audior_asr.exe" >nul || goto :asrfail
copy /y "build\asr-x64\bin\whisper.dll" "bin\x64\" >nul || goto :asrfail
copy /y "build\asr-x64\bin\ggml*.dll" "bin\x64\" >nul || goto :asrfail

REM The list of the helper's files with their SHA-256, which the recorder checks before it starts the helper.
type nul > "bin\x64\audior_asr.sha256"
for %%f in (bin\x64\audior_asr.exe bin\x64\ggml*.dll bin\x64\whisper.dll) do (
    set "ASRHASH="
    for /f "skip=1 tokens=* delims=" %%h in ('certutil -hashfile "%%f" SHA256') do if not defined ASRHASH set "ASRHASH=%%h"
    if not defined ASRHASH goto :asrfail
    set "ASRHASH=!ASRHASH: =!"
    >> "bin\x64\audior_asr.sha256" echo !ASRHASH!  %%~nxf
)

REM Only Windows' own DLLs may be imported; the helper's own DLLs are allowed to each other, and the Vulkan backend
REM to the graphics driver's vulkan-1.
set "OWNDLLS="
for %%f in (bin\x64\ggml*.dll bin\x64\whisper.dll) do set "OWNDLLS=!OWNDLLS!,%%~nf"
set "OWNDLLS=!OWNDLLS:~1!"
for %%f in (bin\x64\audior_asr.exe bin\x64\ggml*.dll bin\x64\whisper.dll) do (
    set "ALLOWED=!OWNDLLS!"
    if /i "%%~nxf"=="ggml-vulkan.dll" set "ALLOWED=!OWNDLLS!,vulkan-1"
    powershell -NoProfile -ExecutionPolicy Bypass -File tests\check_imports.ps1 -Allow "!ALLOWED!" "%%f" || goto :asrfail
)
endlocal & exit /b 0

:asrfail
if defined DRV (
    popd
    subst !DRV! /d
)
endlocal & exit /b 1
