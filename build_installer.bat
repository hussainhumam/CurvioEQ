@echo off
setlocal
call "%~dp0build_app.bat"
if errorlevel 1 exit /b 1

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php
  exit /b 1
)

if exist "%~dp0dist\PerAppEQ-Setup.exe" del /f /q "%~dp0dist\PerAppEQ-Setup.exe"
if exist "%~dp0dist\bin\untitled1.exe" del /f /q "%~dp0dist\bin\untitled1.exe"
if exist "%~dp0dist\bin\untitled1_debug.exe" del /f /q "%~dp0dist\bin\untitled1_debug.exe"

"%ISCC%" "%~dp0installer\CurvioEQ.iss"
exit /b %errorlevel%
