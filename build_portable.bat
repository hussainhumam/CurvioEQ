@echo off
setlocal
call "%~dp0build_app.bat"
if errorlevel 1 exit /b 1

set "ROOT=%~dp0"
set "DIST=%ROOT%dist"
set "STAGE=%DIST%\portable-stage"
set "PAYLOAD=%STAGE%\CurvioEQ"
set "ZIP=%DIST%\CurvioEQ-1.3.1-portable.zip"

if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%ZIP%" del /f /q "%ZIP%"
mkdir "%PAYLOAD%" >nul

robocopy "%DIST%" "%PAYLOAD%" /E /NFL /NDL /NJH /NJS /nc /ns /np ^
  /XD portable-stage ^
  /XF CurvioEQ-Setup.exe PerAppEQ-Setup.exe *.zip CurvioEQ_DspVerify.exe CurvioEQ_GenerateHrtf.exe CurvioEQ_SoundModVerify.exe
if %ERRORLEVEL% GEQ 8 exit /b 1

> "%PAYLOAD%\portable.txt" echo CurvioEQ portable mode. Keep this file here so settings stay in this folder.

powershell -NoProfile -Command "Compress-Archive -Path '%PAYLOAD%' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 exit /b 1

echo Portable zip: %ZIP%
exit /b 0
