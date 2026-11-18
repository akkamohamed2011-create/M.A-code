@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem ===== OPTIONAL: type your own paths here. Leave them empty = automatic search =====
set "SFML="
set "GCC_BIN="
rem ===================================================================================

rem ---------- 1) find g++ (MinGW) ----------
if not defined GCC_BIN (
    for %%D in ("C:\Program Files\CodeBlocks\MinGW\bin" "C:\Program Files (x86)\CodeBlocks\MinGW\bin" "C:\CodeBlocks\MinGW\bin" "D:\CodeBlocks\MinGW\bin" "C:\MinGW\bin" "C:\msys64\mingw64\bin" "C:\msys64\ucrt64\bin" "C:\mingw64\bin") do (
        if not defined GCC_BIN if exist "%%~D\g++.exe" set "GCC_BIN=%%~D"
    )
)
if not defined GCC_BIN (
    where g++ >nul 2>nul
    if not errorlevel 1 set "GCC_BIN=PATH"
)
if not defined GCC_BIN (
    echo.
    echo g++.exe was not found automatically.
    echo Type the folder that contains g++.exe
    echo In Code::Blocks you can see it in:  Settings - Compiler - Toolchain executables
    echo Example:  C:\Program Files\CodeBlocks\MinGW\bin
    set /p "GCC_BIN=Folder: "
)
set "GCC_BIN=!GCC_BIN:"=!"
if /i not "!GCC_BIN!"=="PATH" (
    if not exist "!GCC_BIN!\g++.exe" goto nogcc
    set "PATH=!GCC_BIN!;!PATH!"
)

rem ---------- 2) find SFML for MinGW (the folder must contain lib\libsfml-graphics.a) ----------
if not defined SFML (
    for /d %%S in ("%~dp0libs\SFML*" "%USERPROFILE%\Desktop\MA_code\libs\SFML*" "C:\SFML*" "D:\SFML*" "C:\Program Files\SFML*" "C:\Program Files (x86)\SFML*" "%USERPROFILE%\Documents\SFML*" "%USERPROFILE%\Desktop\SFML*" "%USERPROFILE%\Downloads\SFML*") do (
        if exist "%%~S\include\SFML\Graphics.hpp" if exist "%%~S\lib\libsfml-graphics.a" set "SFML=%%~S"
    )
)
if not defined SFML (
    echo.
    echo SFML for MinGW was not found automatically.
    echo Type the SFML folder: it contains the folders  include  lib  bin
    echo and inside  lib  there must be files named  libsfml-graphics.a
    echo In Code::Blocks you can see it in:  Settings - Compiler - Search directories
    echo Example:  C:\SFML-2.5.1
    set /p "SFML=Folder: "
)
set "SFML=!SFML:"=!"
if not exist "!SFML!\include\SFML\Graphics.hpp" goto nosfml
if not exist "!SFML!\lib\libsfml-graphics.a" goto sfmlwrong

echo.
echo g++  : !GCC_BIN!
for /f "delims=" %%M in ('g++ -dumpmachine') do echo target: %%M
for /f "delims=" %%V in ('g++ -dumpversion') do echo gcc   : %%V
echo SFML : !SFML!
echo.

if not exist build mkdir build

rem ---------- 3) icon resource (optional) ----------
set "RES="
where windres >nul 2>nul
if not errorlevel 1 (
    windres app.rc -O coff -o build\app.res
    if not errorlevel 1 set "RES=build\app.res"
)
if not defined RES echo windres not found: building without the icon inside the exe.

rem ---------- 4) compile ----------
g++ -std=c++17 -O2 -DEMBED_FONT main.cpp !RES! -o build\MA_code.exe -I"!SFML!\include" -L"!SFML!\lib" -lsfml-graphics -lsfml-window -lsfml-system -lopengl32 -lgdi32 -lwinmm -lcomdlg32 -mwindows -static-libgcc -static-libstdc++
if errorlevel 1 goto fail

rem ---------- 5) copy the SFML dlls next to the exe ----------
for %%F in (sfml-graphics-2.dll sfml-window-2.dll sfml-system-2.dll) do (
    if exist "!SFML!\bin\%%F" (
        copy /Y "!SFML!\bin\%%F" build >nul
    ) else (
        echo WARNING: %%F was not found in !SFML!\bin
    )
)
copy /Y editor.ico build >nul

echo.
echo DONE:  %~dp0build\MA_code.exe
echo Next:  double-click CreateDesktopShortcut.vbs
pause
exit /b 0

:nogcc
echo.
echo ERROR: g++.exe was not found in:  !GCC_BIN!
goto fail

:nosfml
echo.
echo ERROR: the folder  !SFML!  does not contain include\SFML\Graphics.hpp
goto fail

:sfmlwrong
echo.
echo ERROR: the folder  !SFML!  is NOT an SFML for MinGW / GCC.
echo        It has no lib\libsfml-graphics.a  (it is probably the Visual C++ version, with .lib files).
echo        Use the SFML folder that you use in Code::Blocks (GCC / MinGW version).
goto fail

:fail
echo.
echo BUILD FAILED. Read the messages above.
echo Tip: SFML and MinGW must match: both 32-bit or both 64-bit, and the same GCC version
echo      (look at "target:" and "gcc :" above, and at the name of the SFML folder).
pause
exit /b 1
