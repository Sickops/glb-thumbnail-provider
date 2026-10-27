@echo off
net session >nul 2>&1
if errorlevel 1 (echo Lancez ce script en tant qu'administrateur ^(clic droit ^> Executer en tant qu'administrateur^). & pause & exit /b 1)
cd /d "%~dp0"
regsvr32 /s "%~dp0GlbThumbnail.dll"
if errorlevel 1 (echo Echec de l'enregistrement. & pause & exit /b 1)
rem Vider le cache de miniatures pour voir le resultat tout de suite
taskkill /f /im explorer.exe >nul 2>&1
del /f /q "%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db" >nul 2>&1
start explorer.exe
echo Installation terminee. Les fichiers .glb ont maintenant des miniatures.
pause
