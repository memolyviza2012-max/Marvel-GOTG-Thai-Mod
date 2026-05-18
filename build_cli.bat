@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set PROJ=E:\Mod_Workspace\MarvelGOTG_Mod_Workspace\GOTG_Translation
set OUTDIR=%PROJ%\build
set OBJDIR=%PROJ%\obj\Release_cli

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set INCLUDES=/I"%PROJ%\include" /I"%PROJ%\src\lib" /I"%PROJ%\src\lib\imgui" /I"%PROJ%\src\sp"
set DEFINES=/DNDEBUG /D_WINDOWS /D_USRDLL /DGOTGTRANSLATION_EXPORTS /DMINHOOK_AVAILABLE
set CFLAGS=/nologo /c /MT /O1 /std:c++17 /EHsc /W3 %INCLUDES% %DEFINES%
set CFLAGS_C=/nologo /c /MT /O1 /W3 %INCLUDES% %DEFINES%

echo ======================================
echo  Building GOTG version.dll (x64 Release)
echo ======================================

echo [1/7] Compiling MinHook (C)...
cl %CFLAGS_C% /Fo"%OBJDIR%\hook.obj"       "%PROJ%\src\lib\minhook\hook.c"
cl %CFLAGS_C% /Fo"%OBJDIR%\trampoline.obj" "%PROJ%\src\lib\minhook\trampoline.c"
cl %CFLAGS_C% /Fo"%OBJDIR%\buffer.obj"     "%PROJ%\src\lib\minhook\buffer.c"
cl %CFLAGS_C% /Fo"%OBJDIR%\hde32.obj"      "%PROJ%\src\lib\minhook\hde\hde32.c"
cl %CFLAGS_C% /Fo"%OBJDIR%\hde64.obj"      "%PROJ%\src\lib\minhook\hde\hde64.c"

echo [2/7] Compiling Proxy.cpp...
cl %CFLAGS% /Fo"%OBJDIR%\Proxy.obj" "%PROJ%\src\Proxy.cpp"

echo [3/7] Compiling DllMain.cpp...
cl %CFLAGS% /Fo"%OBJDIR%\DllMain.obj" "%PROJ%\src\DllMain.cpp"

echo [4/7] Compiling TranslationHooks.cpp...
cl %CFLAGS% /Fo"%OBJDIR%\TranslationHooks.obj" "%PROJ%\src\TranslationHooks.cpp"

echo [5/7] Compiling PatternScanner.cpp...
cl %CFLAGS% /Fo"%OBJDIR%\PatternScanner.obj" "%PROJ%\src\PatternScanner.cpp"

echo [6/7] Compiling sp library...
cl %CFLAGS% /Fo"%OBJDIR%\environment.obj" "%PROJ%\src\sp\environment.cpp"
cl %CFLAGS% /Fo"%OBJDIR%\file.obj"        "%PROJ%\src\sp\file.cpp"

REM [7/7] GhostOverlay removed — native font injection replaces external overlay
REM cl %CFLAGS% /Fo"%OBJDIR%\GhostOverlay.obj" "%PROJ%\src\GhostOverlay.cpp"

echo.
echo [LINK] Linking version.dll...
link /nologo /DLL /OUT:"%OUTDIR%\version.dll" ^
  /SUBSYSTEM:WINDOWS ^
  "%OBJDIR%\hook.obj" "%OBJDIR%\trampoline.obj" "%OBJDIR%\buffer.obj" ^
  "%OBJDIR%\hde32.obj" "%OBJDIR%\hde64.obj" ^
  "%OBJDIR%\Proxy.obj" "%OBJDIR%\DllMain.obj" "%OBJDIR%\TranslationHooks.obj" ^
  "%OBJDIR%\PatternScanner.obj" "%OBJDIR%\environment.obj" "%OBJDIR%\file.obj" ^
  kernel32.lib user32.lib psapi.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ======================================
    echo  BUILD SUCCEEDED: %OUTDIR%\version.dll
    echo ======================================
    echo.
    echo [DEPLOY] Copying to game directory...
    copy /Y "%OUTDIR%\version.dll" "F:\Epic Games\MarvelGOTG\retail\version.dll"
    if %ERRORLEVEL% EQU 0 (
        echo [DEPLOY] SUCCESS - version.dll deployed to retail\
    ) else (
        echo [DEPLOY] FAILED - could not copy to retail\
    )
) else (
    echo.
    echo ======================================
    echo  BUILD FAILED
    echo ======================================
)
