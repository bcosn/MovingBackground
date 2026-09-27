@echo off
rem ===========================================================================
rem  一键编译 MovingBackground.exe
rem  用法：双击本文件，或在命令行执行 build.bat
rem  优先使用 MSVC(Visual Studio)，找不到就用 MinGW 的 g++。
rem ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set SRC=MovingBackground.cpp
set OUT=MovingBackground.exe

rem ---------- 1) MSVC ----------
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VSDIR=%%i
)
if defined VSDIR (
    if exist "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" (
        echo [build] 使用 MSVC: !VSDIR!
        call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
        cl /nologo /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE "%SRC%" ^
           /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib shell32.lib comdlg32.lib advapi32.lib winmm.lib ^
           /OUT:"%OUT%"
        if errorlevel 1 ( echo [build] MSVC 编译失败 & pause & exit /b 1 )
        goto done
    )
)

rem ---------- 2) 直接在 PATH 里找 cl ----------
where cl >nul 2>nul
if not errorlevel 1 (
    echo [build] 使用 PATH 中的 cl
    cl /nologo /utf-8 /EHsc /O2 /W3 /DUNICODE /D_UNICODE "%SRC%" ^
       /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib shell32.lib comdlg32.lib advapi32.lib winmm.lib ^
       /OUT:"%OUT%"
    if errorlevel 1 ( echo [build] 编译失败 & pause & exit /b 1 )
    goto done
)

rem ---------- 3) MinGW ----------
where g++ >nul 2>nul
if not errorlevel 1 (
    echo [build] 使用 MinGW g++
    g++ -O2 -municode -mwindows -Wall "%SRC%" -o "%OUT%" ^
        -lgdiplus -lgdi32 -luser32 -lshell32 -lcomdlg32 -lwinmm -static
    if errorlevel 1 ( echo [build] 编译失败 & pause & exit /b 1 )
    goto done
)

echo [build] 没找到 MSVC 或 MinGW，请先安装 Visual Studio (C++ 桌面开发) 或 MinGW-w64。
pause
exit /b 1

:done
echo.
echo [build] 完成：%CD%\%OUT%
echo [build] 直接运行它会挂到桌面上，托盘图标里可以打开设置。
pause
