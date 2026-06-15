@echo off
chcp 65001 > nul
title เครื่องมือล้างแคชเกมและการ์ดจอ (Clear Game and GPU Cache)
color 0B

:: ==========================================
:: ตรวจสอบสิทธิ์ Administrator (Run as Admin)
:: ==========================================
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"
if '%errorlevel%' NEQ '0' (
    echo.
    echo [คำเตือน] กำลังร้องขอสิทธิ์ Administrator เพื่อทำความสะอาดระบบ...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin.vbs"
    "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    if exist "%temp%\getadmin.vbs" ( del "%temp%\getadmin.vbs" )
    pushd "%CD%"
    CD /D "%~dp0"

:: ==========================================
:: เริ่มต้นการทำงาน
:: ==========================================
cls
echo ========================================================
echo เครื่องมือล้างแคชเกมและการ์ดจอ (Clear Game ^& GPU Cache)
echo พัฒนาโดย: หนวด translator
echo ========================================================
echo.
echo [คำเตือน] 
echo โปรด ปิดโปรแกรม Steam, Epic Games และเกมทั้งหมด ก่อนกดทำงาน!
echo.
pause

echo.
echo [1/4] กำลังล้างแคชการ์ดจอ (NVIDIA / AMD / DirectX)...
:: NVIDIA
del /q /s /f "%LocalAppData%\NVIDIA\DXCache\*.*" >nul 2>&1
del /q /s /f "%LocalAppData%\NVIDIA\GLCache\*.*" >nul 2>&1
del /q /s /f "%LocalAppData%\NVIDIA Corporation\NV_Cache\*.*" >nul 2>&1
rd /s /q "%LocalAppData%\NVIDIA\DXCache" >nul 2>&1
rd /s /q "%LocalAppData%\NVIDIA\GLCache" >nul 2>&1
:: AMD
del /q /s /f "%LocalAppData%\AMD\DxCache\*.*" >nul 2>&1
del /q /s /f "%LocalAppData%\AMD\GLCache\*.*" >nul 2>&1
:: DirectX
del /q /s /f "%LocalAppData%\D3DSCache\*.*" >nul 2>&1

echo.
echo [2/4] กำลังล้างแคชระบบ (Windows Temp)...
:: ล้างไฟล์ขยะระบบที่มักทำให้เกมแครช
del /q /s /f "%TEMP%\*.*" >nul 2>&1
del /q /s /f "C:\Windows\Temp\*.*" >nul 2>&1

echo.
echo [3/4] กำลังล้างแคช Steam...
:: ล้างแคชดาวน์โหลดและเว็บของ Steam
if exist "C:\Program Files (x86)\Steam" (
    rd /s /q "C:\Program Files (x86)\Steam\appcache\httpcache" >nul 2>&1
    rd /s /q "C:\Program Files (x86)\Steam\config\htmlcache" >nul 2>&1
    del /q /s /f "C:\Program Files (x86)\Steam\steamapps\downloading\*.*" >nul 2>&1
)
if exist "%LocalAppData%\Steam\htmlcache" (
    rd /s /q "%LocalAppData%\Steam\htmlcache" >nul 2>&1
)

echo.
echo [4/4] กำลังล้างแคช Epic Games Launcher...
:: ล้างโฟลเดอร์ webcache ของ Epic
if exist "%LocalAppData%\EpicGamesLauncher\Saved\webcache" (
    rd /s /q "%LocalAppData%\EpicGamesLauncher\Saved\webcache" >nul 2>&1
)
if exist "%LocalAppData%\EpicGamesLauncher\Saved\webcache_4147" (
    rd /s /q "%LocalAppData%\EpicGamesLauncher\Saved\webcache_4147" >nul 2>&1
)
if exist "%LocalAppData%\EpicGamesLauncher\Saved\webcache_4430" (
    rd /s /q "%LocalAppData%\EpicGamesLauncher\Saved\webcache_4430" >nul 2>&1
)

echo.
echo ========================================================
echo เสร็จสิ้น! ล้างแคชทั้งหมดเรียบร้อยแล้ว
echo อาการเกมเด้ง กระตุก หรือจอดำ ควรจะดีขึ้นครับ
echo แนะนำให้ "รีสตาร์ทคอมพิวเตอร์" 1 ครั้งก่อนเข้าเกม!
echo ========================================================
echo กดปุ่มใดก็ได้เพื่อปิดหน้าต่างนี้...
pause >nul
exit
