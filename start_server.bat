@echo off
echo ====================================
echo   Starting Object Recognition Server
echo ====================================
echo.

REM التحقق من تثبيت Node.js
where node >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Node.js is not installed!
    echo Please install Node.js from https://nodejs.org
    pause
    exit /b 1
)

echo [INFO] Node.js found
echo.

REM الانتقال إلى مجلد المشروع
cd /d "%~dp0"

REM التحقق من وجود node_modules
if not exist "node_modules\" (
    echo [INFO] Installing dependencies...
    call npm install
    echo.
)

echo [INFO] Starting server...
echo [INFO] Server will listen on: 0.0.0.0:3000
echo [INFO] Make sure Firewall allows connections on port 3000
echo.
echo Press Ctrl+C to stop the server
echo ====================================
echo.

REM تشغيل السيرفر
node server.js

pause
