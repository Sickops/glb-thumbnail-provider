@echo off
net session >nul 2>&1
if errorlevel 1 (echo Lancez ce script en tant qu'administrateur. & pause & exit /b 1)
regsvr32 /u /s "%~dp0GlbThumbnail.dll"
echo Desinstallation terminee.
pause
